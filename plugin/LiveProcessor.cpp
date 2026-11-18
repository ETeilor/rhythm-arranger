#include "LiveProcessor.h"
#include "LiveEditor.h"
#include <algorithm>
#include <cmath>

RhythmLiveProcessor::RhythmLiveProcessor()
    : RhythmBase(BusesProperties(),
                 [] {
                     juce::AudioProcessorValueTreeState::ParameterLayout l;
                     RhythmBase::addRhythmParameters(l);
                     l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"mode", 1}, "Mode",
                                                                         juce::StringArray{"Gate", "Arpeggio", "Voice split"}, 1));
                     l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"latch", 1}, "Latch", false));
                     l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"chordWindow", 1}, "Chord window", 5, 200, 50));
                     return l;
                 }()) {
    held_.reserve(32); chord_.reserve(32); chordShown_.reserve(32); offs_.reserve(512); rec_.reserve(8192);
}

void RhythmLiveProcessor::prepareToPlay(double sr, int) {
    sampleRate_ = sr;
    flush_ = !offs_.empty();           // notes still sounding get their note-off in the first block
    held_.clear(); chord_.clear();
    {
        const juce::SpinLock::ScopedLockType l(lock_);
        chordShown_.clear();
    }
    clock_ = 0;
}

juce::AudioProcessorEditor* RhythmLiveProcessor::createEditor() { return new RhythmLiveEditor(*this); }

void RhythmLiveProcessor::noteOn(int note, int channel, long long clock) {
    // The keys pressed together form the chord. A key pressed while others are held joins the chord; the chord
    // starts over only when every key was released before (with latch: when the latched keys are replaced).
    const long long window = (long long)(params.getRawParameterValue("chordWindow")->load() * 0.001 * sampleRate_);
    channel_ = channel;
    if (held_.empty()) {
        chord_.clear();
        chordOpenUntil_ = clock + window;        // the first hit waits until the chord is complete
        hitCounter_ = 0;
    }
    if (std::find(held_.begin(), held_.end(), note) == held_.end()) held_.push_back(note);
    if (std::find(chord_.begin(), chord_.end(), note) == chord_.end()) chord_.push_back(note);
    std::sort(chord_.begin(), chord_.end());
    const juce::SpinLock::ScopedLockType l(lock_);
    chordShown_ = chord_;
}

void RhythmLiveProcessor::noteOff(int note) {
    held_.erase(std::remove(held_.begin(), held_.end(), note), held_.end());
    const bool latch = params.getRawParameterValue("latch")->load() > 0.5f;
    if (!latch) chord_.erase(std::remove(chord_.begin(), chord_.end(), note), chord_.end());
    const juce::SpinLock::ScopedLockType l(lock_);
    chordShown_ = chord_;
}

void RhythmLiveProcessor::allNotesOff() {
    held_.clear();
    chord_.clear();
    const juce::SpinLock::ScopedLockType l(lock_);
    chordShown_.clear();
}

void RhythmLiveProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    juce::AudioPlayHead::PositionInfo info;
    bool have = false;
    readHost(info, have);
    // with "project tempo" off the plug-in runs on its own clock and slider tempo even while the project plays
    const bool playing = have && info.getIsPlaying() && params.getRawParameterValue("hostTempo")->load() > 0.5f;
    const double bpm = effectiveBpm();
    const double tps = bpm * rhy::kTpb / (60.0 * sampleRate_);        // ticks per sample
    double t0 = freeTicks_;
    if (playing) { if (auto ppq = info.getPpqPosition()) t0 = *ppq * rhy::kTpb; }
    const double t1 = t0 + tps * n;
    freeTicks_ = t1;
    const int mode = (int)std::lround(params.getRawParameterValue("mode")->load());
    auto lp = loop();
    // latch switched off: the chord is what the fingers hold, nothing stays behind
    const bool latch = params.getRawParameterValue("latch")->load() > 0.5f;
    if (lastLatch_ && !latch) {
        chord_ = held_;
        std::sort(chord_.begin(), chord_.end());
        const juce::SpinLock::ScopedLockType l(lock_);
        chordShown_ = chord_;
    }
    lastLatch_ = latch;

    // the hits of this block
    struct Hit { int at; double abs; int layer, vel, len; };
    Hit hits[512];
    int nh = 0;
    if (lp && lp->cycleTicks > 0) {
        const long long c0 = (long long)std::floor(t0 / lp->cycleTicks), c1 = (long long)std::floor(t1 / lp->cycleTicks);
        const auto& src = mode == rhy::amVoiceSplit ? lp->events : lp->merged;
        for (long long c = c0 - 1; c <= c1; ++c)        // one cycle back: a note can be moved earlier across the bar line
            for (const auto& base : src) {
                const auto e = rhy::shapeEvent(base, c, lp->feel);
                const double abs = (double)c * lp->cycleTicks + e.tick;
                if (abs < t0 || abs >= t1 || nh >= 512) continue;
                hits[nh++] = {(int)((abs - t0) / tps), abs, e.layer, e.velocity, e.length};
            }
        std::stable_sort(hits, hits + nh, [](const Hit& a, const Hit& b) { return a.abs < b.abs; });
        posTicks_ = std::fmod(t1, (double)lp->cycleTicks);
    }

    juce::MidiBuffer in;
    in.swapWith(midi);
    std::vector<juce::MidiMessageMetadata> msgs;
    msgs.reserve(64);
    for (const auto m : in) msgs.push_back(m);
    size_t mi = 0;
    juce::MidiBuffer out;
    const long long base = clock_;

    auto handleMessages = [&](int upTo) {
        while (mi < msgs.size() && msgs[mi].samplePosition <= upTo) {
            const auto msg = msgs[mi].getMessage();
            const int pos = msgs[mi].samplePosition;
            ++mi;
            if (msg.isNoteOn()) noteOn(msg.getNoteNumber(), msg.getChannel() - 1, base + pos);
            else if (msg.isNoteOff()) noteOff(msg.getNoteNumber());
            else {
                if (msg.isAllNotesOff() || msg.isAllSoundOff()) { allNotesOff(); flush_ = true; }
                out.addEvent(msg, pos);
            }                                  // everything else passes through
        }
    };

    if (flush_) {                                                          // the ends of notes that are still open
        for (const auto& o : offs_) out.addEvent(juce::MidiMessage::noteOff(o.channel + 1, o.note), 0);
        offs_.clear();
        flush_ = false;
    }
    for (int h = 0; h < nh; ++h) {
        handleMessages(hits[h].at);
        if (chord_.empty() || base + hits[h].at < chordOpenUntil_) continue;
        const auto notes = rhy::arpNotes(mode, chord_, lp ? lp->rank[std::min(hits[h].layer, rhy::kMaxLayers - 1)] : hits[h].layer, hitCounter_++);
        const long long len = std::max<long long>(1, (long long)std::lround(hits[h].len / tps));
        for (int note : notes) {
            for (size_t o = 0; o < offs_.size();)              // the same key still sounding: close it first
                if (offs_[o].note == note && offs_[o].channel == channel_) {
                    out.addEvent(juce::MidiMessage::noteOff(channel_ + 1, note), hits[h].at);
                    offs_.erase(offs_.begin() + (long)o);
                } else ++o;
            out.addEvent(juce::MidiMessage::noteOn(channel_ + 1, note, (juce::uint8)hits[h].vel), hits[h].at);
            offs_.push_back({base + hits[h].at + len, note, channel_});
            if (recOn_.load()) {
                const juce::SpinLock::ScopedLockType rl(recLock_);
                if (recOrigin_ < 0) {
                    recOrigin_ = lp ? std::floor(hits[h].abs / lp->cycleTicks) * lp->cycleTicks : hits[h].abs;
                    recBpm_ = bpm;
                    if (lp) { recNum_ = lp->meterNum; recDen_ = lp->meterDen; }
                }
                if (rec_.size() < 20000) rec_.push_back({(int)std::lround(hits[h].abs - recOrigin_), note, hits[h].vel});
                recCount_ = (int)rec_.size();
            }
        }
    }
    handleMessages(n);                                                    // the rest of the input
    for (size_t o = 0; o < offs_.size();) {
        if (offs_[o].at < base + n) {
            out.addEvent(juce::MidiMessage::noteOff(offs_[o].channel + 1, offs_[o].note), (int)std::max<long long>(0, offs_[o].at - base));
            offs_.erase(offs_.begin() + (long)o);
        } else ++o;
    }
    clock_ += n;
    midi.swapWith(out);
    (void)buf;
}

void RhythmLiveProcessor::clearRecording() {
    const juce::SpinLock::ScopedLockType rl(recLock_);
    rec_.clear();
    recOrigin_ = -1;
    recCount_ = 0;
}

juce::File RhythmLiveProcessor::writeRecordingFile() {
    std::vector<rhy::SourceNote> copy;
    int num, den;
    double bpm;
    {
        const juce::SpinLock::ScopedLockType rl(recLock_);
        copy = rec_; num = recNum_; den = recDen_; bpm = recBpm_;
    }
    if (copy.empty()) return {};
    return writeTempFile(rhy::writeNotes(copy, num, den, bpm, "Rhythm Arranger Live take"), "Rhythm Live take");
}

#ifndef RHYTHM_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RhythmLiveProcessor(); }
#endif

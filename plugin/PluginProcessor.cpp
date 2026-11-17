#include "PluginProcessor.h"
#include "PluginEditor.h"

RhythmProcessor::RhythmProcessor()
    : RhythmBase(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true),
                 [] {
                     juce::AudioProcessorValueTreeState::ParameterLayout l;
                     RhythmBase::addRhythmParameters(l);
                     l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"bars", 1}, "Bars", 1, 16, 4));
                     return l;
                 }()) {}

void RhythmProcessor::prepareToPlay(double sr, int) {
    sampleRate_ = sr;
    synth_.prepare(sr);
}

void RhythmProcessor::setPreview(bool on) {
    if (on) restart_ = true;
    previewOn_ = on;
}

juce::AudioProcessorEditor* RhythmProcessor::createEditor() { return new RhythmEditor(*this); }

void RhythmProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    midi.clear();
    buf.clear();
    const int n = buf.getNumSamples();
    juce::AudioPlayHead::PositionInfo info;
    bool have = false;
    readHost(info, have);

    const int pc = pendingClick_.exchange(-1);
    if (pc >= 0) synth_.trigger(pc, 100);

    struct Hit { int at, layer, vel; };
    Hit hits[256];
    int nh = 0;
    const bool on = previewOn_.load();
    if (on) {
        auto lp = loop();
        if (lp && lp->cycleTicks > 0) {
            if (restart_.exchange(false)) tick_ = 0;
            const double bpm = hostBpm(params.getRawParameterValue("tempo")->load());
            const double ticksPerSample = bpm * rhy::kTpb / (60.0 * sampleRate_);
            const double t0 = tick_, t1 = tick_ + ticksPerSample * n;
            const long long c0 = (long long)std::floor(t0 / lp->cycleTicks), c1 = (long long)std::floor(t1 / lp->cycleTicks);
            for (long long c = c0; c <= c1; ++c)
                for (const auto& e : lp->events) {
                    const double abs = (double)c * lp->cycleTicks + e.tick;
                    if (abs >= t0 && abs < t1 && nh < 256) hits[nh++] = {(int)((abs - t0) / ticksPerSample), e.layer, e.velocity};
                }
            tick_ = std::fmod(t1, 64.0 * lp->cycleTicks);
            previewTick_ = std::fmod(t1, (double)lp->cycleTicks);
        }
    } else {
        tick_ = 0;
        previewTick_ = 0;
    }

    auto* l = buf.getWritePointer(0);
    auto* r = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr;
    int hi = 0;
    for (int i = 0; i < n; ++i) {
        while (hi < nh && hits[hi].at <= i) { synth_.trigger(hits[hi].layer, hits[hi].vel); ++hi; }
        const float s = synth_.next();
        l[i] = s;
        if (r) r[i] = s;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
#ifndef RHYTHM_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RhythmProcessor(); }
#endif

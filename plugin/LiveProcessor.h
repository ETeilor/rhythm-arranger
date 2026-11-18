#pragma once
#include "Base.h"

// Rhythm Arranger Live: a MIDI FX. The chord you hold is played as the rhythm, in one of three ways:
// gate (the whole chord on every hit), arpeggio (one chord note per hit), voice split (every layer plays its own voice).
class RhythmLiveProcessor : public RhythmBase {
public:
    RhythmLiveProcessor();
    void prepareToPlay(double sampleRate, int) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    const juce::String getName() const override { return "Rhythm Arranger Live"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    void releaseResources() override { flush_ = true; }

    // for the editor and the tests
    std::vector<int> chord() const { const juce::SpinLock::ScopedLockType l(lock_); return chordShown_; }
    double positionTicks() const { return posTicks_.load(); }
    int recordedNotes() const { return recCount_.load(); }
    void setRecording(bool on) { recOn_ = on; }
    bool isRecording() const { return recOn_.load(); }
    void clearRecording();
    juce::File writeRecordingFile();

private:
    void noteOn(int note, int channel, long long clock);
    void allNotesOff();               // forget every held and latched key
    void noteOff(int note);
    std::vector<int> held_, chord_;            // physically held keys; the chord that is played
    std::vector<int> chordShown_;
    int channel_ = 0;
    long long clock_ = 0, chordOpenUntil_ = 0;
    bool lastLatch_ = false, flush_ = false;
    int hitCounter_ = 0;
    double sampleRate_ = 44100, freeTicks_ = 0;
    struct Off { long long at; int note, channel; };
    std::vector<Off> offs_;
    std::atomic<double> posTicks_{0};
    mutable juce::SpinLock lock_;

    mutable juce::SpinLock recLock_;
    std::vector<rhy::SourceNote> rec_;
    double recOrigin_ = -1;
    std::atomic<int> recCount_{0};
    std::atomic<bool> recOn_{false};
    double recBpm_ = 100;
    int recNum_ = 4, recDen_ = 4;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RhythmLiveProcessor)
};

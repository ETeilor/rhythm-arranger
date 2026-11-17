#pragma once
#include "Base.h"

// Rhythm Arranger: the catalog and generators, audition with a plain click, drag to a track.
class RhythmProcessor : public RhythmBase {
public:
    RhythmProcessor();
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    const juce::String getName() const override { return "Rhythm Arranger"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    void setPreview(bool on);
    bool isPreviewing() const { return previewOn_.load(); }
    double previewPositionTicks() const { return previewTick_.load(); }
    // plays one hit of the click now (a click on a layer name in the editor)
    void clickNow(int layer) { pendingClick_ = layer; }

private:
    ClickSynth synth_;
    double sampleRate_ = 44100;
    std::atomic<bool> previewOn_{false}, restart_{false};
    std::atomic<double> previewTick_{0};
    std::atomic<int> pendingClick_{-1};
    double tick_ = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RhythmProcessor)
};

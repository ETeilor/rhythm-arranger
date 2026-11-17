#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include "Rhythm.h"

// Everything both plug-ins share: parameters, the rhythm built from them, the loop the audio thread plays,
// an imported MIDI rhythm, and files for dragging into a track.
struct Loop {
    std::vector<rhy::Event> events;      // one cycle, swing already written into the ticks
    int cycleTicks = 0, layers = 0, meterNum = 4, meterDen = 4;
};

class RhythmBase : public juce::AudioProcessor,
                   protected juce::AudioProcessorValueTreeState::Listener,
                   protected juce::AsyncUpdater {
public:
    RhythmBase(const BusesProperties& buses, juce::AudioProcessorValueTreeState::ParameterLayout layout);
    ~RhythmBase() override;

    // parameters shared by both plug-ins (ids never change once released)
    static void addRhythmParameters(juce::AudioProcessorValueTreeState::ParameterLayout&);
    juce::AudioProcessorValueTreeState params;

    // message thread
    rhy::Settings settings() const;
    const rhy::Rhythm& rhythm() const { return rhythm_; }
    void rebuild();
    bool loadMidiFile(const juce::File& f, juce::String& error);
    juce::String importedName() const { return importedName_; }
    void setParamValue(const char* id, float value);
    std::function<void()> onRhythmChanged;
    juce::File writeTempFile(const std::vector<unsigned char>& bytes, const juce::String& baseName) const;
    juce::File writeRhythmFile(int bars, bool separateTracks) const;

    // audio thread
    std::shared_ptr<const Loop> loop() const { return std::atomic_load(&loop_); }
    // the tempo everything runs at: the project tempo when "project tempo" is on and the host gives one, else the slider
    double effectiveBpm() const {
        const double slider = params.getRawParameterValue("tempo")->load();
        if (params.getRawParameterValue("hostTempo")->load() < 0.5f) return slider;
        const double b = hostBpm_.load();
        return b > 20 ? b : slider;
    }

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    double getTailLengthSeconds() const override { return 0.5; }
    void releaseResources() override {}

protected:
    void parameterChanged(const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuild(); }
    void readHost(juce::AudioPlayHead::PositionInfo& info, bool& have);
    std::atomic<double> hostBpm_{0.0};

private:
    rhy::Rhythm rhythm_, imported_;
    juce::String importedName_;
    std::shared_ptr<const Loop> loop_;
    mutable int dragCounter_ = 0;
};

// one voice per hit: a short wooden click, higher for the upper octaves
class ClickSynth {
public:
    void prepare(double sampleRate) { sr_ = sampleRate; allOff(); }
    void allOff() { for (auto& v : v_) v.on = false; }
    void trigger(int layer, int velocity);
    float next();
private:
    struct Voice { bool on = false; double phase = 0, inc = 0; float env = 0, amp = 0, decay = 0; };
    Voice v_[24];
    double sr_ = 44100;
};

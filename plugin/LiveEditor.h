#pragma once
#include "LiveProcessor.h"
#include "Panel.h"

class RhythmLiveEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit RhythmLiveEditor(RhythmLiveProcessor&);
    ~RhythmLiveEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    RhythmPanel panel;
private:
    void timerCallback() override;
    RhythmLiveProcessor& proc;
    RhythmLook look;
    juce::TooltipWindow tooltips{this, 600};
    juce::ComboBox mode;
    std::unique_ptr<juce::ComboBoxParameterAttachment> modeAtt;
    juce::ToggleButton latch, hostTempo;
    std::unique_ptr<juce::ButtonParameterAttachment> hostTempoAtt;
    std::unique_ptr<juce::ButtonParameterAttachment> latchAtt;
    juce::TextButton rec, clear;
    juce::Slider tempo;
    juce::Label tempoLabel, chordLabel, hint, modeLabel;
    std::unique_ptr<juce::SliderParameterAttachment> tempoAtt;
    Card take;
};

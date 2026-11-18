#pragma once
#include "Panel.h"
#include "PluginProcessor.h"

class RhythmEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit RhythmEditor(RhythmProcessor&);
    ~RhythmEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    RhythmPanel panel;
private:
    void timerCallback() override;
    RhythmProcessor& proc;
    RhythmLook look;
    juce::TooltipWindow tooltips{this, 600};
    std::unique_ptr<juce::Drawable> logo;
    juce::TextButton play;
    juce::Slider tempo, bars;
    juce::ToggleButton hostTempo;
    std::unique_ptr<juce::ButtonParameterAttachment> hostTempoAtt;
    juce::Label tempoLabel, barsLabel;
    std::unique_ptr<juce::SliderParameterAttachment> tempoAtt, barsAtt;
    Card cardMix, cardLayers;
};

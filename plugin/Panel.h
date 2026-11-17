#pragma once
#include "Base.h"
#include "Look.h"

// The rhythm picture: layers as rows (or rings), one dot per hit, size is the accent, a playhead moves over it.
class PatternView : public juce::Component {
public:
    PatternView(RhythmBase& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    std::function<double()> position;        // ticks inside the cycle, or negative when idle
    std::function<void(int)> onLayerClick;   // a click on a layer label
    bool circle = false;
private:
    RhythmBase& proc;
    std::vector<juce::Rectangle<float>> labelRects;
};

// A card that can be dragged to a Logic track as a MIDI file.
class Card : public juce::Component, public juce::SettableTooltipClient {
public:
    Card(RhythmBase& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
    juce::String title, subtitle;
    std::function<juce::File()> makeFile;
    bool showLayers = true;
private:
    RhythmBase& proc;
    bool dragging = false, hover = false;
};

// source tabs, source settings, picture and transformations; shared by both plug-ins
class RhythmPanel : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer,
                    private juce::ListBoxModel {
public:
    RhythmPanel(RhythmBase& p);
    ~RhythmPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& f) override;
    void filesDropped(const juce::StringArray& f, int, int) override;
    PatternView view;
    juce::String lastError;

private:
    struct Row {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::SliderParameterAttachment> att;
    };
    void addRow(Row& r, const char* text, const char* paramId, const juce::String& tip = {});
    void timerCallback() override;
    void refreshCatalog();
    void showSource();
    void openMidi();
    int getNumRows() override { return (int)shown_.size(); }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int row) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    RhythmBase& proc;
    RhythmLook look_;
    juce::OwnedArray<juce::TextButton> tabs;
    // catalog
    juce::TextEditor search;
    juce::ComboBox category;
    juce::ListBox list{{}, this};
    std::vector<const rhy::CatalogEntry*> shown_;
    bool syncing_ = false;
    // other sources
    Row eK, eN, eRot, pA, pB, pBeats, rSteps, rDens, rSeed, tLen, tPhrase;
    juce::ToggleButton resultant;
    std::unique_ptr<juce::ButtonParameterAttachment> resultantAtt;
    juce::ComboBox randKind;
    std::unique_ptr<juce::ComboBoxParameterAttachment> randKindAtt;
    juce::TextButton newSeed, openFile, circleBtn;
    juce::Label fileName, srcHint;
    // transformations
    Row rotate, density, accentShift, phase, swing;
    juce::ToggleButton retro, invert;
    std::unique_ptr<juce::ButtonParameterAttachment> retroAtt, invertAtt;
    juce::ComboBox augment;
    std::unique_ptr<juce::ComboBoxParameterAttachment> augmentAtt;
    juce::Label augLabel, titleLabel, infoLabel, transHead, srcHead;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastSource_ = -1, lastShownCatalog_ = -1;
    juce::Rectangle<int> srcArea, viewArea, transArea;
};

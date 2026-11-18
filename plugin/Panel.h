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
    std::function<void(int, int)> onNoteSelected;   // a click on a hit: layer and step, or -1 -1 for none
    bool circle = false;
    int selLayer = -1, selStep = -1;          // the hit whose own length is edited
private:
    void markNote(juce::Graphics&, int layer, int step, float cx, float cy, float rad);
    struct Dot { int layer, step; juce::Point<float> c; float r; };
    RhythmBase& proc;
    std::vector<juce::Rectangle<float>> labelRects;
    std::vector<Dot> dots;
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
    struct CatRow { const rhy::CatalogEntry* e; int header; int count; };   // a rhythm, or a category header
    std::vector<CatRow> shown_;
    void syncCatalogSelection();
    bool syncing_ = false;
    // other sources
    Row eK, eN, eRot, pA, pB, pBeats, rSteps, rDens, tLen, tPhrase;
    juce::ToggleButton resultant;
    std::unique_ptr<juce::ButtonParameterAttachment> resultantAtt;
    juce::ComboBox randKind;
    std::unique_ptr<juce::ComboBoxParameterAttachment> randKindAtt;
    juce::TextButton prevVar, nextVar, openFile, circleBtn, rerollHuman, resetNote;
    juce::Label fileName, srcHint, variantLabel;
    // expression
    Row swing, noteLength, dynamics, curve, velRandom, timeRandom, noteSlider;
    juce::Label exprHead, noteInfo;
    bool syncingNote_ = false;
    void selectNote(int layer, int step);
    void updateNoteControls();
    // transformations
    Row rotate, density, accentShift, phase;
    juce::ToggleButton retro, invert;
    std::unique_ptr<juce::ButtonParameterAttachment> retroAtt, invertAtt;
    juce::ComboBox augment;
    std::unique_ptr<juce::ComboBoxParameterAttachment> augmentAtt;
    juce::Label augLabel, titleLabel, infoLabel, transHead, srcHead;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastSource_ = -1, lastShownCatalog_ = -1;
    juce::Rectangle<int> srcArea, viewArea, transArea;
};

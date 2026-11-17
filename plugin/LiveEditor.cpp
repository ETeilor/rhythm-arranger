#include "LiveEditor.h"

static juce::String noteNameLive(int n) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[n % 12]) + juce::String(n / 12 - 2);
}

RhythmLiveEditor::RhythmLiveEditor(RhythmLiveProcessor& p) : AudioProcessorEditor(&p), panel(p), proc(p), take(p) {
    setLookAndFeel(&look);
    addAndMakeVisible(panel);
    panel.view.position = [this]() { return proc.chord().empty() ? -1.0 : proc.positionTicks(); };

    mode.addItemList({U("Гейт: весь аккорд на каждый удар"), U("Арпеджио: по ноте аккорда на удар"), U("Расщепление: слой на голос")}, 1);
    mode.setTooltip(U("Как ритм играет аккорд"));
    modeAtt = std::make_unique<juce::ComboBoxParameterAttachment>(*proc.params.getParameter("mode"), mode);
    addAndMakeVisible(mode);
    modeLabel.setText(U("Режим"), juce::dontSendNotification);
    modeLabel.setFont(ui::font(12.5f));
    modeLabel.setColour(juce::Label::textColourId, ui::textDim);
    addAndMakeVisible(modeLabel);
    latch.setButtonText(U("Фиксация аккорда"));
    latch.setTooltip(U("Аккорд продолжает играть после того, как вы отпустили клавиши. Новый аккорд заменяет старый."));
    latchAtt = std::make_unique<juce::ButtonParameterAttachment>(*proc.params.getParameter("latch"), latch);
    addAndMakeVisible(latch);
    tempoLabel.setText(U("Темп"), juce::dontSendNotification);
    tempoLabel.setFont(ui::font(12.5f));
    tempoLabel.setColour(juce::Label::textColourId, ui::textDim);
    tempo.setSliderStyle(juce::Slider::LinearHorizontal);
    tempo.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 22);
    tempo.setNumDecimalPlacesToDisplay(0);
    tempo.setTooltip(U("Темп, когда проект не играет. Когда проект играет, берётся его темп и такт."));
    tempoAtt = std::make_unique<juce::SliderParameterAttachment>(*proc.params.getParameter("tempo"), tempo);
    addAndMakeVisible(tempo);
    addAndMakeVisible(tempoLabel);

    rec.setButtonText(U("Запись"));
    rec.getProperties().set("icon", "rec");
    rec.setClickingTogglesState(true);
    rec.setTooltip(U("Записывать сыгранное в файл, который можно потянуть на трек"));
    rec.onClick = [this]() { proc.setRecording(rec.getToggleState()); };
    addAndMakeVisible(rec);
    clear.setButtonText(U("Очистить"));
    clear.getProperties().set("icon", "close");
    clear.onClick = [this]() { proc.clearRecording(); };
    addAndMakeVisible(clear);
    take.title = U("Запись в трек");
    take.showLayers = false;
    take.makeFile = [this]() { return proc.writeRecordingFile(); };
    take.setTooltip(U("Потяните на трек Logic: сыгранное станет MIDI-регионом."));
    addAndMakeVisible(take);
    chordLabel.setFont(ui::font(14.0f, true));
    chordLabel.setColour(juce::Label::textColourId, ui::accent);
    addAndMakeVisible(chordLabel);
    hint.setFont(ui::font(12.0f));
    hint.setColour(juce::Label::textColourId, ui::textDim);
    hint.setText(U("Поставьте плагин в слот MIDI FX перед инструментом. Зажмите аккорд."), juce::dontSendNotification);
    addAndMakeVisible(hint);
    setSize(1100, 720);
    startTimerHz(10);
}

RhythmLiveEditor::~RhythmLiveEditor() { setLookAndFeel(nullptr); }

void RhythmLiveEditor::timerCallback() {
    const auto c = proc.chord();
    juce::String s;
    for (int n : c) s += (s.isEmpty() ? "" : "  ") + noteNameLive(n);
    chordLabel.setText(s.isEmpty() ? U("Аккорд не зажат") : s, juce::dontSendNotification);
    chordLabel.setColour(juce::Label::textColourId, s.isEmpty() ? ui::textDim : ui::accent);
    take.subtitle = proc.recordedNotes() ? U("нот: ") + juce::String(proc.recordedNotes()) : U("пока пусто");
    take.repaint();
}

void RhythmLiveEditor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(ui::text);
    g.setFont(ui::font(13.0f, true));
    g.drawText("RHYTHM ARRANGER LIVE", 24, 12, 300, 22, juce::Justification::centredLeft);
    g.setColour(ui::textDim);
    g.setFont(ui::font(12.0f));
    g.drawText("Teilormusic", getWidth() - 24 - 200, 12, 200, 22, juce::Justification::centredRight);
    g.setColour(ui::line);
    g.drawHorizontalLine(getHeight() - 150, 24, (float)getWidth() - 24);
}

void RhythmLiveEditor::resized() {
    auto b = getLocalBounds().reduced(24, 0);
    b.removeFromTop(44);
    auto bottom = b.removeFromBottom(140);
    b.removeFromBottom(12);
    panel.setBounds(b);
    bottom.removeFromTop(10);
    auto left = bottom.removeFromLeft(420);
    auto r0 = left.removeFromTop(28);
    modeLabel.setBounds(r0.removeFromLeft(70)); mode.setBounds(r0);
    left.removeFromTop(8);
    latch.setBounds(left.removeFromTop(26));
    left.removeFromTop(6);
    auto r1 = left.removeFromTop(28);
    tempoLabel.setBounds(r1.removeFromLeft(70)); tempo.setBounds(r1);
    bottom.removeFromLeft(24);
    auto mid = bottom.removeFromLeft(300);
    chordLabel.setBounds(mid.removeFromTop(28));
    mid.removeFromTop(6);
    auto rr = mid.removeFromTop(30);
    rec.setBounds(rr.removeFromLeft(120)); rr.removeFromLeft(8); clear.setBounds(rr.removeFromLeft(120));
    mid.removeFromTop(8);
    hint.setBounds(mid);
    bottom.removeFromLeft(14);
    take.setBounds(bottom.removeFromLeft(280).reduced(0, 2));
}

#include "PluginEditor.h"

RhythmEditor::RhythmEditor(RhythmProcessor& p)
    : AudioProcessorEditor(&p), panel(p), proc(p), cardMix(p), cardLayers(p) {
    setLookAndFeel(&look);
    addAndMakeVisible(panel);
    panel.view.position = [this]() { return proc.isPreviewing() ? proc.previewPositionTicks() : -1.0; };
    panel.view.onLayerClick = [this](int layer) { proc.clickNow(layer); };

    play.setButtonText(U("Слушать"));
    play.getProperties().set("icon", "play");
    play.setTooltip(U("Прослушать ритм обычным кликом. Темп берётся из проекта, а если проект не играет, из ползунка."));
    play.onClick = [this]() {
        const bool on = !proc.isPreviewing();
        proc.setPreview(on);
        play.setButtonText(on ? U("Стоп") : U("Слушать"));
        play.getProperties().set("icon", on ? "stop" : "play");
        play.repaint();
    };
    addAndMakeVisible(play);

    auto setup = [&](juce::Slider& s, juce::Label& l, const char* text, const char* id, std::unique_ptr<juce::SliderParameterAttachment>& att) {
        l.setText(U(text), juce::dontSendNotification);
        l.setFont(ui::font(12.5f));
        l.setColour(juce::Label::textColourId, ui::textDim);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 22);
        s.setNumDecimalPlacesToDisplay(0);
        att = std::make_unique<juce::SliderParameterAttachment>(*proc.params.getParameter(id), s);
        addAndMakeVisible(s);
        addAndMakeVisible(l);
    };
    setup(tempo, tempoLabel, "Темп", "tempo", tempoAtt);
    tempo.setTooltip(U("Темп прослушивания, когда проект не играет"));
    setup(bars, barsLabel, "Тактов в файле", "bars", barsAtt);
    bars.setTooltip(U("Сколько раз цикл повторяется в перетаскиваемом файле"));

    cardMix.title = U("Ритм в трек");
    cardLayers.title = U("Слои по трекам");
    cardMix.makeFile = [this]() { return proc.writeRhythmFile((int)std::lround(proc.params.getRawParameterValue("bars")->load()), false); };
    cardLayers.makeFile = [this]() { return proc.writeRhythmFile((int)std::lround(proc.params.getRawParameterValue("bars")->load()), true); };
    cardLayers.showLayers = false;
    cardMix.setTooltip(U("Потяните карточку на трек Logic. Все слои в одном треке, каждый на своей октаве, ноты стаккато."));
    cardLayers.setTooltip(U("Потяните на пустое место в Logic: каждый слой станет отдельным треком, везде нота C3."));
    addAndMakeVisible(cardMix);
    addAndMakeVisible(cardLayers);

    setSize(1100, 720);
    startTimerHz(10);
    timerCallback();
}

RhythmEditor::~RhythmEditor() { proc.setPreview(false); setLookAndFeel(nullptr); }

void RhythmEditor::timerCallback() {
    const auto& r = proc.rhythm();
    const int bars_ = (int)std::lround(proc.params.getRawParameterValue("bars")->load());
    cardMix.subtitle = r.ok() ? U("тактов: ") + juce::String(bars_) + U(", слоёв: ") + juce::String((int)r.layers.size()) : juce::String();
    cardLayers.subtitle = r.ok() ? U("дорожек: ") + juce::String((int)r.layers.size()) + U(", везде C3") : juce::String();
    cardMix.repaint();
    cardLayers.repaint();
}

void RhythmEditor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(ui::text);
    g.setFont(ui::font(13.0f, true));
    g.drawText("RHYTHM ARRANGER", 24, 12, 260, 22, juce::Justification::centredLeft);
    g.setColour(ui::textDim);
    g.setFont(ui::font(12.0f));
    g.drawText("Teilormusic", getWidth() - 24 - 200, 12, 200, 22, juce::Justification::centredRight);
    g.setColour(ui::line);
    g.drawHorizontalLine(getHeight() - 150, 24, (float)getWidth() - 24);
}

void RhythmEditor::resized() {
    auto b = getLocalBounds().reduced(24, 0);
    b.removeFromTop(44);
    auto bottom = b.removeFromBottom(140);
    b.removeFromBottom(12);
    panel.setBounds(b);
    bottom.removeFromTop(10);
    auto left = bottom.removeFromLeft(380);
    play.setBounds(left.removeFromTop(40).removeFromLeft(140));
    left.removeFromTop(10);
    auto r1 = left.removeFromTop(28);
    tempoLabel.setBounds(r1.removeFromLeft(110)); tempo.setBounds(r1);
    left.removeFromTop(4);
    auto r2 = left.removeFromTop(28);
    barsLabel.setBounds(r2.removeFromLeft(110)); bars.setBounds(r2);
    bottom.removeFromLeft(24);
    auto cards = bottom.removeFromLeft(560);
    cardMix.setBounds(cards.removeFromLeft(320).reduced(0, 2));
    cards.removeFromLeft(14);
    cardLayers.setBounds(cards.reduced(0, 2));
}

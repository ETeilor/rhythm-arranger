#include "Panel.h"
#include <algorithm>
#include <cmath>
#include <set>

static juce::String noteName(int note) {
    static const char* n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(n[note % 12]) + juce::String(note / 12 - 2);
}

// ---------------------------------------------------------------- pattern view
void PatternView::paint(juce::Graphics& g) {
    labelRects.clear();
    dots.clear();
    auto area = getLocalBounds().toFloat().reduced(6.0f);
    g.setColour(ui::panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f);
    const auto& r = proc.rhythm();
    if (!r.ok()) {
        g.setColour(ui::textDim);
        g.setFont(ui::font(14));
        g.drawText(U("Нет ритма. Выберите другой источник или параметры."), getLocalBounds(), juce::Justification::centred);
        return;
    }
    const int swing = (int)std::lround(proc.params.getRawParameterValue("swing")->load());
    const int layers = (int)r.layers.size();
    const double pos = position ? position() : -1.0;
    auto xOf = [&](int step) { return (double)(r.tickOf(step) + rhy::swingDelay(r, step, swing)) / r.cycleTicks; };

    if (!circle) {
        const float labelW = 118.0f;
        const float rowH = std::min(64.0f, area.getHeight() / (float)layers);
        const float x0 = area.getX() + labelW, w = area.getWidth() - labelW - 4.0f;
        const float y0 = area.getY() + (area.getHeight() - rowH * layers) * 0.5f;
        for (int t = 0; t < r.cycleTicks; t += rhy::kTpb) {
            const float x = x0 + w * (float)t / (float)r.cycleTicks;
            g.setColour(t == 0 ? ui::line.brighter(0.4f) : ui::line);
            g.drawVerticalLine((int)x, y0, y0 + rowH * layers);
        }
        for (int l = 0; l < layers; ++l) {
            const float y = y0 + rowH * (float)l, cy = y + rowH * 0.5f;
            const auto col = ui::layerColour(l);
            juce::Rectangle<float> lab(area.getX(), y + 2, labelW - 8, rowH - 4);
            labelRects.push_back(lab);
            g.setColour(ui::panelHi);
            g.fillRoundedRectangle(lab, 6.0f);
            g.setColour(col);
            g.setFont(ui::font(12.5f, true));
            g.drawText(U(r.layerNames[(size_t)l]), lab.reduced(8, 0).withHeight(lab.getHeight() * 0.55f), juce::Justification::bottomLeft);
            g.setColour(ui::textDim);
            g.setFont(ui::font(11.0f));
            g.drawText(noteName(r.noteOf(l)) + "  (" + juce::String(r.noteOf(l)) + ")",
                       lab.reduced(8, 0).withTrimmedTop(lab.getHeight() * 0.55f), juce::Justification::topLeft);
            const float base = std::min(rowH * 0.40f, w / (float)r.length * 0.46f);
            for (int i = 0; i < r.length; ++i) {
                const float cx = x0 + w * (float)xOf(i);
                const int lv = r.layers[(size_t)l][(size_t)i];
                if (!lv) { g.setColour(ui::line.brighter(0.2f)); g.fillEllipse(cx - 1.5f, cy - 1.5f, 3.0f, 3.0f); continue; }
                const float rad = lv == 3 ? base : lv == 2 ? base * 0.72f : base * 0.45f;
                g.setColour(col.withAlpha(lv == 1 ? 0.55f : 1.0f));
                g.fillEllipse(cx - rad, cy - rad, rad * 2, rad * 2);
                markNote(g, l, i, cx, cy, rad);
            }
        }
        if (pos >= 0) {
            g.setColour(ui::text.withAlpha(0.8f));
            g.drawVerticalLine((int)(x0 + w * (float)(pos / r.cycleTicks)), y0, y0 + rowH * layers);
        }
    } else {
        const auto c = area.getCentre();
        const float R = std::min(area.getWidth(), area.getHeight()) * 0.5f - 16.0f;
        const float pi = juce::MathConstants<float>::pi;
        for (int t = 0; t < r.cycleTicks; t += rhy::kTpb) {
            const float a = 2 * pi * (float)t / (float)r.cycleTicks - pi / 2;
            g.setColour(ui::line);
            g.drawLine(c.x, c.y, c.x + std::cos(a) * (R + 8), c.y + std::sin(a) * (R + 8), 1.0f);
        }
        for (int l = 0; l < layers; ++l) {
            const float rr = R * (1.0f - 0.2f * (float)l);
            const auto col = ui::layerColour(l);
            g.setColour(ui::line.brighter(0.15f));
            g.drawEllipse(c.x - rr, c.y - rr, rr * 2, rr * 2, 1.0f);
            const float base = std::min(rr * 0.12f, rr * pi / (float)r.length * 0.7f);
            for (int i = 0; i < r.length; ++i) {
                const float a = 2 * pi * (float)xOf(i) - pi / 2;
                const float px = c.x + std::cos(a) * rr, py = c.y + std::sin(a) * rr;
                const int lv = r.layers[(size_t)l][(size_t)i];
                if (!lv) { g.setColour(ui::line.brighter(0.2f)); g.fillEllipse(px - 1.5f, py - 1.5f, 3.0f, 3.0f); continue; }
                const float rad = lv == 3 ? base : lv == 2 ? base * 0.72f : base * 0.45f;
                g.setColour(col.withAlpha(lv == 1 ? 0.55f : 1.0f));
                g.fillEllipse(px - rad, py - rad, rad * 2, rad * 2);
                markNote(g, l, i, px, py, rad);
            }
        }
        if (pos >= 0) {
            const float a = 2 * pi * (float)(pos / r.cycleTicks) - pi / 2;
            g.setColour(ui::text.withAlpha(0.8f));
            g.drawLine(c.x, c.y, c.x + std::cos(a) * (R + 8), c.y + std::sin(a) * (R + 8), 1.5f);
        }
    }
}

// a ring round the selected hit, a short bar under a hit that has a length of its own; the dot is remembered for clicks
void PatternView::markNote(juce::Graphics& g, int layer, int step, float cx, float cy, float rad) {
    dots.push_back({layer, step, {cx, cy}, std::max(rad, 7.0f)});
    if (layer == selLayer && step == selStep) {
        g.setColour(ui::text);
        g.drawEllipse(cx - rad - 3, cy - rad - 3, (rad + 3) * 2, (rad + 3) * 2, 1.5f);
    }
    if (proc.noteLenOf(layer, step) >= 0) {
        g.setColour(ui::text.withAlpha(0.85f));
        g.fillRect(cx - 4.0f, cy + rad + 3.0f, 8.0f, 2.0f);
    }
}

void PatternView::mouseDown(const juce::MouseEvent& e) {
    for (size_t i = 0; i < labelRects.size(); ++i)
        if (labelRects[i].contains(e.position)) { if (onLayerClick) onLayerClick((int)i); return; }
    for (const auto& d : dots)
        if (d.c.getDistanceFrom(e.position) <= d.r + 2.0f) {
            const bool same = d.layer == selLayer && d.step == selStep;
            selLayer = same ? -1 : d.layer;
            selStep = same ? -1 : d.step;
            if (onNoteSelected) onNoteSelected(selLayer, selStep);
            repaint();
            return;
        }
}

// ---------------------------------------------------------------- card
void Card::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(hover ? ui::panelHi.brighter(0.1f) : ui::panelHi);
    g.fillRoundedRectangle(b, 10.0f);
    g.setColour(hover ? ui::accent : ui::line);
    g.drawRoundedRectangle(b, 10.0f, 1.0f);
    auto in = b.reduced(12, 8);
    g.setColour(ui::text);
    g.setFont(ui::font(14.0f, true));
    g.drawText(title, in.removeFromTop(20), juce::Justification::centredLeft);
    g.setColour(ui::textDim);
    g.setFont(ui::font(11.5f));
    g.drawText(subtitle, in.removeFromTop(16), juce::Justification::centredLeft);
    const auto& r = proc.rhythm();
    if (showLayers && r.ok()) {
        in.removeFromTop(4);
        const int layers = (int)r.layers.size();
        const float rowH = std::min(10.0f, in.getHeight() / (float)layers);
        for (int l = 0; l < layers; ++l) {
            const float cy = in.getY() + rowH * ((float)l + 0.5f);
            for (int i = 0; i < r.length; ++i) {
                const int lv = r.layers[(size_t)l][(size_t)i];
                if (!lv) continue;
                const float cx = in.getX() + in.getWidth() * (float)r.tickOf(i) / (float)r.cycleTicks;
                g.setColour(ui::layerColour(l).withAlpha(lv == 1 ? 0.5f : 1.0f));
                const float d = lv == 3 ? 5.0f : 3.6f;
                g.fillEllipse(cx - d / 2, cy - d / 2, d, d);
            }
        }
    }
}

void Card::mouseDrag(const juce::MouseEvent& e) {
    if (dragging || e.getDistanceFromDragStart() < 6 || !makeFile) return;
    dragging = true;
    auto f = makeFile();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
}

// ---------------------------------------------------------------- panel
void RhythmPanel::addRow(Row& r, const char* text, const char* id, const juce::String& tip) {
    r.label.setText(U(text), juce::dontSendNotification);
    r.label.setFont(ui::font(12.5f));
    r.label.setColour(juce::Label::textColourId, ui::textDim);
    r.label.setTooltip(tip);
    r.slider.setSliderStyle(juce::Slider::LinearHorizontal);
    r.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 22);
    r.slider.setNumDecimalPlacesToDisplay(0);
    r.slider.setTooltip(tip);
    r.att = std::make_unique<juce::SliderParameterAttachment>(*proc.params.getParameter(id), r.slider);
    addChildComponent(r.label);
    addChildComponent(r.slider);
}

RhythmPanel::RhythmPanel(RhythmBase& p) : view(p), proc(p) {
    setLookAndFeel(&look_);
    addAndMakeVisible(view);
    view.position = [this]() { return -1.0; };
    static const char* names[] = {"Каталог", "Евклид", "Полиритм", "Полиметр", "Случайный", "Тихай", "Из MIDI"};
    static const char* tips[] = {
        "Ритмы мировой практики. Каждый сверен с источником.",
        "Евклидов ритм: k ударов равномерно на n шагов.",
        "Полиритм a:b. Слои на разных октавах.",
        "Полиметр: один пульс, два размера, акценты совпадают через НОК.",
        "Случайный ритм. Кнопки «Назад» и «Вперёд» листают варианты, помнятся 10 вариантов вместе с положением ползунков.",
        "Тихай: фраза трижды с паузами, последний удар третьей фразы ровно на первой доле следующего цикла (приём индийской музыки).",
        "Взять ритм первого такта из MIDI-файла."};
    for (int i = 0; i < 7; ++i) {
        auto* b = tabs.add(new juce::TextButton(U(names[i])));
        b->setClickingTogglesState(false);
        b->setTooltip(U(tips[i]));
        b->onClick = [this, i]() { proc.setParamValue("source", (float)i); };
        addAndMakeVisible(b);
    }
    // catalog
    search.setTextToShowWhenEmpty(U("поиск по названию, региону"), ui::textDim);
    search.setFont(ui::font(13.5f));
    search.onTextChange = [this]() { refreshCatalog(); };
    addChildComponent(search);
    category.addItem(U("Все категории"), 1);   // the list below is split by category, this narrows it to one
    {
        std::set<int> cats;
        for (const auto& e : rhy::catalog()) cats.insert(e.category);
        for (int c : cats) category.addItem(U(rhy::categoryName(c)), 100 + c);
    }
    category.setSelectedId(1, juce::dontSendNotification);
    category.onChange = [this]() { refreshCatalog(); };
    addChildComponent(category);
    list.setRowHeight(26);
    list.setColour(juce::ListBox::backgroundColourId, ui::panel);
    list.setColour(juce::ListBox::outlineColourId, ui::line);
    list.setOutlineThickness(1);
    addChildComponent(list);
    // generators
    addRow(eK, "Ударов", "euclidK", U("k: сколько ударов"));
    addRow(eN, "Шагов", "euclidN", U("n: длина цикла в шестнадцатых"));
    addRow(eRot, "Поворот", "euclidRot", U("На сколько шагов сдвинуть рисунок влево"));
    addRow(pA, "Слой A", "polyA", U("Число равных долей первого слоя"));
    addRow(pB, "Слой B", "polyB", U("Число равных долей второго слоя"));
    addRow(pBeats, "Длина, долей", "polyBeats", U("На сколько долей растянут цикл"));
    resultant.setButtonText(U("Результирующий ритм (Шиллингер)"));
    resultant.setTooltip(U("Третий слой: удары обоих слоёв вместе"));
    resultantAtt = std::make_unique<juce::ButtonParameterAttachment>(*proc.params.getParameter("resultant"), resultant);
    addChildComponent(resultant);
    addRow(rSteps, "Шаги", "randSteps");
    addRow(rDens, "Плотность", "randDensity", U("Сколько шагов цикла занято ударами"));
    randKind.addItemList({U("Евклидов"), U("Группы 2-3-4"), U("По сильным долям"), U("Разные рисунки")}, 1);
    randKind.setTooltip(U("Евклидов: удары разложены ровно. Группы 2-3-4: цикл собран из групп по 2, 3 и 4 шага. "
                          "По сильным долям: удары чаще попадают на сильные доли такта, получается похоже на грув. "
                          "Разные рисунки: каждый вариант это новый рисунок с тем же числом ударов, повернутые копии друг друга не повторяются."));
    randKindAtt = std::make_unique<juce::ComboBoxParameterAttachment>(*proc.params.getParameter("randKind"), randKind);
    addChildComponent(randKind);
    prevVar.setButtonText(U("Назад"));
    prevVar.getProperties().set("icon", "prev");
    prevVar.setTooltip(U("Предыдущий вариант: ползунки вернутся как были"));
    prevVar.onClick = [this]() { proc.stepVariant(-1); };
    nextVar.setButtonText(U("Вперёд"));
    nextVar.getProperties().set("icon", "next");
    nextVar.setTooltip(U("Следующий вариант. Текущий запоминается. В конце строится новый с тем же положением ползунков."));
    nextVar.onClick = [this]() { proc.stepVariant(1); };
    addChildComponent(prevVar);
    addChildComponent(nextVar);
    variantLabel.setFont(ui::font(13.0f, true));
    variantLabel.setColour(juce::Label::textColourId, ui::text);
    addChildComponent(variantLabel);
    addRow(tLen, "Цикл", "tihaiLen", U("Длина цикла в шагах"));
    addRow(tPhrase, "Паттерн", "tihaiPhrase", U("Длина фразы в шагах"));
    openFile.setButtonText(U("Открыть MIDI"));
    openFile.getProperties().set("icon", "open");
    openFile.onClick = [this]() { openMidi(); };
    addChildComponent(openFile);
    fileName.setFont(ui::font(12.5f));
    fileName.setColour(juce::Label::textColourId, ui::textDim);
    addChildComponent(fileName);
    srcHint.setFont(ui::font(12.0f));
    srcHint.setColour(juce::Label::textColourId, ui::textDim);
    srcHint.setText(U("Или перетащите .mid на окно плагина. Берётся первый такт, сетка в шестнадцатых."), juce::dontSendNotification);
    srcHint.setMinimumHorizontalScale(1.0f);
    addChildComponent(srcHint);
    // transformations
    RhythmLook::sectionLabel(transHead, U("Преобразования"));
    addAndMakeVisible(transHead);
    addRow(rotate, "Поворот", "rotate", U("Сдвиг рисунка по кругу"));
    addRow(density, "Плотность", "density", U("Меньше нуля: убрать самые слабые удары. Больше: добавить на сильные места."));
    addRow(accentShift, "Сдвиг акцента", "accentShift", U("Удары остаются, уровни акцентов едут по ним"));
    addRow(phase, "Фаза, копия", "phase", U("Копия слоя со сдвигом на столько шагов (фазовый сдвиг Райха)"));
    retro.setButtonText(U("Реверс")); retro.setTooltip(U("Ритм задом наперёд"));
    invert.setButtonText(U("Дополнение")); invert.setTooltip(U("Паузы становятся ударами и наоборот"));
    retroAtt = std::make_unique<juce::ButtonParameterAttachment>(*proc.params.getParameter("retro"), retro);
    invertAtt = std::make_unique<juce::ButtonParameterAttachment>(*proc.params.getParameter("invert"), invert);
    augment.addItemList({U("Сетка как есть"), U("Увеличение x2"), U("Уменьшение /2")}, 1);
    augmentAtt = std::make_unique<juce::ComboBoxParameterAttachment>(*proc.params.getParameter("augment"), augment);
    for (auto* c : std::initializer_list<juce::Component*>{&retro, &invert, &augment}) addAndMakeVisible(c);
    RhythmLook::sectionLabel(exprHead, U("Выразительность"));
    addAndMakeVisible(exprHead);
    addRow(swing, "Свинг, %", "swing", U("Нечётные шаги звучат позже. Записывается в MIDI."));
    addRow(noteLength, "Длина нот", "noteLength", U("0: короткие ноты (стаккато). 100: нота тянется до начала следующей, но не заходит на неё. Записывается в MIDI."));
    addRow(dynamics, "Динамика", "dynamics", U("Расстояние между акцентом и тихой нотой. 50: как было, 0: все ноты ровные, 100: вдвое шире. Записывается в MIDI."));
    addRow(curve, "Кривая такта", "curve", U("Громкость внутри такта: сильный первый удар, небольшой спад, к концу рост. 0: выключено."));
    addRow(velRandom, "Разброс velocity", "velRandom", U("0: громкость как задано. 10: сильный случайный разброс для живости. Записывается в MIDI."));
    addRow(timeRandom, "Разброс начала", "timeRandom", U("0: точно по сетке. 10: заметная живость. Ноты не меняются местами и не наезжают друг на друга. Записывается в MIDI."));
    rerollHuman.setButtonText(U("Другой разброс"));
    rerollHuman.getProperties().set("icon", "refresh");
    rerollHuman.setTooltip(U("Выпадают другие случайные числа для разброса начала и громкости"));
    rerollHuman.onClick = [this]() { proc.rerollHumanSeed(); };
    addAndMakeVisible(rerollHuman);
    noteInfo.setFont(ui::font(12.0f));
    noteInfo.setColour(juce::Label::textColourId, ui::textDim);
    addAndMakeVisible(noteInfo);
    noteSlider.label.setText(U("Длина ноты"), juce::dontSendNotification);
    noteSlider.label.setFont(ui::font(12.5f));
    noteSlider.label.setColour(juce::Label::textColourId, ui::textDim);
    noteSlider.slider.setSliderStyle(juce::Slider::LinearHorizontal);
    noteSlider.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 22);
    noteSlider.slider.setRange(0, 100, 1);
    noteSlider.slider.setTooltip(U("Длина выбранной ноты. Нота не заходит на начало следующей."));
    noteSlider.slider.onValueChange = [this]() {
        if (!syncingNote_ && view.selLayer >= 0) proc.setNoteLen(view.selLayer, view.selStep, (int)std::lround(noteSlider.slider.getValue()));
    };
    addAndMakeVisible(noteSlider.label);
    addAndMakeVisible(noteSlider.slider);
    resetNote.setButtonText(U("Общая длина"));
    resetNote.setTooltip(U("Вернуть выбранной ноте общую длину"));
    resetNote.onClick = [this]() { if (view.selLayer >= 0) proc.setNoteLen(view.selLayer, view.selStep, -1); };
    addAndMakeVisible(resetNote);
    view.onNoteSelected = [this](int l, int st) { selectNote(l, st); };
    for (auto* r : {&rotate, &density, &accentShift, &phase, &swing, &noteLength, &dynamics, &curve, &velRandom, &timeRandom}) { r->label.setVisible(true); r->slider.setVisible(true); }
    RhythmLook::sectionLabel(srcHead, U("Источник"));
    addAndMakeVisible(srcHead);
    titleLabel.setFont(ui::font(18.0f, true));
    titleLabel.setColour(juce::Label::textColourId, ui::text);
    addAndMakeVisible(titleLabel);
    infoLabel.setFont(ui::font(12.0f));
    infoLabel.setColour(juce::Label::textColourId, ui::textDim);
    infoLabel.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(infoLabel);
    circleBtn.setButtonText(U("Круг"));
    circleBtn.setClickingTogglesState(true);
    circleBtn.setTooltip(U("Показать ритм по кругу"));
    circleBtn.onClick = [this]() { view.circle = circleBtn.getToggleState(); view.repaint(); };
    addAndMakeVisible(circleBtn);

    proc.onRhythmChanged = [this]() { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<RhythmPanel>(this)]() { if (safe) safe->timerCallback(); }); };
    refreshCatalog();
    showSource();
    startTimerHz(25);
}

RhythmPanel::~RhythmPanel() {
    proc.onRhythmChanged = nullptr;
    setLookAndFeel(nullptr);
}

void RhythmPanel::paint(juce::Graphics&) {}

bool RhythmPanel::isInterestedInFileDrag(const juce::StringArray& f) {
    for (auto& s : f) if (s.endsWithIgnoreCase(".mid") || s.endsWithIgnoreCase(".midi")) return true;
    return false;
}
void RhythmPanel::filesDropped(const juce::StringArray& f, int, int) {
    for (auto& s : f) if (s.endsWithIgnoreCase(".mid") || s.endsWithIgnoreCase(".midi")) {
        if (!proc.loadMidiFile(juce::File(s), lastError)) fileName.setText(lastError, juce::dontSendNotification);
        break;
    }
}
void RhythmPanel::openMidi() {
    chooser = std::make_unique<juce::FileChooser>(U("MIDI-файл с ритмом"), juce::File(), "*.mid;*.midi");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
        auto f = c.getResult();
        if (f.existsAsFile() && !proc.loadMidiFile(f, lastError)) fileName.setText(lastError, juce::dontSendNotification);
    });
}

void RhythmPanel::syncCatalogSelection() {
    const int id = (int)std::lround(proc.params.getRawParameterValue("catalog")->load());
    int row = -1;
    for (int i = 0; i < (int)shown_.size(); ++i) if (shown_[(size_t)i].e && shown_[(size_t)i].e->id == id) row = i;
    syncing_ = true;
    if (row >= 0) list.selectRow(row, true, true); else list.deselectAllRows();
    syncing_ = false;
}

void RhythmPanel::refreshCatalog() {
    shown_.clear();
    const int cat = category.getSelectedId() > 1 ? category.getSelectedId() - 100 : 0;
    const auto q = search.getText().trim();
    std::vector<const rhy::CatalogEntry*> match;
    for (const auto& e : rhy::catalog()) {
        if (cat && e.category != cat) continue;
        if (q.isNotEmpty() && !(U(e.name).containsIgnoreCase(q) || U(e.region).containsIgnoreCase(q) || U(e.grouping).containsIgnoreCase(q))) continue;
        match.push_back(&e);
    }
    std::stable_sort(match.begin(), match.end(), [](const rhy::CatalogEntry* a, const rhy::CatalogEntry* b) { return a->category < b->category; });
    for (size_t i = 0; i < match.size();) {
        size_t j = i;
        while (j < match.size() && match[j]->category == match[i]->category) ++j;
        shown_.push_back({nullptr, match[i]->category, (int)(j - i)});      // category header
        for (size_t k = i; k < j; ++k) shown_.push_back({match[k], 0, 0});
        i = j;
    }
    syncing_ = true;
    list.updateContent();
    syncing_ = false;
    syncCatalogSelection();
    list.repaint();
}

void RhythmPanel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (row < 0 || row >= (int)shown_.size()) return;
    const auto& r = shown_[(size_t)row];
    if (!r.e) {
        g.setColour(ui::panelHi);
        g.fillRect(0, 0, w, h);
        g.setColour(ui::accent);
        g.setFont(ui::font(11.5f, true));
        g.drawText(U(rhy::categoryName(r.header)).toUpperCase(), 10, 0, w - 60, h, juce::Justification::centredLeft, true);
        g.setColour(ui::textDim);
        g.drawText(juce::String(r.count), w - 52, 0, 44, h, juce::Justification::centredRight);
        return;
    }
    const auto* e = r.e;
    if (selected) { g.setColour(ui::accent.withAlpha(0.18f)); g.fillRect(0, 0, w, h); g.setColour(ui::accent); g.fillRect(0, 3, 3, h - 6); }
    g.setColour(selected ? ui::text : ui::text.withAlpha(0.88f));
    g.setFont(ui::font(13.0f));
    g.drawText(U(e->name), 16, 0, w - 76, h, juce::Justification::centredLeft, true);
    g.setColour(ui::textDim);
    g.setFont(ui::font(11.5f));
    g.drawText(juce::String((int)std::string(e->pattern).size()), w - 52, 0, 44, h, juce::Justification::centredRight);
}

void RhythmPanel::selectedRowsChanged(int row) {
    if (syncing_ || row < 0 || row >= (int)shown_.size()) return;
    if (!shown_[(size_t)row].e) { syncCatalogSelection(); return; }       // a header is not a rhythm
    proc.setParamValue("catalog", (float)shown_[(size_t)row].e->id);
    proc.setParamValue("source", (float)rhy::sCatalog);
}
void RhythmPanel::listBoxItemClicked(int row, const juce::MouseEvent&) {
    if (row >= 0 && row < (int)shown_.size() && shown_[(size_t)row].e) selectedRowsChanged(row);
}

void RhythmPanel::selectNote(int layer, int step) {
    view.selLayer = layer;
    view.selStep = step;
    updateNoteControls();
    view.repaint();
}

void RhythmPanel::updateNoteControls() {
    const auto& r = proc.rhythm();
    bool valid = view.selLayer >= 0 && r.ok() && view.selLayer < (int)r.layers.size() && view.selStep >= 0 && view.selStep < r.length &&
                 r.layers[(size_t)view.selLayer][(size_t)view.selStep] > 0;
    if (!valid && view.selLayer >= 0) { view.selLayer = view.selStep = -1; }
    noteSlider.slider.setEnabled(valid);
    resetNote.setEnabled(valid && proc.noteLenOf(view.selLayer, view.selStep) >= 0);
    if (!valid) {
        noteInfo.setText(U("Нота не выбрана. Нажмите на удар в рисунке."), juce::dontSendNotification);
        return;
    }
    const int own = proc.noteLenOf(view.selLayer, view.selStep);
    const int shownValue = own >= 0 ? own : (int)std::lround(proc.params.getRawParameterValue("noteLength")->load());
    noteInfo.setText(U("Слой ") + juce::String(view.selLayer + 1) + U(", шаг ") + juce::String(view.selStep + 1) + (own >= 0 ? U(", своя длина") : U(", общая длина")),
                     juce::dontSendNotification);
    syncingNote_ = true;
    noteSlider.slider.setValue(shownValue, juce::dontSendNotification);
    syncingNote_ = false;
}

void RhythmPanel::showSource() {
    const int s = (int)std::lround(proc.params.getRawParameterValue("source")->load());
    lastSource_ = s;
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState(i == s, juce::dontSendNotification);
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->getProperties().set("active", i == s);
    auto vis = [](std::initializer_list<juce::Component*> l, bool v) { for (auto* c : l) c->setVisible(v); };
    vis({&search, &category, &list}, s == rhy::sCatalog);
    vis({&eK.label, &eK.slider, &eN.label, &eN.slider, &eRot.label, &eRot.slider}, s == rhy::sEuclid);
    vis({&pA.label, &pA.slider, &pB.label, &pB.slider}, s == rhy::sPoly || s == rhy::sPolymeter);
    vis({&resultant, &pBeats.label, &pBeats.slider}, s == rhy::sPoly);
    vis({&rSteps.label, &rSteps.slider, &rDens.label, &rDens.slider, &randKind, &prevVar, &nextVar, &variantLabel}, s == rhy::sRandom);
    vis({&tLen.label, &tLen.slider, &tPhrase.label, &tPhrase.slider}, s == rhy::sTihai);
    vis({&openFile, &fileName, &srcHint}, s == rhy::sImported);
    resized();
}

void RhythmPanel::timerCallback() {
    const int s = (int)std::lround(proc.params.getRawParameterValue("source")->load());
    if (s != lastSource_) showSource();
    if (s == rhy::sCatalog) {
        const int id = (int)std::lround(proc.params.getRawParameterValue("catalog")->load());
        if (id != lastShownCatalog_) { lastShownCatalog_ = id; refreshCatalog(); }
    }
    if (s == rhy::sRandom) {
        const int n = std::max(1, proc.variantCount()), k = proc.variantCount() ? proc.variantNumber() : 1;
        variantLabel.setText(U("Вариант ") + juce::String(k) + U(" из ") + juce::String(n), juce::dontSendNotification);
        prevVar.setEnabled(proc.canGoBack());
    }
    updateNoteControls();
    const auto& r = proc.rhythm();
    titleLabel.setText(r.ok() ? U(r.name) : U("Нет ритма"), juce::dontSendNotification);
    juce::String info;
    if (r.ok()) info = U(r.info) + "\n" + U("Размер ") + juce::String(r.meterNum) + "/" + juce::String(r.meterDen) + U(", шагов в цикле ") + juce::String(r.length) +
                       U(", слоёв ") + juce::String((int)r.layers.size());
    infoLabel.setText(info, juce::dontSendNotification);
    if (s == rhy::sImported) fileName.setText(proc.importedName().isNotEmpty() ? proc.importedName() : lastError, juce::dontSendNotification);
    view.repaint();
}

void RhythmPanel::resized() {
    auto b = getLocalBounds();
    auto top = b.removeFromTop(34);
    int x = 0;
    for (auto* t : tabs) { const int w = 100; t->setBounds(x, top.getY(), w - 6, 30); x += w; }
    circleBtn.setBounds(b.getRight() - 270 - 70, top.getY(), 64, 30);
    b.removeFromTop(8);
    auto left = b.removeFromLeft(330);
    b.removeFromLeft(14);
    auto right = b.removeFromRight(260);
    b.removeFromRight(14);

    // source column
    srcHead.setBounds(left.removeFromTop(20));
    left.removeFromTop(4);
    const int s = lastSource_ < 0 ? 0 : lastSource_;
    auto row = [&](Row& r) { auto a = left.removeFromTop(30); r.label.setBounds(a.removeFromLeft(100)); r.slider.setBounds(a); left.removeFromTop(4); };
    if (s == rhy::sCatalog) {
        search.setBounds(left.removeFromTop(28)); left.removeFromTop(6);
        category.setBounds(left.removeFromTop(28)); left.removeFromTop(6);
        list.setBounds(left);
    } else if (s == rhy::sEuclid) { row(eK); row(eN); row(eRot); }
    else if (s == rhy::sPoly) { row(pA); row(pB); row(pBeats); resultant.setBounds(left.removeFromTop(28)); }
    else if (s == rhy::sPolymeter) { row(pA); row(pB); }
    else if (s == rhy::sRandom) {
        variantLabel.setBounds(left.removeFromTop(24)); left.removeFromTop(2);
        {
            auto a = left.removeFromTop(30);
            prevVar.setBounds(a.removeFromLeft(130)); a.removeFromLeft(8); nextVar.setBounds(a.removeFromLeft(130));
        }
        left.removeFromTop(10);
        row(rSteps); row(rDens);
        randKind.setBounds(left.removeFromTop(28));
    } else if (s == rhy::sTihai) { row(tLen); row(tPhrase); }
    else if (s == rhy::sImported) {
        openFile.setBounds(left.removeFromTop(30).removeFromLeft(150)); left.removeFromTop(8);
        fileName.setBounds(left.removeFromTop(24));
        srcHint.setBounds(left.removeFromTop(60));
    }

    // transformations
    transHead.setBounds(right.removeFromTop(20));
    right.removeFromTop(4);
    auto trow = [&](Row& r) { auto a = right.removeFromTop(28); r.label.setBounds(a.removeFromLeft(110)); r.slider.setBounds(a); right.removeFromTop(2); };
    trow(rotate);
    {
        auto a = right.removeFromTop(28);
        retro.setBounds(a.removeFromLeft(90));
        invert.setBounds(a);
        right.removeFromTop(6);
    }
    augment.setBounds(right.removeFromTop(28)); right.removeFromTop(8);
    trow(density); trow(accentShift); trow(phase);
    right.removeFromTop(8);
    exprHead.setBounds(right.removeFromTop(20));
    right.removeFromTop(4);
    trow(swing); trow(noteLength); trow(dynamics); trow(curve); trow(velRandom); trow(timeRandom);
    rerollHuman.setBounds(right.removeFromTop(30).removeFromLeft(160));
    right.removeFromTop(8);
    noteInfo.setBounds(right.removeFromTop(20));
    trow(noteSlider);
    resetNote.setBounds(right.removeFromTop(30).removeFromLeft(160));

    // centre: title, picture, info
    titleLabel.setBounds(b.removeFromTop(28));
    infoLabel.setBounds(b.removeFromBottom(54));
    b.removeFromBottom(6);
    view.setBounds(b);
}

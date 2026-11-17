#include "Base.h"
#include <cmath>

using APVTS = juce::AudioProcessorValueTreeState;

static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }

void RhythmBase::addRhythmParameters(APVTS::ParameterLayout& l) {
    using namespace juce;
    auto choice = [&](const char* id, const char* name, StringArray items, int def) {
        l.add(std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, items, def));
    };
    auto integer = [&](const char* id, const char* name, int lo, int hi, int def) {
        l.add(std::make_unique<AudioParameterInt>(ParameterID{id, 1}, name, lo, hi, def));
    };
    auto flag = [&](const char* id, const char* name, bool def) {
        l.add(std::make_unique<AudioParameterBool>(ParameterID{id, 1}, name, def));
    };
    // new choices are only ever appended, ids of the catalog never move
    choice("source", "Source", {"Catalog", "Euclid", "Polyrhythm", "Polymeter", "Random", "Tihai", "MIDI file"}, 0);
    integer("catalog", "Catalog rhythm", 1001, 20000, 2001);
    integer("euclidK", "Euclid hits", 1, 32, 5);
    integer("euclidN", "Euclid steps", 2, 32, 8);
    integer("euclidRot", "Euclid rotation", 0, 31, 0);
    integer("polyA", "Poly A", 1, 16, 3);
    integer("polyB", "Poly B", 1, 16, 2);
    flag("resultant", "Resultant layer", false);
    integer("polyBeats", "Poly beats", 1, 16, 4);
    integer("seed", "Seed", 1, 99999, 1);
    integer("randSteps", "Random steps", 4, 32, 16);
    integer("randDensity", "Random density", 5, 95, 40);
    choice("randKind", "Random kind", {"Euclid", "Additive", "Weighted", "Necklace"}, 0);
    integer("tihaiLen", "Tihai cycle", 8, 64, 16);
    integer("tihaiPhrase", "Tihai phrase", 2, 20, 5);
    integer("rotate", "Rotate", -16, 16, 0);
    flag("retro", "Retrograde", false);
    flag("invert", "Complement", false);
    choice("augment", "Grid", {"Normal", "Augment", "Diminish"}, 0);
    integer("density", "Density", -6, 6, 0);
    integer("accentShift", "Accent shift", -8, 8, 0);
    integer("phase", "Phase copy", 0, 16, 0);
    integer("swing", "Swing", 0, 50, 0);
    integer("tempo", "Tempo", 40, 240, 100);
}

RhythmBase::RhythmBase(const BusesProperties& buses, APVTS::ParameterLayout layout)
    : AudioProcessor(buses), params(*this, nullptr, "STATE", std::move(layout)) {
    for (auto* p : getParameters())
        if (auto* pp = dynamic_cast<juce::RangedAudioParameter*>(p)) params.addParameterListener(pp->paramID, this);
    rebuild();
}

RhythmBase::~RhythmBase() {
    cancelPendingUpdate();
    for (auto* p : getParameters())
        if (auto* pp = dynamic_cast<juce::RangedAudioParameter*>(p)) params.removeParameterListener(pp->paramID, this);
}

rhy::Settings RhythmBase::settings() const {
    auto i = [&](const char* id) { return (int)std::lround(params.getRawParameterValue(id)->load()); };
    rhy::Settings s;
    s.source = i("source");
    s.catalogId = i("catalog");
    s.k = i("euclidK"); s.n = i("euclidN"); s.rot = i("euclidRot");
    s.polyA = i("polyA"); s.polyB = i("polyB"); s.resultant = i("resultant") != 0; s.polyBeats = i("polyBeats");
    s.seed = i("seed"); s.randSteps = i("randSteps"); s.randDensity = i("randDensity"); s.randKind = i("randKind");
    s.tihaiLen = i("tihaiLen"); s.tihaiPhrase = i("tihaiPhrase");
    s.rotate = i("rotate"); s.retro = i("retro") != 0; s.invert = i("invert") != 0; s.augment = i("augment");
    s.density = i("density"); s.accentShift = i("accentShift"); s.phase = i("phase"); s.swing = i("swing");
    return s;
}

void RhythmBase::rebuild() {
    const auto s = settings();
    rhythm_ = rhy::build(s, imported_.ok() ? &imported_ : nullptr);
    auto lp = std::make_shared<Loop>();
    if (rhythm_.ok()) {
        lp->events = rhy::cycleEvents(rhythm_, s.swing);
        lp->cycleTicks = rhythm_.cycleTicks;
        lp->layers = (int)rhythm_.layers.size();
        lp->meterNum = rhythm_.meterNum;
        lp->meterDen = rhythm_.meterDen;
    }
    std::atomic_store(&loop_, std::shared_ptr<const Loop>(lp));
    if (onRhythmChanged) onRhythmChanged();
}

void RhythmBase::setParamValue(const char* id, float value) {
    if (auto* p = params.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(value));
}

bool RhythmBase::loadMidiFile(const juce::File& f, juce::String& error) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) { error = U8("Файл не читается"); return false; }
    std::vector<rhy::SourceNote> notes;
    int num, den; double bpm;
    if (!rhy::readMidi((const unsigned char*)mb.getData(), mb.getSize(), notes, num, den, bpm) || notes.empty()) {
        error = U8("В файле нет нот"); return false;
    }
    auto r = rhy::rhythmFromNotes(notes, num, den, f.getFileNameWithoutExtension().toStdString());
    if (!r.ok()) { error = U8("Не удалось взять такт из файла"); return false; }
    imported_ = r;
    importedName_ = f.getFileNameWithoutExtension();
    setParamValue("source", (float)rhy::sImported);
    rebuild();
    return true;
}

juce::File RhythmBase::writeTempFile(const std::vector<unsigned char>& bytes, const juce::String& baseName) const {
    // every drag goes to a fresh folder, so the host never sees a file name twice
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("RhythmArranger")
                   .getChildFile("drag" + juce::String(juce::Time::currentTimeMillis() % 100000000) + "_" + juce::String(++dragCounter_));
    dir.createDirectory();
    auto f = dir.getChildFile(baseName + ".mid");
    f.replaceWithData(bytes.data(), bytes.size());
    return f;
}

juce::File RhythmBase::writeRhythmFile(int bars, bool separateTracks) const {
    if (!rhythm_.ok()) return {};
    rhy::MidiOptions o;
    o.bars = bars;
    o.separateTracks = separateTracks;
    o.bpm = hostBpm(params.getRawParameterValue("tempo")->load());
    o.swing = (int)std::lround(params.getRawParameterValue("swing")->load());
    juce::String name = juce::String(rhythm_.midiName) + (separateTracks ? " layers" : "");
    name = name.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
    return writeTempFile(rhy::writeMidi(rhythm_, o), name);
}

void RhythmBase::readHost(juce::AudioPlayHead::PositionInfo& info, bool& have) {
    have = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            info = *pos;
            have = true;
            if (auto b = pos->getBpm()) hostBpm_ = *b;
        }
}

void RhythmBase::getStateInformation(juce::MemoryBlock& dest) {
    auto state = params.copyState();
    auto xml = std::make_unique<juce::XmlElement>("RhythmArranger");
    xml->addChildElement(state.createXml().release());
    if (imported_.ok()) {
        // the imported rhythm is kept as a pattern string per layer
        auto* im = xml->createNewChildElement("Imported");
        im->setAttribute("name", importedName_);
        im->setAttribute("steps", imported_.length);
        im->setAttribute("num", imported_.meterNum);
        im->setAttribute("den", imported_.meterDen);
        im->setAttribute("pattern", juce::String(rhy::patternString(imported_.layers[0])));
    }
    copyXmlToBinary(*xml, dest);
}

void RhythmBase::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("RhythmArranger")) return;
    if (auto* im = xml->getChildByName("Imported")) {
        const auto pat = rhy::parsePattern(im->getStringAttribute("pattern").toStdString());
        if (!pat.empty()) {
            imported_ = rhy::Rhythm();
            imported_.name = im->getStringAttribute("name").toStdString();
            imported_.midiName = "Rhythm from MIDI";
            imported_.info = "Ритм из MIDI-файла";
            imported_.length = (int)pat.size();
            imported_.stepTicks = 120;
            imported_.cycleTicks = imported_.length * 120;
            imported_.meterNum = im->getIntAttribute("num", 4);
            imported_.meterDen = im->getIntAttribute("den", 4);
            imported_.layers = {pat};
            imported_.layerNames = {"Ритм"};
            importedName_ = im->getStringAttribute("name");
        }
    }
    if (auto* st = xml->getChildByName(params.state.getType())) params.replaceState(juce::ValueTree::fromXml(*st));
    rebuild();
}

// ---------- click
void ClickSynth::trigger(int layer, int velocity) {
    static const double freq[4] = {1100, 700, 1700, 2500};
    Voice* v = &v_[0];
    for (auto& c : v_) { if (!c.on) { v = &c; break; } if (c.env < v->env) v = &c; }
    v->on = true;
    v->phase = 0;
    v->inc = 2.0 * juce::MathConstants<double>::pi * freq[layer < 0 ? 0 : layer > 3 ? 3 : layer] / sr_;
    v->env = 1.0f;
    v->amp = 0.7f * std::pow((float)velocity / 127.0f, 1.5f);
    v->decay = (float)std::exp(-1.0 / (0.018 * sr_));      // about 18 ms
}

float ClickSynth::next() {
    float out = 0;
    for (auto& v : v_) {
        if (!v.on) continue;
        out += std::sin((float)v.phase) * v.env * v.amp;
        v.phase += v.inc;
        v.env *= v.decay;
        if (v.env < 0.0005f) v.on = false;
    }
    return out;
}

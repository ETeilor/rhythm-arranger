#include "Base.h"
#include <cmath>
#include <algorithm>

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
    flag("hostTempo", "Project tempo", true);
    // expression (added in 0.2.0)
    integer("noteLength", "Note length", 0, 100, 0);
    integer("dynamics", "Dynamics", 0, 100, 50);
    integer("curve", "Bar curve", 0, 10, 0);
    integer("velRandom", "Velocity random", 0, 10, 0);
    integer("timeRandom", "Timing random", 0, 10, 0);
    integer("humanSeed", "Random seed", 1, 99999, 1);
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
    s.noteLength = i("noteLength"); s.dynamics = i("dynamics"); s.curve = i("curve");
    s.velRandom = i("velRandom"); s.timeRandom = i("timeRandom"); s.humanSeed = i("humanSeed");
    return s;
}

// everything that changes the shape of the rhythm; the notes with their own length belong to one such shape
unsigned RhythmBase::structureHash(const rhy::Settings& s, const juce::String& importedName) {
    unsigned h = 2166136261u;
    auto add = [&](long long v) { h = (h ^ (unsigned)(v & 0xFFFFFFFF)) * 16777619u; h = (h ^ (unsigned)(v >> 32)) * 16777619u; };
    add(s.source); add(s.catalogId); add(s.k); add(s.n); add(s.rot); add(s.polyA); add(s.polyB); add(s.resultant); add(s.polyBeats);
    add(s.seed); add(s.randSteps); add(s.randDensity); add(s.randKind); add(s.tihaiLen); add(s.tihaiPhrase);
    add(s.rotate); add(s.retro); add(s.invert); add(s.augment); add(s.density); add(s.accentShift); add(s.phase);
    for (auto c : importedName) add((long long)c);
    return h;
}

void RhythmBase::rebuild() {
    auto s = settings();
    const unsigned sig = structureHash(s, importedName_);
    if (sig != lensSig_) { noteLens_.clear(); lensSig_ = sig; }
    s.noteLens = noteLens_;
    rhythm_ = rhy::build(s, imported_.ok() ? &imported_ : nullptr);
    auto lp = std::make_shared<Loop>();
    if (rhythm_.ok()) {
        lp->feel = rhy::feelOf(s);
        lp->events = rhy::cycleEvents(rhythm_, lp->feel);
        for (const auto& e : lp->events) {
            if (!lp->merged.empty() && lp->merged.back().tick == e.tick) lp->merged.back().velocity = std::max(lp->merged.back().velocity, e.velocity);
            else lp->merged.push_back(e);
        }
        for (int l = 0; l < rhy::kMaxLayers; ++l) lp->notes[l] = rhythm_.noteOf(l);
        const int nl = std::min((int)rhythm_.layers.size(), rhy::kMaxLayers);
        for (int l = 0; l < rhy::kMaxLayers; ++l) {
            int rk = 0;
            for (int m = 0; m < nl; ++m)                  // only the layers that exist take part in the ranking
                if (lp->notes[m] < lp->notes[l] || (lp->notes[m] == lp->notes[l] && m < l)) ++rk;
            lp->rank[l] = rk;
        }
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
    o.bpm = effectiveBpm();
    o.feel = rhy::feelOf(settings());
    juce::String name = juce::String(rhythm_.midiName) + (separateTracks ? " layers" : "");
    name = name.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
    return writeTempFile(rhy::writeMidi(rhythm_, o), name);
}

void RhythmBase::stepVariant(int dir) {
    if (variants_.empty()) { variants_.push_back((int)std::lround(params.getRawParameterValue("seed")->load())); varPos_ = 0; }
    if (dir < 0) {
        if (varPos_ <= 0) return;
        --varPos_;
    } else if (varPos_ + 1 < (int)variants_.size()) {
        ++varPos_;
    } else {
        const int cur = variants_[(size_t)varPos_];
        int v = cur;
        while (v == cur) v = 1 + juce::Random::getSystemRandom().nextInt(99999);
        variants_.push_back(v);
        if (variants_.size() > 100) variants_.erase(variants_.begin());
        varPos_ = (int)variants_.size() - 1;
    }
    setParamValue("seed", (float)variants_[(size_t)varPos_]);
}

void RhythmBase::rerollHumanSeed() {
    setParamValue("humanSeed", (float)(1 + juce::Random::getSystemRandom().nextInt(99999)));
}

void RhythmBase::setNoteLen(int layer, int step, int pct) {
    noteLens_.erase(std::remove_if(noteLens_.begin(), noteLens_.end(), [&](const rhy::NoteLen& n) { return n.layer == layer && n.step == step; }), noteLens_.end());
    if (pct >= 0) noteLens_.push_back({layer, step, std::min(100, pct)});
    rebuild();
}

int RhythmBase::noteLenOf(int layer, int step) const {
    for (const auto& n : noteLens_) if (n.layer == layer && n.step == step) return n.pct;
    return -1;
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
    if (!noteLens_.empty()) {
        juce::StringArray a;
        for (const auto& n : noteLens_) a.add(juce::String(n.layer) + ":" + juce::String(n.step) + ":" + juce::String(n.pct));
        auto* nl = xml->createNewChildElement("NoteLens");
        nl->setAttribute("list", a.joinIntoString(","));
        nl->setAttribute("sig", juce::String((juce::int64)lensSig_));
    }
    if (!variants_.empty()) {
        juce::StringArray a;
        for (int v : variants_) a.add(juce::String(v));
        auto* vr = xml->createNewChildElement("Variants");
        vr->setAttribute("list", a.joinIntoString(","));
        vr->setAttribute("pos", varPos_);
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
    variants_.clear(); varPos_ = -1;
    if (auto* vr = xml->getChildByName("Variants")) {
        for (auto& t : juce::StringArray::fromTokens(vr->getStringAttribute("list"), ",", "")) if (t.getIntValue() > 0) variants_.push_back(t.getIntValue());
        varPos_ = std::min(vr->getIntAttribute("pos", 0), (int)variants_.size() - 1);
        if (variants_.empty()) varPos_ = -1;
    }
    noteLens_.clear();
    lensSig_ = 0;
    if (auto* nl = xml->getChildByName("NoteLens")) {
        for (auto& t : juce::StringArray::fromTokens(nl->getStringAttribute("list"), ",", "")) {
            auto p = juce::StringArray::fromTokens(t, ":", "");
            if (p.size() == 3) noteLens_.push_back({p[0].getIntValue(), p[1].getIntValue(), std::max(0, std::min(100, p[2].getIntValue()))});
        }
        lensSig_ = (unsigned)nl->getStringAttribute("sig").getLargeIntValue();
    }
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

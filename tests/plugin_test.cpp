// Plug-in level tests without a host: parameters, every catalog rhythm, state round trip, click preview audio,
// host tempo, the Live arpeggiator (three modes, latch, legato, pass-through, recording), import and editor snapshots.
#include <juce_audio_utils/juce_audio_utils.h>
#include "LiveEditor.h"
#include "LiveProcessor.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

struct FakeHead : juce::AudioPlayHead {
    bool playing = false;
    double ppq = 0, bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setIsPlaying(playing);
        p.setPpqPosition(ppq);
        p.setBpm(bpm);
        p.setTimeSignature(TimeSignature{4, 4});
        return p;
    }
};

static int failures = 0;
static void expect(bool ok, const juce::String& text) {
    std::string raw;
    bool narrow = true;
    for (auto ch : text) { if (ch > 255) { narrow = false; break; } raw += (char)ch; }
    const juce::String what = narrow ? juce::String::fromUTF8(raw.c_str()) : text;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) ++failures;
}
static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
static void set(juce::AudioProcessorValueTreeState& ps, const char* id, float v) {
    auto* p = ps.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(v));
}
static void shot(juce::AudioProcessorEditor& ed, const juce::File& dir, const juce::String& name) {
    pump(150);
    auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
    auto out = dir.getChildFile(name);
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    os.flush();
    expect(out.getSize() > 1000, "снимок интерфейса " + name);
}

// runs `blocks` blocks of the Live plug-in, the head advances with the block
struct LiveRun {
    std::vector<std::pair<int, juce::MidiMessage>> out;     // absolute sample, message
    long long sample = 0;
};
static void runLive(RhythmLiveProcessor& p, FakeHead& head, LiveRun& r, int blocks, double sr, const std::vector<std::pair<long long, juce::MidiMessage>>& input) {
    juce::AudioBuffer<float> buf(0, 512);
    for (int b = 0; b < blocks; ++b) {
        juce::MidiBuffer mb;
        for (auto& [at, m] : input) if (at >= r.sample && at < r.sample + 512) mb.addEvent(m, (int)(at - r.sample));
        p.processBlock(buf, mb);
        for (const auto meta : mb) r.out.push_back({(int)(r.sample + meta.samplePosition), meta.getMessage()});
        r.sample += 512;
        if (head.playing) head.ppq += 512.0 / sr * head.bpm / 60.0;
    }
}
static int countOn(const LiveRun& r, int note = -1) { int n = 0; for (auto& [t, m] : r.out) if (m.isNoteOn() && (note < 0 || m.getNoteNumber() == note)) ++n; return n; }

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::File outDir(argc > 1 ? argv[1] : ".");
    outDir.createDirectory();
    const double sr = 48000;

    // 1. every catalog rhythm through the parameters
    {
        RhythmProcessor p;
        p.prepareToPlay(sr, 512);
        int bad = 0;
        for (const auto& e : rhy::catalog()) {
            set(p.params, "source", 0);
            set(p.params, "catalog", (float)e.id);
            p.rebuild();
            if (!p.rhythm().ok() || p.rhythm().name != e.name) ++bad;
            auto f = p.writeRhythmFile(2, false);
            juce::MemoryBlock mb;
            std::vector<rhy::SourceNote> notes; int num, den; double bpm;
            if (!f.loadFileAsData(mb) || !rhy::readMidi((const unsigned char*)mb.getData(), mb.getSize(), notes, num, den, bpm) || notes.empty()) ++bad;
        }
        expect(bad == 0, juce::String::fromUTF8("все ритмы каталога собираются через параметры и пишутся в файл, всего ") + juce::String((int)rhy::catalog().size()));
    }
    // 2. state round trip
    {
        RhythmProcessor a;
        set(a.params, "source", 4); set(a.params, "seed", 4242); set(a.params, "randKind", 2); set(a.params, "swing", 25); set(a.params, "rotate", 3);
        a.rebuild();
        juce::MemoryBlock state;
        a.getStateInformation(state);
        RhythmProcessor b;
        b.setStateInformation(state.getData(), (int)state.getSize());
        expect(b.settings().seed == 4242 && b.settings().randKind == 2 && b.settings().swing == 25 && b.settings().rotate == 3, "состояние: настройки возвращаются");
        expect(b.rhythm().layers == a.rhythm().layers && b.rhythm().name == a.rhythm().name, "состояние: тот же ритм по тому же зерну");
    }
    // 3. click preview and host tempo
    {
        RhythmProcessor p;
        p.prepareToPlay(sr, 512);
        FakeHead head; head.bpm = 120; p.setPlayHead(&head);
        set(p.params, "source", 1); set(p.params, "euclidK", 4); set(p.params, "euclidN", 16);
        p.rebuild();
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer mb;
        float silent = 0;
        for (int i = 0; i < 20; ++i) { buf.clear(); p.processBlock(buf, mb); silent = std::max(silent, buf.getMagnitude(0, 512)); }
        expect(silent == 0.0f, "без прослушивания плагин молчит");
        p.setPreview(true);
        int onsets = 0; float peak = 0, env = 0; bool armed = true;
        const int blocks = (int)(4.0 * sr / 512);          // four seconds
        for (int i = 0; i < blocks; ++i) {
            buf.clear(); p.processBlock(buf, mb);
            peak = std::max(peak, buf.getMagnitude(0, 512));
            auto* d = buf.getReadPointer(0);
            for (int s = 0; s < 512; ++s) {       // an envelope follower counts one onset per click
                env = std::max(std::abs(d[s]), env * 0.998f);
                if (armed && env > 0.05f) { ++onsets; armed = false; }
                if (!armed && env < 0.005f) armed = true;
            }
        }
        expect(peak > 0.1f && peak <= 1.0f, "клик слышен и не перегружает, пик " + juce::String(peak, 2));
        // E(4,16) at 120 bpm: four hits every two seconds
        expect(onsets >= 7 && onsets <= 9, "четыре секунды при 120 bpm дают восемь ударов, найдено " + juce::String(onsets));
        // "project tempo" off: the slider rules, 60 bpm gives half the hits
        set(p.params, "hostTempo", 0); set(p.params, "tempo", 60);
        p.setPreview(false); p.setPreview(true);
        int slow = 0; env = 0; armed = true;
        for (int i = 0; i < blocks; ++i) {
            buf.clear(); p.processBlock(buf, mb);
            auto* d = buf.getReadPointer(0);
            for (int s = 0; s < 512; ++s) { env = std::max(std::abs(d[s]), env * 0.998f); if (armed && env > 0.05f) { ++slow; armed = false; } if (!armed && env < 0.005f) armed = true; }
        }
        expect(slow >= 3 && slow <= 5, "темп проекта выключен: ползунок 60 bpm даёт четыре удара за четыре секунды, найдено " + juce::String(slow));
        expect(std::abs(p.effectiveBpm() - 60) < 0.01, "effectiveBpm берёт ползунок");
        set(p.params, "hostTempo", 1);
        expect(std::abs(p.effectiveBpm() - 120) < 0.01, "темп проекта включён: effectiveBpm берёт темп хоста");
        p.setPreview(false);
        for (int i = 0; i < 30; ++i) { buf.clear(); p.processBlock(buf, mb); }
        expect(buf.getMagnitude(0, 512) == 0.0f, "после стопа тишина");
    }
    // 4. import from a MIDI file
    {
        RhythmProcessor p;
        rhy::Settings s; s.source = rhy::sCatalog; s.catalogId = 2001;
        auto src = rhy::build(s);
        auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("rhythm_import_test.mid");
        auto bytes = rhy::writeMidi(src, rhy::MidiOptions());
        f.replaceWithData(bytes.data(), bytes.size());
        juce::String err;
        expect(p.loadMidiFile(f, err), "импорт MIDI принят");
        expect(p.settings().source == rhy::sImported && p.rhythm().length == 16 && rhy::hitCount(p.rhythm().layers[0]) == 5, "импорт: 16 шагов и пять ударов");
        juce::MemoryBlock st; p.getStateInformation(st);
        RhythmProcessor q; q.setStateInformation(st.getData(), (int)st.getSize());
        expect(q.rhythm().ok() && q.rhythm().layers[0] == p.rhythm().layers[0], "импортированный ритм переживает сохранение проекта");
        auto junk = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("rhythm_junk.mid");
        junk.replaceWithText("not midi");
        expect(!p.loadMidiFile(junk, err) && err.isNotEmpty(), "не-MIDI файл отклоняется с сообщением");
    }
    // 5. Live: three modes
    {
        const int C = 60, E = 64, G = 67;
        auto chordIn = [&](long long at, bool latchTest = false) {
            (void)latchTest;
            std::vector<std::pair<long long, juce::MidiMessage>> in;
            for (int n : {C, E, G}) in.push_back({at, juce::MidiMessage::noteOn(1, n, (juce::uint8)100)});
            return in;
        };
        // gate: the whole chord on every hit
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);   // a bit past the first beat, the window is open by the next hit
            set(p.params, "source", 1); set(p.params, "euclidK", 4); set(p.params, "euclidN", 16); set(p.params, "mode", 0);
            p.rebuild();
            LiveRun r;
            runLive(p, head, r, 400, sr, chordIn(0));
            const int hits = countOn(r, C);
            expect(hits >= 6 && countOn(r, E) == hits && countOn(r, G) == hits, "гейт: весь аккорд на каждый удар, ударов " + juce::String(hits));
            int offs = 0; for (auto& [t, m] : r.out) offs += m.isNoteOff();
            expect(offs >= countOn(r) - 3, "гейт: каждая нота закрывается");
            // staccato length 60 ticks at 120 bpm = 3000 samples
            int on0 = -1, off0 = -1;
            for (auto& [t, m] : r.out) { if (m.isNoteOn() && m.getNoteNumber() == C && on0 < 0) on0 = t; if (m.isNoteOff() && m.getNoteNumber() == C && off0 < 0) off0 = t; }
            expect(on0 >= 0 && std::abs((off0 - on0) - 3000) <= 2, "длина ноты 60 тиков, сэмплов: " + juce::String(off0 - on0));
            expect(p.chord() == std::vector<int>({C, E, G}), "аккорд виден редактору");
        }
        // arpeggio: one chord note per hit, in turn
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 8); set(p.params, "euclidN", 16); set(p.params, "mode", 1);
            p.rebuild();
            LiveRun r; runLive(p, head, r, 200, sr, chordIn(0));
            std::vector<int> seq; for (auto& [t, m] : r.out) if (m.isNoteOn()) seq.push_back(m.getNoteNumber());
            bool inOrder = seq.size() >= 6;
            const int order[3] = {C, E, G};
            for (size_t i = 0; i < seq.size(); ++i) if (seq[i] != order[i % 3]) inOrder = false;
            expect(inOrder, "арпеджио: до, ми, соль по кругу, нот " + juce::String((int)seq.size()));
        }
        // voice split: polyrhythm 3:2, layer C3 takes the second voice, layer C2 the lowest
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 2); set(p.params, "polyA", 3); set(p.params, "polyB", 2); set(p.params, "mode", 2);
            p.rebuild();
            LiveRun r; runLive(p, head, r, 200, sr, chordIn(0));
            std::set<int> used; for (auto& [t, m] : r.out) if (m.isNoteOn()) used.insert(m.getNoteNumber());
            expect(used == std::set<int>({C, E}), "расщепление: слои играют разные голоса аккорда");
        }
        // release stops it, latch keeps it
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 8); set(p.params, "euclidN", 16); set(p.params, "mode", 0);
            p.rebuild();
            auto in = chordIn(0);
            for (int n : {C, E, G}) in.push_back({48000, juce::MidiMessage::noteOff(1, n)});
            LiveRun r; runLive(p, head, r, 190, sr, in);
            int after = 0; for (auto& [t, m] : r.out) if (m.isNoteOn() && t > 48000 + 512) ++after;
            expect(countOn(r) > 0 && after == 0, "после отпускания клавиш удары прекращаются");
            RhythmLiveProcessor q; q.prepareToPlay(sr, 512);
            FakeHead h2; h2.playing = true; h2.ppq = 0.25; q.setPlayHead(&h2);
            set(q.params, "source", 1); set(q.params, "euclidK", 8); set(q.params, "euclidN", 16); set(q.params, "mode", 0); set(q.params, "latch", 1);
            q.rebuild();
            LiveRun r2; runLive(q, h2, r2, 190, sr, in);
            int after2 = 0; for (auto& [t, m] : r2.out) if (m.isNoteOn() && t > 48000 + 512) ++after2;
            expect(after2 > 0, "фиксация: аккорд играет после отпускания");
        }
        // legato: a new key while the old one is held replaces the chord
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 8); set(p.params, "euclidN", 16); set(p.params, "mode", 0);
            p.rebuild();
            std::vector<std::pair<long long, juce::MidiMessage>> in{{0, juce::MidiMessage::noteOn(1, C, (juce::uint8)100)}, {24000, juce::MidiMessage::noteOn(1, G, (juce::uint8)100)}};
            LiveRun r; runLive(p, head, r, 150, sr, in);
            int cLate = 0; for (auto& [t, m] : r.out) if (m.isNoteOn() && m.getNoteNumber() == C && t > 24000 + 600) ++cLate;
            expect(cLate == 0 && countOn(r, G) > 0, "легато: новая нота при зажатой старой заменяет аккорд");
        }
        // other MIDI passes through, the key itself does not
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; p.setPlayHead(&head);
            std::vector<std::pair<long long, juce::MidiMessage>> in{{10, juce::MidiMessage::controllerEvent(1, 64, 127)}, {20, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100)}};
            LiveRun r; runLive(p, head, r, 2, sr, in);
            bool cc = false; for (auto& [t, m] : r.out) if (m.isController() && m.getControllerNumber() == 64) cc = true;
            expect(cc, "педаль и прочий MIDI проходят насквозь");
        }
        // chord window: three keys within a few ms make one chord
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 8); set(p.params, "euclidN", 16); set(p.params, "mode", 0);
            p.rebuild();
            std::vector<std::pair<long long, juce::MidiMessage>> in{{0, juce::MidiMessage::noteOn(1, C, (juce::uint8)100)}, {200, juce::MidiMessage::noteOn(1, E, (juce::uint8)100)}, {450, juce::MidiMessage::noteOn(1, G, (juce::uint8)100)}};
            LiveRun r; runLive(p, head, r, 60, sr, in);
            expect(p.chord() == std::vector<int>({C, E, G}), "окно 14 мс собирает аккорд из нот, сыгранных почти разом");
        }
        // recording and the file for dragging
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.0; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 4); set(p.params, "euclidN", 16); set(p.params, "mode", 1);
            p.rebuild();
            p.setRecording(true);
            LiveRun r; runLive(p, head, r, 200, sr, chordIn(0));
            expect(p.recordedNotes() > 3, "запись копит ноты, их " + juce::String(p.recordedNotes()));
            auto f = p.writeRecordingFile();
            juce::MemoryBlock mb; std::vector<rhy::SourceNote> notes; int num, den; double bpm;
            expect(f.loadFileAsData(mb) && rhy::readMidi((const unsigned char*)mb.getData(), mb.getSize(), notes, num, den, bpm) && (int)notes.size() == p.recordedNotes(), "файл записи читается и полон");
            p.clearRecording();
            expect(p.recordedNotes() == 0 && !p.writeRecordingFile().existsAsFile(), "очистка записи");
        }
    }
    // 6. drag files
    {
        RhythmProcessor p;
        set(p.params, "source", 2); set(p.params, "polyA", 3); set(p.params, "polyB", 2); set(p.params, "resultant", 1);
        p.rebuild();
        auto f1 = p.writeRhythmFile(4, false), f2 = p.writeRhythmFile(4, false), f3 = p.writeRhythmFile(4, true);
        expect(f1.existsAsFile() && f1 != f2 && f1.getParentDirectory() != f2.getParentDirectory(), "каждое перетаскивание получает свою папку");
        juce::MemoryBlock mb; std::vector<rhy::SourceNote> notes; int num, den; double bpm;
        f3.loadFileAsData(mb);
        rhy::readMidi((const unsigned char*)mb.getData(), mb.getSize(), notes, num, den, bpm);
        std::set<int> used; for (auto& n : notes) used.insert(n.note);
        expect(used == std::set<int>({60}) && f3.getFileName().containsIgnoreCase("layers"), "слои по трекам: везде C3");
    }
    // 7. editor snapshots
    {
        RhythmProcessor p;
        p.prepareToPlay(sr, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        expect(ed != nullptr, "редактор создаётся");
        shot(*ed, outDir, "arranger_catalog.png");
        set(p.params, "source", 2); set(p.params, "polyA", 7); set(p.params, "polyB", 4); set(p.params, "resultant", 1);
        shot(*ed, outDir, "arranger_poly.png");
        set(p.params, "source", 4);
        shot(*ed, outDir, "arranger_random.png");
        if (auto* e = dynamic_cast<RhythmEditor*>(ed.get())) { e->panel.view.circle = true; }
        set(p.params, "source", 1);
        shot(*ed, outDir, "arranger_euclid_circle.png");
        set(p.params, "source", 6);
        shot(*ed, outDir, "arranger_midi.png");
        ed.reset();
        RhythmLiveProcessor lp; lp.prepareToPlay(sr, 512);
        std::unique_ptr<juce::AudioProcessorEditor> le(lp.createEditor());
        shot(*le, outDir, "live_catalog.png");
        set(lp.params, "source", 2);
        shot(*le, outDir, "live_poly.png");
    }
    std::cout << (failures ? "ОШИБКИ: " : "все проверки пройдены, ошибок ") << failures << "\n";
    return failures ? 1 : 0;
}

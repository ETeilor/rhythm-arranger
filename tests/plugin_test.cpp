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
        // a key pressed while another is held joins the chord (it used to replace it, which looked like "only the last note")
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 8); set(p.params, "euclidN", 16); set(p.params, "mode", 0);
            p.rebuild();
            std::vector<std::pair<long long, juce::MidiMessage>> in{{0, juce::MidiMessage::noteOn(1, C, (juce::uint8)100)}, {24000, juce::MidiMessage::noteOn(1, G, (juce::uint8)100)}};
            LiveRun r; runLive(p, head, r, 150, sr, in);
            int cLate = 0; for (auto& [t, m] : r.out) if (m.isNoteOn() && m.getNoteNumber() == C && t > 24000 + 600) ++cLate;
            expect(cLate > 0 && countOn(r, G) > 0 && p.chord() == std::vector<int>({C, G}), "нота, нажатая при зажатой другой, дополняет аккорд");
        }
        // legato: the old key is released after the new one is pressed, only the new one stays
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 8); set(p.params, "euclidN", 16); set(p.params, "mode", 0);
            p.rebuild();
            std::vector<std::pair<long long, juce::MidiMessage>> in{{0, juce::MidiMessage::noteOn(1, C, (juce::uint8)100)}, {24000, juce::MidiMessage::noteOn(1, G, (juce::uint8)100)},
                                                                     {24100, juce::MidiMessage::noteOff(1, C)}};
            LiveRun r; runLive(p, head, r, 150, sr, in);
            int cLate = 0; for (auto& [t, m] : r.out) if (m.isNoteOn() && m.getNoteNumber() == C && t > 24100 + 600) ++cLate;
            expect(cLate == 0 && countOn(r, G) > 0, "легато: старая клавиша отпущена, играет только новая");
        }
        // the chord window: keys within 40 ms land on the same first hit
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 8); set(p.params, "euclidN", 16); set(p.params, "mode", 0);
            p.rebuild();
            std::vector<std::pair<long long, juce::MidiMessage>> in{{0, juce::MidiMessage::noteOn(1, C, (juce::uint8)100)}, {1000, juce::MidiMessage::noteOn(1, E, (juce::uint8)100)}, {1900, juce::MidiMessage::noteOn(1, G, (juce::uint8)100)}};
            LiveRun r; runLive(p, head, r, 60, sr, in);
            int firstAt = -1; for (auto& [t, m] : r.out) if (m.isNoteOn()) { firstAt = t; break; }
            int together = 0; for (auto& [t, m] : r.out) if (m.isNoteOn() && t == firstAt) ++together;
            expect(firstAt >= 0 && together == 3, "аккорд, сыгранный за 40 мс, звучит целиком с первого удара, нот " + juce::String(together));
            expect(p.chord() == std::vector<int>({C, E, G}), "аккорд собран");
        }
        // the voice split with a chord played in 40 ms uses every voice
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 2); set(p.params, "polyA", 3); set(p.params, "polyB", 2); set(p.params, "resultant", 1); set(p.params, "mode", 2);
            p.rebuild();
            std::vector<std::pair<long long, juce::MidiMessage>> in{{0, juce::MidiMessage::noteOn(1, C, (juce::uint8)100)}, {1000, juce::MidiMessage::noteOn(1, E, (juce::uint8)100)}, {1900, juce::MidiMessage::noteOn(1, G, (juce::uint8)100)}};
            LiveRun r; runLive(p, head, r, 200, sr, in);
            std::set<int> used; for (auto& [t, m] : r.out) if (m.isNoteOn()) used.insert(m.getNoteNumber());
            expect(used == std::set<int>({C, E, G}), "расщепление: три слоя, три голоса аккорда");
        }
        // latch switched off: nothing stays behind
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 8); set(p.params, "euclidN", 16); set(p.params, "mode", 0); set(p.params, "latch", 1);
            p.rebuild();
            auto in = chordIn(0);
            for (int n : {C, E, G}) in.push_back({20000, juce::MidiMessage::noteOff(1, n)});
            LiveRun r; runLive(p, head, r, 60, sr, in);
            const int before = countOn(r);
            expect(before > 0 && !p.chord().empty(), "фиксация держит аккорд после отпускания");
            set(p.params, "latch", 0);
            const int mark = (int)r.sample + 1024;
            runLive(p, head, r, 100, sr, in);
            int after = 0; for (auto& [t, m] : r.out) if (m.isNoteOn() && t > mark) ++after;
            expect(after == 0 && p.chord().empty(), "фиксацию выключили: игра прекращается, аккорд пуст");
        }
        // all notes off from the host stops everything
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 8); set(p.params, "euclidN", 16); set(p.params, "mode", 0);
            p.rebuild();
            auto in = chordIn(0);
            in.push_back({20000, juce::MidiMessage::allNotesOff(1)});
            LiveRun r; runLive(p, head, r, 150, sr, in);
            int after = 0; for (auto& [t, m] : r.out) if (m.isNoteOn() && t > 20000 + 512) ++after;
            expect(countOn(r) > 0 && after == 0 && p.chord().empty(), "CC 123 от хоста останавливает игру");
        }
        // a note that is still open when the host restarts the plug-in gets its note-off
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.0; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 8); set(p.params, "euclidN", 16); set(p.params, "mode", 0);
            p.rebuild();
            auto in = chordIn(0);
            LiveRun r;
            bool open = false;
            for (int i = 0; i < 100 && !open; ++i) {
                runLive(p, head, r, 1, sr, in);
                int ons = 0, offs = 0;
                for (auto& [t, m] : r.out) { ons += m.isNoteOn(); offs += m.isNoteOff(); }
                open = ons > offs;
            }
            expect(open, "в тесте есть открытая нота");
            LiveRun r2;
            p.prepareToPlay(sr, 512);
            p.setPlayHead(&head);
            runLive(p, head, r2, 1, sr, {});
            int offs = 0; for (auto& [t, m] : r2.out) offs += m.isNoteOff();
            expect(offs >= 1, "после перезапуска открытые ноты закрываются");
        }
        // a longer note length reaches the output
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 1); set(p.params, "euclidK", 4); set(p.params, "euclidN", 16); set(p.params, "mode", 0); set(p.params, "noteLength", 100);
            p.rebuild();
            LiveRun r; runLive(p, head, r, 200, sr, chordIn(0));
            int on0 = -1, off0 = -1;
            for (auto& [t, m] : r.out) { if (m.isNoteOn() && m.getNoteNumber() == C && on0 < 0) on0 = t; if (m.isNoteOff() && m.getNoteNumber() == C && off0 < 0) off0 = t; }
            // 479 ticks at 120 bpm is 23 950 samples, and the note closes before the next hit (480 ticks)
            expect(on0 >= 0 && std::abs((off0 - on0) - 23950) <= 4, "длина 100: нота тянется до следующей, сэмплов " + juce::String(off0 - on0));
        }
        // randomizers in Live: the hits stay in order, notes close
        {
            RhythmLiveProcessor p; p.prepareToPlay(sr, 512);
            FakeHead head; head.playing = true; head.ppq = 0.25; p.setPlayHead(&head);
            set(p.params, "source", 0); set(p.params, "catalog", 2001); set(p.params, "mode", 0); set(p.params, "velRandom", 10); set(p.params, "timeRandom", 10); set(p.params, "noteLength", 100);
            p.rebuild();
            LiveRun r; runLive(p, head, r, 400, sr, chordIn(0));
            std::set<int> vels; int ons = 0, offs = 0;
            for (auto& [t, m] : r.out) { if (m.isNoteOn() && m.getNoteNumber() == C) { vels.insert(m.getVelocity()); ++ons; } if (m.isNoteOff() && m.getNoteNumber() == C) ++offs; }
            expect(ons > 8 && vels.size() > 3 && offs >= ons - 1, "рандомайзеры в Live: громкость меняется, ноты закрываются");
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
            expect(p.chord() == std::vector<int>({C, E, G}), "аккорд из нот, сыгранных почти разом");
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
    // 5b. variants: each one keeps the seed and the sliders of the random tab, ten are remembered
    {
        RhythmProcessor p;
        auto val = [&](const char* id) { return (int)std::lround(p.params.getRawParameterValue(id)->load()); };
        set(p.params, "source", 4); set(p.params, "seed", 7); set(p.params, "randSteps", 12); set(p.params, "randDensity", 50); set(p.params, "randKind", 0);
        p.rebuild();
        const auto first = p.rhythm().layers[0];
        expect(!p.canGoBack(), "варианты: назад нельзя, пока истории нет");
        p.stepVariant(1); pump(20);                                  // second variant: other seed, same sliders
        expect(p.variantNumber() == 2 && val("seed") != 7 && val("randSteps") == 12 && val("randDensity") == 50, "вперёд: новый вариант с теми же ползунками");
        set(p.params, "randSteps", 20); set(p.params, "randDensity", 70); set(p.params, "randKind", 2);   // the user sets other values
        p.rebuild();
        const auto second = p.rhythm().layers[0];
        const int seed2 = val("seed");
        p.stepVariant(-1); pump(20);
        expect(p.variantNumber() == 1 && val("seed") == 7 && val("randSteps") == 12 && val("randDensity") == 50 && val("randKind") == 0, "назад: ползунки вернулись на значения первого варианта");
        p.rebuild();
        expect(p.rhythm().layers[0] == first, "назад: вернулся тот же рисунок");
        p.stepVariant(1); pump(20);
        p.rebuild();
        expect(val("seed") == seed2 && val("randSteps") == 20 && val("randDensity") == 70 && val("randKind") == 2 && p.rhythm().layers[0] == second, "вперёд по истории: второй вариант со своими ползунками");
        // ten are kept, the oldest falls out
        for (int i = 0; i < 14; ++i) { p.stepVariant(1); pump(10); }
        expect(p.variantCount() == 10 && p.variantNumber() == 10, "в памяти десять вариантов");
        for (int i = 0; i < 20; ++i) { p.stepVariant(-1); pump(10); }
        expect(p.variantNumber() == 1 && !p.canGoBack(), "назад до самого раннего из десяти");
        // state
        p.stepVariant(1); pump(10);
        set(p.params, "randSteps", 9);
        juce::MemoryBlock st; p.getStateInformation(st);
        RhythmProcessor q; q.setStateInformation(st.getData(), (int)st.getSize());
        expect(q.variantCount() == 10 && q.variantNumber() == 2, "варианты живут в проекте");
        q.stepVariant(-1); pump(20); q.stepVariant(1); pump(20);
        expect((int)std::lround(q.params.getRawParameterValue("randSteps")->load()) == 9, "после загрузки проекта ползунки варианта на месте");
    }
    {
        RhythmProcessor p;
        set(p.params, "source", 1); set(p.params, "euclidK", 4); set(p.params, "euclidN", 16);
        p.rebuild();
        p.setNoteLen(0, 4, 100);
        bool own = false, others = true;
        for (const auto& e : p.loop()->events) {
            if (e.step == 4) own = e.length == e.gapNext - 1; else if (e.length != rhy::kStaccato) others = false;
        }
        expect(own && others && p.noteLenOf(0, 4) == 100, "длина одной ноты: она тянется до следующей, остальные стаккато");
        juce::MemoryBlock st; p.getStateInformation(st);
        RhythmProcessor q; q.setStateInformation(st.getData(), (int)st.getSize());
        expect(q.noteLenOf(0, 4) == 100, "длина одной ноты живёт в проекте");
        auto f = q.writeRhythmFile(1, false);
        juce::MemoryBlock mb; f.loadFileAsData(mb);
        // the file holds the long note: the second note-off sits 479 ticks after its note-on
        int onAt = -1, offAt = -1, count = 0;
        {
            const unsigned char* d = (const unsigned char*)mb.getData();
            std::vector<std::pair<int, int>> evs;
            size_t pos = 14; int tracks = 0;
            while (pos + 8 <= mb.getSize()) {
                const size_t len = (size_t)(d[pos + 4] << 24 | d[pos + 5] << 16 | d[pos + 6] << 8 | d[pos + 7]);
                if (++tracks == 2) {
                    size_t i = pos + 8; long long tick = 0;
                    while (i < pos + 8 + len) {
                        unsigned delta = 0; while (true) { const unsigned char c = d[i++]; delta = (delta << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
                        tick += delta;
                        if (d[i] == 0xFF) { i += 2; i += 1 + d[i - 0]; continue; }
                        const int kind = d[i] & 0xF0;
                        if (kind == 0x90) { if (count++ == 1) onAt = (int)tick; }
                        else if (kind == 0x80 && onAt >= 0 && offAt < 0) offAt = (int)tick;
                        i += 3;
                    }
                }
                pos += 8 + len;
            }
        }
        expect(onAt == 480 && offAt == 480 + 479, "в файле длинная нота идёт до следующей без наложения, нота с " + juce::String(onAt) + " до " + juce::String(offAt));
        set(p.params, "euclidK", 5); p.rebuild();
        expect(p.noteLenOf(0, 4) == -1, "длины отдельных нот сбрасываются, когда рисунок меняется");
    }
    {
        RhythmProcessor p;
        set(p.params, "source", 0); set(p.params, "catalog", 2001); p.rebuild();
        auto readVel = [&](juce::File f) {
            juce::MemoryBlock mb; f.loadFileAsData(mb);
            std::vector<rhy::SourceNote> notes; int num, den; double bpm;
            rhy::readMidi((const unsigned char*)mb.getData(), mb.getSize(), notes, num, den, bpm);
            return notes;
        };
        auto plain = readVel(p.writeRhythmFile(8, false));
        std::set<int> v0; for (auto& n : plain) v0.insert(n.velocity);
        set(p.params, "velRandom", 10); set(p.params, "timeRandom", 10); p.rebuild();
        auto rnd = readVel(p.writeRhythmFile(8, false));
        std::set<int> v1; for (auto& n : rnd) v1.insert(n.velocity);
        expect(v0.size() <= 3 && v1.size() > 8 && rnd.size() == plain.size(), "в файле с рандомайзерами громкости разные, нот столько же: " + juce::String((int)v1.size()) + " значений");
        bool moved = false, sorted = true;
        for (size_t i = 0; i < rnd.size(); ++i) { if (rnd[i].tick != plain[i].tick) moved = true; if (i && rnd[i].tick < rnd[i - 1].tick) sorted = false; }
        expect(moved && sorted, "в файле с рандомайзерами начала нот сдвинуты и идут по порядку");
        set(p.params, "velRandom", 0); set(p.params, "timeRandom", 0); p.rebuild();
        auto again = readVel(p.writeRhythmFile(8, false));
        bool same = again.size() == plain.size();
        for (size_t i = 0; same && i < again.size(); ++i) if (again[i].tick != plain[i].tick || again[i].velocity != plain[i].velocity) same = false;
        expect(same, "рандомайзеры на нуле: файл как был");
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

// Core tests: catalog data, reference rhythms, generator rules, determinism, MIDI round trip, arpeggiator.
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include "Rhythm.h"
using namespace rhy;

static int failures = 0, checks = 0;
static const Feel nofeel;
static void expect(bool ok, const std::string& what) {
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL %s\n", what.c_str()); }
}
static std::string onsets(const Pattern& p) {
    std::string s;
    for (size_t i = 0; i < p.size(); ++i) if (p[i]) s += (s.empty() ? "" : ",") + std::to_string(i);
    return s;
}
static std::string byName(const char* name) {
    for (const auto& e : catalog()) if (std::string(e.name) == name) return e.pattern;
    return "?";
}
static bool sameRotation(const std::string& a, const std::string& b) { return a.size() == b.size() && (a + a).find(b) != std::string::npos; }

int main() {
    // 1. catalog data
    {
        std::set<int> ids;
        std::set<std::string> names;
        int bad = 0;
        for (const auto& e : catalog()) {
            const std::string p = e.pattern;
            if (!ids.insert(e.id).second) ++bad;
            if (p.size() < 2 || p.size() > 40) ++bad;
            if (p.find_first_not_of("xo-.") != std::string::npos) ++bad;
            if (p.find_first_of("xo-") == std::string::npos) ++bad;
            if (e.stepTicks != 120 && e.stepTicks != 240) ++bad;
            if (std::string(e.status) != "подтв." && std::string(e.status) != "расчёт") ++bad;
            if (!*categoryName(e.category)) ++bad;
            if (e.id / 1000 != e.category) ++bad;
            names.insert(e.name);
        }
        expect(bad == 0, "каталог: длины, знаки, id, статусы");
        expect(catalog().size() == 128, "каталог: 128 ритмов, найдено " + std::to_string(catalog().size()));
        expect(names.size() > 110, "каталог: имена почти не повторяются");
        expect(findCatalog(2001) != nullptr && findCatalog(99999) == nullptr, "поиск по id");
    }
    // 2. reference rhythms (patterns checked against the sources in the research note)
    {
        expect(onsets(parsePattern(byName("Клаве сон 3-2"))) == "0,3,6,10,12", "сон 3-2");
        expect(onsets(parsePattern(byName("Клаве румба 3-2"))) == "0,3,7,10,12", "румба 3-2");
        expect(onsets(parsePattern(byName("Шико"))) == "0,4,6,10,12", "шико");
        expect(onsets(parsePattern(byName("Соукус"))) == "0,3,6,10,11", "соукус");
        expect(onsets(parsePattern(byName("Гаху"))) == "0,3,6,10,14", "гаху");
        expect(onsets(parsePattern(byName("Босса-нова клаве 3-2"))) == "0,3,6,10,13", "босса-клаве");
        expect(byName("Стандартный колокол (бембе), 12/8") == "x.x.xx.x.x.x", "колокол 12");
        expect(byName("Стандартный колокол, форма 4/4") == "x..x..xx..x.x..x", "колокол 16");
        expect(byName("Clapping Music (Райх), исходная фигура") == "xxx.xx.x.xx.", "Clapping Music");
        // the clave 2-3 is the clave 3-2 half a cycle later
        expect(rotate(parsePattern(byName("Клаве сон 3-2")), 8) == parsePattern(byName("Клаве сон 2-3")), "сон 2-3 это сон 3-2 со сдвигом на 8");
    }
    // 3. Euclid, table from Toussaint 2005 and Demaine 2009 (as strings, up to rotation)
    {
        struct T { int k, n; const char* s; } t[] = {
            {3, 8, "x..x..x."}, {5, 8, "x.xx.xx."}, {4, 9, "x.x.x.x.."}, {5, 12, "x..x.x..x.x."},
            {7, 12, "x.xx.x.xx.x."}, {5, 16, "x..x..x..x..x..."}, {7, 16, "x..x.x.x..x.x.x."}, {9, 16, "x.xx.x.x.xx.x.x."}};
        for (auto& e : t) {
            const std::string got = patternString(euclid(e.k, e.n)).c_str();
            std::string g2;
            for (auto c : got) g2 += (c == 'o' ? 'x' : c);
            expect(sameRotation(g2, e.s), "E(" + std::to_string(e.k) + "," + std::to_string(e.n) + ") " + g2);
        }
        int bad = 0;
        for (int n = 2; n <= 24; ++n)
            for (int k = 1; k <= n; ++k) {
                auto p = euclid(k, n);
                if (hitCount(p) != k || (int)p.size() != n) ++bad;
            }
        expect(bad == 0, "Евклид: число ударов и длина для всех k,n до 24");
        {
            std::string t;
            for (auto c : patternString(euclid(7, 12))) t += c == 'o' ? 'x' : c;
            expect(sameRotation(t, "x.x.xx.x.x.x"), "колокол 12/8 это поворот E(7,12)");
        }
        expect(hitCount(euclid(5, 16)) == 5, "E(5,16) даёт пять ударов");
    }
    // 4. generator rules
    {
        expect(necklaces(3, 6).size() == 4 && necklaces(4, 8).size() == 10 && necklaces(2, 5).size() == 2, "ожерелья: 4, 10, 2");
        auto w = metricWeights(16);
        expect(w[0] == 5 && w[8] == 4 && w[4] == 3 && w[2] == 2 && w[1] == 1, "вес долей в 16");
        auto w12 = metricWeights(12);
        expect(w12[0] == 4 && w12[6] == 3 && w12[3] == 2 && w12[1] == 1, "вес долей в 12");
        Pattern p = parsePattern("x.o.x.o.");
        expect(retrograde(retrograde(p)) == p, "ретроград дважды");
        expect(rotate(rotate(p, 3), -3) == p, "поворот туда и обратно");
        expect(complement(parsePattern("x.x.")) == parsePattern(".o.o"), "дополнение: паузы становятся ударами");
        Pattern d;
        expect(diminish(augment(p), d) && d == p, "увеличение и уменьшение");
        expect(!diminish(parsePattern("xx.."), d), "уменьшение отказывает при ударах на нечётных шагах");
        Pattern th = thin(euclid(5, 8), 2);
        expect(hitCount(th) == 3, "прореживание убирает два удара");
        expect(th[0] == 2, "прореживание держит сильную долю");
        expect(hitCount(thin(parsePattern("x......."), 3)) == 1, "последний удар не снимается");
        Pattern de = densify(parsePattern("x......."), 3);
        expect(hitCount(de) == 4 && de[4] == 2, "уплотнение ставит удары на сильные места");
        Pattern sh = shiftAccents(parsePattern("xo-o"), 1);
        expect(patternString(sh) == "oxo-", "сдвиг акцентов на 1: уровни едут по ударам, удары стоят");
    }
    // 5. polyrhythm, polymeter, tihai
    {
        auto r = polyrhythm(3, 2, true, 4);
        expect(r.length == 6 && r.layers.size() == 3 && r.cycleTicks == 1920, "полиритм 3:2: 6 шагов, три слоя, такт 1920");
        expect(onsets(r.layers[0]) == "0,2,4" && onsets(r.layers[1]) == "0,3" && onsets(r.layers[2]) == "0,2,3,4", "полиритм 3:2: позиции");
        auto ev = cycleEvents(r, nofeel);
        std::set<int> t3;
        for (auto& e : ev) if (e.layer == 0) t3.insert(e.tick);
        expect(t3 == std::set<int>({0, 640, 1280}), "полиритм 3:2: тики первого слоя");
        auto r74 = polyrhythm(7, 4, false, 4);
        expect(r74.length == 28 && hitCount(r74.layers[0]) == 7 && hitCount(r74.layers[1]) == 4, "полиритм 7:4: 28 шагов");
        int bad = 0;
        for (int a = 1; a <= 16; ++a) for (int b = 1; b <= 15; ++b) { auto q = polyrhythm(a, b, true, 4); if (!q.ok() || q.length != lcm(a, b) || hitCount(q.layers[0]) != a || hitCount(q.layers[1]) != b) ++bad; }
        expect(bad == 0, "полиритм до 16:15 даёт верное число ударов");
        auto pm = polymeter(3, 4);
        expect(pm.length == 12 && pm.layers[0][0] == 3 && pm.layers[0][3] == 3 && pm.layers[1][4] == 3 && pm.layers[0][12 - 1] == 1, "полиметр 3 и 4: акценты совпадают через 12");
        auto pm57 = polymeter(5, 7);
        expect(pm57.length == 35, "полиметр 5 и 7: точка совпадения 35");
        bool exact = false;
        auto th = tihai(16, 5, &exact);
        expect(exact && th.ok(), "тихай 16/5 точный");
        // three phrases of five steps with a gap of one: 5+1+5+1+5 = 17 = 16 + 1
        expect(th.layers[0][0] == 3 && onsets(th.layers[0]) == "0,2,4,6,8,10,12,14", "тихай: фразы с шагом p+g, последний удар на первой доле");
        expect(!tihai(8, 5).ok(), "тихай: слишком длинная фраза");
        tihai(16, 4, &exact);
        expect(!exact, "тихай 16/4 не точный (нечётный остаток)");
    }
    // 6. transformations through build()
    {
        Settings s; s.source = sEuclid; s.k = 5; s.n = 8;
        auto base = build(s);
        s.rotate = 3;
        expect(build(s).layers[0] == rotate(base.layers[0], 3), "build: поворот");
        s = Settings(); s.source = sEuclid; s.k = 3; s.n = 8; s.augment = 1;
        auto big = build(s);
        expect(big.length == 16 && big.stepTicks == 120 && big.cycleTicks == 1920, "build: увеличение удваивает цикл");
        s.augment = 2;
        expect(build(s).length == 8 || build(s).length == 4, "build: уменьшение");
        s = Settings(); s.source = sEuclid; s.k = 5; s.n = 8; s.phase = 2;
        auto ph = build(s);
        expect(ph.layers.size() == 2 && ph.layers[1] == rotate(ph.layers[0], 2), "build: фазовый сдвиг даёт второй слой");
        s = Settings(); s.source = sCatalog; s.catalogId = catalog()[10].id;
        auto ar = build(s);
        expect(ar.ok(), "каталог: арабская ика собирается");
        // dum and tek on two layers when the pattern has both signs
        int two = 0;
        for (const auto& e : catalog()) if (e.category == 7 && std::string(e.pattern).find('o') != std::string::npos) {
            Settings q; q.source = sCatalog; q.catalogId = e.id; auto rr = build(q);
            if (rr.layers.size() == 2 && rr.layerNames[1] == "Дум") ++two;
        }
        expect(two >= 1, "дум и тек лежат на двух слоях");
        // folding of layers
        Rhythm f = polyrhythm(3, 2, true, 4);
        f.layers.push_back(f.layers[0]); f.layerNames.push_back("x");
        auto folded = foldLayers(f, 3);
        expect(folded.layers.size() == 3 && hitCount(folded.layers[2]) >= hitCount(f.layers[2]), "лишний слой сливается в последний");
    }
    // 7. randomizer: same seed same rhythm, different seeds differ, every kind is valid
    {
        for (int kind = 0; kind < rkNumKinds; ++kind) {
            Settings s; s.source = sRandom; s.randKind = kind; s.seed = 77;
            auto a = build(s), b = build(s);
            expect(a.ok() && a.layers == b.layers && a.name == b.name, "зерно 77 повторяется, вид " + std::to_string(kind));
            std::set<std::string> seen;
            for (int sd = 1; sd <= 30; ++sd) { s.seed = sd; seen.insert(patternString(build(s).layers[0])); }
            expect(seen.size() >= 10, "разные зёрна дают разные ритмы, вид " + std::to_string(kind) + ": " + std::to_string(seen.size()));
        }
        // regression value: the generator must not change between releases
        Settings s; s.source = sRandom; s.seed = 12345; s.randSteps = 16; s.randDensity = 40; s.randKind = rkEuclid;
        expect(patternString(build(s).layers[0]) == "x..o..o.o..-..o.", "зерно 12345 даёт тот же рисунок, что в версии 0.1");
        Rng r(1);
        const uint64_t v0 = r.next(), v1 = r.next();
        expect(v0 == Rng(1).next() && v0 != v1, "генератор чисел детерминирован");
    }
    // 8. MIDI: every catalog rhythm and a sweep of settings write a file that reads back
    {
        int bad = 0, total = 0;
        for (const auto& e : catalog()) {
            Settings s; s.source = sCatalog; s.catalogId = e.id;
            auto r = build(s);
            MidiOptions o; o.bars = 3;
            auto bytes = writeMidi(r, o);
            std::vector<SourceNote> notes; int num, den; double bpm;
            int hits = 0; for (auto& l : r.layers) hits += hitCount(l);
            ++total;
            if (!readMidi(bytes.data(), bytes.size(), notes, num, den, bpm) || (int)notes.size() != 3 * hits || num != r.meterNum || den != r.meterDen) ++bad;
            for (auto& n : notes) if (n.note != 60 && n.note != 48) ++bad;
        }
        expect(bad == 0, "каталог: файл читается обратно, число нот и размер совпадают, всего " + std::to_string(total));

        Settings s; s.source = sCatalog; s.catalogId = findCatalog(2001) ? 2001 : 3001;
        auto r = build(s);
        MidiOptions o; o.bars = 2; o.bpm = 97;
        auto bytes = writeMidi(r, o);
        std::vector<SourceNote> notes; int num, den; double bpm;
        readMidi(bytes.data(), bytes.size(), notes, num, den, bpm);
        expect(std::abs(bpm - 97) < 0.01, "темп в файле");
        expect(notes[0].tick == 0 && notes[0].velocity == velocityOf(r.layers[0][0]), "первая нота на нуле, скорость из акцента");
        // staccato: every note-off sits 60 ticks after its note-on
        int offBad = 0, statusBad = 0, onCount = 0;
        {
            auto be32 = [&](size_t q) { return (size_t)(bytes[q] << 24 | bytes[q + 1] << 16 | bytes[q + 2] << 8 | bytes[q + 3]); };
            size_t q = 14;
            q += 8 + be32(q + 4);                    // skip the conductor track
            const size_t end = q + 8 + be32(q + 4);
            q += 8;
            long long tick = 0;
            std::map<int, long long> open;
            while (q < end) {
                unsigned d = 0;
                while (true) { unsigned char b = bytes[q++]; d = (d << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
                tick += d;
                if (bytes[q] == 0xFF) { q += 2; q += 1 + bytes[q]; continue; }
                const int st = bytes[q], note = bytes[q + 1];
                if (st != 0x90 && st != 0x80) ++statusBad;
                if (st == 0x90) { open[note] = tick; ++onCount; }
                else if (tick - open[note] != kStaccato) ++offBad;
                q += 3;
            }
        }
        expect(statusBad == 0, "все ноты на канале 1");
        expect(offBad == 0 && onCount > 0, "каждая нота длится ровно 60 тиков (стаккато)");
        // layer notes on octaves
        auto pr = polyrhythm(3, 2, true, 4);
        MidiOptions mo; mo.bars = 1;
        auto pb = writeMidi(pr, mo);
        readMidi(pb.data(), pb.size(), notes, num, den, bpm);
        std::set<int> used; for (auto& n : notes) used.insert(n.note);
        expect(used == std::set<int>({36, 60, 48}), "полиритм: первый слой C1, второй C3, результат C2");
        MidiOptions sep; sep.separateTracks = true; sep.bars = 1;
        auto sb = writeMidi(pr, sep);
        readMidi(sb.data(), sb.size(), notes, num, den, bpm);
        used.clear(); for (auto& n : notes) used.insert(n.note);
        expect(used == std::set<int>({60}), "каждый слой на своём треке, везде C3");
        // import: write, read, rebuild the pattern
        Settings cs; cs.source = sCatalog; cs.catalogId = 2001;
        auto src = build(cs);
        auto sbytes = writeMidi(src, MidiOptions());
        readMidi(sbytes.data(), sbytes.size(), notes, num, den, bpm);
        auto back = rhythmFromNotes(notes, num, den, "x");
        std::string a = patternString(src.layers[0]), b2;
        for (auto v : back.layers[0]) b2 += v ? 'x' : '.';
        std::string a2; for (auto c : a) a2 += (c == '.' ? '.' : 'x');
        expect(a2 == b2, "ритм из MIDI совпадает с исходным по ударам");
        // sweep
        int sweepBad = 0;
        for (int i = 0; i < 400; ++i) {
            Settings q;
            Rng g((uint64_t)i + 5);
            q.source = g.below(sImported);
            q.catalogId = catalog()[(size_t)g.below((int)catalog().size())].id;
            q.k = 1 + g.below(12); q.n = q.k + 1 + g.below(14); q.rot = g.below(16);
            q.polyA = 2 + g.below(15); q.polyB = 2 + g.below(14);
            q.seed = g.below(100000); q.randKind = g.below(rkNumKinds); q.randSteps = 8 + g.below(16); q.randDensity = 10 + g.below(70);
            q.tihaiLen = 8 + g.below(24); q.tihaiPhrase = 2 + g.below(7);
            q.rotate = g.below(9); q.retro = g.below(2); q.invert = g.below(2); q.augment = g.below(3);
            q.density = g.below(7) - 3; q.accentShift = g.below(4); q.phase = g.below(4); q.swing = g.below(51);
            auto r2 = build(q);
            if (!r2.ok()) { if (q.source == sTihai || q.source == sPoly || q.source == sPolymeter) continue; ++sweepBad; continue; }
            MidiOptions mm; mm.feel = feelOf(q); mm.bars = 2;
            auto by = writeMidi(r2, mm);
            int h = 0; for (auto& l : r2.layers) { if ((int)l.size() != r2.length) ++sweepBad; h += hitCount(l); }
            if (!readMidi(by.data(), by.size(), notes, num, den, bpm) || (int)notes.size() != 2 * h) ++sweepBad;
            auto s1 = build(q); if (s1.layers != r2.layers) ++sweepBad;
        }
        expect(sweepBad == 0, "400 случайных настроек: собираются, пишутся, читаются, повторяются");
        // swing
        Settings ss; ss.source = sEuclid; ss.k = 8; ss.n = 16;
        auto sr = build(ss);
        Feel f30; f30.swing = 30;
        auto evs = cycleEvents(sr, f30);
        int off = 0, on = 0; for (auto& e : evs) { if (e.step % 2) { on += (e.tick == e.step * 120 + 36); } else off += (e.tick == e.step * 120); }
        expect(off + on == (int)evs.size(), "свинг 30% сдвигает нечётные шаги на 36 тиков");
        expect(cycleEvents(sr, nofeel)[0].length == kStaccato, "длина ноты всегда 60 тиков");
    }
    // 8b. a note list written as a file reads back unchanged
    {
        std::vector<SourceNote> in{{0, 60, 100}, {240, 64, 80}, {480, 60, 112}}, out;
        auto bytes = writeNotes(in, 3, 4, 90, "Rhythm Arranger Live take");
        int num, den; double bpm;
        bool ok = readMidi(bytes.data(), bytes.size(), out, num, den, bpm);
        expect(ok && out.size() == 3 && out[1].tick == 240 && out[1].note == 64 && out[2].velocity == 112 && num == 3 && den == 4 && std::abs(bpm - 90) < 0.01, "запись Live читается обратно");
    }
    // 8c. expression: length, dynamics, curve, randomizers
    {
        Settings s; s.source = sEuclid; s.k = 4; s.n = 16;
        auto r = build(s);
        // length: 0 is staccato, 100 reaches one tick before the next note, never further
        Feel f; f.noteLength = 100;
        auto ev = cycleEvents(r, f);
        bool full = true, stac = true;
        for (auto& e : ev) { if (e.length != e.gapNext - 1) full = false; }
        for (auto& e : cycleEvents(r, nofeel)) if (e.length != kStaccato) stac = false;
        expect(full && stac, "длина: 0 это стаккато 60, 100 это до начала следующей ноты без наложения");
        f.noteLength = 50;
        auto half = cycleEvents(r, f);
        expect(half[0].length > kStaccato && half[0].length < half[0].gapNext - 1, "длина 50 лежит между стаккато и полной");
        // a note of its own length
        Settings s2 = s; s2.noteLens.push_back({0, 4, 100});
        auto r2 = build(s2);
        int own = 0, other = 0;
        for (auto& e : cycleEvents(r2, nofeel)) { if (e.step == 4) own = e.length; else other = e.length; }
        expect(own == 479 && other == kStaccato, "длина одной выбранной ноты");
        // very close notes get a shorter staccato instead of an overlap
        Settings s3; s3.source = sEuclid; s3.k = 16; s3.n = 16; auto r3 = build(s3);
        bool nooverlap = true;
        for (auto& e : cycleEvents(r3, f)) if (e.length >= e.gapNext) nooverlap = false;
        expect(nooverlap, "длина не пересекает начало следующей ноты");
        // dynamics: 50 as it was, 0 flat, 100 wider
        Settings d; d.source = sCatalog; d.catalogId = 2001;
        auto rd = build(d);
        Feel fd;
        auto base = cycleEvents(rd, fd);
        fd.dynamics = 0; auto flat = cycleEvents(rd, fd);
        fd.dynamics = 100; auto wide = cycleEvents(rd, fd);
        int vb = 0, vf = 0, vw = 0;
        for (size_t i = 0; i < base.size(); ++i) {
            if (base[i].level == 3) { vb = base[i].velocity; vf = flat[i].velocity; vw = wide[i].velocity; }
        }
        expect(vb == 112 && vf == 90 && vw == 127, "динамика: 50 как раньше, 0 ровно, 100 шире");
        // curve: first note keeps its force, the middle dips, the end rises
        Settings c; c.source = sEuclid; c.k = 16; c.n = 16; c.curve = 0;
        auto rc = build(c);
        Feel fc; fc.curve = 10;
        auto flatc = cycleEvents(rc, nofeel), curved = cycleEvents(rc, fc);
        expect(curved[0].velocity == flatc[0].velocity, "кривая: первый удар не меняется");
        expect(curved[8].velocity < flatc[8].velocity - 8, "кривая: середина проседает");
        expect(curved[15].velocity > flatc[15].velocity + 8, "кривая: к концу растёт");
        // randomizers: 0 changes nothing, deterministic, bounded, order and no overlap
        Settings h; h.source = sCatalog; h.catalogId = 2001;
        auto rh = build(h);
        Feel fh; fh.velRandom = 10; fh.timeRandom = 10; fh.humanSeed = 5; fh.noteLength = 100;
        auto bh = cycleEvents(rh, fh);
        bool same0 = true;
        { Feel z; auto be = cycleEvents(rh, z); for (auto& e : be) { auto o = shapeEvent(e, 3, z); if (o.tick != e.tick || o.velocity != e.velocity || o.length != e.length) same0 = false; } }
        expect(same0, "рандомайзеры на нуле ничего не меняют");
        bool det = true, bounded = true, ordered = true, clear = true, varies = false;
        for (long long cyc = 0; cyc < 40; ++cyc) {
            std::vector<Event> sh;
            for (auto& e : bh) {
                auto a = shapeEvent(e, cyc, fh), b = shapeEvent(e, cyc, fh);
                if (a.tick != b.tick || a.velocity != b.velocity || a.length != b.length) det = false;
                if (std::abs(a.tick - e.tick) > 30 || std::abs(a.velocity - e.velocity) > 30 || a.velocity < 1 || a.velocity > 127) bounded = false;
                if (a.tick != e.tick || a.velocity != e.velocity) varies = true;
                sh.push_back(a);
            }
            for (size_t i = 0; i + 1 < sh.size(); ++i) {                  // single layer: sorted by tick, next in line
                if (sh[i + 1].tick <= sh[i].tick) ordered = false;
                if (sh[i].tick + sh[i].length >= sh[i + 1].tick) clear = false;
            }
        }
        expect(det && bounded && varies, "рандомайзеры: повторяемы, в пределах 30 тиков и 30 скоростей, действительно меняют");
        expect(ordered && clear, "рандомайзеры: порядок нот не меняется, длина не наезжает на следующую");
        // wrapped note: the last note of the cycle against the first of the next one
        auto lastShaped = shapeEvent(bh.back(), 7, fh);
        auto firstNext = shapeEvent(bh.front(), 8, fh);
        expect(lastShaped.tick + lastShaped.length < rh.cycleTicks + firstNext.tick, "длина последней ноты не залезает в первую следующего такта");
        // the file is bounded too and starts at 0 or later
        MidiOptions mo; mo.bars = 6; mo.feel = fh;
        auto bytes = writeMidi(rh, mo);
        std::vector<SourceNote> notes; int num, den; double bpm;
        readMidi(bytes.data(), bytes.size(), notes, num, den, bpm);
        expect(notes.size() == 30 && notes[0].tick >= 0, "файл с рандомайзерами читается, пять ударов на шесть тактов");
        // layers of the polyrhythm and polymeter: C1, C3, result on C2
        auto pr = polyrhythm(3, 2, true, 4);
        expect(pr.noteOf(0) == 36 && pr.noteOf(1) == 60 && pr.noteOf(2) == 48, "полиритм: слои C1, C3, результат C2");
        auto pm = polymeter(3, 4);
        expect(pm.noteOf(0) == 36 && pm.noteOf(1) == 60, "полиметр: слои C1 и C3");
        Settings q; q.source = sEuclid; q.k = 3; q.n = 8;
        expect(build(q).noteOf(0) == 60, "обычный ритм остаётся на C3");
        expect(euclidName(3, 7) == "Евклид: 3 удара на 7 шагов" && euclidName(5, 8) == "Евклид: 5 ударов на 8 шагов" && euclidName(2, 3) == "Евклид: 2 удара на 3 шага" &&
               euclidName(1, 21) == "Евклид: 1 удар на 21 шаг", "понятные имена евклидовых ритмов");
    }
    // 9. arpeggiator
    {
        std::vector<int> chord{48, 55, 64, 72};
        expect(arpNotes(amGate, chord, 0, 0) == chord, "гейт: весь аккорд");
        expect(arpNotes(amArpeggio, chord, 0, 0) == std::vector<int>{48} && arpNotes(amArpeggio, chord, 0, 5) == std::vector<int>{55}, "арпеджио по кругу");
        expect(arpNotes(amVoiceSplit, chord, 0, 0) == std::vector<int>{48}, "расщепление: нижний слой берёт низ");
        expect(arpNotes(amVoiceSplit, chord, 1, 0) == std::vector<int>{55}, "расщепление: второй слой берёт второй голос");
        expect(arpNotes(amVoiceSplit, {60}, 3, 0) == std::vector<int>{60}, "расщепление: один звук для всех слоёв");
        expect(arpNotes(amGate, {}, 0, 0).empty(), "пустой аккорд молчит");
    }
    std::printf("%d проверок, ошибок %d\n", checks, failures);
    return failures ? 1 : 0;
}

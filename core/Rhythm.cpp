#include "Rhythm.h"
#include <algorithm>

namespace rhy {

int velocityOf(int level) {
    static const int v[4] = {0, 64, 90, 112};
    return v[level < 0 ? 0 : level > 3 ? 3 : level];
}

int gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }
int lcm(int a, int b) { return a / gcd(a, b) * b; }

// ---------- notation
Pattern parsePattern(const std::string& s) {
    Pattern p;
    for (char c : s) p.push_back(c == 'x' ? 3 : c == 'o' ? 2 : c == '-' ? 1 : 0);
    return p;
}
std::string patternString(const Pattern& p) {
    std::string s;
    for (auto v : p) s += v >= 3 ? 'x' : v == 2 ? 'o' : v == 1 ? '-' : '.';
    return s;
}
int hitCount(const Pattern& p) { int n = 0; for (auto v : p) n += v > 0; return n; }

const CatalogEntry* findCatalog(int id) {
    for (const auto& e : catalog()) if (e.id == id) return &e;
    return nullptr;
}

// ---------- building blocks
Pattern rotate(const Pattern& p, int k) {
    const int n = (int)p.size();
    if (n == 0) return p;
    k = ((k % n) + n) % n;
    Pattern o(p.size());
    for (int i = 0; i < n; ++i) o[(size_t)i] = p[(size_t)((i + k) % n)];
    return o;
}

Pattern euclid(int k, int n, int rotation) {
    Pattern p;
    if (n < 1) return p;
    k = std::max(0, std::min(k, n));
    for (int i = 0; i < n; ++i) p.push_back((i * k) % n < k ? 2 : 0);
    return rotate(p, rotation);
}

Pattern additive(const std::vector<int>& groups) {
    Pattern p;
    for (int g : groups)
        for (int i = 0; i < g; ++i) p.push_back(i == 0 ? 3 : 0);
    return p;
}

std::vector<int> metricWeights(int n) {
    std::vector<int> w((size_t)std::max(n, 0), 1);
    if (n < 2) return w;
    std::vector<int> q;
    int m = n;
    for (int f = 2; f * f <= m; ++f)
        while (m % f == 0) { q.push_back(f); m /= f; }
    if (m > 1) q.push_back(m);
    std::vector<int> periods{n};
    for (int f : q) periods.push_back(periods.back() / f);
    for (int i = 0; i < n; ++i) {
        int c = 0;
        for (int per : periods) if (i % per == 0) ++c;
        w[(size_t)i] = c;
    }
    return w;
}

Pattern retrograde(const Pattern& p) { return Pattern(p.rbegin(), p.rend()); }

Pattern complement(const Pattern& p) {
    Pattern o(p.size());
    for (size_t i = 0; i < p.size(); ++i) o[i] = p[i] ? 0 : 2;
    return o;
}

Pattern augment(const Pattern& p) {
    if (p.size() * 2 > (size_t)kMaxLength) return p;
    Pattern o;
    for (auto v : p) { o.push_back(v); o.push_back(0); }
    return o;
}

bool diminish(const Pattern& p, Pattern& out) {
    if (p.size() < 2 || p.size() % 2) return false;
    for (size_t i = 1; i < p.size(); i += 2) if (p[i]) return false;
    out.clear();
    for (size_t i = 0; i < p.size(); i += 2) out.push_back(p[i]);
    return true;
}

Pattern thin(const Pattern& p, int count) {
    Pattern o = p;
    const auto w = metricWeights((int)o.size());
    for (int c = 0; c < count && hitCount(o) > 1; ++c) {
        int best = -1;
        for (int i = 0; i < (int)o.size(); ++i)
            if (o[(size_t)i] && (best < 0 || w[(size_t)i] <= w[(size_t)best])) best = i;
        o[(size_t)best] = 0;
    }
    return o;
}

Pattern densify(const Pattern& p, int count) {
    Pattern o = p;
    const auto w = metricWeights((int)o.size());
    for (int c = 0; c < count; ++c) {
        int best = -1;
        for (int i = 0; i < (int)o.size(); ++i)
            if (!o[(size_t)i] && (best < 0 || w[(size_t)i] > w[(size_t)best])) best = i;
        if (best < 0) break;
        o[(size_t)best] = 2;
    }
    return o;
}

Pattern shiftAccents(const Pattern& p, int k) {
    std::vector<int> pos;
    std::vector<uint8_t> lv;
    for (int i = 0; i < (int)p.size(); ++i) if (p[(size_t)i]) { pos.push_back(i); lv.push_back(p[(size_t)i]); }
    Pattern o = p;
    const int m = (int)pos.size();
    if (m < 2) return o;
    k = ((k % m) + m) % m;
    for (int i = 0; i < m; ++i) o[(size_t)pos[(size_t)i]] = lv[(size_t)((i + m - k) % m)];
    return o;
}

std::vector<std::vector<int>> necklaces(int k, int n) {
    std::vector<std::vector<int>> out;
    if (n < 1 || n > 20 || k < 0 || k > n) return out;
    std::vector<int> pos((size_t)k);
    auto canonical = [&](const std::vector<int>& ps) {
        std::string s((size_t)n, '0');
        for (int p : ps) s[(size_t)p] = '1';
        for (int r = 1; r < n; ++r) {
            std::string t = s.substr((size_t)r) + s.substr(0, (size_t)r);
            if (t < s) return false;
        }
        return true;
    };
    // all combinations in lexicographic order
    std::vector<int> c((size_t)k);
    for (int i = 0; i < k; ++i) c[(size_t)i] = i;
    while (true) {
        if (canonical(c)) out.push_back(c);
        int i = k - 1;
        while (i >= 0 && c[(size_t)i] == n - k + i) --i;
        if (i < 0) break;
        ++c[(size_t)i];
        for (int j = i + 1; j < k; ++j) c[(size_t)j] = c[(size_t)j - 1] + 1;
    }
    return out;
}

// ---------- layout helpers
static void setMeter(Rhythm& r) {
    if (r.stepTicks > 0) {
        r.cycleTicks = r.length * r.stepTicks;
        int num = r.length, den = r.stepTicks == 120 ? 16 : r.stepTicks == 240 ? 8 : 0;
        if (den == 0) { num = r.cycleTicks / 120; den = 16; }
        if (den == 16 && num % 4 == 0) { num /= 4; den = 4; }
        else if (den == 8 && num % 3 != 0 && num % 2 == 0) { num /= 2; den = 4; }
        r.meterNum = num; r.meterDen = den;
    } else if (r.cycleTicks % kTpb == 0) {
        r.meterNum = r.cycleTicks / kTpb; r.meterDen = 4;
    } else {
        r.meterNum = r.cycleTicks / 120; r.meterDen = 16;
    }
}

// the hits that sit on the strongest place of the bar get the accent, the rest are normal
static Pattern accentByWeight(Pattern p) {
    const auto w = metricWeights((int)p.size());
    int top = 0;
    for (size_t i = 0; i < p.size(); ++i) top = std::max(top, w[i]);
    for (size_t i = 0; i < p.size(); ++i) if (p[i]) p[i] = (w[i] == top && top > 1) ? 3 : 2;
    return p;
}

static Rhythm fromPattern(const std::string& name, const std::string& midiName, int stepTicks, const Pattern& p) {
    Rhythm r;
    r.name = name; r.midiName = midiName;
    r.length = (int)p.size();
    r.stepTicks = stepTicks;
    r.layers = {p};
    r.layerNames = {"Ритм"};
    setMeter(r);
    return r;
}

static Rhythm fromCatalog(const CatalogEntry& e) {
    std::string pat = e.pattern;
    const bool two = pat.find('o') != std::string::npos;
    Rhythm r;
    r.name = e.name;
    r.midiName = "Rhythm " + std::to_string(e.id);
    r.info = std::string(e.region) + "; " + e.grouping + "; " + e.source + "; " + e.status;
    r.length = (int)pat.size();
    r.stepTicks = e.stepTicks;
    Pattern p = parsePattern(pat);
    if (e.category == 7 && two && pat.find('x') != std::string::npos) {
        // dum and tek on two octaves: tek first (C3), dum below it (C2)
        Pattern tek(p.size()), dum(p.size());
        for (size_t i = 0; i < p.size(); ++i) { if (pat[i] == 'o') tek[i] = 2; if (pat[i] == 'x') dum[i] = 3; }
        r.layers = {tek, dum};
        r.layerNames = {"Тек", "Дум"};
    } else {
        if (!two) for (size_t i = 0; i < p.size(); ++i) if (p[i] == 3) p[i] = 2;
        if (!two && !p.empty() && p[0] == 2) p[0] = 3;
        r.layers = {p};
        r.layerNames = {"Ритм"};
    }
    setMeter(r);
    return r;
}

Rhythm polyrhythm(int a, int b, bool resultant, int beats) {
    Rhythm r;
    if (a < 1 || b < 1 || a > 16 || b > 16) return r;
    const int L = lcm(a, b);
    if (L > kMaxLength) return r;
    beats = std::max(1, std::min(beats, 16));
    Pattern pa((size_t)L), pb((size_t)L), pr((size_t)L);
    for (int i = 0; i < L; ++i) {
        const bool ha = i % (L / a) == 0, hb = i % (L / b) == 0;
        pa[(size_t)i] = ha ? (i == 0 ? 3 : 2) : 0;
        pb[(size_t)i] = hb ? (i == 0 ? 3 : 2) : 0;
        pr[(size_t)i] = (ha || hb) ? (i == 0 ? 3 : 2) : 0;
    }
    r.name = std::to_string(a) + ":" + std::to_string(b);
    r.midiName = "Polyrhythm " + std::to_string(a) + "-" + std::to_string(b);
    r.info = "Полиритм " + r.name + ", общий цикл " + std::to_string(L) + " шагов";
    r.length = L;
    r.cycleTicks = beats * kTpb;
    r.stepTicks = 0;
    r.layers = {pa, pb};
    r.layerNames = {"Слой " + std::to_string(a), "Слой " + std::to_string(b)};
    if (resultant) { r.layers.push_back(pr); r.layerNames.push_back("Результат"); }
    setMeter(r);
    return r;
}

Rhythm polymeter(int a, int b) {
    Rhythm r;
    if (a < 2 || b < 2 || a > 16 || b > 16) return r;
    const int L = lcm(a, b);
    if (L > kMaxLength) return r;
    Pattern pa((size_t)L), pb((size_t)L);
    for (int i = 0; i < L; ++i) {
        pa[(size_t)i] = i % a == 0 ? 3 : 1;
        pb[(size_t)i] = i % b == 0 ? 3 : 1;
    }
    r.name = std::to_string(a) + " против " + std::to_string(b);
    r.midiName = "Polymeter " + std::to_string(a) + "-" + std::to_string(b);
    r.info = "Полиметр: один пульс, такты по " + std::to_string(a) + " и " + std::to_string(b) +
             ", акценты совпадают через " + std::to_string(L) + " шагов";
    r.length = L;
    r.stepTicks = 240;
    r.layers = {pa, pb};
    r.layerNames = {"Такт " + std::to_string(a), "Такт " + std::to_string(b)};
    setMeter(r);
    return r;
}

Rhythm tihai(int U, int p, bool* exact) {
    Rhythm r;
    if (exact) *exact = false;
    if (U < 4 || U > kMaxLength || p < 2 || 3 * p > U + 1) return r;
    const int rest = U + 1 - 3 * p;
    const int g = rest / 2;
    if (exact) *exact = rest % 2 == 0;
    Pattern ph = euclid(std::max(1, (p + 1) / 2), p);
    ph[(size_t)p - 1] = 2;
    Pattern out((size_t)U, 0);
    for (int i = 0; i < 3; ++i)
        for (int o = 0; o < p; ++o)
            if (ph[(size_t)o]) {
                const int idx = (i * (p + g) + o) % U;
                out[(size_t)idx] = std::max<uint8_t>(out[(size_t)idx], 2);
            }
    out[0] = 3;
    r = fromPattern("Тихай " + std::to_string(U) + "/" + std::to_string(p), "Tihai " + std::to_string(U) + "-" + std::to_string(p), 120, out);
    r.info = "Фраза " + std::to_string(p) + " шагов трижды с паузой " + std::to_string(g) +
             "; последний удар третьей фразы приходится на первую долю следующего цикла (3p+2g=U+1)";
    return r;
}

static Pattern randomPattern(const Settings& s, std::string& label) {
    const int n = std::max(4, std::min(s.randSteps, 32));
    const int k = std::max(1, std::min((n * s.randDensity + 50) / 100, n - 1));
    Rng rng((uint64_t)(unsigned)s.seed);
    Pattern p((size_t)n, 0);
    switch (s.randKind) {
        case rkAdditive: {
            std::vector<int> g;
            int left = n;
            while (left > 0) {
                if (left <= 4) { if (left == 1 && !g.empty()) g.back() += 1; else g.push_back(left); break; }
                const int sz = 2 + rng.below(3);                  // 2, 3 or 4
                g.push_back(sz);
                left -= sz;
            }
            p = additive(g);
            for (auto& v : p) if (v == 3) v = 2;
            label = "группы";
            for (size_t i = 0; i < g.size(); ++i) label += (i ? "+" : " ") + std::to_string(g[i]);
            break;
        }
        case rkWeighted: {
            const auto w = metricWeights(n);
            std::vector<int> pool;
            for (int i = 0; i < n; ++i) pool.push_back(i);
            for (int c = 0; c < k; ++c) {
                long long total = 0;
                for (int i : pool) total += (long long)w[(size_t)i] * w[(size_t)i];
                long long pick = (long long)(rng.next() % (uint64_t)total);
                for (size_t j = 0; j < pool.size(); ++j) {
                    pick -= (long long)w[(size_t)pool[j]] * w[(size_t)pool[j]];
                    if (pick < 0) { p[(size_t)pool[j]] = 2; pool.erase(pool.begin() + (long)j); break; }
                }
            }
            label = "по весу долей";
            break;
        }
        case rkNecklace: {
            auto all = necklaces(k, n);
            if (all.empty()) { p = euclid(k, n, rng.below(n)); label = "евклидов"; break; }
            const auto& pos = all[(size_t)rng.below((int)all.size())];
            for (int x : pos) p[(size_t)x] = 2;
            p = rotate(p, rng.below(n));
            label = "ожерелье из " + std::to_string(all.size());
            break;
        }
        default:
            p = euclid(k, n, rng.below(n));
            label = "евклидов";
    }
    p = accentByWeight(p);
    // a few soft hits, chosen by the same generator so the seed fixes them
    for (size_t i = 0; i < p.size(); ++i) if (p[i] == 2 && rng.below(5) == 0) p[i] = 1;
    return p;
}

Rhythm build(const Settings& s, const Rhythm* imported) {
    Rhythm r;
    switch (s.source) {
        case sCatalog: { const auto* e = findCatalog(s.catalogId); if (e) r = fromCatalog(*e); break; }
        case sEuclid: {
            Pattern p = accentByWeight(euclid(s.k, s.n, s.rot));
            r = fromPattern("E(" + std::to_string(s.k) + "," + std::to_string(s.n) + ")", "Euclid " + std::to_string(s.k) + "-" + std::to_string(s.n), 120, p);
            r.info = "Евклидов ритм: " + std::to_string(s.k) + " ударов на " + std::to_string(s.n) + " шагов, поворот " + std::to_string(s.rot);
            break;
        }
        case sPoly: r = polyrhythm(s.polyA, s.polyB, s.resultant, s.polyBeats); break;
        case sPolymeter: r = polymeter(s.polyA, s.polyB); break;
        case sRandom: {
            std::string label;
            Pattern p = randomPattern(s, label);
            r = fromPattern("Случайный " + std::to_string(s.seed), "Random seed " + std::to_string(s.seed), 120, p);
            r.info = "Зерно " + std::to_string(s.seed) + ", " + label + ", шагов " + std::to_string(p.size());
            break;
        }
        case sTihai: r = tihai(s.tihaiLen, s.tihaiPhrase); break;
        case sImported: if (imported) r = *imported; break;
        default: break;
    }
    if (!r.ok()) return r;
    const bool grid = r.stepTicks > 0;

    // transformations, in the order of the settings
    for (auto& l : r.layers) {
        if (s.rotate) l = rotate(l, s.rotate);
        if (s.retro) l = retrograde(l);
        if (s.invert) l = complement(l);
    }
    if (s.augment == 1 && r.length * 2 <= kMaxLength) {
        for (auto& l : r.layers) l = augment(l);
        r.length *= 2;
        if (!grid) r.cycleTicks *= 2;
    } else if (s.augment == 2) {
        std::vector<Pattern> out;
        bool all = true;
        for (const auto& l : r.layers) { Pattern o; if (diminish(l, o)) out.push_back(o); else { all = false; break; } }
        if (all) { r.layers = out; r.length /= 2; if (!grid) r.cycleTicks /= 2; }
    }
    for (auto& l : r.layers) {
        if (s.density < 0) l = thin(l, -s.density);
        if (s.density > 0) l = densify(l, s.density);
        if (s.accentShift) l = shiftAccents(l, s.accentShift);
    }
    if (s.phase > 0 && (int)r.layers.size() < kMaxLayers) {
        r.layers.push_back(rotate(r.layers[0], s.phase));
        r.layerNames.push_back("Фаза +" + std::to_string(s.phase));
    }
    setMeter(r);
    return r;
}

Rhythm foldLayers(const Rhythm& r, int maxLayers) {
    if ((int)r.layers.size() <= maxLayers || maxLayers < 1) return r;
    Rhythm o = r;
    o.layers.resize((size_t)maxLayers);
    o.layerNames.resize((size_t)maxLayers);
    auto& target = o.layers.back();
    for (size_t j = (size_t)maxLayers; j < r.layers.size(); ++j)
        for (size_t i = 0; i < target.size(); ++i)
            if (r.layers[j][i] && !target[i]) target[i] = 1;
    return o;
}

// ---------- events
int swingDelay(const Rhythm& r, int step, int swingPercent) {
    if (r.stepTicks <= 0 || swingPercent <= 0 || (step & 1) == 0) return 0;
    return r.stepTicks * std::min(swingPercent, 50) / 100;
}

std::vector<Event> cycleEvents(const Rhythm& r, int swing) {
    std::vector<Event> ev;
    for (size_t l = 0; l < r.layers.size(); ++l)
        for (int i = 0; i < r.length; ++i) {
            const int lv = r.layers[l][(size_t)i];
            if (!lv) continue;
            ev.push_back({r.tickOf(i) + swingDelay(r, i, swing), (int)l, i, lv, velocityOf(lv), kStaccato});
        }
    std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.layer < b.layer;
    });
    return ev;
}

// ---------- live
std::vector<int> arpNotes(int mode, const std::vector<int>& chord, int layer, int hitIndex) {
    std::vector<int> out;
    if (chord.empty()) return out;
    const int n = (int)chord.size();
    switch (mode) {
        case amGate: out = chord; break;
        case amArpeggio: out.push_back(chord[(size_t)(((hitIndex % n) + n) % n)]); break;
        default: {
            static const int rank[kMaxLayers] = {1, 0, 2, 3};      // C3 layer takes the second voice from the bottom
            out.push_back(chord[(size_t)(rank[std::max(0, std::min(layer, kMaxLayers - 1))] % n)]);
        }
    }
    return out;
}

}  // namespace rhy

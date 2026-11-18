#include "Rhythm.h"
#include <algorithm>
#include <cmath>

namespace rhy {

namespace {
void putVlq(std::vector<unsigned char>& o, unsigned v) {
    unsigned char b[5];
    int n = 0;
    b[n++] = v & 0x7F;
    while (v >>= 7) b[n++] = (unsigned char)((v & 0x7F) | 0x80);
    while (n) o.push_back(b[--n]);
}
void put32(std::vector<unsigned char>& o, unsigned v) { for (int s = 24; s >= 0; s -= 8) o.push_back((unsigned char)(v >> s)); }
void put16(std::vector<unsigned char>& o, unsigned v) { o.push_back((unsigned char)(v >> 8)); o.push_back((unsigned char)v); }

void putTrack(std::vector<unsigned char>& file, const std::vector<unsigned char>& body) {
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    put32(file, (unsigned)body.size());
    file.insert(file.end(), body.begin(), body.end());
}
void putName(std::vector<unsigned char>& t, const std::string& name) {
    t.insert(t.end(), {0x00, 0xFF, 0x03, (unsigned char)name.size()});
    t.insert(t.end(), name.begin(), name.end());
}

struct Raw { long long tick; int order; int note, vel; };   // order 0 off, 1 on

std::vector<unsigned char> noteTrack(const std::string& name, std::vector<Raw> ev, long long endTick) {
    std::sort(ev.begin(), ev.end(), [](const Raw& a, const Raw& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.order != b.order) return a.order < b.order;
        return a.note < b.note;
    });
    std::vector<unsigned char> t;
    putName(t, name);
    long long last = 0;
    for (const auto& e : ev) {
        putVlq(t, (unsigned)(e.tick - last));
        last = e.tick;
        t.push_back(e.order ? 0x90 : 0x80);          // channel 1
        t.push_back((unsigned char)e.note);
        t.push_back((unsigned char)(e.order ? e.vel : 0));
    }
    putVlq(t, (unsigned)std::max(0LL, endTick - last));
    t.insert(t.end(), {0xFF, 0x2F, 0x00});
    return t;
}
}  // namespace

std::vector<unsigned char> writeMidi(const Rhythm& r, const MidiOptions& o) {
    std::vector<unsigned char> f;
    if (!r.ok()) return f;
    const int bars = std::max(1, std::min(o.bars, 64));
    const auto cyc = cycleEvents(r, o.feel);
    const bool sep = o.separateTracks;
    const int ntracks = sep ? (int)r.layers.size() : 1;
    f.insert(f.end(), {'M', 'T', 'h', 'd'});
    put32(f, 6);
    put16(f, 1);
    put16(f, (unsigned)(1 + ntracks));
    put16(f, (unsigned)kTpb);

    // conductor track: name, tempo, meter
    std::vector<unsigned char> c;
    putName(c, r.midiName.empty() ? "Rhythm" : r.midiName);
    const unsigned mpq = (unsigned)std::lround(60000000.0 / std::max(20.0, std::min(o.bpm, 400.0)));
    c.insert(c.end(), {0x00, 0xFF, 0x51, 0x03, (unsigned char)(mpq >> 16), (unsigned char)(mpq >> 8), (unsigned char)mpq});
    int dd = 0;
    for (int d = r.meterDen; d > 1; d >>= 1) ++dd;
    c.insert(c.end(), {0x00, 0xFF, 0x58, 0x04, (unsigned char)r.meterNum, (unsigned char)dd, 24, 8});
    c.insert(c.end(), {0x00, 0xFF, 0x2F, 0x00});
    putTrack(f, c);

    const long long endTick = (long long)bars * r.cycleTicks;
    for (int tr = 0; tr < ntracks; ++tr) {
        std::vector<Raw> ev;
        for (int b = 0; b < bars; ++b)
            for (const auto& base : cyc) {
                if (sep && base.layer != tr) continue;
                const Event e = shapeEvent(base, b, o.feel);
                const int note = sep ? kLayerNote[0] : r.noteOf(e.layer);
                const long long t = std::max(0LL, (long long)b * r.cycleTicks + e.tick);
                ev.push_back({t, 1, note, e.velocity});
                ev.push_back({t + e.length, 0, note, 0});
            }
        std::string name = r.midiName.empty() ? "Rhythm" : r.midiName;
        if (sep) name += " layer " + std::to_string(tr + 1);
        long long end = endTick;
        for (const auto& e : ev) end = std::max(end, e.tick);
        putTrack(f, noteTrack(name, ev, end));
    }
    return f;
}

bool readMidi(const unsigned char* d, size_t size, std::vector<SourceNote>& notes, int& num, int& den, double& bpm) {
    notes.clear();
    num = 4; den = 4; bpm = 120;
    auto u16 = [&](size_t p) { return (unsigned)(d[p] << 8 | d[p + 1]); };
    auto u32 = [&](size_t p) { return (unsigned)(d[p] << 24 | d[p + 1] << 16 | d[p + 2] << 8 | d[p + 3]); };
    if (size < 14 || d[0] != 'M' || d[1] != 'T' || d[2] != 'h' || d[3] != 'd') return false;
    const unsigned tracks = u16(10), div = u16(12);
    if (div == 0 || (div & 0x8000)) return false;
    size_t p = 8 + u32(4);
    bool gotTempo = false, gotMeter = false;
    for (unsigned t = 0; t < tracks && p + 8 <= size; ++t) {
        if (d[p] != 'M' || d[p + 1] != 'T' || d[p + 2] != 'r' || d[p + 3] != 'k') return false;
        const size_t len = u32(p + 4);
        size_t q = p + 8;
        const size_t end = std::min(size, q + len);
        long long tick = 0;
        int running = 0;
        while (q < end) {
            unsigned delta = 0;
            for (int i = 0; i < 4 && q < end; ++i) { const unsigned char b = d[q++]; delta = (delta << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
            tick += delta;
            if (q >= end) break;
            int st = d[q];
            if (st & 0x80) { ++q; if (st < 0xF0) running = st; } else st = running;
            if (st == 0xFF) {
                if (q + 1 > end) break;
                const int type = d[q++];
                unsigned l = 0;
                for (int i = 0; i < 4 && q < end; ++i) { const unsigned char b = d[q++]; l = (l << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
                if (type == 0x51 && l == 3 && q + 3 <= end && !gotTempo) {
                    const unsigned mpq = (unsigned)(d[q] << 16 | d[q + 1] << 8 | d[q + 2]);
                    if (mpq) { bpm = 60000000.0 / mpq; gotTempo = true; }
                } else if (type == 0x58 && l >= 2 && q + 2 <= end && !gotMeter) {
                    num = std::max(1, (int)d[q]); den = 1 << std::min(6, (int)d[q + 1]); gotMeter = true;
                }
                q += l;
            } else if (st == 0xF0 || st == 0xF7) {
                unsigned l = 0;
                for (int i = 0; i < 4 && q < end; ++i) { const unsigned char b = d[q++]; l = (l << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
                q += l;
            } else {
                const int kind = st & 0xF0;
                const int need = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
                if (q + (size_t)need > end) break;
                if (kind == 0x90 && d[q + 1] > 0)
                    notes.push_back({(int)(tick * kTpb / div), d[q], d[q + 1]});
                q += (size_t)need;
            }
        }
        p += 8 + len;
    }
    std::sort(notes.begin(), notes.end(), [](const SourceNote& a, const SourceNote& b) { return a.tick != b.tick ? a.tick < b.tick : a.note < b.note; });
    return true;
}

std::vector<unsigned char> writeNotes(const std::vector<SourceNote>& notes, int num, int den, double bpm, const std::string& name) {
    std::vector<unsigned char> f;
    f.insert(f.end(), {'M', 'T', 'h', 'd'});
    put32(f, 6); put16(f, 1); put16(f, 2); put16(f, (unsigned)kTpb);
    std::vector<unsigned char> c;
    putName(c, name);
    const unsigned mpq = (unsigned)std::lround(60000000.0 / std::max(20.0, std::min(bpm, 400.0)));
    c.insert(c.end(), {0x00, 0xFF, 0x51, 0x03, (unsigned char)(mpq >> 16), (unsigned char)(mpq >> 8), (unsigned char)mpq});
    int dd = 0;
    for (int d = den; d > 1; d >>= 1) ++dd;
    c.insert(c.end(), {0x00, 0xFF, 0x58, 0x04, (unsigned char)std::max(1, num), (unsigned char)dd, 24, 8});
    c.insert(c.end(), {0x00, 0xFF, 0x2F, 0x00});
    putTrack(f, c);
    std::vector<Raw> ev;
    long long end = 0;
    for (const auto& n : notes) {
        ev.push_back({n.tick, 1, n.note, std::max(1, std::min(127, n.velocity))});
        ev.push_back({(long long)n.tick + kStaccato, 0, n.note, 0});
        end = std::max(end, (long long)n.tick + kStaccato);
    }
    putTrack(f, noteTrack(name, ev, end));
    return f;
}

Rhythm rhythmFromNotes(const std::vector<SourceNote>& notes, int num, int den, const std::string& name) {
    Rhythm r;
    const long long bar = (long long)num * 4 * kTpb / std::max(1, den);
    const int n = (int)std::lround((double)bar / 120.0);
    if (notes.empty() || n < 2 || n > kMaxLength) return r;
    Pattern p((size_t)n, 0);
    for (const auto& s : notes) {
        int step = (int)std::lround((double)(s.tick % bar) / 120.0) % n;
        const int lv = s.velocity >= 105 ? 3 : s.velocity >= 75 ? 2 : 1;
        p[(size_t)step] = std::max<uint8_t>(p[(size_t)step], (uint8_t)lv);
    }
    r.name = name;
    r.midiName = "Rhythm from MIDI";
    r.info = "Ритм из MIDI-файла, сетка в шестнадцатых, нот " + std::to_string(notes.size());
    r.length = n;
    r.stepTicks = 120;
    r.cycleTicks = n * 120;
    r.meterNum = num; r.meterDen = den;
    r.layers = {p};
    r.layerNames = {"Ритм"};
    return r;
}

}  // namespace rhy

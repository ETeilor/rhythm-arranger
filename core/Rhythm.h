#pragma once
// Rhythm Arranger core: no JUCE, plain C++17, integer arithmetic only, so the same settings give the same
// notes on every machine. A rhythm is a cycle of N steps and one to four layers of accent levels.
#include <cstdint>
#include <string>
#include <vector>

namespace rhy {

constexpr int kTpb = 480;          // MIDI ticks per quarter
constexpr int kStaccato = 60;      // note length in ticks, always short
constexpr int kMaxLayers = 4;
constexpr int kMaxLength = 512;    // longest cycle in steps
// Output notes of the layers, in the order the layers are built: the first layer sits on C3 (60),
// then C2, C4, C5. The editor shows this order next to the layers.
constexpr int kLayerNote[kMaxLayers] = {60, 48, 72, 84};

// accent levels: 0 rest, 1 soft, 2 normal, 3 accent; velocity 64 / 90 / 112
using Pattern = std::vector<uint8_t>;
int velocityOf(int level);

struct CatalogEntry {
    int id;                 // stable: category * 1000 + index inside the category
    int category;
    const char* name;
    const char* region;
    int stepTicks;          // 120 for sixteenths, 240 for eighths
    const char* pattern;    // x accent, o normal, - soft, . rest
    const char* grouping;
    const char* source;
    const char* status;     // подтв. or расчёт
};
const std::vector<CatalogEntry>& catalog();
const char* categoryName(int category);
const CatalogEntry* findCatalog(int id);

struct Rhythm {
    std::string name;
    std::string info;                 // source, grouping, remarks
    std::string midiName;             // English, for track names in the file
    int length = 0;                   // steps in one cycle
    int cycleTicks = 0;               // length of the cycle in ticks
    int stepTicks = 0;                // grid step when the cycle is an even grid, else 0
    int meterNum = 4, meterDen = 4;
    std::vector<Pattern> layers;      // each has `length` entries
    std::vector<std::string> layerNames;
    bool ok() const { return length > 0 && !layers.empty(); }
    int tickOf(int step) const { return (int)(((long long)step * cycleTicks + length / 2) / length); }
};

// ---------- notation
Pattern parsePattern(const std::string& s);          // x o - . (anything else is a rest)
std::string patternString(const Pattern& p);
int hitCount(const Pattern& p);

// ---------- building blocks
Pattern euclid(int k, int n, int rotation = 0);
Pattern additive(const std::vector<int>& groups);     // an accent at the start of every group
std::vector<int> metricWeights(int n);                 // higher is a stronger place in the bar
Pattern rotate(const Pattern& p, int k);               // k steps to the left
Pattern retrograde(const Pattern& p);
Pattern complement(const Pattern& p);                  // rests become hits and back
Pattern augment(const Pattern& p);                     // every step doubled in length
bool diminish(const Pattern& p, Pattern& out);         // halves the grid when all hits sit on even steps
Pattern thin(const Pattern& p, int count);             // drops the weakest hits
Pattern densify(const Pattern& p, int count);          // adds hits on the strongest free places
Pattern shiftAccents(const Pattern& p, int k);         // hits stay, their levels travel k hits
std::vector<std::vector<int>> necklaces(int k, int n); // all binary necklaces with k ones, as positions
int gcd(int a, int b);
int lcm(int a, int b);

// ---------- deterministic random
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int below(int n) { return n <= 1 ? 0 : (int)(next() % (uint64_t)n); }
};

// ---------- the single entry point: settings -> rhythm
enum Source { sCatalog, sEuclid, sPoly, sPolymeter, sRandom, sTihai, sImported, sNumSources };
enum RandKind { rkEuclid, rkAdditive, rkWeighted, rkNecklace, rkNumKinds };

struct Settings {
    int source = sCatalog;
    int catalogId = 1001;
    int k = 5, n = 8, rot = 0;               // Euclid
    int polyA = 3, polyB = 2;                // polyrhythm a:b and polymeter lengths
    bool resultant = false;                  // Schillinger resultant as a third layer
    int polyBeats = 4;                       // beats the polyrhythm lasts
    int seed = 1;                            // randomizer
    int randSteps = 16, randDensity = 40, randKind = rkEuclid;
    int tihaiLen = 16, tihaiPhrase = 5;      // tihai: cycle and phrase length in steps
    // transformations, applied in this order
    int rotate = 0;
    bool retro = false, invert = false;
    int augment = 0;                         // 1 doubles the grid, 2 halves it where possible
    int density = 0;                         // negative thins, positive densifies
    int accentShift = 0;
    int phase = 0;                           // above 0 adds a copy rotated by this many steps (Reich)
    int swing = 0;                           // 0..50 percent of a step, odd steps come later
};

Rhythm build(const Settings& s, const Rhythm* imported = nullptr);
Rhythm polyrhythm(int a, int b, bool resultant, int beats);
Rhythm polymeter(int a, int b);
Rhythm tihai(int length, int phrase, bool* exact = nullptr);
// layers beyond maxLayers are folded into the last allowed layer as soft hits
Rhythm foldLayers(const Rhythm& r, int maxLayers);

// ---------- events
struct Event {
    int tick;       // inside the cycle
    int layer, step, level, velocity, length;
};
// one cycle of events, sorted by tick then layer; swing is written into the ticks
std::vector<Event> cycleEvents(const Rhythm& r, int swingPercent);
int swingDelay(const Rhythm& r, int step, int swingPercent);

// ---------- MIDI
struct MidiOptions {
    int bars = 4;                  // cycles written
    double bpm = 120;
    bool separateTracks = false;   // one track per layer, every layer on C3
    int swing = 0;
};
std::vector<unsigned char> writeMidi(const Rhythm& r, const MidiOptions& o);
struct SourceNote { int tick, note, velocity; };
bool readMidi(const unsigned char* data, size_t size, std::vector<SourceNote>& notes, int& num, int& den, double& bpm);
// a plain note list as a file, channel 1, every note staccato (used for the recording of the Live plug-in)
std::vector<unsigned char> writeNotes(const std::vector<SourceNote>& notes, int num, int den, double bpm, const std::string& name);
Rhythm rhythmFromNotes(const std::vector<SourceNote>& notes, int num, int den, const std::string& name);

// ---------- live arpeggiator
enum ArpMode { amGate, amArpeggio, amVoiceSplit, amNumModes };
// chord: sorted held notes; hitIndex counts hits since the chord changed; returns the notes to play
std::vector<int> arpNotes(int mode, const std::vector<int>& chord, int layer, int hitIndex);

}  // namespace rhy

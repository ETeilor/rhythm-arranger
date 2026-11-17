// Console tool: shows a rhythm as a grid and writes it as a .mid file.
//   rhythm_cli list
//   rhythm_cli catalog <id> [out.mid]
//   rhythm_cli euclid <k> <n> [rot] [out.mid]
//   rhythm_cli poly <a> <b> [out.mid]
//   rhythm_cli random <seed> [steps] [density] [kind] [out.mid]
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include "Rhythm.h"

static void show(const rhy::Rhythm& r) {
    if (!r.ok()) { std::puts("пустой ритм"); return; }
    std::printf("%s  (%d шагов, %d/%d)\n", r.name.c_str(), r.length, r.meterNum, r.meterDen);
    for (size_t l = 0; l < r.layers.size(); ++l)
        std::printf("  %-12s C%d  %s\n", r.layerNames[l].c_str(), rhy::kLayerNote[l] / 12 - 2, rhy::patternString(r.layers[l]).c_str());
    if (!r.info.empty()) std::printf("  %s\n", r.info.c_str());
}
static void save(const rhy::Rhythm& r, const char* path) {
    rhy::MidiOptions o;
    auto b = rhy::writeMidi(r, o);
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)b.data(), (std::streamsize)b.size());
    std::printf("записано %s, %zu байт\n", path, b.size());
}

int main(int argc, char** argv) {
    if (argc < 2) { std::puts("rhythm_cli list | catalog id | euclid k n [rot] | poly a b | random seed [steps density kind]"); return 1; }
    const std::string cmd = argv[1];
    rhy::Settings s;
    const char* out = nullptr;
    if (cmd == "list") {
        for (const auto& e : rhy::catalog())
            std::printf("%5d  %-14s %-3d %s  %s\n", e.id, rhy::categoryName(e.category), (int)std::string(e.pattern).size(), e.pattern, e.name);
        std::printf("всего %zu\n", rhy::catalog().size());
        return 0;
    }
    if (cmd == "catalog" && argc >= 3) { s.source = rhy::sCatalog; s.catalogId = atoi(argv[2]); if (argc > 3) out = argv[3]; }
    else if (cmd == "euclid" && argc >= 4) { s.source = rhy::sEuclid; s.k = atoi(argv[2]); s.n = atoi(argv[3]); if (argc > 4 && isdigit((unsigned char)argv[4][0])) { s.rot = atoi(argv[4]); if (argc > 5) out = argv[5]; } else if (argc > 4) out = argv[4]; }
    else if (cmd == "poly" && argc >= 4) { s.source = rhy::sPoly; s.polyA = atoi(argv[2]); s.polyB = atoi(argv[3]); s.resultant = true; if (argc > 4) out = argv[4]; }
    else if (cmd == "random" && argc >= 3) { s.source = rhy::sRandom; s.seed = atoi(argv[2]); if (argc > 3) s.randSteps = atoi(argv[3]); if (argc > 4) s.randDensity = atoi(argv[4]); if (argc > 5) s.randKind = atoi(argv[5]); if (argc > 6) out = argv[6]; }
    else { std::puts("неверные аргументы"); return 1; }
    auto r = rhy::build(s);
    show(r);
    if (out && r.ok()) save(r, out);
    return r.ok() ? 0 : 2;
}

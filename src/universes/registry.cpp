#include "universes/registry.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#define UNIVERSE(id, weight) const UniverseEntry& universe_##id();
#include "universes/registry.def"
#undef UNIVERSE

namespace universes {

const std::vector<const UniverseEntry*>& all() {
    static const std::vector<const UniverseEntry*> list = {
#define UNIVERSE(id, weight) &universe_##id(),
#include "universes/registry.def"
#undef UNIVERSE
    };
    return list;
}

float weight(int i) {
    static const std::vector<float> list = {
#define UNIVERSE(id, weight) (float)(weight),
#include "universes/registry.def"
#undef UNIVERSE
    };
    return list[std::clamp(i, 0, count() - 1)];
}

int count() { return (int)all().size(); }
const UniverseEntry& entry(int i) { return *all()[std::clamp(i, 0, count() - 1)]; }

static std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

int find(const std::string& name) {
    const std::string n = upper(name);
    for (int i = 0; i < count(); i++)
        if (n == entry(i).name) return i;
    // a name of several words can be given by its first one ("polar" for POLAR NIGHT)
    for (int i = 0; i < count(); i++) {
        const std::string e = entry(i).name;
        const size_t sp = e.find(' ');
        if (sp != std::string::npos && n == e.substr(0, sp)) return i;
    }
    return -1;
}

int parse(const std::string& spec, int* variant) {
    if (variant) *variant = -1;
    const size_t colon = spec.find(':');
    const int u = find(spec.substr(0, colon));
    if (u < 0 || colon == std::string::npos || !variant) return u;
    const std::string v = upper(spec.substr(colon + 1));
    const auto& vs = entry(u).variants;
    for (size_t k = 0; k < vs.size(); k++)
        if (v == vs[k]) *variant = (int)k;
    for (size_t k = 0; k < vs.size() && *variant < 0; k++) // a part of the name is enough ("vector:amber")
        if (!v.empty() && vs[k].find(v) != std::string::npos) *variant = (int)k;
    if (*variant < 0 && !v.empty() && std::isdigit((unsigned char)v[0])) *variant = std::atoi(v.c_str());
    if (*variant >= (int)vs.size()) *variant = -1;
    if (*variant < 0) std::fprintf(stderr, "[universe] %s has no variant '%s'\n", entry(u).name, v.c_str());
    return u;
}

std::string label(int universe, int variant) {
    const UniverseEntry& e = entry(universe);
    std::string s = e.name;
    if (variant >= 0 && variant < (int)e.variants.size() && e.variants.size() > 1) s += " \xC2\xB7 " + e.variants[variant];
    return s;
}

int pick(const SongLook& look, uint64_t seed, int current) {
    std::vector<int> cand;
    std::vector<float> w;
    for (int i = 0; i < count(); i++) {
        const UniverseEntry& e = entry(i);
        if (e.hidden) continue;
        const float a = (e.affinity ? std::max(0.f, e.affinity(look)) : 1.f) * weight(i);
        if (a <= 0.f) continue;
        cand.push_back(i);
        w.push_back(a);
    }
    if (cand.empty()) return std::max(0, current);
    if (cand.size() > 1)
        for (size_t k = 0; k < cand.size(); k++)
            if (cand[k] == current) w[k] = 0.f;
    float sum = 0;
    for (float x : w) sum += x;
    if (sum <= 0) return cand[0];
    Rng r(seed ^ 0x0011111EEull);
    float u = r.uniform() * sum;
    for (size_t k = 0; k < cand.size(); k++) {
        u -= w[k];
        if (u <= 0 && w[k] > 0) return cand[k];
    }
    return cand.back();
}

int next(int current, bool withHidden) {
    for (int k = 1; k <= count(); k++) {
        const int i = ((current + k) % count() + count()) % count();
        if (withHidden || !entry(i).hidden) return i;
    }
    return std::max(0, current);
}

} // namespace universes

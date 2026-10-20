#pragma once

// Deterministic, order-independent randomness.
//
// Generation must give identical results for identical (level, config, mask,
// seed), and regenerating one zone must not change anything elsewhere. So the
// engines never draw from one shared sequential stream: every decision derives
// its own stream from a hash of (layer seed, decision kind, position...). The
// result of one placement then does not depend on how many placements were
// evaluated before it.

#include <cstdint>
#include <string_view>
#include <vector>

namespace gdds {

constexpr uint64_t mix64(uint64_t z) {
    z += 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

constexpr uint64_t hashCombine(uint64_t h, uint64_t v) { return mix64(h ^ mix64(v)); }

template <class... Ts>
constexpr uint64_t hashOf(uint64_t seed, Ts... values) {
    uint64_t h = mix64(seed);
    ((h = hashCombine(h, static_cast<uint64_t>(values))), ...);
    return h;
}

constexpr uint64_t hashString(std::string_view s, uint64_t h = 0xcbf29ce484222325ULL) {
    for (char c : s) { h ^= uint8_t(c); h *= 0x100000001b3ULL; }
    return mix64(h);
}

// Quantizes a coordinate so it can be hashed reproducibly (1/8 unit precision).
constexpr int64_t quantize(double v) { return static_cast<int64_t>(v * 8.0 + (v >= 0 ? 0.5 : -0.5)); }

class Rng {
public:
    explicit Rng(uint64_t seed) : m_state(mix64(seed)) {}

    uint64_t next() {
        m_state += 0x9e3779b97f4a7c15ULL;
        return mix64(m_state);
    }
    // Uniform in [0, 1).
    double uniform() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
    double range(double lo, double hi) { return lo + (hi - lo) * uniform(); }
    int rangeInt(int lo, int hiInclusive) {
        if (hiInclusive <= lo) return lo;
        return lo + int(next() % uint64_t(hiInclusive - lo + 1));
    }
    bool chance(double p) { return uniform() < p; }

    // Weighted pick; returns -1 if every weight is <= 0.
    int weighted(std::vector<double> const& weights) {
        double total = 0;
        for (double w : weights) if (w > 0) total += w;
        if (total <= 0) return -1;
        double r = uniform() * total;
        for (size_t i = 0; i < weights.size(); ++i) {
            if (weights[i] <= 0) continue;
            if (r < weights[i]) return int(i);
            r -= weights[i];
        }
        for (size_t i = weights.size(); i-- > 0;) if (weights[i] > 0) return int(i);
        return -1;
    }

private:
    uint64_t m_state;
};

}  // namespace gdds

#pragma once
// Deterministic PRNG (xoshiro256**, seeded via splitmix64). Same seed => same sequence on every platform.
#include <cstdint>

namespace Scavar
{
class Rng
{
public:
    explicit Rng(uint64_t seed = 0x5CA7A12ULL) { Seed(seed); }

    void Seed(uint64_t seed)
    {
        for (uint64_t& s : State) { seed += 0x9E3779B97F4A7C15ULL; uint64_t z = seed; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; s = z ^ (z >> 31); }
    }

    uint64_t NextU64()
    {
        const uint64_t result = Rotl(State[1] * 5, 7) * 9;
        const uint64_t t = State[1] << 17;
        State[2] ^= State[0]; State[3] ^= State[1]; State[1] ^= State[2]; State[0] ^= State[3];
        State[2] ^= t; State[3] = Rotl(State[3], 45);
        return result;
    }

    double NextUnit() { return static_cast<double>(NextU64() >> 11) * (1.0 / 9007199254740992.0); }
    bool Chance(double p) { return NextUnit() < p; }
    int RangeInt(int lo, int hi) { return lo + static_cast<int>(NextU64() % static_cast<uint64_t>(hi - lo + 1)); }

private:
    static uint64_t Rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t State[4];
};
} // namespace Scavar

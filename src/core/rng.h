#pragma once

struct Rng {                                   // SplitMix64: identical output on every compiler
    unsigned long long s;
    explicit Rng(unsigned long long seed) : s(seed) {}
    unsigned long long next() {
        s += 0x9E3779B97F4A7C15ull;
        unsigned long long z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double unit() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }   // [0, 1)
    int below(int n) { return int(next() % (unsigned long long)n); }              // 0 .. n-1
};

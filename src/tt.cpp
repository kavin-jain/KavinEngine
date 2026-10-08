#include "tt.h"
#include <cstdlib>
#include <cstring>
#include "platform.h"

bool TT::resize(size_t bytes, bool fast) {
    size_t n = 1;
    while (n * 2 * sizeof(Slot) <= bytes) n *= 2;
    std::free(table);
    table = static_cast<Slot*>(alloc_mem(n * sizeof(Slot), fast));
    if (!table) { mask = 0; return false; }
    mask = n - 1;
    clear();
    return true;
}

void TT::clear() { if (table) std::memset(static_cast<void*>(table), 0, (mask + 1) * sizeof(Slot)); }

bool TT::probe(Key key, TTEntry& out) const {
    if (!table) return false;
    const Slot& s = table[key & mask];
    const uint64_t d = s.data;
    if ((s.check ^ d) != key) return false;
    out = TTEntry{key, Move(d), int16_t(d >> 16), uint8_t(d >> 32), uint8_t(d >> 40)};
    return out.bound != BOUND_NONE;
}

// ponytail: one entry per slot, depth-preferred only for the same key; buckets/aging if SPRT shows a gain (M3).
void TT::store(Key key, Move move, int score, int depth, int bound) {
    if (!table) return;
    Slot& s = table[key & mask];
    const uint64_t old = s.data;
    const bool same = (s.check ^ old) == key && uint8_t(old >> 40) != BOUND_NONE;
    if (same && depth < int(uint8_t(old >> 32)) && bound != BOUND_EXACT) return;
    if (move == NO_MOVE && same) move = Move(old);
    const uint64_t d = uint64_t(move) | uint64_t(uint16_t(int16_t(score))) << 16 | uint64_t(uint8_t(depth < 0 ? 0 : depth)) << 32
                     | uint64_t(uint8_t(bound)) << 40;
    s.data = d;
    s.check = key ^ d;
}

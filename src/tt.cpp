#include "tt.h"
#include <cstdlib>
#include <cstring>
#include "platform.h"

bool TT::resize(size_t bytes, bool fast) {
    size_t n = 1;
    while (n * 2 * sizeof(TTEntry) <= bytes) n *= 2;
    std::free(table);
    table = static_cast<TTEntry*>(alloc_mem(n * sizeof(TTEntry), fast));
    if (!table) { mask = 0; return false; }
    mask = n - 1;
    clear();
    return true;
}

void TT::clear() { if (table) std::memset(static_cast<void*>(table), 0, (mask + 1) * sizeof(TTEntry)); }

bool TT::probe(Key key, TTEntry& out) const {
    if (!table) return false;
    const TTEntry& e = table[key & mask];
    if (e.bound == BOUND_NONE || e.key != key) return false;
    out = e;
    return true;
}

// ponytail: one entry per slot, depth-preferred only for the same key; buckets/aging if SPRT shows a gain (M3).
void TT::store(Key key, Move move, int score, int depth, int bound) {
    if (!table) return;
    TTEntry& e = table[key & mask];
    if (e.key == key && depth < e.depth && bound != BOUND_EXACT) return;
    if (move == NO_MOVE && e.key == key) move = e.move;
    e = TTEntry{key, move, int16_t(score), uint8_t(depth < 0 ? 0 : depth), uint8_t(bound)};
}

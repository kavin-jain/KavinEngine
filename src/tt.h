#pragma once
#include <cstddef>
#include "types.h"

enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };
struct TTEntry { Key key; Move move; int16_t score; uint8_t depth; uint8_t bound; };  // decoded entry

class TT {
public:
    bool resize(size_t bytes, bool fast);  // largest power-of-two entry count that fits; false if allocation fails
    void clear();
    bool probe(Key key, TTEntry& out) const;
    void store(Key key, Move move, int score, int depth, int bound);
private:
    // Lazy SMP writes race: a slot stores key ^ data, so a slot torn between two writers fails the key check.
    struct Slot { uint64_t check, data; };  // 16 bytes
    Slot* table = nullptr;
    size_t mask = 0;
};

// Mate scores are stored relative to the node, not the root.
inline int score_to_tt(int s, int ply) { return s >= MATE_BOUND ? s + ply : s <= -MATE_BOUND ? s - ply : s; }
inline int score_from_tt(int s, int ply) { return s >= MATE_BOUND ? s - ply : s <= -MATE_BOUND ? s + ply : s; }

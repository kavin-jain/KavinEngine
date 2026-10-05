#pragma once
#include "board.h"

struct MoveList {
    Move moves[MAX_MOVES];
    int size = 0;
    void add(Move m) { moves[size++] = m; }
};

// Pseudo-legal moves (Board::make rejects illegal ones).
// captures_only: captures plus queen promotions, for quiescence search.
void generate(const Board& b, MoveList& list, bool captures_only);
uint64_t perft(Board& b, int depth);

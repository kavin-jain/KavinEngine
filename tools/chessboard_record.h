#pragma once
#include <cstdint>
#include "../src/board.h"

// bullet's "ChessBoard" training record (bulletformat 1.8.0, src/chess.rs): 32 bytes, side-to-move relative.
struct ChessBoardRecord {
    uint64_t occ;
    uint8_t pcs[16];
    int16_t score;
    uint8_t result;
    uint8_t ksq;
    uint8_t opp_ksq;
    uint8_t extra[3];
};
static_assert(sizeof(ChessBoardRecord) == 32, "bulletformat ChessBoard is 32 bytes");

// score_white: centipawns from White's view. result_white: 0 = Black won, 1 = draw, 2 = White won.
inline ChessBoardRecord make_record(const Board& b, int score_white, int result_white) {
    ChessBoardRecord r{};
    const bool flip = b.stm == BLACK;
    int idx = 0;
    for (int rel = 0; rel < 64; ++rel) {  // ascending relative square = bulletformat's nibble order
        const int sq = flip ? rel ^ 56 : rel;
        const int pc = b.mailbox[sq];
        if (pc == NO_PIECE) continue;
        const uint8_t nib = uint8_t((color_of(pc) != b.stm ? 8 : 0) | int(type_of(pc)));
        r.occ |= 1ull << rel;
        r.pcs[idx / 2] |= uint8_t(nib << (4 * (idx & 1)));
        ++idx;
        if (type_of(pc) == KING) {
            if (color_of(pc) == b.stm) r.ksq = uint8_t(rel);
            else r.opp_ksq = uint8_t(rel ^ 56);
        }
    }
    r.score = int16_t(flip ? -score_white : score_white);
    r.result = uint8_t(flip ? 2 - result_white : result_white);
    return r;
}

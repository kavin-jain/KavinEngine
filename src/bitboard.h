#pragma once
#include "types.h"

inline int popcount(Bitboard b) { return __builtin_popcountll(b); }
inline int lsb(Bitboard b) { return __builtin_ctzll(b); }
inline int pop_lsb(Bitboard& b) { int s = lsb(b); b &= b - 1; return s; }

extern Bitboard PawnAttacks[2][64];
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];

// "Fancy" magic bitboards. On 32-bit CPUs (ESP32) the index uses two 32-bit
// multiplies instead of one 64-bit multiply, which Xtensa would emulate.
struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    unsigned shift;
    unsigned index(Bitboard occ) const {
#if UINTPTR_MAX == 0xFFFFFFFFu
        unsigned lo = unsigned(occ) & unsigned(mask);
        unsigned hi = unsigned(occ >> 32) & unsigned(mask >> 32);
        return (lo * unsigned(magic) ^ hi * unsigned(magic >> 32)) >> shift;
#else
        return unsigned(((occ & mask) * magic) >> shift);
#endif
    }
};
extern Magic RookMagics[64];
extern Magic BishopMagics[64];

inline Bitboard rook_attacks(int sq, Bitboard occ) { return RookMagics[sq].attacks[RookMagics[sq].index(occ)]; }
inline Bitboard bishop_attacks(int sq, Bitboard occ) { return BishopMagics[sq].attacks[BishopMagics[sq].index(occ)]; }
inline Bitboard queen_attacks(int sq, Bitboard occ) { return rook_attacks(sq, occ) | bishop_attacks(sq, occ); }

// Slow reference ray walk; used to build the tables and by tests.
Bitboard slider_attacks_slow(int sq, Bitboard occ, bool rook);

void init_bitboards();  // idempotent; call before anything else

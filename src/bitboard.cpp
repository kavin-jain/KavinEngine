#include "bitboard.h"
#include <cstdlib>
#include "platform.h"

Bitboard PawnAttacks[2][64], KnightAttacks[64], KingAttacks[64];
Magic RookMagics[64], BishopMagics[64];

namespace {

const int RookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
const int BishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

uint64_t rng = 0x9E3779B97F4A7C15ull;
uint64_t rand64() {  // xorshift64*: fixed seed, so magics are identical on every run
    rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;
    return rng * 2685821657736338717ull;
}

void init_magics(Magic* magics, Bitboard* table, bool rook) {
    Bitboard* occupancy = new Bitboard[4096];
    Bitboard* reference = new Bitboard[4096];
    int* epoch = new int[4096]();
    int attempt = 0;
    Bitboard* next = table;
    for (int sq = 0; sq < 64; ++sq) {
        Magic& m = magics[sq];
        Bitboard edges = ((0xFFull | 0xFF00000000000000ull) & ~(0xFFull << (8 * rank_of(sq))))
                       | ((0x0101010101010101ull | 0x8080808080808080ull) & ~(0x0101010101010101ull << file_of(sq)));
        m.mask = slider_attacks_slow(sq, 0, rook) & ~edges;
#if UINTPTR_MAX == 0xFFFFFFFFu
        m.shift = 32 - popcount(m.mask);
#else
        m.shift = 64 - popcount(m.mask);
#endif
        m.attacks = next;
        int size = 0;
        Bitboard b = 0;
        do {  // Carry-Rippler: enumerate every subset of the mask
            occupancy[size] = b;
            reference[size] = slider_attacks_slow(sq, b, rook);
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);
        next += size;
        for (int i = 0; i < size;) {
            for (m.magic = 0; popcount((m.mask * m.magic) >> 56) < 6;)
                m.magic = rand64() & rand64() & rand64();
            ++attempt;
            for (i = 0; i < size; ++i) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < attempt) { epoch[idx] = attempt; m.attacks[idx] = reference[i]; }
                else if (m.attacks[idx] != reference[i]) break;  // collision: try another magic
            }
        }
    }
    delete[] occupancy;
    delete[] reference;
    delete[] epoch;
}

}  // namespace

Bitboard slider_attacks_slow(int sq, Bitboard occ, bool rook) {
    const int (*dirs)[2] = rook ? RookDirs : BishopDirs;
    Bitboard att = 0;
    for (int d = 0; d < 4; ++d) {
        int f = file_of(sq) + dirs[d][0], r = rank_of(sq) + dirs[d][1];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            att |= bb(r * 8 + f);
            if (occ & bb(r * 8 + f)) break;
            f += dirs[d][0];
            r += dirs[d][1];
        }
    }
    return att;
}

void init_bitboards() {
    static bool done = false;
    if (done) return;
    done = true;
    for (int sq = 0; sq < 64; ++sq) {
        int f = file_of(sq), r = rank_of(sq);
        auto add = [&](Bitboard& t, int df, int dr) {
            int nf = f + df, nr = r + dr;
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) t |= bb(nr * 8 + nf);
        };
        add(PawnAttacks[WHITE][sq], -1, 1); add(PawnAttacks[WHITE][sq], 1, 1);
        add(PawnAttacks[BLACK][sq], -1, -1); add(PawnAttacks[BLACK][sq], 1, -1);
        const int jumps[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        for (const auto& j : jumps) add(KnightAttacks[sq], j[0], j[1]);
        for (int df = -1; df <= 1; ++df)
            for (int dr = -1; dr <= 1; ++dr)
                if (df || dr) add(KingAttacks[sq], df, dr);
    }
    // 102,400 rook + 5,248 bishop entries (~860 KB): PSRAM on ESP32, normal heap on PC.
    auto* rook_table = static_cast<Bitboard*>(alloc_mem(102400 * sizeof(Bitboard), false));
    auto* bishop_table = static_cast<Bitboard*>(alloc_mem(5248 * sizeof(Bitboard), false));
    if (!rook_table || !bishop_table) std::abort();
    init_magics(RookMagics, rook_table, true);
    init_magics(BishopMagics, bishop_table, false);
}

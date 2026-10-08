// src/nnue.cpp
#include "nnue.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int N = NNUE_HIDDEN;
const int16_t* W0 = nullptr;  // [NNUE_INPUTS][N], scaled by QA
const int16_t* B0 = nullptr;  // [N], QA
const int16_t* W1 = nullptr;  // [OB][2N], QB: per output bucket, first N for side to move, next N for the other side
const int16_t* B1 = nullptr;  // [OB], QA * QB
constexpr int OB = NNUE_OUTPUT_BUCKETS;

#if NNUE_KING_BUCKETS
// King bucket per square of the mirrored half-board (rank-major, files a-d): train/src/main.rs KB_LAYOUT.
constexpr int KB_LAYOUT[32] = {0, 1, 2, 3, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7,
                               8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9};
static_assert(NNUE_KING_BUCKETS == 10, "KB_LAYOUT defines 10 buckets");
#endif

// Feature offset and file mirror for one perspective, from its own king square (bullet ChessBucketsMirrored).
struct KingCtx { int base, flip; };
inline KingCtx king_ctx(int persp, int ksq) {
#if NNUE_KING_BUCKETS
    const int rel = persp == WHITE ? ksq : ksq ^ 56, f = rel & 7;
    return {768 * KB_LAYOUT[(rel >> 3) * 4 + (f > 3 ? 7 - f : f)], f > 3 ? 7 : 0};
#else
    (void)persp; (void)ksq;
    return {0, 0};
#endif
}
inline KingCtx king_ctx(const Board& b, int persp) { return king_ctx(persp, lsb(b.pieces[make_piece(Color(persp), KING)])); }

inline int feature(int persp, int pc, int sq, KingCtx k) {
    return k.base + (((int(color_of(pc)) == persp ? 0 : 384) + 64 * int(type_of(pc)) + (persp == WHITE ? sq : sq ^ 56)) ^ k.flip);
}
inline void add(int16_t* a, int f) { const int16_t* w = W0 + f * N; for (int i = 0; i < N; ++i) a[i] += w[i]; }
inline void sub(int16_t* a, int f) { const int16_t* w = W0 + f * N; for (int i = 0; i < N; ++i) a[i] -= w[i]; }

#if NNUE_KING_BUCKETS
// Refresh cache ("Finny tables"): per perspective, king bucket and mirror half, the accumulator of the last position
// refreshed there and its pieces. A king-bucket change patches that entry by the pieces that differ (usually a few)
// instead of summing all ~30 features. int16 wraps modulo 2^16, so the result has the same bits as a full refresh.
struct FinnyEntry { int16_t acc[N]; Bitboard pieces[12]; bool valid; };
FinnyEntry finny[2][NNUE_KING_BUCKETS][2];
#endif
}  // namespace

size_t nnue_expected_size() {
    const size_t raw = size_t(NNUE_INPUTS * N + N + OB * (2 * N + 1)) * sizeof(int16_t);
    return (raw + 63) / 64 * 64;
}

bool nnue_load(const unsigned char* data, size_t size) {
    if (size != nnue_expected_size()) return false;
    const int16_t* w0 = reinterpret_cast<const int16_t*>(data);
    const int16_t* b0 = w0 + NNUE_INPUTS * N;
    const int16_t* w1 = b0 + N;
    // Range proof for the integer arithmetic below (bullet clips weights to +-1.98: |W0| <= 505, |W1| <= 127):
    // 32 pieces never overflow an int16 accumulator, and x * w1 fits int16 in nnue_evaluate.
    int m0 = 0, mb = 0, m1 = 0;
    for (int i = 0; i < NNUE_INPUTS * N; ++i) m0 = std::max(m0, std::abs(int(w0[i])));
    for (int i = 0; i < N; ++i) mb = std::max(mb, std::abs(int(b0[i])));
    for (int i = 0; i < OB * 2 * N; ++i) m1 = std::max(m1, std::abs(int(w1[i])));
    if (mb + 32 * m0 > INT16_MAX || m1 > 127) return false;
    W0 = w0;
    B0 = b0;
#if NNUE_KING_BUCKETS
    for (auto& persp : finny) for (auto& bucket : persp) for (auto& e : bucket) e.valid = false;  // new net
#endif
    W1 = w1;
    B1 = W1 + OB * 2 * N;
    return true;
}

bool nnue_ready() { return W0 != nullptr; }

static void refresh_persp(const Board& b, int16_t* a, int c) {
    std::memcpy(a, B0, sizeof(int16_t) * N);
    const KingCtx k = king_ctx(b, c);
    for (int pc = 0; pc < 12; ++pc)
        for (Bitboard x = b.pieces[pc]; x;) add(a, feature(c, pc, pop_lsb(x), k));
}

#if NNUE_KING_BUCKETS
static void refresh_cached(const Board& b, int16_t* a, int c) {
    const KingCtx k = king_ctx(b, c);
    FinnyEntry& e = finny[c][k.base / 768][k.flip ? 1 : 0];
    if (!e.valid) {
        refresh_persp(b, e.acc, c);
        std::memcpy(e.pieces, b.pieces, sizeof e.pieces);
        e.valid = true;
    } else {
        for (int pc = 0; pc < 12; ++pc) {
            for (Bitboard x = b.pieces[pc] & ~e.pieces[pc]; x;) add(e.acc, feature(c, pc, pop_lsb(x), k));
            for (Bitboard x = e.pieces[pc] & ~b.pieces[pc]; x;) sub(e.acc, feature(c, pc, pop_lsb(x), k));
            e.pieces[pc] = b.pieces[pc];
        }
    }
    std::memcpy(a, e.acc, sizeof e.acc);
}
#endif

void nnue_refresh(const Board& b, Accumulator& acc) {
    for (int c = 0; c < 2; ++c) refresh_persp(b, acc.v[c], c);
}

// Fused copy + patch for quiet moves (1 add, 1 sub) and captures (1 add, 2 subs); rarer moves patch a copy.
// int16 wraps modulo 2^16 either way, so every path gives the same bits as sequential adds.
void nnue_update(const Accumulator& parent, Accumulator& child, const Board& b) {
    for (int c = 0; c < 2; ++c) {
        const KingCtx k = king_ctx(b, c);  // after the move
#if NNUE_KING_BUCKETS
        bool rebuild = false;  // own king changed bucket or mirror side: rebuild this perspective from scratch
        for (int i = 0; i < b.dirty_n; ++i)
            if (b.dirty[i].pc == make_piece(Color(c), KING) && !b.dirty[i].add) {
                const KingCtx o = king_ctx(c, b.dirty[i].sq);
                rebuild = o.base != k.base || o.flip != k.flip;
            }
        if (rebuild) { refresh_cached(b, child.v[c], c); continue; }
#endif
        const int16_t* add[6];
        const int16_t* sub[6];
        int na = 0, ns = 0;
        for (int i = 0; i < b.dirty_n; ++i) {
            const Board::DirtyPiece& d = b.dirty[i];
            const int16_t* w = W0 + feature(c, d.pc, d.sq, k) * N;
            if (d.add) add[na++] = w; else sub[ns++] = w;
        }
        const int16_t* __restrict p = parent.v[c];
        int16_t* __restrict o = child.v[c];
        if (na == 1 && ns == 1) {
            for (int i = 0; i < N; ++i) o[i] = int16_t(p[i] + add[0][i] - sub[0][i]);
        } else if (na == 1 && ns == 2) {
            for (int i = 0; i < N; ++i) o[i] = int16_t(p[i] + add[0][i] - sub[0][i] - sub[1][i]);
        } else {
            std::memcpy(o, p, sizeof child.v[c]);
            for (int k = 0; k < na; ++k) for (int i = 0; i < N; ++i) o[i] = int16_t(o[i] + add[k][i]);
            for (int k = 0; k < ns; ++k) for (int i = 0; i < N; ++i) o[i] = int16_t(o[i] - sub[k][i]);
        }
    }
}

// SCReLU sum as (x * w) * x: x * w fits int16 (|w| <= 127, checked at load), and a 256-term block sums to at
// most 256 * 255 * 255 * 127 < 2^31, so each block is exact in int32 (vectorisable); blocks add in int64.
constexpr int DOT_BLOCK = N < 256 ? N : 256;
static_assert(N % DOT_BLOCK == 0, "hidden size must be a multiple of 256 above 256");
static int64_t screlu_dot(const int16_t* __restrict a, const int16_t* __restrict w) {
    int64_t total = 0;
    for (int b = 0; b < N; b += DOT_BLOCK) {
        int32_t s = 0;
        for (int i = b; i < b + DOT_BLOCK; ++i) {
            const int16_t x = std::clamp<int16_t>(a[i], 0, NNUE_QA);
            s += int16_t(x * w[i]) * x;
        }
        total += s;
    }
    return total;
}

int nnue_evaluate(const Accumulator& acc, Color stm, int pieces) {
    constexpr int DIV = (32 + OB - 1) / OB;  // bullet MaterialCount<OB>: bucket = (pieces - 2) / ceil(32 / OB)
    const int ob = (pieces - 2) / DIV;
    const int16_t* w = W1 + ob * 2 * N;
    const int64_t sum = screlu_dot(acc.v[stm], w) + screlu_dot(acc.v[~stm], w + N);
    const int64_t out = sum / NNUE_QA + B1[ob];
    return int(out * NNUE_SCALE / (NNUE_QA * NNUE_QB));
}

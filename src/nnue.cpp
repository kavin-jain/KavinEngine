// src/nnue.cpp
#include "nnue.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int N = NNUE_HIDDEN;
const int16_t* W0 = nullptr;  // [768][N], scaled by QA
const int16_t* B0 = nullptr;  // [N], QA
const int16_t* W1 = nullptr;  // [2N], QB: first N for side to move, next N for the other side
int B1 = 0;                   // QA * QB

inline int feature(int persp, int pc, int sq) {
    return (int(color_of(pc)) == persp ? 0 : 384) + 64 * int(type_of(pc)) + (persp == WHITE ? sq : sq ^ 56);
}
inline void add(int16_t* a, int f) { const int16_t* w = W0 + f * N; for (int i = 0; i < N; ++i) a[i] += w[i]; }
}  // namespace

size_t nnue_expected_size() {
    const size_t raw = size_t(768 * N + N + 2 * N + 1) * sizeof(int16_t);
    return (raw + 63) / 64 * 64;
}

bool nnue_load(const unsigned char* data, size_t size) {
    if (size != nnue_expected_size()) return false;
    const int16_t* w0 = reinterpret_cast<const int16_t*>(data);
    const int16_t* b0 = w0 + 768 * N;
    const int16_t* w1 = b0 + N;
    // Range proof for the integer arithmetic below (bullet clips weights to +-1.98: |W0| <= 505, |W1| <= 127):
    // 32 pieces never overflow an int16 accumulator, and x * w1 fits int16 in nnue_evaluate.
    int m0 = 0, mb = 0, m1 = 0;
    for (int i = 0; i < 768 * N; ++i) m0 = std::max(m0, std::abs(int(w0[i])));
    for (int i = 0; i < N; ++i) mb = std::max(mb, std::abs(int(b0[i])));
    for (int i = 0; i < 2 * N; ++i) m1 = std::max(m1, std::abs(int(w1[i])));
    if (mb + 32 * m0 > INT16_MAX || m1 > 127) return false;
    W0 = w0;
    B0 = b0;
    W1 = w1;
    B1 = W1[2 * N];
    return true;
}

bool nnue_ready() { return W0 != nullptr; }

void nnue_refresh(const Board& b, Accumulator& acc) {
    for (int c = 0; c < 2; ++c) std::memcpy(acc.v[c], B0, sizeof acc.v[c]);
    for (int pc = 0; pc < 12; ++pc)
        for (Bitboard x = b.pieces[pc]; x;) {
            const int sq = pop_lsb(x);
            add(acc.v[WHITE], feature(WHITE, pc, sq));
            add(acc.v[BLACK], feature(BLACK, pc, sq));
        }
}

// Fused copy + patch for quiet moves (1 add, 1 sub) and captures (1 add, 2 subs); rarer moves patch a copy.
// int16 wraps modulo 2^16 either way, so every path gives the same bits as sequential adds.
void nnue_update(const Accumulator& parent, Accumulator& child, const Board& b) {
    for (int c = 0; c < 2; ++c) {
        const int16_t* add[6];
        const int16_t* sub[6];
        int na = 0, ns = 0;
        for (int i = 0; i < b.dirty_n; ++i) {
            const Board::DirtyPiece& d = b.dirty[i];
            const int16_t* w = W0 + feature(c, d.pc, d.sq) * N;
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

// SCReLU sum as (x * w) * x: x * w fits int16 (|w| <= 127, checked at load), and one perspective's
// N-term sum is at most N * 255 * 255 * 127 < 2^31 for N <= 256. Exact integers, same result as int64.
static_assert(N <= 256, "int32 perspective sums need N <= 256");
static int32_t screlu_dot(const int16_t* __restrict a, const int16_t* __restrict w) {
    int32_t s = 0;
    for (int i = 0; i < N; ++i) {
        const int16_t x = std::clamp<int16_t>(a[i], 0, NNUE_QA);
        s += int16_t(x * w[i]) * x;
    }
    return s;
}

int nnue_evaluate(const Accumulator& acc, Color stm) {
    const int64_t sum = int64_t(screlu_dot(acc.v[stm], W1)) + screlu_dot(acc.v[~stm], W1 + N);
    const int64_t out = sum / NNUE_QA + B1;
    return int(out * NNUE_SCALE / (NNUE_QA * NNUE_QB));
}

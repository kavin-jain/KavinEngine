// Held-out check for a quantised net: mean (sigmoid(eval/400) - sigmoid(score/400))^2 over bulletformat records,
// the same loss bullet trains on (WDL weight 0). Run it on val.bin and on a train shard: a gap means overfitting.
// Usage: nnue_loss NET.bin DATA.bin [MAX]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "chessboard_record.h"
#include "../src/nnue.h"

static Board B;

static std::vector<unsigned char> read_file(const char* path) {
    std::vector<unsigned char> v;
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::perror(path); std::exit(1); }
    std::fseek(f, 0, SEEK_END);
    v.resize(size_t(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    if (std::fread(v.data(), 1, v.size(), f) != v.size()) { std::perror(path); std::exit(1); }
    std::fclose(f);
    return v;
}

static double sigmoid(double cp) { return 1.0 / (1.0 + std::exp(-cp / NNUE_SCALE)); }

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: nnue_loss NET.bin DATA.bin [MAX]\n"); return 1; }
    const std::vector<unsigned char> net = read_file(argv[1]);
    if (!nnue_load(net.data(), net.size())) {
        std::fprintf(stderr, "%s: %zu bytes, expected %zu for NNUE_HIDDEN=%d\n", argv[1], net.size(), nnue_expected_size(), NNUE_HIDDEN);
        return 1;
    }
    FILE* f = std::fopen(argv[2], "rb");
    if (!f) { std::perror(argv[2]); return 1; }
    const unsigned long long max = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : ~0ull;
    ChessBoardRecord r;
    Accumulator acc;
    double sum = 0;
    unsigned long long n = 0;
    while (n < max && std::fread(&r, sizeof r, 1, f) == 1) {
        record_pieces(r, B.pieces);
        nnue_refresh(B, acc);
        const double d = sigmoid(nnue_evaluate(acc, WHITE)) - sigmoid(r.score);
        sum += d * d;
        ++n;
    }
    std::fclose(f);
    std::printf("%s on %s: %llu positions, loss %.6f\n", argv[1], argv[2], n, n ? sum / double(n) : 0.0);
    return n ? 0 : 1;
}

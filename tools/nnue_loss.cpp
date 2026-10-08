// Held-out check for a quantised net: mean (sigmoid(eval/400) - sigmoid(score/400))^2 over bulletformat records,
// the same loss bullet trains on (WDL weight 0). Run it on val.bin and on a train shard: a gap means overfitting.
// Usage: nnue_loss NET.bin DATA.bin [MAX]
// Data selection (self-improvement loop): keep the FRACTION of records the net gets most wrong, i.e. the largest
// per-record loss (Sorscher et al. 2022: with abundant data, keep the hard examples), or a random FRACTION as control:
//        nnue_loss NET.bin DATA.bin --select FRACTION OUT.bin
//        nnue_loss NET.bin DATA.bin --random FRACTION OUT.bin
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
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

static double record_loss(const ChessBoardRecord& r, Accumulator& acc) {
    record_pieces(r, B.pieces);
    nnue_refresh(B, acc);
    const double d = sigmoid(nnue_evaluate(acc, WHITE, popcount(r.occ))) - sigmoid(r.score);
    return d * d;
}

static int select_records(FILE* f, bool hardest, double fraction, const char* out_path) {
    ChessBoardRecord r;
    Accumulator acc;
    double threshold = 0;
    if (hardest) {  // pass 1: the (1 - fraction) quantile of per-record loss over a 2M-record sample
        std::vector<double> sample;
        while (sample.size() < 2000000 && std::fread(&r, sizeof r, 1, f) == 1) sample.push_back(record_loss(r, acc));
        if (sample.empty()) return 1;
        const size_t k = std::min(sample.size() - 1, size_t(double(sample.size()) * (1 - fraction)));
        std::nth_element(sample.begin(), sample.begin() + long(k), sample.end());
        threshold = sample[k];
        std::rewind(f);
    }
    FILE* out = std::fopen(out_path, "wb");
    if (!out) { std::perror(out_path); return 1; }
    std::mt19937_64 rng(12345);
    unsigned long long n = 0, kept = 0;
    for (; std::fread(&r, sizeof r, 1, f) == 1; ++n)
        if (hardest ? record_loss(r, acc) >= threshold : std::uniform_real_distribution<double>(0, 1)(rng) < fraction) {
            std::fwrite(&r, sizeof r, 1, out);
            ++kept;
        }
    std::fclose(out);
    std::printf("%s: kept %llu of %llu records (%s %.3f, loss threshold %.6f)\n", out_path, kept, n,
                hardest ? "hardest" : "random", fraction, threshold);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: nnue_loss NET.bin DATA.bin [MAX]\n"); return 1; }
    const std::vector<unsigned char> net = read_file(argv[1]);
    if (!nnue_load(net.data(), net.size())) {
        std::fprintf(stderr, "%s: %zu bytes, expected %zu for NNUE_HIDDEN=%d\n", argv[1], net.size(), nnue_expected_size(), NNUE_HIDDEN);
        return 1;
    }
    FILE* f = std::fopen(argv[2], "rb");
    if (!f) { std::perror(argv[2]); return 1; }
    if (argc == 6 && (std::string(argv[3]) == "--select" || std::string(argv[3]) == "--random"))
        return select_records(f, std::string(argv[3]) == "--select", std::atof(argv[4]), argv[5]);
    const unsigned long long max = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : ~0ull;
    ChessBoardRecord r;
    Accumulator acc;
    double sum = 0, el = 0, ll = 0;  // el / ll: least-squares slope of the net's eval against the labels
    unsigned long long n = 0;
    while (n < max && std::fread(&r, sizeof r, 1, f) == 1) {
        record_pieces(r, B.pieces);
        nnue_refresh(B, acc);
        const double d = sigmoid(nnue_evaluate(acc, WHITE, popcount(r.occ))) - sigmoid(r.score);
        sum += d * d;
        if (std::abs(r.score) < 2000) { const double e = nnue_evaluate(acc, WHITE, popcount(r.occ)); el += e * r.score; ll += double(r.score) * r.score; }
        ++n;
    }
    std::fclose(f);
    std::printf("%s on %s: %llu positions, loss %.6f, eval/label slope %.4f\n", argv[1], argv[2], n, n ? sum / double(n) : 0.0,
                ll > 0 ? el / ll : 0.0);
    return n ? 0 : 1;
}

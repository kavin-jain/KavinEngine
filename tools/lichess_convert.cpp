// Lichess eval DB -> bullet ChessBoard shards. Streams stdin; never stores the raw file.
//   zstd -dc lichess_db_eval.jsonl.zst | lichess_convert convert OUTDIR MAX_POSITIONS
//   lichess_convert shuffle FILE...        in-place Fisher-Yates on 32-byte records
//   lichess_convert text MAX < jsonl       "FEN | cp | 0.5" lines for kept positions (bullet-utils cross-check)
//   lichess_convert binary OUT MAX < jsonl same positions as `text`, as records in one file
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "chessboard_record.h"
#include "lichess_parse.h"

static Board B;

// Fast line reader: libc++'s std::getline(std::cin) locks once per character (profiled: >50% of runtime).
static bool next_line(std::string& line) {
    static char* buf = nullptr;
    static size_t cap = 0;
    ssize_t n = ::getline(&buf, &cap, stdin);
    if (n < 0) return false;
    if (n > 0 && buf[n - 1] == '\n') --n;
    line.assign(buf, size_t(n));
    return true;
}
static const char* VERDICT_NAMES[VERDICT_COUNT] = {"keep", "bad_fen", "bad_move", "mate", "too_shallow", "too_big", "in_check", "noisy"};

static void report(uint64_t lines, const uint64_t* counts) {
    std::fprintf(stderr, "lines %llu", (unsigned long long)lines);
    for (int v = 0; v < VERDICT_COUNT; ++v) std::fprintf(stderr, " %s %llu", VERDICT_NAMES[v], (unsigned long long)counts[v]);
    std::fprintf(stderr, "\n");
}

static int convert(const std::string& dir, uint64_t max_positions) {
    constexpr int SHARDS = 32;
    std::vector<FILE*> shards;
    for (int i = 0; i < SHARDS; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "/train_%02d.bin", i);
        shards.push_back(std::fopen((dir + name).c_str(), "wb"));
        if (!shards.back()) { std::perror("open shard"); return 1; }
    }
    FILE* val = std::fopen((dir + "/val.bin").c_str(), "wb");
    if (!val) { std::perror("open val"); return 1; }
    uint64_t counts[VERDICT_COUNT] = {}, lines = 0, kept = 0;
    std::string line;
    EvalLine e;
    while (kept < max_positions && next_line(line)) {
        ++lines;
        Verdict v = parse_eval_line(line, e) ? classify(e, B) : BAD_FEN;
        ++counts[v];
        if (v == KEEP) {
            const ChessBoardRecord r = make_record(B, e.cp, 1);
            const Key k = B.key * 0x9E3779B97F4A7C15ull;  // spread the Zobrist key before splitting
            FILE* out = (k >> 57) == 0 ? val : shards[(k >> 32) % SHARDS];  // ~0.8% validation
            if (std::fwrite(&r, sizeof r, 1, out) != 1) { std::perror("write"); return 1; }
            ++kept;
        }
        if (lines % 10000000 == 0) report(lines, counts);
    }
    report(lines, counts);
    for (FILE* f : shards) std::fclose(f);
    std::fclose(val);
    return 0;
}

static int shuffle(const char* path) {
    FILE* f = std::fopen(path, "rb+");
    if (!f) { std::perror(path); return 1; }
    std::fseek(f, 0, SEEK_END);
    const long bytes = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<ChessBoardRecord> v(size_t(bytes) / sizeof(ChessBoardRecord));
    if (std::fread(v.data(), sizeof(ChessBoardRecord), v.size(), f) != v.size()) { std::perror("read"); return 1; }
    std::mt19937_64 rng(std::hash<std::string>{}(path));
    for (size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[rng() % i]);
    std::fseek(f, 0, SEEK_SET);
    if (std::fwrite(v.data(), sizeof(ChessBoardRecord), v.size(), f) != v.size()) { std::perror("write"); return 1; }
    std::fclose(f);
    std::fprintf(stderr, "shuffled %s: %zu records\n", path, v.size());
    return 0;
}

static int sample(bool as_text, const char* out_path, uint64_t max_positions) {
    FILE* out = as_text ? stdout : std::fopen(out_path, "wb");
    if (!out) { std::perror(out_path); return 1; }
    std::string line;
    EvalLine e;
    uint64_t kept = 0;
    while (kept < max_positions && next_line(line)) {
        if (!parse_eval_line(line, e) || classify(e, B) != KEEP) continue;
        if (as_text) std::fprintf(out, "%s | %d | 0.5\n", B.fen().c_str(), e.cp);
        else { ChessBoardRecord r = make_record(B, e.cp, 1); std::fwrite(&r, sizeof r, 1, out); }
        ++kept;
    }
    if (!as_text) std::fclose(out);
    return 0;
}

int main(int argc, char** argv) {
    init_bitboards();
    Board::init();
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "convert" && argc == 4) return convert(argv[2], std::stoull(argv[3]));
    if (cmd == "shuffle" && argc >= 3) { for (int i = 2; i < argc; ++i) if (shuffle(argv[i])) return 1; return 0; }
    if (cmd == "text" && argc == 3) return sample(true, nullptr, std::stoull(argv[2]));
    if (cmd == "binary" && argc == 4) return sample(false, argv[2], std::stoull(argv[3]));
    std::fprintf(stderr, "usage: see header comment in tools/lichess_convert.cpp\n");
    return 2;
}

#include "test.h"
#include <cctype>
#include <sstream>
#include <string>
#include "../src/board.h"
#include "../src/eval.h"

static Board B;

// Colour-flipped FEN: mirror ranks, swap piece colours, side to move, castling, ep.
static std::string flip_fen(const std::string& fen) {
    std::istringstream ss(fen);
    std::string place, side, cast, ep, hm, fm;
    ss >> place >> side >> cast >> ep >> hm >> fm;
    std::string ranks[8], cur;
    int r = 0;
    for (char c : place) { if (c == '/') { ranks[r++] = cur; cur.clear(); } else cur += c; }
    ranks[r] = cur;
    std::string out;
    for (int i = 7; i >= 0; --i) {
        for (char c : ranks[i]) out += std::isalpha(c) ? char(std::isupper(c) ? std::tolower(c) : std::toupper(c)) : c;
        if (i) out += '/';
    }
    std::string fc;
    for (char c : cast) fc += c == '-' ? '-' : char(std::isupper(c) ? std::tolower(c) : std::toupper(c));
    std::string sorted;  // canonical KQkq order
    for (char c : std::string("KQkq")) if (fc.find(c) != std::string::npos) sorted += c;
    if (sorted.empty()) sorted = "-";
    std::string fep = ep == "-" ? "-" : std::string(1, ep[0]) + (ep[1] == '3' ? "6" : "3");
    return out + (side == "w" ? " b " : " w ") + sorted + " " + fep + " " + hm + " " + fm;
}

TEST(eval_startpos_is_zero) {
    CHECK(B.set_fen(START_FEN));
    CHECK_EQ(evaluate(B), 0);
}

TEST(eval_is_colour_symmetric) {
    const char* fens[] = {
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    };
    for (const char* f : fens) {
        CHECK(B.set_fen(f));
        int a = evaluate(B);
        CHECK(B.set_fen(flip_fen(f)));
        CHECK_EQ(evaluate(B), a);
    }
}

TEST(eval_material_sign) {
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/Q3K3 w - - 0 1"));
    CHECK(evaluate(B) > 800);   // white to move, a queen up
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/Q3K3 b - - 0 1"));
    CHECK(evaluate(B) < -800);  // side-to-move relative
}

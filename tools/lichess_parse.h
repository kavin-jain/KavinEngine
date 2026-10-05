#pragma once
#include <cstdlib>
#include <string>
#include "../src/board.h"

struct EvalLine { std::string fen; int cp; bool mate; int depth; std::string first_move; };

// One line of lichess_db_eval.jsonl. Keeps the deepest eval; its first PV gives cp/mate and the first move.
inline bool parse_eval_line(const std::string& s, EvalLine& out) {
    const std::string kFen = "\"fen\":\"", kPvs = "{\"pvs\":[";
    size_t p = s.find(kFen);
    if (p == std::string::npos) return false;
    p += kFen.size();
    const size_t q = s.find('"', p);
    if (q == std::string::npos) return false;
    out.fen = s.substr(p, q - p);
    out.depth = -1;
    size_t pos = s.find(kPvs, q);
    while (pos != std::string::npos) {
        const size_t next = s.find(kPvs, pos + kPvs.size());
        const size_t end = next == std::string::npos ? s.size() : next;
        const size_t dp = s.rfind("\"depth\":", end);
        if (dp == std::string::npos || dp < pos) return false;
        const int depth = std::atoi(s.c_str() + dp + 8);
        if (depth > out.depth) {
            const size_t cpp = s.find("\"cp\":", pos), mp = s.find("\"mate\":", pos), lp = s.find("\"line\":\"", pos);
            if (lp == std::string::npos || lp >= end) return false;
            const bool mate = mp != std::string::npos && mp < end && (cpp == std::string::npos || mp < cpp);
            if (!mate && (cpp == std::string::npos || cpp >= end)) return false;
            out.mate = mate;
            out.cp = mate ? 0 : std::atoi(s.c_str() + cpp + 5);
            const size_t ms = lp + 8, me = s.find_first_of(" \"", ms);
            if (me == std::string::npos) return false;
            out.first_move = s.substr(ms, me - ms);
            out.depth = depth;
        }
        pos = next;
    }
    return out.depth >= 0;
}

enum Verdict { KEEP, BAD_FEN, BAD_MOVE, MATE_EVAL, TOO_SHALLOW, TOO_BIG, IN_CHECK, NOISY, VERDICT_COUNT };
constexpr int MIN_DEPTH = 20, MAX_ABS_CP = 3000;

// Quiet, deep, non-mate positions only (spec section 5). Leaves `b` set to the position when KEEP.
inline Verdict classify(const EvalLine& e, Board& b) {
    if (e.mate) return MATE_EVAL;
    if (e.depth < MIN_DEPTH) return TOO_SHALLOW;
    if (e.cp > MAX_ABS_CP || e.cp < -MAX_ABS_CP) return TOO_BIG;
    if (!b.set_fen(e.fen)) return BAD_FEN;
    if (b.in_check()) return IN_CHECK;
    const std::string& m = e.first_move;
    if (m.size() < 4 || m[0] < 'a' || m[0] > 'h' || m[2] < 'a' || m[2] > 'h' ||
        m[1] < '1' || m[1] > '8' || m[3] < '1' || m[3] > '8') return BAD_MOVE;
    const int from = (m[1] - '1') * 8 + (m[0] - 'a'), to = (m[3] - '1') * 8 + (m[2] - 'a');
    const int pc = b.mailbox[from];
    if (pc == NO_PIECE || color_of(pc) != b.stm) return BAD_MOVE;
    if (m.size() == 5) return NOISY;                                              // promotion
    const int target = b.mailbox[to];
    if (target != NO_PIECE && color_of(target) != b.stm) return NOISY;           // capture (own rook = 960 castle)
    if (type_of(pc) == PAWN && to == b.ep) return NOISY;                         // en passant
    return KEEP;
}

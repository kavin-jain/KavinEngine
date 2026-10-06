#include "movegen.h"

namespace {

void add_promos(MoveList& list, int from, int to, bool capture, bool queen_only) {
    int base = capture ? PROMO_CAPTURE : PROMO;
    list.add(make_move(from, to, base | 3));  // queen first
    if (queen_only) return;
    for (int p = 0; p < 3; ++p) list.add(make_move(from, to, base | p));
}

void add_targets(MoveList& list, int from, Bitboard targets, Bitboard enemies) {
    while (targets) {
        int to = pop_lsb(targets);
        list.add(make_move(from, to, (enemies & bb(to)) ? CAPTURE : QUIET));
    }
}

}  // namespace

void generate(const Board& b, MoveList& list, bool captures_only) {
    const Color us = b.stm, them = ~us;
    const Bitboard enemies = b.colors[them];
    const Bitboard targets = captures_only ? enemies : ~b.colors[us];
    const Bitboard* p = b.pieces + int(us) * 6;
    const int up = us == WHITE ? 8 : -8;
    const int promo_rank = us == WHITE ? 7 : 0, start_rank = us == WHITE ? 1 : 6;

    for (Bitboard pawns = p[PAWN]; pawns;) {
        int from = pop_lsb(pawns), to = from + up;
        bool promo = rank_of(to) == promo_rank;
        if (!(b.occ & bb(to))) {
            if (promo) add_promos(list, from, to, false, captures_only);
            else if (!captures_only) {
                list.add(make_move(from, to, QUIET));
                if (rank_of(from) == start_rank && !(b.occ & bb(to + up)))
                    list.add(make_move(from, to + up, DOUBLE_PUSH));
            }
        }
        for (Bitboard caps = PawnAttacks[us][from] & enemies; caps;) {
            int t = pop_lsb(caps);
            if (promo) add_promos(list, from, t, true, captures_only);
            else list.add(make_move(from, t, CAPTURE));
        }
        if (b.ep != NO_SQ && (PawnAttacks[us][from] & bb(b.ep)))
            list.add(make_move(from, b.ep, EP_CAPTURE));
    }
    for (Bitboard s = p[KNIGHT]; s;) { int f = pop_lsb(s); add_targets(list, f, KnightAttacks[f] & targets, enemies); }
    for (Bitboard s = p[BISHOP]; s;) { int f = pop_lsb(s); add_targets(list, f, bishop_attacks(f, b.occ) & targets, enemies); }
    for (Bitboard s = p[ROOK]; s;)   { int f = pop_lsb(s); add_targets(list, f, rook_attacks(f, b.occ) & targets, enemies); }
    for (Bitboard s = p[QUEEN]; s;)  { int f = pop_lsb(s); add_targets(list, f, queen_attacks(f, b.occ) & targets, enemies); }
    const int k = b.king_sq(us);
    add_targets(list, k, KingAttacks[k] & targets, enemies);

    if (captures_only || b.attacked(k, them)) return;
    // Castling: path empty, king not in check, transit square not attacked. The destination is checked by make().
    if (us == WHITE) {
        if ((b.castling & WK) && !(b.occ & 0x60ull) && !b.attacked(5, them)) list.add(make_move(4, 6, KING_CASTLE));
        if ((b.castling & WQ) && !(b.occ & 0x0Eull) && !b.attacked(3, them)) list.add(make_move(4, 2, QUEEN_CASTLE));
    } else {
        if ((b.castling & BK) && !(b.occ & (0x60ull << 56)) && !b.attacked(61, them)) list.add(make_move(60, 62, KING_CASTLE));
        if ((b.castling & BQ) && !(b.occ & (0x0Eull << 56)) && !b.attacked(59, them)) list.add(make_move(60, 58, QUEEN_CASTLE));
    }
}

uint64_t perft(Board& b, int depth) {
    if (depth == 0) return 1;
    MoveList list;
    generate(b, list, false);
    uint64_t n = 0;
    for (int i = 0; i < list.size; ++i) {
        if (!b.make(list.moves[i])) continue;
        n += perft(b, depth - 1);
        b.unmake(list.moves[i]);
    }
    return n;
}

namespace { const int SEE_VALUE[6] = {100, 300, 300, 500, 900, 0}; }

bool see_ge(const Board& b, Move m, int threshold) {
    const int fl = flags_of(m);
    if (fl != QUIET && fl != DOUBLE_PUSH && fl != CAPTURE) return 0 >= threshold;
    const int from = from_sq(m), to = to_sq(m);
    int swap = (fl == CAPTURE ? SEE_VALUE[type_of(b.mailbox[to])] : 0) - threshold;
    if (swap < 0) return false;  // even keeping the victim free is not enough
    swap = SEE_VALUE[type_of(b.mailbox[from])] - swap;
    if (swap <= 0) return true;  // even losing the mover keeps us at the threshold
    Bitboard occ = b.occ ^ bb(from) ^ bb(to);
    const Bitboard bishops = b.pieces[make_piece(WHITE, BISHOP)] | b.pieces[make_piece(BLACK, BISHOP)]
                           | b.pieces[make_piece(WHITE, QUEEN)] | b.pieces[make_piece(BLACK, QUEEN)];
    const Bitboard rooks = b.pieces[make_piece(WHITE, ROOK)] | b.pieces[make_piece(BLACK, ROOK)]
                         | b.pieces[make_piece(WHITE, QUEEN)] | b.pieces[make_piece(BLACK, QUEEN)];
    Bitboard attackers = (PawnAttacks[BLACK][to] & b.pieces[make_piece(WHITE, PAWN)])
                       | (PawnAttacks[WHITE][to] & b.pieces[make_piece(BLACK, PAWN)])
                       | (KnightAttacks[to] & (b.pieces[make_piece(WHITE, KNIGHT)] | b.pieces[make_piece(BLACK, KNIGHT)]))
                       | (KingAttacks[to] & (b.pieces[make_piece(WHITE, KING)] | b.pieces[make_piece(BLACK, KING)]))
                       | (bishop_attacks(to, occ) & bishops) | (rook_attacks(to, occ) & rooks);
    Color stm = b.stm;
    int res = 1;  // 1 = the side that made move m is winning the exchange so far
    for (;;) {
        stm = ~stm;
        attackers &= occ;
        const Bitboard mine = attackers & b.colors[stm];
        if (!mine) break;
        res ^= 1;
        int pt = PAWN;
        while (!(mine & b.pieces[make_piece(stm, PieceType(pt))])) ++pt;
        if (pt == KING) return (attackers & b.colors[~stm]) ? res ^ 1 : res;  // king may only take an undefended piece
        if ((swap = SEE_VALUE[pt] - swap) < res) break;
        occ ^= bb(lsb(mine & b.pieces[make_piece(stm, PieceType(pt))]));
        if (pt == PAWN || pt == BISHOP || pt == QUEEN) attackers |= bishop_attacks(to, occ) & bishops;
        if (pt == ROOK || pt == QUEEN) attackers |= rook_attacks(to, occ) & rooks;
    }
    return res;
}

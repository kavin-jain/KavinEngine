#include "board.h"
#include <algorithm>
#include <cstring>
#include <sstream>

const char* const START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

namespace {
Key PieceKeys[12][64], CastleKeys[16], EpKeys[8], SideKey;
int CastlingMask[64];  // rights that survive a move touching this square
const char PIECE_CHARS[] = "PNBRQKpnbrqk";
}  // namespace

void Board::init() {
    uint64_t s = 0x2545F4914F6CDD1Dull;
    auto rnd = [&s]() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; };
    for (auto& row : PieceKeys) for (auto& k : row) k = rnd();
    for (auto& k : CastleKeys) k = rnd();
    for (auto& k : EpKeys) k = rnd();
    SideKey = rnd();
    for (int& m : CastlingMask) m = WK | WQ | BK | BQ;
    CastlingMask[0] &= ~WQ;  CastlingMask[7] &= ~WK;  CastlingMask[4] &= ~(WK | WQ);
    CastlingMask[56] &= ~BQ; CastlingMask[63] &= ~BK; CastlingMask[60] &= ~(BK | BQ);
}

void Board::put(int pc, int sq) {
    pieces[pc] |= bb(sq); colors[color_of(pc)] |= bb(sq); occ |= bb(sq);
    mailbox[sq] = pc; key ^= PieceKeys[pc][sq];
}

void Board::remove(int sq) {
    int pc = mailbox[sq];
    pieces[pc] &= ~bb(sq); colors[color_of(pc)] &= ~bb(sq); occ &= ~bb(sq);
    mailbox[sq] = NO_PIECE; key ^= PieceKeys[pc][sq];
}

void Board::move_piece(int from, int to) { int pc = mailbox[from]; remove(from); put(pc, to); }

Key Board::compute_key() const {
    Key k = CastleKeys[castling];
    for (int sq = 0; sq < 64; ++sq) if (mailbox[sq] != NO_PIECE) k ^= PieceKeys[mailbox[sq]][sq];
    if (ep != NO_SQ) k ^= EpKeys[file_of(ep)];
    if (stm == BLACK) k ^= SideKey;
    return k;
}

bool Board::set_fen(const std::string& fen) {
    std::istringstream ss(fen);
    std::string placement, side, cast, eps;
    if (!(ss >> placement >> side >> cast >> eps)) return false;
    int hm = 0, fm = 1;
    if (!(ss >> hm)) hm = 0;
    if (!(ss >> fm)) fm = 1;
    int mb[64];
    std::fill(mb, mb + 64, NO_PIECE);
    int rank = 7, file = 0, kings[2] = {0, 0};
    for (char c : placement) {
        if (c == '/') {
            if (file != 8 || rank == 0) return false;
            --rank; file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return false;
        } else {
            const char* p = std::strchr(PIECE_CHARS, c);
            if (!p || file > 7) return false;
            int pc = int(p - PIECE_CHARS);
            if (type_of(pc) == PAWN && (rank == 0 || rank == 7)) return false;
            if (type_of(pc) == KING) ++kings[color_of(pc)];
            mb[rank * 8 + file++] = pc;
        }
    }
    if (rank != 0 || file != 8 || kings[WHITE] != 1 || kings[BLACK] != 1) return false;
    if (side != "w" && side != "b") return false;
    Color us = side == "w" ? WHITE : BLACK;
    int ep_sq = NO_SQ;
    if (eps != "-") {
        if (eps.size() != 2 || eps[0] < 'a' || eps[0] > 'h') return false;
        if (eps[1] == (us == WHITE ? '6' : '3')) ep_sq = (eps[1] - '1') * 8 + (eps[0] - 'a');
    }

    std::memset(pieces, 0, sizeof pieces);
    colors[WHITE] = colors[BLACK] = occ = 0;
    key = 0;
    for (int sq = 0; sq < 64; ++sq) { mailbox[sq] = NO_PIECE; if (mb[sq] != NO_PIECE) put(mb[sq], sq); }
    stm = us;
    castling = 0;
    const int wk = make_piece(WHITE, KING), wr = make_piece(WHITE, ROOK);
    const int bk = make_piece(BLACK, KING), br = make_piece(BLACK, ROOK);
    for (char c : cast) {
        if (c == 'K' && mailbox[4] == wk && mailbox[7] == wr) castling |= WK;
        if (c == 'Q' && mailbox[4] == wk && mailbox[0] == wr) castling |= WQ;
        if (c == 'k' && mailbox[60] == bk && mailbox[63] == br) castling |= BK;
        if (c == 'q' && mailbox[60] == bk && mailbox[56] == br) castling |= BQ;
    }
    ep = ep_sq;
    halfmove = hm;
    fullmove = fm;
    game_ply = 0;
    key ^= CastleKeys[castling];
    if (ep != NO_SQ) key ^= EpKeys[file_of(ep)];
    if (stm == BLACK) key ^= SideKey;
    return !attacked(king_sq(~stm), stm);  // the side that just moved cannot be in check
}

std::string Board::fen() const {
    std::string s;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            int pc = mailbox[r * 8 + f];
            if (pc == NO_PIECE) { ++empty; continue; }
            if (empty) { s += char('0' + empty); empty = 0; }
            s += PIECE_CHARS[pc];
        }
        if (empty) s += char('0' + empty);
        if (r) s += '/';
    }
    s += stm == WHITE ? " w " : " b ";
    std::string c;
    if (castling & WK) c += 'K';
    if (castling & WQ) c += 'Q';
    if (castling & BK) c += 'k';
    if (castling & BQ) c += 'q';
    s += c.empty() ? "-" : c;
    s += ' ';
    if (ep == NO_SQ) s += '-';
    else { s += char('a' + file_of(ep)); s += char('1' + rank_of(ep)); }
    s += ' ' + std::to_string(halfmove) + ' ' + std::to_string(fullmove);
    return s;
}

bool Board::attacked(int sq, Color by) const {
    const Bitboard* p = pieces + int(by) * 6;
    if (PawnAttacks[~by][sq] & p[PAWN]) return true;
    if (KnightAttacks[sq] & p[KNIGHT]) return true;
    if (KingAttacks[sq] & p[KING]) return true;
    if (bishop_attacks(sq, occ) & (p[BISHOP] | p[QUEEN])) return true;
    return (rook_attacks(sq, occ) & (p[ROOK] | p[QUEEN])) != 0;
}

std::string move_to_uci(Move m) {
    if (m == NO_MOVE) return "0000";
    std::string s;
    s += char('a' + file_of(from_sq(m))); s += char('1' + rank_of(from_sq(m)));
    s += char('a' + file_of(to_sq(m)));   s += char('1' + rank_of(to_sq(m)));
    if (is_promo(m)) s += "nbrq"[promo_type(m) - KNIGHT];
    return s;
}

bool Board::make(Move m) {
    StateInfo& st = history[game_ply];
    st.key = key; st.castling = castling; st.ep = ep; st.halfmove = halfmove; st.captured = NO_PIECE;
    const int from = from_sq(m), to = to_sq(m), flags = flags_of(m);
    const Color us = stm;
    const int pc = mailbox[from];

    key ^= SideKey;
    if (ep != NO_SQ) { key ^= EpKeys[file_of(ep)]; ep = NO_SQ; }
    ++halfmove;
    if (flags == EP_CAPTURE) {
        int cap = to + (us == WHITE ? -8 : 8);
        st.captured = mailbox[cap]; remove(cap); halfmove = 0;
    } else if (is_capture(m)) {
        st.captured = mailbox[to]; remove(to); halfmove = 0;
    }
    move_piece(from, to);
    if (type_of(pc) == PAWN) {
        halfmove = 0;
        if (flags == DOUBLE_PUSH) { ep = (from + to) / 2; key ^= EpKeys[file_of(ep)]; }
        else if (is_promo(m)) { remove(to); put(make_piece(us, promo_type(m)), to); }
    } else if (flags == KING_CASTLE) {
        move_piece(to + 1, to - 1);  // h-rook to f-file
    } else if (flags == QUEEN_CASTLE) {
        move_piece(to - 2, to + 1);  // a-rook to d-file
    }
    key ^= CastleKeys[castling];
    castling &= CastlingMask[from] & CastlingMask[to];
    key ^= CastleKeys[castling];
    stm = ~us;
    if (us == BLACK) ++fullmove;
    ++game_ply;
    if (attacked(king_sq(us), stm)) { unmake(m); return false; }
    return true;
}

void Board::unmake(Move m) {
    --game_ply;
    const StateInfo& st = history[game_ply];
    const int from = from_sq(m), to = to_sq(m), flags = flags_of(m);
    stm = ~stm;
    const Color us = stm;
    if (us == BLACK) --fullmove;
    if (is_promo(m)) { remove(to); put(make_piece(us, PAWN), to); }
    if (flags == KING_CASTLE) move_piece(to - 1, to + 1);
    else if (flags == QUEEN_CASTLE) move_piece(to + 1, to - 2);
    move_piece(to, from);
    if (flags == EP_CAPTURE) put(st.captured, to + (us == WHITE ? -8 : 8));
    else if (st.captured != NO_PIECE) put(st.captured, to);
    key = st.key; castling = st.castling; ep = st.ep; halfmove = st.halfmove;  // restore exactly
}

void Board::trim_history() {
    int keep = std::min(halfmove, game_ply);
    std::memmove(history, history + (game_ply - keep), size_t(keep) * sizeof(StateInfo));
    game_ply = keep;
}

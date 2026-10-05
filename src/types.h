#pragma once
#include <cstdint>

using Bitboard = uint64_t;
using Key = uint64_t;
using Move = uint16_t;

#ifndef MAX_PLY
#define MAX_PLY 128
#endif
constexpr int MAX_MOVES = 256;
#ifndef MAX_GAME_PLY
#define MAX_GAME_PLY 1024  // history entries per Board; UCI trims older plies (repetition needs <= 100 back)
#endif

enum Color : int { WHITE = 0, BLACK = 1 };
inline Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
// Piece index = color * 6 + type (0..11); NO_PIECE = 12.
constexpr int NO_PIECE = 12;
inline int make_piece(Color c, PieceType t) { return int(c) * 6 + int(t); }
inline PieceType type_of(int pc) { return PieceType(pc % 6); }
inline Color color_of(int pc) { return Color(pc / 6); }

// Squares: 0 = a1, 7 = h1, 56 = a8, 63 = h8.
constexpr int NO_SQ = 64;
inline int file_of(int sq) { return sq & 7; }
inline int rank_of(int sq) { return sq >> 3; }
inline Bitboard bb(int sq) { return Bitboard(1) << sq; }

// Move: bits 0-5 from, 6-11 to, 12-15 flags (chessprogramming.org "Encoding Moves").
enum MoveFlag : int {
    QUIET = 0, DOUBLE_PUSH = 1, KING_CASTLE = 2, QUEEN_CASTLE = 3,
    CAPTURE = 4, EP_CAPTURE = 5, PROMO = 8, PROMO_CAPTURE = 12  // promo | 0..3 = N, B, R, Q
};
constexpr Move NO_MOVE = 0;
inline Move make_move(int from, int to, int flags) { return Move(from | (to << 6) | (flags << 12)); }
inline int from_sq(Move m) { return m & 63; }
inline int to_sq(Move m) { return (m >> 6) & 63; }
inline int flags_of(Move m) { return m >> 12; }
inline bool is_capture(Move m) { return (flags_of(m) & 4) != 0; }
inline bool is_promo(Move m) { return (flags_of(m) & 8) != 0; }
inline PieceType promo_type(Move m) { return PieceType(KNIGHT + (flags_of(m) & 3)); }

constexpr int INF = 32000;
constexpr int MATE = 31000;
constexpr int MATE_BOUND = MATE - MAX_PLY;  // |score| >= this means a forced mate

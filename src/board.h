#pragma once
#include <string>
#include "bitboard.h"

struct StateInfo { Key key; int castling, ep, halfmove, captured; };
enum CastlingRight : int { WK = 1, WQ = 2, BK = 4, BQ = 8 };

class Board {
public:
    Bitboard pieces[12];
    Bitboard colors[2];
    Bitboard occ;
    int mailbox[64];
    Color stm;
    int castling, ep, halfmove, fullmove;
    Key key;
    int game_ply;                       // number of entries used in history
    StateInfo history[MAX_GAME_PLY];    // ~24 KB: keep Boards in static storage on ESP32
    struct DirtyPiece { int8_t pc, sq; bool add; };
    DirtyPiece dirty[6];                // piece changes of the last make(), consumed by NNUE updates
    int dirty_n = 0;
    bool track_dirty = false;

    static void init();                 // Zobrist keys; call once after init_bitboards()
    // Parses a FEN (move counters optional). On false the board is unspecified: reset it.
    bool set_fen(const std::string& fen);
    std::string fen() const;
    Key compute_key() const;            // from scratch; tests compare it with the incremental key

    bool make(Move m);                  // false (board unchanged) if the move leaves our king in check
    void unmake(Move m);
    void make_null();
    void unmake_null();
    void trim_history();                // drop history older than the 50-move window

    bool attacked(int sq, Color by) const;
    int king_sq(Color c) const { return lsb(pieces[make_piece(c, KING)]); }
    bool in_check() const { return attacked(king_sq(stm), ~stm); }
    // ply_from_root: plies searched since the root. A repeat of a position from inside the search is a draw at once;
    // a position from the game before the root needs its third occurrence (0 = game level: threefold only).
    bool is_repetition(int ply_from_root = 0) const;
    bool is_draw(int ply_from_root = 0) const;  // 50-move rule, repetition, insufficient material
    bool has_non_pawn_material(Color c) const;

private:
    void put(int pc, int sq);
    void remove(int sq);
    void move_piece(int from, int to);
};

extern const char* const START_FEN;
std::string move_to_uci(Move m);

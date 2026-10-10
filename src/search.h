#pragma once
#include <atomic>
#include "board.h"
#include "tt.h"

struct Limits {
    int64_t time[2] = {-1, -1}, inc[2] = {0, 0};  // ms; time -1 = no clock given
    int movestogo = 0;
    int depth = MAX_PLY - 2;
    int64_t movetime = 0;
    uint64_t nodes = 0;
    bool infinite = false;
    bool ponder = false;  // "go ponder": no clock until ponderhit (g_pondering cleared) or stop
};
struct TimeBudget { int64_t soft, hard; };  // ms since search start; -1 = unlimited
TimeBudget compute_budget(const Limits& l, Color us);

struct SearchResult { Move best; int score; int depth; uint64_t nodes; Move ponder = NO_MOVE; };  // ponder: expected reply

extern TT g_tt;
extern std::atomic<bool> g_stop;
extern std::atomic<bool> g_pondering;  // set by "go ponder", cleared by ponderhit/stop: the clock is off while set
extern int g_clock_reserve;  // ms the time budget never plans to use (UCI "Clock Reserve", online play)
extern int g_threads;        // search threads (UCI "Threads"), 1..SEARCH_THREADS_MAX
extern int g_move_overhead;  // ms kept in reserve per move for GUI/network lag (UCI "Move Overhead")

void search_init();         // reduction table; call once
void clear_search_state();
#ifdef TUNE
#include <string>
void tune_print_options();  // UCI options for the tunable search constants (make TUNE=1, for SPSA)
void tune_set_option(const std::string& name, const std::string& value);
#endif  // ucinewgame: TT, history, killers
// Searches a copy of `root`. Never returns NO_MOVE if a legal move exists. Prints UCI info lines if verbose.
SearchResult search(const Board& root, const Limits& limits, bool verbose);

#pragma once
#include <string>
#include "search.h"

#ifndef TT_DEFAULT_BYTES
#define TT_DEFAULT_BYTES (16u << 20)
#endif
#ifndef TT_FAST
#define TT_FAST 0
#endif
#ifndef BENCH_DEPTH
#define BENCH_DEPTH 11
#endif
#ifndef ENGINE_NAME
#define ENGINE_NAME "KavinEngine 1.0"
#endif

extern const char* const BENCH_FENS[];
extern const int BENCH_COUNT;

void engine_init();                         // idempotent: tables, keys, eval, search, TT
bool uci_command(const std::string& line);  // false on "quit"
void uci_loop();                            // reads lines via platform read_line until quit/EOF
const Board& uci_board();                   // current position (for tests)
uint64_t run_bench(int depth);              // prints "<nodes> nodes <nps> nps" (OpenBench format)
int uci_main(int argc, char** argv);        // PC entry: `engine bench [depth]` or the UCI loop

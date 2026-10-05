#pragma once
#include "board.h"

void eval_init();                // build tables; call once at startup
int evaluate(const Board& b);    // centipawns, from the side to move's point of view

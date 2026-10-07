// src/nnue.h — (768 or 768x10 king buckets -> NNUE_HIDDEN)x2 -> 1 SCReLU network in bullet's quantised format.
#pragma once
#include <cstddef>
#include "board.h"

#ifndef NNUE_HIDDEN
#define NNUE_HIDDEN 256
#endif
#ifndef NNUE_KING_BUCKETS
#define NNUE_KING_BUCKETS 0  // 0: plain 768 inputs (m2 nets); 10: horizontally mirrored king buckets (layout in nnue.cpp)
#endif
constexpr int NNUE_INPUTS = 768 * (NNUE_KING_BUCKETS ? NNUE_KING_BUCKETS : 1);
constexpr int NNUE_QA = 255, NNUE_QB = 64, NNUE_SCALE = 400;

struct alignas(64) Accumulator { int16_t v[2][NNUE_HIDDEN]; };  // [perspective colour][neuron]

size_t nnue_expected_size();
bool nnue_load(const unsigned char* data, size_t size);  // false if the size doesn't match NNUE_HIDDEN
bool nnue_ready();
void nnue_refresh(const Board& b, Accumulator& acc);
void nnue_update(const Accumulator& parent, Accumulator& child, const Board& b);  // applies b.dirty
int nnue_evaluate(const Accumulator& acc, Color stm);  // centipawns, side-to-move relative

extern const unsigned char NNUE_DATA[];  // generated from EVALFILE by tools/bin2cpp.py
extern const size_t NNUE_DATA_SIZE;

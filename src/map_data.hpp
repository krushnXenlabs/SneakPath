#pragma once

#include "cell.hpp"

constexpr int COLS = 20;
constexpr int ROWS = 14;

extern const Cell START;
extern const Cell EXIT;
extern const Cell KEY_CELL;
extern const Cell PROFESSOR1_SPAWN;
extern const Cell PROFESSOR2_SPAWN;

inline bool in_bounds(Cell c) { return c.x >= 0 && c.x < COLS && c.y >= 0 && c.y < ROWS; }

// Out-of-bounds cells count as walls.
bool is_wall(Cell c);

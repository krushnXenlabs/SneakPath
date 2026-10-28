#include "map_data.hpp"

#include <cassert>

const Cell START{1, 1};
const Cell EXIT{18, 12};
const Cell KEY_CELL{2, 12};
const Cell PROFESSOR1_SPAWN{2, 6};
const Cell PROFESSOR2_SPAWN{15, 2};  // top right, far from the first

namespace {

struct Grid {
    bool wall[ROWS][COLS] = {};

    Grid() {
        // Outer boundary
        for (int x = 0; x < COLS; x++) {
            wall[0][x] = true;
            wall[ROWS - 1][x] = true;
        }
        for (int y = 0; y < ROWS; y++) {
            wall[y][0] = true;
            wall[y][COLS - 1] = true;
        }

        // Classroom walls, each with one doorway
        for (int x = 3; x < 8; x++) wall[3][x] = true;
        wall[3][5] = false;

        for (int x = 9; x < 15; x++) wall[5][x] = true;
        wall[5][11] = false;

        for (int x = 4; x < 8; x++) wall[8][x] = true;
        wall[8][6] = false;

        for (int x = 12; x < 17; x++) wall[9][x] = true;
        wall[9][14] = false;
    }
};

const Grid grid;

}  // namespace

bool is_wall(Cell c) {
    return !in_bounds(c) || grid.wall[c.y][c.x];
}

namespace {
// Same sanity checks map_data.py runs on import
struct Checks {
    Checks() {
        assert(!is_wall(START) && !is_wall(EXIT) && !is_wall(KEY_CELL));
        assert(!is_wall(PROFESSOR1_SPAWN) && !is_wall(PROFESSOR2_SPAWN));
    }
};
const Checks checks;
}  // namespace

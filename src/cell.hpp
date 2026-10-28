#pragma once

#include <cstddef>
#include <cstdlib>

// A grid cell. Always (x, y) = (col, row), like the Python tuples.
struct Cell {
    int x = 0;
    int y = 0;
};

inline bool operator==(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Cell a, Cell b) { return !(a == b); }

inline int manhattan(Cell a, Cell b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

struct CellHash {
    std::size_t operator()(Cell c) const {
        return static_cast<std::size_t>(c.x) * 1000003u ^ static_cast<std::size_t>(c.y);
    }
};

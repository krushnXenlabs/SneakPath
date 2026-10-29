#pragma once

#include <vector>

#include "cell.hpp"

struct SearchStats {
    int explored = 0;  // nodes expanded (popped from the frontier)
};

int danger_cost(Cell cell, const std::vector<Cell>& professors);

// Walkable 4-way neighbours, in the order +x, -x, +y, -y.
std::vector<Cell> get_neighbors(Cell node);

// A* from start to goal. Each step costs 1 + danger_cost, so the
// route is the safest one, not the shortest. Returns {} if the goal is unreachable.
std::vector<Cell> astar(Cell start, Cell goal, const std::vector<Cell>& professors,
                        SearchStats* stats = nullptr);

// First step of a plain BFS shortest route; start if unreachable.
Cell step_towards(Cell start, Cell goal);

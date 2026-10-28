#include <cstdio>

#include "../src/map_data.hpp"
#include "../src/pathfinding.hpp"

int main() {
    std::vector<Cell> path = astar(START, EXIT, {PROFESSOR1_SPAWN});

    std::printf("A* Path:\n");
    for (Cell c : path) std::printf("(%d, %d)\n", c.x, c.y);

    std::printf("\nNumber of steps: %d\n", static_cast<int>(path.size()) - 1);
    return 0;
}

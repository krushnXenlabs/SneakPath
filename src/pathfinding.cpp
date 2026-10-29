#include "pathfinding.hpp"

#include <algorithm>
#include <climits>
#include <queue>
#include <unordered_map>

#include "map_data.hpp"

int danger_cost(Cell cell, const std::vector<Cell>& professors) {
    int distance = INT_MAX;
    for (Cell p : professors) distance = std::min(distance, manhattan(cell, p));

    if (distance == 0) return 10000;
    if (distance <= 1) return 1000;
    if (distance <= 2) return 500;
    if (distance <= 3) return 200;
    if (distance <= 4) return 80;
    return 0;
}

std::vector<Cell> get_neighbors(Cell node) {
    const Cell candidates[4] = {
        {node.x + 1, node.y}, {node.x - 1, node.y}, {node.x, node.y + 1}, {node.x, node.y - 1}};

    std::vector<Cell> result;
    for (Cell c : candidates)
        if (!is_wall(c)) result.push_back(c);
    return result;
}

namespace {

struct Entry {
    int priority;
    Cell cell;
};

// Min-heap on (priority, x, y): Python compares (priority, (x, y)) tuples.
struct EntryAfter {
    bool operator()(const Entry& a, const Entry& b) const {
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.cell.x != b.cell.x) return a.cell.x > b.cell.x;
        return a.cell.y > b.cell.y;
    }
};

}  // namespace

std::vector<Cell> astar(Cell start, Cell goal, const std::vector<Cell>& professors,
                        SearchStats* stats) {
    int explored = 0;

    std::priority_queue<Entry, std::vector<Entry>, EntryAfter> frontier;
    frontier.push({0, start});

    std::unordered_map<Cell, Cell, CellHash> came_from;
    std::unordered_map<Cell, int, CellHash> cost_so_far;
    came_from[start] = start;  // start maps to itself; it is the root
    cost_so_far[start] = 0;

    while (!frontier.empty()) {
        Cell current = frontier.top().cell;
        frontier.pop();
        explored++;

        if (current == goal) break;

        for (Cell neighbor : get_neighbors(current)) {
            int new_cost = cost_so_far[current] + 1 + danger_cost(neighbor, professors);

            auto it = cost_so_far.find(neighbor);
            if (it == cost_so_far.end() || new_cost < it->second) {
                cost_so_far[neighbor] = new_cost;
                frontier.push({new_cost + manhattan(neighbor, goal), neighbor});
                came_from[neighbor] = current;
            }
        }
    }

    if (stats) stats->explored = explored;

    if (!came_from.count(goal)) return {};

    std::vector<Cell> path;
    for (Cell c = goal;; c = came_from[c]) {
        path.push_back(c);
        if (c == start) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

Cell step_towards(Cell start, Cell goal) {
    if (start == goal) return start;

    std::unordered_map<Cell, Cell, CellHash> came_from;
    std::queue<Cell> queue;
    came_from[start] = start;
    queue.push(start);

    while (!queue.empty()) {
        Cell current = queue.front();
        queue.pop();
        if (current == goal) break;

        for (Cell neighbor : get_neighbors(current)) {
            if (!came_from.count(neighbor)) {
                came_from[neighbor] = current;
                queue.push(neighbor);
            }
        }
    }

    if (!came_from.count(goal)) return start;

    Cell current = goal;
    while (came_from[current] != start) current = came_from[current];
    return current;
}

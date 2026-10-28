#include "game.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_set>

Game::Game(unsigned seed) : rng(seed) {
    reset();
}

// --------------------------------
// Pickups
// --------------------------------

namespace {

// Every cell the student can walk to from START
std::unordered_set<Cell, CellHash> reachable_from_start() {
    std::unordered_set<Cell, CellHash> seen = {START};
    std::queue<Cell> queue;
    queue.push(START);

    while (!queue.empty()) {
        Cell current = queue.front();
        queue.pop();
        for (Cell n : get_neighbors(current))
            if (seen.insert(n).second) queue.push(n);
    }
    return seen;
}

}  // namespace

void Game::random_pickups() {
    // The key stays put; COFFEE and BOOK go to random free cells each game.
    // Candidates must be reachable and not right next to a professor spawn.
    std::vector<Cell> free;
    for (Cell c : reachable_from_start()) {
        if (c == START || c == EXIT || c == KEY_CELL) continue;
        if (manhattan(c, PROFESSOR1_SPAWN) <= 2 || manhattan(c, PROFESSOR2_SPAWN) <= 2) continue;
        free.push_back(c);
    }
    // Hash-set order is arbitrary; sort so a seed gives a repeatable game.
    std::sort(free.begin(), free.end(), [](Cell a, Cell b) {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    std::shuffle(free.begin(), free.end(), rng);

    pickups = {{KEY_CELL, Kind::Key}, {free[0], Kind::Coffee}, {free[1], Kind::Book}};
}

bool Game::has_pickup_at(Cell c) const {
    return std::any_of(pickups.begin(), pickups.end(), [&](const Pickup& p) { return p.cell == c; });
}

void Game::collect_pickup() {
    auto it = std::find_if(pickups.begin(), pickups.end(),
                           [&](const Pickup& p) { return p.cell == student; });
    if (it == pickups.end()) return;

    switch (it->kind) {
        case Kind::Key: has_key = true; break;
        case Kind::Coffee: boost_timer = BOOST_TIME; break;
        case Kind::Book: freeze_timer = FREEZE_TIME; break;
    }
    pickups.erase(it);
}

// --------------------------------
// A* hint path (avoids the professors)
// --------------------------------

void Game::find_path() {
    recalculations++;
    // The exit is locked, so the hint leads to the key first
    path = astar(student, has_key ? EXIT : KEY_CELL, professor_cells(), &search_stats);
}

// --------------------------------
// Reset everything for a new game
// --------------------------------

void Game::reset() {
    professor_delay = professor_delay_for(difficulty);

    elapsed_ms = 0;
    steps = 0;
    final_score = 0;
    new_best = false;

    random_pickups();
    has_key = false;
    boost_timer = 0;
    freeze_timer = 0;

    student = START;
    professors = {PROFESSOR1_SPAWN, PROFESSOR2_SPAWN};
    professors_previous = professors;

    professor_timer = 0;
    student_timer = 0;

    search_stats = SearchStats{};
    recalculations = 0;

    find_path();
}

// --------------------------------
// One step of the game
// --------------------------------

Outcome Game::update(int dt, Cell direction) {
    bool caught = false;

    elapsed_ms += dt;
    boost_timer = std::max(boost_timer - dt, 0);
    freeze_timer = std::max(freeze_timer - dt, 0);

    auto professor_at = [&](Cell c) { return c == professors[0] || c == professors[1]; };

    // --------------------------------
    // Move student
    // --------------------------------

    student_timer += dt;

    // Holding a key repeats the step every step_delay ms (shorter with coffee)
    int step_delay = boost_timer > 0 ? BOOST_DELAY : STUDENT_DELAY;
    bool wants_move = direction.x != 0 || direction.y != 0;

    if (wants_move && student_timer >= step_delay) {
        Cell target{student.x + direction.x, student.y + direction.y};

        if (!is_wall(target)) {
            student_timer = 0;
            student = target;
            steps++;

            collect_pickup();

            // Student walked into a professor
            if (professor_at(student)) caught = true;

            find_path();
        }
    }

    // --------------------------------
    // Move professors
    // --------------------------------

    if (freeze_timer > 0)
        professor_timer = 0;  // frozen: no time builds up toward a step
    else
        professor_timer += dt;

    if (professor_timer >= professor_delay) {
        // Keep the leftover time so the speed stays accurate, but never
        // bank more than one step
        professor_timer = std::min(professor_timer - professor_delay, professor_delay);

        for (std::size_t i = 0; i < professors.size(); i++) {
            Cell professor = professors[i];
            Cell new_cell;

            if (manhattan(student, professor) <= CHASE_RANGE) {
                // Student noticed: step along the shortest route
                new_cell = step_towards(professor, student);
            } else {
                // Wander randomly, avoiding turning straight back
                std::vector<Cell> options = get_neighbors(professor);
                std::vector<Cell> forward;
                for (Cell c : options)
                    if (c != professors_previous[i]) forward.push_back(c);
                const std::vector<Cell>& pool = forward.empty() ? options : forward;
                new_cell = pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(rng)];
            }

            professors_previous[i] = professor;
            professors[i] = new_cell;

            if (student == new_cell) caught = true;
        }

        // Professors moved, so the safest route may have changed
        find_path();
    }

    // --------------------------------
    // Collision, then exit (catching takes priority)
    // --------------------------------

    if (professor_at(student)) caught = true;

    if (caught) return Outcome::Caught;

    if (student == EXIT && has_key) {
        final_score = calculate_score();

        if (final_score > best_for(difficulty)) {
            best_scores[static_cast<int>(difficulty)] = final_score;
            new_best = true;
            save_best_scores();
        }
        return Outcome::Won;
    }

    return Outcome::None;
}

// --------------------------------
// Score and saved best scores
// --------------------------------

int Game::calculate_score() const {
    // Faster and fewer steps score higher, pickups add a bonus,
    // and harder difficulties multiply the total
    double seconds = elapsed_ms / 1000.0;
    int collected = 3 - static_cast<int>(pickups.size());
    double base = std::max(2000 - 10 * seconds - 5 * steps, 0.0) + 100 * collected;

    return static_cast<int>(base * multiplier_for(difficulty));
}

int Game::best_for(Difficulty d) const {
    auto it = best_scores.find(static_cast<int>(d));
    return it == best_scores.end() ? 0 : it->second;
}

// File format: one "NAME score" line per difficulty.
void Game::load_best_scores() {
    best_scores.clear();
    if (score_file.empty()) return;

    std::ifstream in(score_file);
    std::string name;
    long long value;
    while (in >> name >> value) {
        for (int i = 0; i < DIFFICULTY_COUNT; i++)
            if (name == name_of(static_cast<Difficulty>(i)) && value >= 0 && value < 100000000)
                best_scores[i] = static_cast<int>(value);
    }
}

void Game::save_best_scores() const {
    if (score_file.empty()) return;

    std::ofstream out(score_file);  // the game works fine if this fails
    for (const auto& [d, score] : best_scores)
        out << name_of(static_cast<Difficulty>(d)) << ' ' << score << '\n';
}

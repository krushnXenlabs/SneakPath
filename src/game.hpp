#pragma once

#include <array>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "cell.hpp"
#include "pathfinding.hpp"
#include "settings.hpp"

// Which screen is shown and handled
enum class State { Menu, Instructions, Playing, Paused, GameOver, Win };

enum class Kind { Key, Coffee, Book };

struct Pickup {
    Cell cell;
    Kind kind;
};

enum class Outcome { None, Caught, Won };

// All gameplay state. reset() puts every field back, so a new field only
// has to be added here and in reset().
struct Game {
    explicit Game(unsigned seed = std::random_device{}());

    Difficulty difficulty = DEFAULT_DIFFICULTY;
    std::string score_file;  // empty = never touch the disk

    Cell student = START;
    std::array<Cell, 2> professors = {PROFESSOR1_SPAWN, PROFESSOR2_SPAWN};
    std::array<Cell, 2> professors_previous = professors;

    int professor_delay = professor_delay_for(DEFAULT_DIFFICULTY);
    int professor_timer = 0;
    int student_timer = 0;
    int boost_timer = 0;   // ms of coffee boost left
    int freeze_timer = 0;  // ms of professor freeze left

    std::vector<Pickup> pickups;  // pickups still on the map
    bool has_key = false;

    int elapsed_ms = 0;  // play time, only counts while updating
    int steps = 0;
    int final_score = 0;
    bool new_best = false;
    std::map<int, int> best_scores;  // Difficulty -> best score

    SearchStats search_stats;
    int recalculations = 0;
    std::vector<Cell> path;

    void reset();

    // Advance the game by dt ms. `direction` is the held input, {0,0} for none.
    Outcome update(int dt_ms, Cell direction);

    int calculate_score() const;
    int best_for(Difficulty d) const;
    bool has_pickup_at(Cell c) const;

    void load_best_scores();
    void save_best_scores() const;

private:
    std::mt19937 rng;

    void random_pickups();
    void collect_pickup();
    void find_path();
    std::vector<Cell> professor_cells() const { return {professors[0], professors[1]}; }
};

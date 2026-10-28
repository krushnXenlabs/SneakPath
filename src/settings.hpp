#pragma once

#include <cstdint>

#include "map_data.hpp"

struct Color {
    std::uint8_t r, g, b, a = 255;
};

constexpr int TILE_SIZE = 40;
constexpr int WIDTH = COLS * TILE_SIZE;
constexpr int HEIGHT = ROWS * TILE_SIZE;
constexpr int FPS = 60;

// Top information bar (drawn above the map)
constexpr int BAR_HEIGHT = 94;
constexpr int SCREEN_WIDTH = WIDTH;
constexpr int SCREEN_HEIGHT = HEIGHT + BAR_HEIGHT;

// Colors
constexpr Color BACKGROUND{30, 30, 35};
constexpr Color FLOOR{220, 220, 210};
constexpr Color FLOOR_ALT{210, 210, 198};
constexpr Color STUDENT_COLOR{40, 100, 240};
constexpr Color PROFESSOR_COLOR{220, 50, 50};
constexpr Color EXIT_COLOR{40, 180, 80};
constexpr Color PATH_COLOR{240, 200, 60};
constexpr Color FROZEN_COLOR{140, 200, 255};

constexpr Color BAR_BG{24, 24, 30};
constexpr Color BAR_BORDER{90, 90, 105};
constexpr Color TEXT_COLOR{235, 235, 235};
constexpr Color TEXT_DIM{150, 150, 165};
constexpr Color BUTTON_COLOR{50, 52, 68};
constexpr Color BUTTON_HOVER{80, 110, 200};
constexpr Color BUTTON_BORDER{140, 140, 165};
constexpr Color DANGER_ZONE_COLOR{220, 40, 40, 70};

constexpr Color BRICK{125, 72, 62};
constexpr Color BRICK_MORTAR{85, 52, 46};
constexpr Color LOCKER{70, 100, 140};
constexpr Color LOCKER_DARK{40, 62, 92};

// Gameplay timings (ms)
constexpr int BOOST_TIME = 5000;   // speed boost from coffee
constexpr int BOOST_DELAY = 90;    // student step delay while boosted
constexpr int FREEZE_TIME = 4000;  // professors frozen by a book
constexpr int STUDENT_DELAY = 180; // minimum ms between player steps
constexpr int CHASE_RANGE = 5;     // Manhattan distance at which a professor notices the student
constexpr int DANGER_RANGE = 4;    // red zone, matches danger_cost in pathfinding

enum class Difficulty { Easy = 0, Medium = 1, Hard = 2 };
constexpr int DIFFICULTY_COUNT = 3;

constexpr int professor_delay_for(Difficulty d) {
    return d == Difficulty::Easy ? 400 : d == Difficulty::Medium ? 300 : 230;
}
constexpr double multiplier_for(Difficulty d) {
    return d == Difficulty::Easy ? 1.0 : d == Difficulty::Medium ? 1.5 : 2.0;
}
constexpr const char* name_of(Difficulty d) {
    return d == Difficulty::Easy ? "EASY" : d == Difficulty::Medium ? "MEDIUM" : "HARD";
}
constexpr Difficulty DEFAULT_DIFFICULTY = Difficulty::Medium;

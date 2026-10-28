#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unordered_set>

#include "../src/game.hpp"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        checks++;                                                         \
        if (!(cond)) {                                                    \
            failures++;                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);           \
        }                                                                 \
    } while (0)

static const Cell NONE{0, 0};
static const Cell RIGHT{1, 0};
static const Cell LEFT{-1, 0};

// A game with the professors parked far away and no pickups, for isolated checks
static Game quiet_game() {
    Game g(1);
    g.pickups.clear();
    g.professors = {Cell{1, 12}, Cell{1, 12}};
    g.professors_previous = g.professors;
    g.professor_delay = 1000000;
    return g;
}

static void test_paths() {
    Game g(1);
    CHECK(!astar(START, KEY_CELL, {PROFESSOR1_SPAWN}).empty());
    CHECK(!astar(START, EXIT, {PROFESSOR1_SPAWN, PROFESSOR2_SPAWN}).empty());
    CHECK(!astar(KEY_CELL, EXIT, {PROFESSOR1_SPAWN, PROFESSOR2_SPAWN}).empty());
    CHECK(astar(START, Cell{0, 0}, {}).empty());  // wall: unreachable
    CHECK(g.path.size() >= 2 && g.path.back() == KEY_CELL);  // hint leads to the key first
}

static void test_walls() {
    Game g = quiet_game();
    Outcome o = g.update(200, LEFT);  // (0,1) is the boundary wall
    CHECK(o == Outcome::None);
    CHECK(g.student == START);
    CHECK(g.steps == 0);

    g.update(200, RIGHT);
    CHECK(g.student == (Cell{2, 1}));
    CHECK(g.steps == 1);

    // Step rate limit: a second call right away must not move
    g.update(50, RIGHT);
    CHECK(g.student == (Cell{2, 1}));
}

static void test_exit_needs_key() {
    Game g = quiet_game();
    g.student = Cell{17, 12};
    g.has_key = false;
    CHECK(g.update(200, RIGHT) == Outcome::None);
    CHECK(g.student == EXIT);

    Game h = quiet_game();
    h.student = Cell{17, 12};
    h.has_key = true;
    CHECK(h.update(200, RIGHT) == Outcome::Won);
}

static void test_key_pickup_unlocks() {
    Game g(7);
    g.professors = {Cell{18, 1}, Cell{18, 2}};
    g.professor_delay = 1000000;
    g.student = Cell{1, 12};
    g.update(200, RIGHT);
    CHECK(g.student == KEY_CELL);
    CHECK(g.has_key);
    CHECK(!g.has_pickup_at(KEY_CELL));
    CHECK(g.path.back() == EXIT);  // hint now leads to the exit
}

static void test_catch_priority() {
    Game g = quiet_game();
    g.student = Cell{17, 12};
    g.has_key = true;
    g.professors = {EXIT, Cell{1, 12}};
    CHECK(g.update(200, RIGHT) == Outcome::Caught);  // catch beats escape

    Game h = quiet_game();
    h.student = Cell{5, 5};
    h.professors = {Cell{6, 5}, Cell{1, 12}};
    CHECK(h.update(200, RIGHT) == Outcome::Caught);  // walked into a professor
}

static void test_professor_catches_student() {
    Game g = quiet_game();
    g.professor_delay = 300;
    g.student = Cell{10, 7};
    g.professors = {Cell{11, 7}, Cell{1, 12}};
    g.professors_previous = g.professors;
    CHECK(g.update(300, NONE) == Outcome::Caught);
}

static void test_coffee() {
    Game g = quiet_game();
    g.pickups = {{Cell{3, 1}, Kind::Coffee}};
    g.update(200, RIGHT);
    g.update(200, RIGHT);
    CHECK(g.student == (Cell{3, 1}));
    CHECK(g.boost_timer == BOOST_TIME);

    // With coffee, a step every 90 ms
    int before = g.steps;
    g.update(100, RIGHT);
    g.update(100, RIGHT);
    CHECK(g.steps == before + 2);

    // Boost runs out after 5 s
    g.update(BOOST_TIME, NONE);
    CHECK(g.boost_timer == 0);
    before = g.steps;
    g.student_timer = 0;
    g.update(100, RIGHT);  // 100 ms < 180 ms normal delay: no step
    CHECK(g.steps == before);
    g.update(100, RIGHT);  // 200 ms total: step
    CHECK(g.steps == before + 1);
}

static void test_book_freeze() {
    Game g = quiet_game();
    g.professor_delay = 300;
    g.professors = {Cell{15, 6}, Cell{4, 11}};
    g.professors_previous = g.professors;
    g.freeze_timer = FREEZE_TIME;
    auto before = g.professors;
    for (int i = 0; i < 30; i++) g.update(100, NONE);  // 3 s < 4 s
    CHECK(g.professors == before);
    for (int i = 0; i < 40; i++) g.update(100, NONE);  // well past the freeze
    CHECK(g.professors != before);
}

static void test_score() {
    Game g = quiet_game();
    g.difficulty = Difficulty::Hard;
    g.elapsed_ms = 10000;
    g.steps = 50;
    CHECK(g.calculate_score() == 3900);  // (2000 - 100 - 250 + 300) * 2.0

    g.difficulty = Difficulty::Medium;
    CHECK(g.calculate_score() == 2925);  // 1950 * 1.5

    g.elapsed_ms = 10000000;  // base is clamped at 0, pickups still count
    CHECK(g.calculate_score() == 450);  // 300 * 1.5
}

static void test_random_pickups_reachable() {
    for (unsigned seed = 0; seed < 1000; seed++) {
        Game g(seed);
        CHECK(g.pickups.size() == 3);

        std::unordered_set<Cell, CellHash> seen;
        for (const Pickup& p : g.pickups) {
            CHECK(!is_wall(p.cell));
            CHECK(seen.insert(p.cell).second);  // all on different cells
            CHECK(p.cell != START && p.cell != EXIT);
            CHECK(manhattan(p.cell, PROFESSOR1_SPAWN) > 2 || p.kind == Kind::Key);
            CHECK(manhattan(p.cell, PROFESSOR2_SPAWN) > 2 || p.kind == Kind::Key);
            CHECK(!astar(START, p.cell, {}).empty());
        }
    }
}

static void test_reset() {
    Game g(3);
    g.difficulty = Difficulty::Hard;
    g.steps = 9;
    g.has_key = true;
    g.freeze_timer = 99;
    g.boost_timer = 99;
    g.student = Cell{9, 9};
    g.reset();
    CHECK(g.steps == 0 && !g.has_key && g.freeze_timer == 0 && g.boost_timer == 0);
    CHECK(g.student == START);
    CHECK(g.professor_delay == 230);
    CHECK(g.recalculations == 1);
    CHECK(g.pickups.size() == 3);
}

static void test_highscores() {
    const char* file = "test_scores.tmp";
    std::remove(file);

    Game g(1);
    g.score_file = file;
    g.load_best_scores();  // missing file -> empty
    CHECK(g.best_scores.empty());

    g.best_scores[static_cast<int>(Difficulty::Easy)] = 1234;
    g.best_scores[static_cast<int>(Difficulty::Hard)] = 4321;
    g.save_best_scores();

    Game h(2);
    h.score_file = file;
    h.load_best_scores();
    CHECK(h.best_for(Difficulty::Easy) == 1234);
    CHECK(h.best_for(Difficulty::Hard) == 4321);
    CHECK(h.best_for(Difficulty::Medium) == 0);

    {
        std::ofstream out(file);
        out << "garbage \x01\x02 EASY notanumber\nHARD -5\nMEDIUM 77\n";
    }
    Game k(3);
    k.score_file = file;
    k.load_best_scores();  // must not crash
    CHECK(k.best_for(Difficulty::Hard) == 0);

    // A real win saves a new best
    Game w = quiet_game();
    w.score_file = file;
    w.student = Cell{17, 12};
    w.has_key = true;
    CHECK(w.update(200, RIGHT) == Outcome::Won);
    CHECK(w.new_best && w.final_score > 0);
    Game r(4);
    r.score_file = file;
    r.load_best_scores();
    CHECK(r.best_for(Difficulty::Medium) == w.final_score);

    std::remove(file);
}

int main() {
    test_paths();
    test_walls();
    test_exit_needs_key();
    test_key_pickup_unlocks();
    test_catch_priority();
    test_professor_catches_student();
    test_coffee();
    test_book_freeze();
    test_score();
    test_random_pickups_reachable();
    test_reset();
    test_highscores();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

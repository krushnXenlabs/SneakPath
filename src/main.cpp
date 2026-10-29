#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "db.hpp"
#include "draw.hpp"
#include "game.hpp"

namespace {

State state = State::Menu;
int fade_alpha = 0;  // > 0 while a screen transition (fade-in) is running
bool running = true;

// --------------------------------
// Leaderboard state (the DB work itself happens on a worker thread)
// --------------------------------

std::unique_ptr<Leaderboard> board;
std::string player_file;       // remembered player name
std::string player_name;       // "" until the player has entered one
std::string name_input;        // what is being typed on the name screen
State name_return = State::Win;  // Win after a first win, Menu when changing the name
std::string submit_status;     // shown on the win screen: Submitting / Rank / offline
Difficulty board_tab = DEFAULT_DIFFICULTY;
std::vector<LeaderboardRow> board_rows;
std::string board_status;
State board_return = State::Menu;  // where BACK leads from the leaderboard
draw::Highlight highlight;

std::string short_error(const std::string& error) {
    return error.size() > 70 ? error.substr(0, 67) + "..." : error;
}

void load_player_name() {
    std::ifstream in(player_file);
    std::string line;
    if (std::getline(in, line)) player_name = sanitize_name(line);
}

void save_player_name() {
    std::ofstream out(player_file);  // the game works fine if this fails
    out << player_name << '\n';
}

void change_state(State new_state) {
    // Pausing/resuming should not flash, every other change fades in
    bool pause_toggle = (state == State::Playing && new_state == State::Paused) ||
                        (state == State::Paused && new_state == State::Playing);
    if (!pause_toggle) fade_alpha = 255;

    if (new_state == State::NameEntry && state != State::NameEntry) SDL_StartTextInput();
    if (state == State::NameEntry && new_state != State::NameEntry) SDL_StopTextInput();

    state = new_state;
}

void submit_run(const Game& game) {
    RunRecord run;
    run.name = player_name;
    run.difficulty = game.difficulty;
    run.score = game.final_score;
    run.time_ms = game.elapsed_ms;
    run.steps = std::min(game.steps, 65535);
    run.pickups = 3 - static_cast<int>(game.pickups.size());

    highlight = {run.name, run.score, run.time_ms};
    submit_status = "Submitting score...";
    board->submit(run);
}

void on_win(const Game& game) {
    if (player_name.empty()) {
        name_input.clear();
        name_return = State::Win;
        change_state(State::NameEntry);
    } else {
        submit_run(game);
        change_state(State::Win);
    }
}

void request_board() {
    board_rows.clear();
    board_status = "Loading...";
    board->fetch(board_tab, 10);
}

void open_leaderboard(const Game& game, State from) {
    board_return = from;
    board_tab = game.difficulty;
    request_board();
    change_state(State::Leaderboard);
}

void set_tab(int index) {
    board_tab = static_cast<Difficulty>(((index % DIFFICULTY_COUNT) + DIFFICULTY_COUNT) % DIFFICULTY_COUNT);
    request_board();
}

// Applies finished background jobs; called every frame
void poll_board() {
    LeaderboardResult r;
    while (board->poll(r)) {
        if (r.kind == LeaderboardResult::Kind::Submit) {
            if (r.ok)
                submit_status = "Rank #" + std::to_string(r.rank) + " of " + std::to_string(r.total) + " on " +
                                name_of(r.difficulty);
            else
                submit_status = "Leaderboard offline: " + short_error(r.error);
        } else if (r.difficulty == board_tab) {  // ignore answers for a tab we already left
            if (r.ok) {
                board_rows = r.rows;
                board_status = board_rows.empty() ? "No scores yet. Be the first!" : "";
            } else {
                board_rows.clear();
                board_status = "Leaderboard offline: " + short_error(r.error);
            }
        }
    }
}

// --------------------------------
// Buttons
// --------------------------------

constexpr int BUTTON_WIDTH = 280;
constexpr int BUTTON_HEIGHT = 48;
constexpr int BUTTON_GAP = 16;

// Stacked vertically and centered
std::vector<Button> make_buttons(const std::vector<std::string>& labels, int top, int gap = BUTTON_GAP) {
    std::vector<Button> buttons;
    for (std::size_t i = 0; i < labels.size(); i++) {
        SDL_Rect r{(SCREEN_WIDTH - BUTTON_WIDTH) / 2,
                   top + static_cast<int>(i) * (BUTTON_HEIGHT + gap), BUTTON_WIDTH, BUTTON_HEIGHT};
        buttons.push_back({labels[i], r});
    }
    return buttons;
}

std::vector<Button> get_buttons(const Game& game) {
    switch (state) {
        case State::Menu:
            return make_buttons({"START GAME", std::string("DIFFICULTY: ") + name_of(game.difficulty),
                                 "PLAYER: " + (player_name.empty() ? std::string("NOT SET") : player_name),
                                 "INSTRUCTIONS", "LEADERBOARD", "QUIT"},
                                250, 12);
        case State::Instructions: return make_buttons({"BACK"}, SCREEN_HEIGHT - 80);
        case State::Paused: return make_buttons({"RESUME", "RESTART", "MAIN MENU"}, 265);
        case State::GameOver: return make_buttons({"RESTART", "LEADERBOARD", "MAIN MENU", "QUIT"}, 265);
        case State::Win: return make_buttons({"PLAY AGAIN", "LEADERBOARD", "MAIN MENU", "QUIT"}, 335);  // room for the score lines
        case State::Leaderboard: {
            std::vector<Button> buttons;
            for (int i = 0; i < DIFFICULTY_COUNT; i++)
                buttons.push_back({name_of(static_cast<Difficulty>(i)), SDL_Rect{144 + i * 176, 100, 160, 44}});
            buttons.push_back({"BACK", SDL_Rect{(SCREEN_WIDTH - BUTTON_WIDTH) / 2, SCREEN_HEIGHT - 72, BUTTON_WIDTH,
                                                BUTTON_HEIGHT}});
            return buttons;
        }
        default: return {};
    }
}

void press_button(Game& game, const std::string& label) {
    if (label == "START GAME" || label == "PLAY AGAIN" || label == "RESTART") {
        game.reset();
        submit_status.clear();
        change_state(State::Playing);
    } else if (label.rfind("DIFFICULTY", 0) == 0) {
        game.difficulty = static_cast<Difficulty>((static_cast<int>(game.difficulty) + 1) % DIFFICULTY_COUNT);
    } else if (label.rfind("PLAYER", 0) == 0) {
        name_input = player_name;  // start from the current name so it can be edited
        name_return = State::Menu;
        change_state(State::NameEntry);
    } else if (label == "INSTRUCTIONS") {
        change_state(State::Instructions);
    } else if (label == "LEADERBOARD") {
        open_leaderboard(game, state);
    } else if (state == State::Leaderboard && (label == "EASY" || label == "MEDIUM" || label == "HARD")) {
        set_tab(label == "EASY" ? 0 : label == "MEDIUM" ? 1 : 2);
    } else if (label == "BACK" && state == State::Leaderboard) {
        change_state(board_return);
    } else if (label == "BACK" || label == "MAIN MENU") {
        change_state(State::Menu);
    } else if (label == "RESUME") {
        change_state(State::Playing);
    } else if (label == "QUIT") {
        running = false;
    }
}

// --------------------------------
// Input
// --------------------------------

// Movement direction from WASD / arrows, {0,0} if none
Cell get_input_direction() {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) return {0, -1};
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) return {0, 1};
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) return {-1, 0};
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) return {1, 0};
    return {0, 0};
}

void type_text(const char* text) {
    for (const char* c = text; *c; ++c) {
        unsigned char ch = static_cast<unsigned char>(*c);
        bool allowed = std::isalnum(ch) || ch == '_' || ch == '-' || (ch == ' ' && !name_input.empty());
        if (allowed && name_input.size() < MAX_NAME_LENGTH) name_input += static_cast<char>(ch);
    }
}

void handle_name_key(Game& game, SDL_Keycode key) {
    if (key == SDLK_BACKSPACE) {
        if (!name_input.empty()) name_input.pop_back();
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        std::string name = sanitize_name(name_input);
        if (!name.empty()) {
            player_name = name;
            save_player_name();
            if (name_return == State::Win) submit_run(game);  // changing the name from the menu submits nothing
            change_state(name_return);
        }
    } else if (key == SDLK_ESCAPE) {
        if (name_return == State::Win) submit_status = "Score not submitted";
        change_state(name_return);
    }
}

void handle_events(Game& game) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;

        } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            SDL_Point p{event.button.x, event.button.y};
            for (const Button& b : get_buttons(game)) {
                if (SDL_PointInRect(&p, &b.rect)) {
                    press_button(game, b.label);
                    break;
                }
            }

        } else if (event.type == SDL_TEXTINPUT) {
            if (state == State::NameEntry) type_text(event.text.text);

        } else if (event.type == SDL_KEYDOWN && state == State::NameEntry) {
            handle_name_key(game, event.key.keysym.sym);  // key repeat is wanted for backspace

        } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
            SDL_Keycode key = event.key.keysym.sym;
            bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER;
            bool escape = key == SDLK_ESCAPE;

            switch (state) {
                case State::Menu:
                    if (enter) press_button(game, "START GAME");
                    break;
                case State::Instructions:
                    if (escape || enter) press_button(game, "BACK");
                    break;
                case State::Playing:
                    if (escape) change_state(State::Paused);
                    break;
                case State::Paused:
                    if (escape || enter)
                        press_button(game, "RESUME");
                    else if (key == SDLK_r)
                        press_button(game, "RESTART");
                    break;
                case State::GameOver:
                case State::Win:
                    if (enter || key == SDLK_r)
                        press_button(game, "RESTART");
                    else if (escape)
                        press_button(game, "MAIN MENU");
                    break;
                case State::Leaderboard:
                    if (escape || enter)
                        press_button(game, "BACK");
                    else if (key == SDLK_LEFT)
                        set_tab(static_cast<int>(board_tab) - 1);
                    else if (key == SDLK_RIGHT)
                        set_tab(static_cast<int>(board_tab) + 1);
                    break;
                case State::NameEntry:
                    break;
            }
        }
    }
}

// Fake data so the new screens can be screenshotted without a database
void fill_demo_leaderboard() {
    const char* names[] = {"Ava", "Noah", "Mia", "Liam", "Zoe", "Ethan", "Ivy", "Leo"};
    for (int i = 0; i < 8; i++) {
        LeaderboardRow row;
        row.rank = i + 1;
        row.name = names[i];
        row.score = 3400 - i * 310;
        row.time_ms = 21000 + i * 3700;
        row.steps = 52 + i * 6;
        row.date = "2026-10-2" + std::to_string(8 - i % 3);
        board_rows.push_back(row);
    }
    highlight = {"Mia", board_rows[2].score, board_rows[2].time_ms};
}

}  // namespace

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("SneakPath", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }

    if (!draw::init(window, renderer)) return 1;

    // --screenshot <screen> <file.bmp>: draw one frame of that screen, save it and exit
    const char* shot_screen = nullptr;
    const char* shot_file = nullptr;
    for (int i = 1; i + 2 < argc; i++) {
        if (std::strcmp(argv[i], "--screenshot") == 0) {
            shot_screen = argv[i + 1];
            shot_file = argv[i + 2];
        }
    }

    Game game;
    char* base_path = SDL_GetBasePath();
    std::string base = base_path ? base_path : "./";
    SDL_free(base_path);

    game.score_file = base + "highscores.txt";
    game.load_best_scores();

    player_file = base + "player.txt";
    load_player_name();
    board = std::make_unique<Leaderboard>(load_db_config(base + "db.conf"));

    Uint32 last_ticks = SDL_GetTicks();

    if (shot_screen) {
        const std::string name = shot_screen;
        if (name == "instructions") state = State::Instructions;
        else if (name == "playing") state = State::Playing;
        else if (name == "paused") state = State::Paused;
        else if (name == "gameover") state = State::GameOver;
        else if (name == "win") {
            state = State::Win;
            submit_status = "Rank #4 of 37 on MEDIUM";
        } else if (name == "nameentry" || name == "nameentry-menu") {
            state = State::NameEntry;
            name_input = "Krushn";
            name_return = name == "nameentry" ? State::Win : State::Menu;
        } else if (name == "leaderboard" || name == "leaderboard-offline") {
            state = State::Leaderboard;
            if (name == "leaderboard") fill_demo_leaderboard();
            else board_status = "Leaderboard offline: Can't connect to MySQL server on '127.0.0.1:3306'";
        }
        player_name = "Krushn";  // the real name from player.txt must not appear in screenshots
        game.elapsed_ms = 23400;
        game.steps = 41;
        game.final_score = 2150;
    }

    while (running) {
        Uint32 now = SDL_GetTicks();
        int dt = static_cast<int>(now - last_ticks);
        last_ticks = now;
        if (dt > 100) dt = 100;  // a stalled frame (window drag) must not teleport the game forward

        SDL_Point mouse{-1, -1};
        if (!shot_screen) SDL_GetMouseState(&mouse.x, &mouse.y);

        handle_events(game);
        poll_board();

        if (state == State::Playing && !shot_screen) {
            Outcome outcome = game.update(dt, get_input_direction());
            if (outcome == Outcome::Caught) change_state(State::GameOver);
            if (outcome == Outcome::Won) on_win(game);
        }

        std::vector<Button> buttons = get_buttons(game);

        switch (state) {
            case State::Menu: draw::menu(buttons, mouse); break;
            case State::Instructions: draw::instructions(buttons, mouse); break;
            case State::Playing: draw::game_scene(game, state); break;
            case State::Paused: draw::overlay(game, state, "GAME PAUSED", TEXT_COLOR, buttons, mouse, ""); break;
            case State::GameOver:
                draw::overlay(game, state, "CAUGHT BY THE PROFESSOR!", PROFESSOR_COLOR, buttons, mouse, "");
                break;
            case State::Win:
                draw::overlay(game, state, "YOU ESCAPED!", EXIT_COLOR, buttons, mouse, submit_status);
                break;
            case State::NameEntry: draw::name_entry(game, name_input, (SDL_GetTicks() / 500) % 2 == 0, name_return == State::Win); break;
            case State::Leaderboard:
                draw::leaderboard(buttons, mouse, board_tab, board_rows, board_status, highlight);
                break;
        }

        draw::transition(fade_alpha);

        if (shot_screen) {
            if (!draw::save_screenshot(shot_file)) std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
            break;
        }

        SDL_RenderPresent(renderer);
    }

    board.reset();  // joins the worker thread
    draw::shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

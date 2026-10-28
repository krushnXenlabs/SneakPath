#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "draw.hpp"
#include "game.hpp"

namespace {

State state = State::Menu;
int fade_alpha = 0;  // > 0 while a screen transition (fade-in) is running
bool running = true;

void change_state(Game&, State new_state) {
    // Pausing/resuming should not flash, every other change fades in
    bool pause_toggle = (state == State::Playing && new_state == State::Paused) ||
                        (state == State::Paused && new_state == State::Playing);
    if (!pause_toggle) fade_alpha = 255;

    state = new_state;
}

// --------------------------------
// Buttons
// --------------------------------

constexpr int BUTTON_WIDTH = 280;
constexpr int BUTTON_HEIGHT = 48;
constexpr int BUTTON_GAP = 16;

// Stacked vertically and centered
std::vector<Button> make_buttons(const std::vector<std::string>& labels, int top) {
    std::vector<Button> buttons;
    for (std::size_t i = 0; i < labels.size(); i++) {
        SDL_Rect r{(SCREEN_WIDTH - BUTTON_WIDTH) / 2,
                   top + static_cast<int>(i) * (BUTTON_HEIGHT + BUTTON_GAP), BUTTON_WIDTH, BUTTON_HEIGHT};
        buttons.push_back({labels[i], r});
    }
    return buttons;
}

std::vector<Button> get_buttons(const Game& game) {
    switch (state) {
        case State::Menu:
            return make_buttons({"START GAME", std::string("DIFFICULTY: ") + name_of(game.difficulty),
                                 "INSTRUCTIONS", "QUIT"},
                                265);
        case State::Instructions: return make_buttons({"BACK"}, SCREEN_HEIGHT - 80);
        case State::Paused: return make_buttons({"RESUME", "RESTART", "MAIN MENU"}, 265);
        case State::GameOver: return make_buttons({"RESTART", "MAIN MENU", "QUIT"}, 265);
        case State::Win: return make_buttons({"PLAY AGAIN", "MAIN MENU", "QUIT"}, 330);  // room for the score lines
        default: return {};
    }
}

void press_button(Game& game, const std::string& label) {
    if (label == "START GAME" || label == "PLAY AGAIN" || label == "RESTART") {
        game.reset();
        change_state(game, State::Playing);
    } else if (label.rfind("DIFFICULTY", 0) == 0) {
        game.difficulty = static_cast<Difficulty>((static_cast<int>(game.difficulty) + 1) % DIFFICULTY_COUNT);
    } else if (label == "INSTRUCTIONS") {
        change_state(game, State::Instructions);
    } else if (label == "BACK" || label == "MAIN MENU") {
        change_state(game, State::Menu);
    } else if (label == "RESUME") {
        change_state(game, State::Playing);
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
                    if (escape) change_state(game, State::Paused);
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
            }
        }
    }
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
    char* base = SDL_GetBasePath();
    game.score_file = std::string(base ? base : "./") + "highscores.txt";
    SDL_free(base);
    game.load_best_scores();

    Uint32 last_ticks = SDL_GetTicks();

    if (shot_screen) {
        const std::string name = shot_screen;
        if (name == "instructions") state = State::Instructions;
        else if (name == "playing") state = State::Playing;
        else if (name == "paused") state = State::Paused;
        else if (name == "gameover") state = State::GameOver;
        else if (name == "win") state = State::Win;
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

        if (state == State::Playing && !shot_screen) {
            Outcome outcome = game.update(dt, get_input_direction());
            if (outcome == Outcome::Caught) change_state(game, State::GameOver);
            if (outcome == Outcome::Won) change_state(game, State::Win);
        }

        std::vector<Button> buttons = get_buttons(game);

        switch (state) {
            case State::Menu: draw::menu(buttons, mouse); break;
            case State::Instructions: draw::instructions(buttons, mouse); break;
            case State::Playing: draw::game_scene(game, state); break;
            case State::Paused: draw::overlay(game, state, "GAME PAUSED", TEXT_COLOR, buttons, mouse); break;
            case State::GameOver:
                draw::overlay(game, state, "CAUGHT BY THE PROFESSOR!", PROFESSOR_COLOR, buttons, mouse);
                break;
            case State::Win: draw::overlay(game, state, "YOU ESCAPED!", EXIT_COLOR, buttons, mouse); break;
        }

        draw::transition(fade_alpha);

        if (shot_screen) {
            if (!draw::save_screenshot(shot_file)) std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
            break;
        }

        SDL_RenderPresent(renderer);
    }

    draw::shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

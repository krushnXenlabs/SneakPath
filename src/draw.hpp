#pragma once

#include <SDL.h>

#include <string>
#include <vector>

#include "game.hpp"

struct Button {
    std::string label;
    SDL_Rect rect;
};

namespace draw {

// Loads fonts and builds the map texture. Returns false (and prints why) on failure.
bool init(SDL_Window* window, SDL_Renderer* renderer);
void shutdown();

void menu(const std::vector<Button>& buttons, SDL_Point mouse);
void instructions(const std::vector<Button>& buttons, SDL_Point mouse);
void game_scene(const Game& game, State state);
// Paused / game over / win: the frozen game behind a dark layer and a panel
void overlay(const Game& game, State state, const char* title, Color title_color,
             const std::vector<Button>& buttons, SDL_Point mouse);
// Writes the current frame (call before presenting) to a BMP file
bool save_screenshot(const char* file);
// Fade-in after a screen change; counts alpha down
void transition(int& fade_alpha);

}  // namespace draw

#include "draw.hpp"

#include <SDL_ttf.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>

namespace draw {

namespace {

enum FontId { TITLE, HEADING, TEXT, SMALL, FONT_COUNT };
enum Anchor { CENTER, TOP_LEFT, TOP_RIGHT };

SDL_Renderer* renderer = nullptr;
TTF_Font* fonts[FONT_COUNT] = {};
SDL_Texture* map_texture = nullptr;
float scale = 1.0f;  // device pixels per logical pixel (2 on retina)

struct CachedText {
    SDL_Texture* texture;
    int w, h;  // logical size
};
std::map<std::tuple<int, std::string, Uint32>, CachedText> text_cache;

// --------------------------------
// Primitives (all in logical pixels; the renderer scale handles retina)
// --------------------------------

void set_color(Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
}

void rect(float x, float y, float w, float h, Color c) {
    set_color(c);
    SDL_FRect r{x, y, w, h};
    SDL_RenderFillRectF(renderer, &r);
}

// Outline drawn inside the rect, like pygame.draw.rect(..., width)
void outline(float x, float y, float w, float h, float t, Color c) {
    if (t * 2 >= w || t * 2 >= h) {
        rect(x, y, w, h, c);
        return;
    }
    rect(x, y, w, t, c);
    rect(x, y + h - t, w, t, c);
    rect(x, y + t, t, h - 2 * t, c);
    rect(x + w - t, y + t, t, h - 2 * t, c);
}

void fill_circle(float cx, float cy, float r, Color c) {
    float step = 1.0f / scale;
    for (float dy = -r; dy < r; dy += step) {
        float yy = dy + step / 2;
        float half = std::sqrt(std::max(r * r - yy * yy, 0.0f));
        rect(cx - half, cy + dy, 2 * half, step, c);
    }
}

// Ring of the given thickness inside radius r
void circle_outline(float cx, float cy, float r, float t, Color c) {
    float step = 1.0f / scale;
    float inner = r - t;
    for (float dy = -r; dy < r; dy += step) {
        float yy = dy + step / 2;
        float outer_half = std::sqrt(std::max(r * r - yy * yy, 0.0f));
        if (std::fabs(yy) >= inner) {
            rect(cx - outer_half, cy + dy, 2 * outer_half, step, c);
        } else {
            float inner_half = std::sqrt(inner * inner - yy * yy);
            rect(cx - outer_half, cy + dy, outer_half - inner_half, step, c);
            rect(cx + inner_half, cy + dy, outer_half - inner_half, step, c);
        }
    }
}

// Rounded rect: filled in `border`, then the inside in `fill` (both opaque)
void rounded_rect(SDL_Rect r, float radius, float border_width, Color border, Color fill) {
    auto draw_one = [&](float x, float y, float w, float h, float rad, Color c) {
        rect(x + rad, y, w - 2 * rad, h, c);
        rect(x, y + rad, rad, h - 2 * rad, c);
        rect(x + w - rad, y + rad, rad, h - 2 * rad, c);
        fill_circle(x + rad, y + rad, rad, c);
        fill_circle(x + w - rad, y + rad, rad, c);
        fill_circle(x + rad, y + h - rad, rad, c);
        fill_circle(x + w - rad, y + h - rad, rad, c);
    };
    draw_one(r.x, r.y, r.w, r.h, radius, border);
    draw_one(r.x + border_width, r.y + border_width, r.w - 2 * border_width,
             r.h - 2 * border_width, radius - border_width, fill);
}

void line(float x1, float y1, float x2, float y2, float width, Color c) {
    // Axis-aligned lines are plain rects, anything else is a rotated quad
    if (y1 == y2) {
        rect(std::min(x1, x2), y1 - width / 2, std::fabs(x2 - x1), width, c);
        return;
    }
    if (x1 == x2) {
        rect(x1 - width / 2, std::min(y1, y2), width, std::fabs(y2 - y1), c);
        return;
    }

    float dx = x2 - x1, dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    float nx = -dy / len * width / 2, ny = dx / len * width / 2;

    SDL_Color col{c.r, c.g, c.b, c.a};
    SDL_Vertex v[4] = {
        {{x1 + nx, y1 + ny}, col, {0, 0}},
        {{x1 - nx, y1 - ny}, col, {0, 0}},
        {{x2 - nx, y2 - ny}, col, {0, 0}},
        {{x2 + nx, y2 + ny}, col, {0, 0}},
    };
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
}

void diamond(float cx, float top, float half_w, float half_h, Color c) {
    SDL_Color col{c.r, c.g, c.b, c.a};
    SDL_Vertex v[4] = {
        {{cx, top}, col, {0, 0}},
        {{cx - half_w, top + half_h}, col, {0, 0}},
        {{cx, top + 2 * half_h}, col, {0, 0}},
        {{cx + half_w, top + half_h}, col, {0, 0}},
    };
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
}

// --------------------------------
// Text
// --------------------------------

void text(FontId font, const std::string& s, Color c, int x, int y, Anchor anchor) {
    if (s.empty()) return;

    Uint32 key_color = (Uint32(c.r) << 16) | (Uint32(c.g) << 8) | c.b;
    auto key = std::make_tuple(int(font), s, key_color);

    auto it = text_cache.find(key);
    if (it == text_cache.end()) {
        // Timer texts change every frame, so don't let the cache grow forever
        if (text_cache.size() > 400) {
            for (auto& entry : text_cache) SDL_DestroyTexture(entry.second.texture);
            text_cache.clear();
        }

        SDL_Surface* surface = TTF_RenderUTF8_Blended(fonts[font], s.c_str(), SDL_Color{c.r, c.g, c.b, 255});
        if (!surface) return;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        CachedText cached{texture, static_cast<int>(surface->w / scale), static_cast<int>(surface->h / scale)};
        SDL_FreeSurface(surface);
        if (!texture) return;
        it = text_cache.emplace(key, cached).first;
    }

    const CachedText& t = it->second;
    SDL_Rect dst{x, y, t.w, t.h};
    if (anchor == CENTER) {
        dst.x = x - t.w / 2;
        dst.y = y - t.h / 2;
    } else if (anchor == TOP_RIGHT) {
        dst.x = x - t.w;
    }
    SDL_RenderCopy(renderer, t.texture, nullptr, &dst);
}

// --------------------------------
// Map art: drawn once into map_texture, then copied every frame
// --------------------------------

void brick(float x, float y, float size) {
    rect(x, y, size, size, BRICK);

    // Mortar lines, with every other course shifted half a brick
    int i = 0;
    for (float cy = y; cy < y + size; cy += 10, i++) {
        rect(x, cy, size, 1, BRICK_MORTAR);
        float offset = (i % 2) ? 10.0f : 0.0f;
        for (float cx = x + offset; cx < x + size; cx += 20)
            rect(cx, cy, 1, std::min(10.0f, y + size - cy), BRICK_MORTAR);
    }
}

void locker(float x, float y, float size) {
    rect(x, y, size, size, FLOOR);
    rect(x, y, size, size, LOCKER);
    outline(x, y, size, size, 2, LOCKER_DARK);

    // Air vents near the top and a handle
    for (int i = 0; i < 3; i++)
        line(x + 9, y + 7 + i * 4, x + size - 10, y + 7 + i * 4, 2, LOCKER_DARK);

    rect(x + size - 12, y + size / 2 + 2, 3, 8, Color{200, 200, 210});
}

void build_map_texture() {
    int px_w = static_cast<int>(WIDTH * scale);
    int px_h = static_cast<int>(HEIGHT * scale);
    map_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, px_w, px_h);

    SDL_SetRenderTarget(renderer, map_texture);
    SDL_RenderSetScale(renderer, scale, scale);  // SDL keeps the scale per render target

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            float fx = static_cast<float>(x * TILE_SIZE), fy = static_cast<float>(y * TILE_SIZE);

            if (is_wall(Cell{x, y})) {
                if (x == 0 || x == COLS - 1 || y == 0 || y == ROWS - 1)
                    brick(fx, fy, TILE_SIZE);
                else
                    locker(fx, fy, TILE_SIZE);
            } else {
                // Checkerboard floor tiles
                rect(fx, fy, TILE_SIZE, TILE_SIZE, (x + y) % 2 == 0 ? FLOOR : FLOOR_ALT);
            }
        }
    }

    SDL_SetRenderTarget(renderer, nullptr);
    SDL_RenderSetScale(renderer, scale, scale);
}

// --------------------------------
// Game scene pieces
// --------------------------------

void cell(Cell c, Color color) {
    rect(static_cast<float>(c.x * TILE_SIZE), static_cast<float>(c.y * TILE_SIZE + BAR_HEIGHT),
         TILE_SIZE - 1, TILE_SIZE - 1, color);
}

void exit_door(bool has_key) {
    float x = static_cast<float>(EXIT.x * TILE_SIZE);
    float y = static_cast<float>(EXIT.y * TILE_SIZE + BAR_HEIGHT);
    float T = TILE_SIZE;

    rect(x, y, T, T, EXIT_COLOR);

    // Door with two panels and a knob
    rect(x + 6, y + 3, T - 12, T - 3, Color{120, 80, 45});
    outline(x + 6, y + 3, T - 12, T - 3, 2, Color{70, 45, 25});
    outline(x + 10, y + 7, T - 20, 11, 1, Color{95, 62, 34});
    outline(x + 10, y + 22, T - 20, 11, 1, Color{95, 62, 34});
    fill_circle(x + T - 12, y + T / 2 + 2, 2, Color{230, 190, 70});

    // Padlock while the key has not been found
    if (!has_key) {
        float cx = x + T / 2, cy = y + T / 2;
        Color gold{230, 190, 70};
        outline(cx - 5, cy - 6, 3, 6, 2, gold);
        outline(cx + 2, cy - 6, 3, 6, 2, gold);
        rect(cx - 6, cy - 1, 12, 9, gold);
        outline(cx - 6, cy - 1, 12, 9, 1, Color{90, 70, 20});
    }
}

void pickup(const Pickup& p) {
    float cx = static_cast<float>(p.cell.x * TILE_SIZE + TILE_SIZE / 2);
    float cy = static_cast<float>(p.cell.y * TILE_SIZE + BAR_HEIGHT + TILE_SIZE / 2);

    // Light badge so the icon stands out on the floor
    fill_circle(cx, cy, 16, Color{250, 250, 235});
    circle_outline(cx, cy, 16, 2, Color{200, 170, 60});

    if (p.kind == Kind::Coffee) {
        Color brown{110, 70, 40}, steam{150, 150, 160};
        rect(cx - 7, cy - 3, 12, 10, brown);
        circle_outline(cx + 6, cy + 2, 4, 2, brown);
        line(cx - 4, cy - 5, cx - 4, cy - 9, 2, steam);
        line(cx, cy - 5, cx, cy - 9, 2, steam);
    } else if (p.kind == Kind::Book) {
        rect(cx - 8, cy - 8, 16, 16, Color{170, 50, 60});
        rect(cx - 6, cy + 4, 14, 3, Color{250, 250, 240});            // pages
        line(cx - 5, cy - 8, cx - 5, cy + 8, 2, Color{100, 25, 35});  // spine
        line(cx - 1, cy - 3, cx + 5, cy - 3, 2, Color{240, 200, 60}); // title
    } else {
        Color gold{225, 175, 40};
        circle_outline(cx - 5, cy, 5, 3, gold);
        line(cx, cy, cx + 9, cy, 3, gold);
        line(cx + 6, cy, cx + 6, cy + 4, 2, gold);
        line(cx + 9, cy, cx + 9, cy + 4, 2, gold);
    }
}

// Stick figure centered in the cell; the professor gets glasses and a tie
void stick_figure(Cell c, Color color, bool professor) {
    float px = static_cast<float>(c.x * TILE_SIZE + TILE_SIZE / 2);
    float py = static_cast<float>(c.y * TILE_SIZE + BAR_HEIGHT);

    circle_outline(px, py + 10, 6, 2, color);                // head
    line(px, py + 15, px, py + 27, 3, color);                // body
    line(px, py + 19, px - 8, py + 25, 3, color);            // arms
    line(px, py + 19, px + 8, py + 25, 3, color);
    line(px, py + 27, px - 7, py + 37, 3, color);            // legs
    line(px, py + 27, px + 7, py + 37, 3, color);

    if (professor) {
        line(px - 5, py + 9, px + 5, py + 9, 2, color);      // glasses
        diamond(px, py + 16, 3, 5, color);                   // tie
    } else {
        outline(px - 4, py + 16, 8, 8, 2, color);            // backpack
    }
}

void danger_zone(const Game& game) {
    // Same area that astar() penalizes: Manhattan distance <= DANGER_RANGE
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            Cell c{x, y};
            if (is_wall(c)) continue;
            if (manhattan(c, game.professors[0]) <= DANGER_RANGE ||
                manhattan(c, game.professors[1]) <= DANGER_RANGE)
                cell(c, DANGER_ZONE_COLOR);
        }
    }
}

const char* status_text(State state) {
    switch (state) {
        case State::Paused: return "PAUSED";
        case State::GameOver: return "CAUGHT";
        case State::Win: return "ESCAPED";
        default: return "ESCAPING";
    }
}

std::string fmt(const char* format, double a) {
    char buf[64];
    std::snprintf(buf, sizeof buf, format, a);
    return buf;
}

void info_bar(const Game& game, State state) {
    rect(0, 0, SCREEN_WIDTH, BAR_HEIGHT, BAR_BG);
    rect(0, BAR_HEIGHT - 2, SCREEN_WIDTH, 2, BAR_BORDER);

    text(HEADING, "SNEAKPATH", TEXT_COLOR, 16, 8, TOP_LEFT);
    text(TEXT, std::string(name_of(game.difficulty)) + "   STATUS: " + status_text(state), PATH_COLOR,
         SCREEN_WIDTH - 16, 14, TOP_RIGHT);

    int path_length = std::max(static_cast<int>(game.path.size()) - 1, 0);
    text(SMALL,
         "A* Path Length: " + std::to_string(path_length) +
             "     Nodes Explored: " + std::to_string(game.search_stats.explored) +
             "     Recalculations: " + std::to_string(game.recalculations),
         TEXT_DIM, 16, 44, TOP_LEFT);

    // Objective and active pickup effects
    if (game.has_key)
        text(SMALL, "KEY: FOUND - reach the exit", EXIT_COLOR, 16, 68, TOP_LEFT);
    else
        text(SMALL, "KEY: find it to unlock the exit", PATH_COLOR, 16, 68, TOP_LEFT);

    text(SMALL,
         fmt("TIME %.1fs", game.elapsed_ms / 1000.0) + "   STEPS " + std::to_string(game.steps),
         TEXT_COLOR, 330, 68, TOP_LEFT);

    std::string effects;
    if (game.boost_timer > 0) effects += fmt("COFFEE %.1fs", game.boost_timer / 1000.0);
    if (game.freeze_timer > 0) {
        if (!effects.empty()) effects += "   ";
        effects += fmt("FROZEN %.1fs", game.freeze_timer / 1000.0);
    }
    text(SMALL, effects, FROZEN_COLOR, SCREEN_WIDTH - 16, 68, TOP_RIGHT);
}

// `active_label` marks a selected tab-style button
void buttons_row(const std::vector<Button>& buttons, SDL_Point mouse, const std::string& active_label = "") {
    for (const Button& b : buttons) {
        bool hovered = SDL_PointInRect(&mouse, &b.rect);
        bool active = b.label == active_label;
        rounded_rect(b.rect, 8, 2, active ? PATH_COLOR : BUTTON_BORDER,
                     hovered ? BUTTON_HOVER : active ? Color{74, 76, 100} : BUTTON_COLOR);
        text(TEXT, b.label, TEXT_COLOR, b.rect.x + b.rect.w / 2, b.rect.y + b.rect.h / 2, CENTER);
    }
}

}  // namespace

// --------------------------------
// Public
// --------------------------------

bool init(SDL_Window* window, SDL_Renderer* r) {
    renderer = r;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // High-DPI: draw in logical pixels, render at device resolution
    int win_w = 0, out_w = 0, out_h = 0;
    SDL_GetWindowSize(window, &win_w, nullptr);
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    scale = win_w > 0 ? static_cast<float>(out_w) / static_cast<float>(win_w) : 1.0f;
    SDL_RenderSetScale(renderer, scale, scale);

    if (TTF_Init() != 0) {
        std::fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return false;
    }

    const char* candidates[] = {
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Geneva.ttf",
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    const int sizes[FONT_COUNT] = {72, 44, 28, 24};

    const char* chosen = nullptr;
    for (const char* path : candidates) {
        TTF_Font* probe = TTF_OpenFont(path, 12);
        if (probe) {
            TTF_CloseFont(probe);
            chosen = path;
            break;
        }
    }
    if (!chosen) {
        std::fprintf(stderr, "No usable font found (tried %zu paths)\n", sizeof candidates / sizeof *candidates);
        return false;
    }

    for (int i = 0; i < FONT_COUNT; i++) {
        // Slightly smaller than the Pygame sizes: pygame's default font is more compact
        fonts[i] = TTF_OpenFont(chosen, static_cast<int>(sizes[i] * 0.72f * scale));
        if (!fonts[i]) {
            std::fprintf(stderr, "TTF_OpenFont failed: %s\n", TTF_GetError());
            return false;
        }
    }

    build_map_texture();
    return map_texture != nullptr;
}

void shutdown() {
    for (auto& entry : text_cache) SDL_DestroyTexture(entry.second.texture);
    text_cache.clear();
    if (map_texture) SDL_DestroyTexture(map_texture);
    for (TTF_Font*& f : fonts) {
        if (f) TTF_CloseFont(f);
        f = nullptr;
    }
    TTF_Quit();
}

void game_scene(const Game& game, State state) {
    set_color(BACKGROUND);
    SDL_RenderClear(renderer);

    // Map (floor tiles, brick border, lockers) was drawn once up front
    SDL_Rect map_dst{0, BAR_HEIGHT, WIDTH, HEIGHT};
    SDL_RenderCopy(renderer, map_texture, nullptr, &map_dst);

    danger_zone(game);

    // A* path
    for (Cell c : game.path)
        if (c != game.student && c != EXIT && !game.has_pickup_at(c)) cell(c, PATH_COLOR);

    exit_door(game.has_key);

    for (const Pickup& p : game.pickups) pickup(p);

    stick_figure(game.student, STUDENT_COLOR, false);

    Color professor_color = game.freeze_timer > 0 ? FROZEN_COLOR : PROFESSOR_COLOR;
    for (Cell p : game.professors) stick_figure(p, professor_color, true);

    info_bar(game, state);
}

void menu(const std::vector<Button>& buttons, SDL_Point mouse) {
    set_color(BACKGROUND);
    SDL_RenderClear(renderer);

    text(TITLE, "SNEAKPATH", TEXT_COLOR, SCREEN_WIDTH / 2, 130, CENTER);
    text(HEADING, "An A* Pathfinding Simulation", PATH_COLOR, SCREEN_WIDTH / 2, 195, CENTER);
    rect(200, 234, SCREEN_WIDTH - 400, 2, BAR_BORDER);

    buttons_row(buttons, mouse);

    text(SMALL, "ENTER = start     WASD / arrows = move     ESC = pause", TEXT_DIM, SCREEN_WIDTH / 2,
         SCREEN_HEIGHT - 30, CENTER);
}

void instructions(const std::vector<Button>& buttons, SDL_Point mouse) {
    set_color(BACKGROUND);
    SDL_RenderClear(renderer);

    text(TITLE, "HOW TO PLAY", TEXT_COLOR, SCREEN_WIDTH / 2, 55, CENTER);

    const char* rules[] = {
        "You are a student trying to reach the EXIT.",
        "Move with WASD or the arrow keys.",
        "Two professors patrol the college and chase you up close.",
        "Avoid the professors and the red danger zone.",
        "The yellow A* path is a hint; it re-routes around the professors.",
        "The exit is locked: find the KEY first.",
        "COFFEE gives a speed boost; a BOOK freezes the professors.",
        "Reach the EXIT before getting caught.",
    };
    for (int i = 0; i < 8; i++)
        text(TEXT, std::string("-  ") + rules[i], TEXT_COLOR, 120, 115 + i * 32, TOP_LEFT);

    text(HEADING, "COLORS AND SYMBOLS", PATH_COLOR, 120, 375, TOP_LEFT);

    struct Legend {
        Color color;
        const char* name;
        const char* meaning;
    };
    const Legend legend[] = {
        {STUDENT_COLOR, "Student", "player"},
        {PROFESSOR_COLOR, "Professors", "enemies"},
        {PATH_COLOR, "Yellow path", "A* calculated path"},
        {Color{235, 150, 150}, "Red area", "professor danger zone"},
        {EXIT_COLOR, "Green area", "exit"},
    };
    for (int i = 0; i < 5; i++) {
        float y = static_cast<float>(425 + i * 32);
        rect(120, y, 22, 22, legend[i].color);
        outline(120, y, 22, 22, 1, BUTTON_BORDER);
        text(TEXT, std::string(legend[i].name) + "  -  " + legend[i].meaning, TEXT_COLOR, 158,
             static_cast<int>(y) + 2, TOP_LEFT);
    }

    buttons_row(buttons, mouse);
}

void overlay(const Game& game, State state, const char* title, Color title_color,
             const std::vector<Button>& buttons, SDL_Point mouse, const std::string& submit_status) {
    game_scene(game, state);

    rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Color{0, 0, 0, 150});

    SDL_Rect panel{0, 0, 560, state == State::Win ? 480 : state == State::GameOver ? 400 : 310};
    panel.x = SCREEN_WIDTH / 2 - panel.w / 2;
    panel.y = SCREEN_HEIGHT / 2 + 20 - panel.h / 2;
    rounded_rect(panel, 12, 2, BAR_BORDER, BAR_BG);

    int middle = SCREEN_WIDTH / 2;
    text(HEADING, title, title_color, middle, panel.y + 45, CENTER);

    if (state == State::Win) {
        text(TEXT, fmt("Time %.1fs", game.elapsed_ms / 1000.0) + "     Steps " + std::to_string(game.steps),
             TEXT_COLOR, middle, panel.y + 90, CENTER);
        text(HEADING, "SCORE  " + std::to_string(game.final_score), PATH_COLOR, middle, panel.y + 128, CENTER);

        std::string name = name_of(game.difficulty);
        if (game.new_best)
            text(SMALL, "NEW BEST for " + name + "!", EXIT_COLOR, middle, panel.y + 160, CENTER);
        else
            text(SMALL, "Best (" + name + "): " + std::to_string(game.best_for(game.difficulty)), TEXT_DIM,
                 middle, panel.y + 160, CENTER);

        // Leaderboard result: submitting, rank, or offline
        bool ranked = submit_status.rfind("Rank", 0) == 0;
        text(SMALL, submit_status, ranked ? EXIT_COLOR : TEXT_DIM, middle, panel.y + 190, CENTER);
    } else if (state == State::GameOver) {
        text(SMALL, fmt("Survived %.1fs", game.elapsed_ms / 1000.0), TEXT_DIM, middle, panel.y + 80, CENTER);
    }

    buttons_row(buttons, mouse);
}

void name_entry(const Game& game, const std::string& input, bool caret_on) {
    game_scene(game, State::Win);
    rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Color{0, 0, 0, 150});

    SDL_Rect panel{SCREEN_WIDTH / 2 - 280, SCREEN_HEIGHT / 2 - 130, 560, 260};
    rounded_rect(panel, 12, 2, BAR_BORDER, BAR_BG);

    int middle = SCREEN_WIDTH / 2;
    text(HEADING, "YOU ESCAPED!", EXIT_COLOR, middle, panel.y + 40, CENTER);
    text(TEXT, "Score " + std::to_string(game.final_score) + " on " + name_of(game.difficulty), PATH_COLOR, middle,
         panel.y + 80, CENTER);
    text(SMALL, "Enter your name for the leaderboard", TEXT_DIM, middle, panel.y + 115, CENTER);

    SDL_Rect box{middle - 180, panel.y + 135, 360, 48};
    rounded_rect(box, 8, 2, BUTTON_BORDER, Color{14, 14, 20});
    text(TEXT, input, TEXT_COLOR, box.x + 14, box.y + 12, TOP_LEFT);

    if (caret_on) {
        int w = 0, h = 0;
        if (!input.empty()) TTF_SizeUTF8(fonts[TEXT], input.c_str(), &w, &h);
        rect(box.x + 14 + std::floor(w / scale) + 2, static_cast<float>(box.y + 10), 2, 28, TEXT_COLOR);
    }

    text(SMALL, "ENTER = submit     ESC = skip", TEXT_DIM, middle, panel.y + 220, CENTER);
}

void leaderboard(const std::vector<Button>& buttons, SDL_Point mouse, Difficulty tab,
                 const std::vector<LeaderboardRow>& rows, const std::string& status, const Highlight& highlight) {
    set_color(BACKGROUND);
    SDL_RenderClear(renderer);

    text(TITLE, "LEADERBOARD", TEXT_COLOR, SCREEN_WIDTH / 2, 50, CENTER);
    buttons_row(buttons, mouse, name_of(tab));

    const int X_RANK = 84, X_NAME = 130, X_SCORE = 330, X_TIME = 430, X_STEPS = 530, X_DATE = 615;
    const int top = 165;
    text(SMALL, "#", TEXT_DIM, X_RANK, top, TOP_LEFT);
    text(SMALL, "PLAYER", TEXT_DIM, X_NAME, top, TOP_LEFT);
    text(SMALL, "SCORE", TEXT_DIM, X_SCORE, top, TOP_LEFT);
    text(SMALL, "TIME", TEXT_DIM, X_TIME, top, TOP_LEFT);
    text(SMALL, "STEPS", TEXT_DIM, X_STEPS, top, TOP_LEFT);
    text(SMALL, "DATE", TEXT_DIM, X_DATE, top, TOP_LEFT);
    rect(70, static_cast<float>(top + 26), SCREEN_WIDTH - 140, 1, BAR_BORDER);

    for (std::size_t i = 0; i < rows.size(); i++) {
        const LeaderboardRow& row = rows[i];
        int y = top + 36 + static_cast<int>(i) * 34;

        bool mine = row.name == highlight.name && row.score == highlight.score && row.time_ms == highlight.time_ms;
        if (mine) rect(70, static_cast<float>(y - 4), SCREEN_WIDTH - 140, 30, Color{240, 200, 60, 45});

        Color color = row.rank <= 3 ? PATH_COLOR : TEXT_COLOR;
        text(TEXT, std::to_string(row.rank), color, X_RANK, y, TOP_LEFT);
        text(TEXT, row.name, color, X_NAME, y, TOP_LEFT);
        text(TEXT, std::to_string(row.score), color, X_SCORE, y, TOP_LEFT);
        text(TEXT, fmt("%.1fs", row.time_ms / 1000.0), color, X_TIME, y, TOP_LEFT);
        text(TEXT, std::to_string(row.steps), color, X_STEPS, y, TOP_LEFT);
        text(TEXT, row.date, TEXT_DIM, X_DATE, y, TOP_LEFT);
    }

    bool offline = status.rfind("Leaderboard offline", 0) == 0;
    text(SMALL, status, offline ? PROFESSOR_COLOR : TEXT_DIM, SCREEN_WIDTH / 2, rows.empty() ? 340 : 556, CENTER);
}

bool save_screenshot(const char* file) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return false;

    bool ok = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0 &&
              SDL_SaveBMP(surface, file) == 0;
    SDL_FreeSurface(surface);
    return ok;
}

void transition(int& fade_alpha) {
    if (fade_alpha <= 0) return;

    rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Color{0, 0, 0, static_cast<std::uint8_t>(fade_alpha)});
    fade_alpha = std::max(fade_alpha - 25, 0);
}

}  // namespace draw

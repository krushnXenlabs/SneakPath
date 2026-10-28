# SneakPath

A school maze stealth game where you guide a student to the exit while dodging the professor, with an A* pathfinding hint that shows the safest route.

Written in C++17 with SDL2. Find the key, grab a coffee for speed or a book to freeze the professors, and reach the exit without getting caught. The yellow A* path re-routes around the professors and the red danger zone as they move.

## Build and run

Needs SDL2 and SDL2_ttf (macOS: `brew install sdl2 sdl2_ttf`) and a C++17 compiler.

```bash
make            # builds ./sneakpath and the test programs
make test       # runs the logic tests (no window needed)
./sneakpath
```

## Controls

| Key | Action |
|---|---|
| WASD / arrows | Move |
| Enter | Start (menu), restart (game over / win) |
| Esc | Pause, back |
| R | Restart (paused, game over, win) |

## How it works

- `src/pathfinding.*`: A* with a 4-way grid, a Manhattan heuristic, a danger penalty near professors and a border penalty, so the "shortest" route is really the safest one. Professors chase with BFS.
- `src/game.*`: all gameplay state and rules, with no SDL dependency, so it runs headless in tests.
- `src/draw.*`, `src/main.cpp`: SDL rendering, screens, buttons and input.
- `src/db.*`, `db/`: MySQL leaderboard layer (in progress).

## Difficulty and score

Professor step time is 400 / 300 / 230 ms on EASY / MEDIUM / HARD. Faster and fewer steps score higher, each pickup adds 100, and harder difficulties multiply the total. Best scores per difficulty are kept in `highscores.txt`.

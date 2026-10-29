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

- `src/pathfinding.*`: A* with a 4-way grid, a Manhattan heuristic and a danger penalty near professors, so the hint is the safest route, not just the shortest. Professors chase with BFS.
- `src/game.*`: all gameplay state and rules, with no SDL dependency, so it runs headless in tests.
- `src/draw.*`, `src/main.cpp`: SDL rendering, screens, buttons and input.
- `src/db.*`, `db/`: MySQL leaderboard (prepared statements, worker thread so the game never freezes if the database is slow or down).

## Difficulty and score

Professor step time is 400 / 300 / 230 ms on EASY / MEDIUM / HARD. Faster and fewer steps score higher, each pickup adds 100, and harder difficulties multiply the total. Best scores per difficulty are kept in `highscores.txt`.

## Leaderboard (MySQL, optional)

The game works without a database: the leaderboard screen just says it is offline and the local best score still saves. To turn it on you need MySQL 8.0.16 or newer.

1. Start your MySQL server.
2. Create the schema and the game's account (run these as the MySQL root user; edit the password in `db/setup.sql` first):

   ```bash
   mysql -u root -p < db/schema.sql
   sed 's/escape_professor/escape_professor_test/g' db/schema.sql | mysql -u root -p   # only for make test-db
   mysql -u root -p < db/setup.sql
   ```

3. Copy `db.conf.example` to `db.conf` and put the same password in it (or set `ESCAPE_DB_HOST`, `ESCAPE_DB_PORT`, `ESCAPE_DB_USER`, `ESCAPE_DB_PASSWORD`, `ESCAPE_DB_NAME`).
4. `make test-db` checks the database layer (it only touches `escape_professor_test`). Then play: after a win you enter a name, your rank is shown, and LEADERBOARD in the menu shows the Top 10 per difficulty.

The build uses the MySQL client in `/usr/local/mysql`. For another install: `make MYSQL_CONFIG=/path/to/mysql_config`.

The game talks to MySQL directly, so the credentials live on the player's machine. The `escape_game` account can only read and add scores, which is fine for local or class use; for a public leaderboard put an API server in between.

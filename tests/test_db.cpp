// Leaderboard database tests. These DELETE every row in the TEST database, so
// they refuse to run against anything but escape_professor_test.
//
// Setup once (as MySQL root):
//   sed 's/escape_professor/escape_professor_test/g' db/schema.sql | mysql -u root -p
//   mysql -u root -p < db/setup.sql
// Then: make test-db     (skips with exit code 0 if MySQL can't be reached)

#include <mysql.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "../src/db.hpp"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        checks++;                                                  \
        if (!(cond)) {                                             \
            failures++;                                            \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);    \
        }                                                          \
    } while (0)

static const char* TEST_DB = "escape_professor_test";

// Runs one statement and returns the first column of the first row ("" if none)
static std::string scalar(const DbConfig& c, const char* sql) {
    MYSQL* m = mysql_init(nullptr);
    std::string out;
    if (mysql_real_connect(m, c.host.c_str(), c.user.c_str(), c.password.c_str(), c.database.c_str(), c.port,
                           nullptr, 0)) {
        if (mysql_query(m, sql) == 0) {
            if (MYSQL_RES* res = mysql_store_result(m)) {
                if (MYSQL_ROW row = mysql_fetch_row(res)) out = row[0] ? row[0] : "";
                mysql_free_result(res);
            }
        } else {
            std::printf("sql error: %s\n", mysql_error(m));
        }
    }
    mysql_close(m);
    return out;
}

static void clear_tables(const DbConfig& c) {
    scalar(c, "DELETE FROM scores");
    scalar(c, "DELETE FROM players");
}

static RunRecord run(const char* name, Difficulty d, int score, int time_ms) {
    RunRecord r;
    r.name = name;
    r.difficulty = d;
    r.score = score;
    r.time_ms = time_ms;
    r.steps = 50;
    r.pickups = 2;
    return r;
}

static void test_sanitize() {
    CHECK(sanitize_name("Krushn") == "Krushn");
    CHECK(sanitize_name("  padded  ") == "padded");
    CHECK(sanitize_name("a_b-c 9") == "a_b-c 9");
    CHECK(sanitize_name("") == "");
    CHECK(sanitize_name("';--()") == "--");  // only '-' survives
    CHECK(sanitize_name("abcdefghijklmnopqrstuvwxyz").size() == MAX_NAME_LENGTH);

    std::string evil = sanitize_name("x'); DROP TABLE scores;--");
    CHECK(evil.find('\'') == std::string::npos);
    CHECK(evil.find(';') == std::string::npos);
    CHECK(evil.find(')') == std::string::npos);
    CHECK(evil.size() <= MAX_NAME_LENGTH);
}

static void test_unreachable() {
    DbConfig bad;
    bad.host = "127.0.0.1";
    bad.port = 1;  // nothing listens here
    int rank = 0, total = 0;
    std::string error;
    auto start = std::chrono::steady_clock::now();
    CHECK(!submit_score(bad, run("Nobody", Difficulty::Easy, 100, 1000), rank, total, error));
    CHECK(!error.empty());
    std::vector<LeaderboardRow> rows;
    CHECK(!fetch_top(bad, Difficulty::Easy, 10, rows, error));
    auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < 6.0);  // fails fast instead of hanging
}

static void test_roundtrip(const DbConfig& c) {
    clear_tables(c);
    int rank = 0, total = 0;
    std::string error;

    CHECK(submit_score(c, run("Ava", Difficulty::Medium, 2000, 30000), rank, total, error));
    CHECK(rank == 1 && total == 1);
    CHECK(submit_score(c, run("Noah", Difficulty::Medium, 3000, 40000), rank, total, error));
    CHECK(rank == 1 && total == 2);
    CHECK(submit_score(c, run("Mia", Difficulty::Medium, 2000, 25000), rank, total, error));  // same score, faster
    CHECK(rank == 2 && total == 3);
    CHECK(submit_score(c, run("Leo", Difficulty::Medium, 500, 60000), rank, total, error));
    CHECK(rank == 4 && total == 4);

    // Another difficulty is ranked separately
    CHECK(submit_score(c, run("Ava", Difficulty::Hard, 100, 90000), rank, total, error));
    CHECK(rank == 1 && total == 1);

    std::vector<LeaderboardRow> rows;
    CHECK(fetch_top(c, Difficulty::Medium, 10, rows, error));
    CHECK(rows.size() == 4);
    if (rows.size() == 4) {
        CHECK(rows[0].name == "Noah" && rows[0].rank == 1 && rows[0].score == 3000);
        CHECK(rows[1].name == "Mia");   // 2000 in 25 s beats 2000 in 30 s
        CHECK(rows[2].name == "Ava");
        CHECK(rows[3].name == "Leo" && rows[3].rank == 4);
        CHECK(rows[0].time_ms == 40000 && rows[0].steps == 50);
        CHECK(rows[0].date.size() == 10);  // YYYY-MM-DD
    }

    CHECK(fetch_top(c, Difficulty::Easy, 10, rows, error));
    CHECK(rows.empty());

    CHECK(fetch_top(c, Difficulty::Medium, 2, rows, error));
    CHECK(rows.size() == 2);

    // The same name is one player, however many scores it has
    CHECK(scalar(c, "SELECT COUNT(*) FROM players WHERE name = 'Ava'") == "1");
    CHECK(scalar(c, "SELECT COUNT(*) FROM players") == "4");
    CHECK(scalar(c, "SELECT COUNT(*) FROM scores") == "5");
}

static void test_rejections(const DbConfig& c) {
    clear_tables(c);
    int rank = 0, total = 0;
    std::string error;

    // A name that sanitize_name would change is refused outright
    CHECK(!submit_score(c, run("x'); DROP TABLE scores;--", Difficulty::Easy, 100, 1000), rank, total, error));
    CHECK(!submit_score(c, run("", Difficulty::Easy, 100, 1000), rank, total, error));
    CHECK(scalar(c, "SELECT COUNT(*) FROM scores") == "0");

    // The CHECK constraint stops impossible scores, and the player row rolls back with it
    CHECK(!submit_score(c, run("Cheater", Difficulty::Easy, 9999, 1000), rank, total, error));
    CHECK(!error.empty());
    CHECK(scalar(c, "SELECT COUNT(*) FROM scores") == "0");
    CHECK(scalar(c, "SELECT COUNT(*) FROM players WHERE name = 'Cheater'") == "0");

    // Quotes and keywords that are legal characters are stored as plain text
    CHECK(submit_score(c, run("DROP TABLE", Difficulty::Easy, 100, 1000), rank, total, error));
    CHECK(scalar(c, "SELECT COUNT(*) FROM players WHERE name = 'DROP TABLE'") == "1");
}

static void test_async(const DbConfig& c) {
    clear_tables(c);
    Leaderboard board(c);

    board.submit(run("Async", Difficulty::Easy, 700, 12000));
    board.fetch(Difficulty::Easy, 10);

    LeaderboardResult submitted, fetched;
    int got = 0;
    for (int i = 0; i < 300 && got < 2; i++) {  // up to ~6 s
        LeaderboardResult r;
        while (board.poll(r)) {
            if (r.kind == LeaderboardResult::Kind::Submit) submitted = r;
            else fetched = r;
            got++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(got == 2);
    CHECK(submitted.ok && submitted.rank == 1 && submitted.total == 1);
    CHECK(fetched.ok && fetched.rows.size() == 1 && fetched.rows[0].name == "Async");
    CHECK(!board.busy());
}

int main() {
    test_sanitize();
    test_unreachable();

    DbConfig config = load_db_config("db.conf");
    if (config.database != TEST_DB) {
        std::printf("Using database %s instead of %s (tests delete rows)\n", TEST_DB, config.database.c_str());
        config.database = TEST_DB;
    }

    int rank = 0, total = 0;
    std::string error;
    std::vector<LeaderboardRow> probe;
    if (!fetch_top(config, Difficulty::Easy, 1, probe, error)) {
        std::printf("SKIPPED the database tests: cannot use %s as %s@%s:%u (%s)\n", TEST_DB, config.user.c_str(),
                    config.host.c_str(), config.port, error.c_str());
        std::printf("%d checks, %d failures (database tests skipped)\n", checks, failures);
        return failures ? 1 : 0;
    }
    (void)rank;
    (void)total;

    test_roundtrip(config);
    test_rejections(config);
    test_async(config);
    clear_tables(config);

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

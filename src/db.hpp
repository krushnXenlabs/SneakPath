#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "settings.hpp"

// --------------------------------
// Config
// --------------------------------

struct DbConfig {
    std::string host = "127.0.0.1";
    unsigned port = 3306;
    std::string user = "escape_game";
    std::string password;
    std::string database = "escape_professor";
};

// Defaults, then the key=value file (if it exists), then ESCAPE_DB_* environment variables.
DbConfig load_db_config(const std::string& conf_path);

// --------------------------------
// Data
// --------------------------------

constexpr std::size_t MAX_NAME_LENGTH = 12;

// Keeps A-Z a-z 0-9 space _ -, trims the ends, cuts to MAX_NAME_LENGTH. May return "".
std::string sanitize_name(const std::string& raw);

struct RunRecord {
    std::string name;  // must already be sanitized
    Difficulty difficulty = DEFAULT_DIFFICULTY;
    int score = 0;
    int time_ms = 0;
    int steps = 0;
    int pickups = 0;
};

struct LeaderboardRow {
    int rank = 0;
    std::string name;
    int score = 0;
    int time_ms = 0;
    int steps = 0;
    std::string date;  // YYYY-MM-DD
};

// --------------------------------
// Blocking calls (own connection per call; short timeouts)
// --------------------------------

// Stores the run and reports its rank (1 = best) among `total` runs of that difficulty.
bool submit_score(const DbConfig& config, const RunRecord& run, int& rank, int& total, std::string& error);

// Top `limit` runs of a difficulty, best first.
bool fetch_top(const DbConfig& config, Difficulty difficulty, int limit, std::vector<LeaderboardRow>& rows,
               std::string& error);

// --------------------------------
// Async wrapper: the game loop never waits on the network
// --------------------------------

struct LeaderboardResult {
    enum class Kind { Submit, Fetch } kind = Kind::Fetch;
    bool ok = false;
    std::string error;
    Difficulty difficulty = DEFAULT_DIFFICULTY;
    int rank = 0;   // Submit
    int total = 0;  // Submit
    std::vector<LeaderboardRow> rows;  // Fetch
};

class Leaderboard {
public:
    explicit Leaderboard(DbConfig config);
    ~Leaderboard();
    Leaderboard(const Leaderboard&) = delete;
    Leaderboard& operator=(const Leaderboard&) = delete;

    // Each queues one job on the worker thread; results come back through poll().
    void submit(const RunRecord& run);
    void fetch(Difficulty difficulty, int limit = 10);

    // True once per finished job.
    bool poll(LeaderboardResult& out);

    // Jobs queued or running
    bool busy() const;

private:
    struct Job {
        LeaderboardResult::Kind kind;
        RunRecord run;
        Difficulty difficulty;
        int limit;
    };

    void worker();

    DbConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<Job> jobs_;
    std::vector<LeaderboardResult> results_;
    int pending_ = 0;
    bool stop_ = false;
    std::thread thread_;
};

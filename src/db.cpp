#include "db.hpp"

#include <mysql.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>

// --------------------------------
// Config
// --------------------------------

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

void set_option(DbConfig& config, const std::string& key, const std::string& value) {
    if (key == "host") config.host = value;
    else if (key == "port") {
        long port = std::strtol(value.c_str(), nullptr, 10);
        if (port > 0 && port < 65536) config.port = static_cast<unsigned>(port);
    }
    else if (key == "user") config.user = value;
    else if (key == "password") config.password = value;
    else if (key == "database") config.database = value;
}

void env_option(DbConfig& config, const char* variable, const char* key) {
    if (const char* value = std::getenv(variable)) set_option(config, key, value);
}

}  // namespace

DbConfig load_db_config(const std::string& conf_path) {
    DbConfig config;

    std::ifstream in(conf_path);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        set_option(config, trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }

    env_option(config, "ESCAPE_DB_HOST", "host");
    env_option(config, "ESCAPE_DB_PORT", "port");
    env_option(config, "ESCAPE_DB_USER", "user");
    env_option(config, "ESCAPE_DB_PASSWORD", "password");
    env_option(config, "ESCAPE_DB_NAME", "database");
    return config;
}

std::string sanitize_name(const std::string& raw) {
    std::string kept;
    for (unsigned char c : raw)
        if (std::isalnum(c) || c == ' ' || c == '_' || c == '-') kept += static_cast<char>(c);

    kept = trim(kept);
    if (kept.size() > MAX_NAME_LENGTH) kept = trim(kept.substr(0, MAX_NAME_LENGTH));
    return kept;
}

// --------------------------------
// MySQL helpers
// --------------------------------

namespace {

const char* DIFFICULTY_NAMES[DIFFICULTY_COUNT] = {"EASY", "MEDIUM", "HARD"};

void library_init() {
    static std::once_flag once;
    std::call_once(once, [] { mysql_library_init(0, nullptr, nullptr); });
}

struct Conn {
    MYSQL* mysql = nullptr;

    Conn() {
        library_init();
        mysql = mysql_init(nullptr);
    }
    ~Conn() {
        if (mysql) mysql_close(mysql);
    }
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    bool open(const DbConfig& c, std::string& error) {
        if (!mysql) {
            error = "out of memory";
            return false;
        }
        unsigned connect_timeout = 2, io_timeout = 3;
        mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
        mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &io_timeout);
        mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &io_timeout);
        mysql_options(mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");

        if (!mysql_real_connect(mysql, c.host.c_str(), c.user.c_str(), c.password.c_str(), c.database.c_str(),
                                c.port, nullptr, 0)) {
            error = mysql_error(mysql);
            return false;
        }
        return true;
    }
};

// One prepared statement with positional parameters and result columns.
class Stmt {
public:
    explicit Stmt(MYSQL* mysql) : stmt_(mysql_stmt_init(mysql)) {}
    ~Stmt() {
        if (stmt_) mysql_stmt_close(stmt_);
    }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool prepare(const char* sql) {
        return stmt_ && mysql_stmt_prepare(stmt_, sql, static_cast<unsigned long>(std::strlen(sql))) == 0;
    }

    // Parameters. Everything is copied, so callers may pass temporaries.
    Stmt& param(const std::string& value) {
        strings_.push_back(std::make_unique<std::string>(value));
        MYSQL_BIND b{};
        b.buffer_type = MYSQL_TYPE_STRING;
        b.buffer = strings_.back()->data();
        b.buffer_length = static_cast<unsigned long>(strings_.back()->size());
        params_.push_back(b);
        return *this;
    }
    Stmt& param(long long value) {
        ints_.push_back(std::make_unique<long long>(value));
        MYSQL_BIND b{};
        b.buffer_type = MYSQL_TYPE_LONGLONG;
        b.buffer = ints_.back().get();
        params_.push_back(b);
        return *this;
    }

    bool execute() {
        if (!params_.empty() && mysql_stmt_bind_param(stmt_, params_.data()) != 0) return false;
        return mysql_stmt_execute(stmt_) == 0;
    }

    unsigned long long insert_id() const { return mysql_stmt_insert_id(stmt_); }
    std::string error() const { return stmt_ ? mysql_stmt_error(stmt_) : "statement init failed"; }

    // Reads rows as strings (the server converts); calls row(columns) per row.
    template <typename Fn>
    bool each_row(int columns, Fn row) {
        std::vector<std::vector<char>> buffers(static_cast<std::size_t>(columns), std::vector<char>(128));
        std::vector<unsigned long> lengths(static_cast<std::size_t>(columns));
        std::vector<MYSQL_BIND> binds(static_cast<std::size_t>(columns));
        for (std::size_t i = 0; i < binds.size(); i++) {
            binds[i] = MYSQL_BIND{};
            binds[i].buffer_type = MYSQL_TYPE_STRING;
            binds[i].buffer = buffers[i].data();
            binds[i].buffer_length = buffers[i].size();
            binds[i].length = &lengths[i];
        }
        if (mysql_stmt_bind_result(stmt_, binds.data()) != 0) return false;

        int rc;
        while ((rc = mysql_stmt_fetch(stmt_)) == 0 || rc == MYSQL_DATA_TRUNCATED) {
            std::vector<std::string> cols;
            for (std::size_t i = 0; i < binds.size(); i++)
                cols.emplace_back(buffers[i].data(), std::min<std::size_t>(lengths[i], buffers[i].size()));
            row(cols);
        }
        return rc == MYSQL_NO_DATA;
    }

private:
    MYSQL_STMT* stmt_;
    std::vector<std::unique_ptr<std::string>> strings_;
    std::vector<std::unique_ptr<long long>> ints_;
    std::vector<MYSQL_BIND> params_;
};

// Runs a statement that returns a single integer
bool query_int(MYSQL* mysql, const char* sql, std::initializer_list<long long> ints,
               const std::string& difficulty, long long& out, std::string& error) {
    Stmt s(mysql);
    if (!s.prepare(sql)) {
        error = s.error();
        return false;
    }
    s.param(difficulty);
    for (long long v : ints) s.param(v);
    if (!s.execute()) {
        error = s.error();
        return false;
    }
    bool got = false;
    bool ok = s.each_row(1, [&](const std::vector<std::string>& cols) {
        out = std::strtoll(cols[0].c_str(), nullptr, 10);
        got = true;
    });
    if (!ok || !got) {
        error = s.error();
        return false;
    }
    return true;
}

}  // namespace

// --------------------------------
// Blocking calls
// --------------------------------

bool submit_score(const DbConfig& config, const RunRecord& run, int& rank, int& total, std::string& error) {
    if (sanitize_name(run.name) != run.name || run.name.empty()) {
        error = "invalid player name";
        return false;
    }

    Conn conn;
    if (!conn.open(config, error)) return false;
    MYSQL* mysql = conn.mysql;

    const std::string difficulty = DIFFICULTY_NAMES[static_cast<int>(run.difficulty)];

    if (mysql_autocommit(mysql, 0) != 0) {
        error = mysql_error(mysql);
        return false;
    }

    // Player row: create the name if it is new, then look its id up. Plain INSERT IGNORE
    // + SELECT keeps the game account at SELECT and INSERT (no UPDATE privilege needed).
    unsigned long long player_id = 0;
    {
        Stmt s(mysql);
        if (!s.prepare("INSERT IGNORE INTO players (name) VALUES (?)")) {
            error = s.error();
            return false;
        }
        s.param(run.name);
        if (!s.execute()) {
            error = s.error();
            mysql_rollback(mysql);
            return false;
        }
    }
    {
        Stmt s(mysql);
        if (!s.prepare("SELECT id FROM players WHERE name = ?")) {
            error = s.error();
            mysql_rollback(mysql);
            return false;
        }
        s.param(run.name);
        bool got = s.execute() && s.each_row(1, [&](const std::vector<std::string>& cols) {
            player_id = std::strtoull(cols[0].c_str(), nullptr, 10);
        });
        if (!got || player_id == 0) {
            error = got ? "player lookup failed" : s.error();
            mysql_rollback(mysql);
            return false;
        }
    }

    {
        Stmt s(mysql);
        if (!s.prepare("INSERT INTO scores (player_id, difficulty, score, time_ms, steps, pickups) "
                       "VALUES (?, ?, ?, ?, ?, ?)")) {
            error = s.error();
            mysql_rollback(mysql);
            return false;
        }
        s.param(static_cast<long long>(player_id)).param(difficulty).param(run.score).param(run.time_ms)
            .param(run.steps).param(run.pickups);
        if (!s.execute()) {
            error = s.error();
            mysql_rollback(mysql);
            return false;
        }
    }

    long long better = 0, count = 0;
    if (!query_int(mysql,
                   "SELECT COUNT(*) FROM scores WHERE difficulty = ? AND (score > ? OR (score = ? AND time_ms < ?))",
                   {run.score, run.score, run.time_ms}, difficulty, better, error) ||
        !query_int(mysql, "SELECT COUNT(*) FROM scores WHERE difficulty = ?", {}, difficulty, count, error)) {
        mysql_rollback(mysql);
        return false;
    }

    if (mysql_commit(mysql) != 0) {
        error = mysql_error(mysql);
        return false;
    }

    rank = static_cast<int>(better) + 1;
    total = static_cast<int>(count);
    return true;
}

bool fetch_top(const DbConfig& config, Difficulty difficulty, int limit, std::vector<LeaderboardRow>& rows,
               std::string& error) {
    rows.clear();
    if (limit < 1 || limit > 100) limit = 10;

    Conn conn;
    if (!conn.open(config, error)) return false;

    Stmt s(conn.mysql);
    if (!s.prepare("SELECT p.name, s.score, s.time_ms, s.steps, DATE_FORMAT(s.created_at, '%Y-%m-%d') "
                   "FROM scores s JOIN players p ON p.id = s.player_id "
                   "WHERE s.difficulty = ? ORDER BY s.score DESC, s.time_ms ASC, s.id ASC LIMIT ?")) {
        error = s.error();
        return false;
    }
    s.param(std::string(DIFFICULTY_NAMES[static_cast<int>(difficulty)])).param(limit);
    if (!s.execute()) {
        error = s.error();
        return false;
    }

    bool ok = s.each_row(5, [&](const std::vector<std::string>& c) {
        LeaderboardRow row;
        row.rank = static_cast<int>(rows.size()) + 1;
        row.name = c[0];
        row.score = std::atoi(c[1].c_str());
        row.time_ms = std::atoi(c[2].c_str());
        row.steps = std::atoi(c[3].c_str());
        row.date = c[4];
        rows.push_back(row);
    });
    if (!ok) {
        error = s.error();
        return false;
    }
    return true;
}

// --------------------------------
// Async wrapper
// --------------------------------

Leaderboard::Leaderboard(DbConfig config) : config_(std::move(config)), thread_([this] { worker(); }) {}

Leaderboard::~Leaderboard() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void Leaderboard::submit(const RunRecord& run) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back({LeaderboardResult::Kind::Submit, run, run.difficulty, 0});
        pending_++;
    }
    wake_.notify_one();
}

void Leaderboard::fetch(Difficulty difficulty, int limit) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back({LeaderboardResult::Kind::Fetch, RunRecord{}, difficulty, limit});
        pending_++;
    }
    wake_.notify_one();
}

bool Leaderboard::poll(LeaderboardResult& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (results_.empty()) return false;
    out = std::move(results_.front());
    results_.erase(results_.begin());
    return true;
}

bool Leaderboard::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_ > 0;
}

void Leaderboard::worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) break;  // jobs still queued at shutdown are dropped
            job = jobs_.front();
            jobs_.erase(jobs_.begin());
        }

        LeaderboardResult result;
        result.kind = job.kind;
        result.difficulty = job.difficulty;
        if (job.kind == LeaderboardResult::Kind::Submit)
            result.ok = submit_score(config_, job.run, result.rank, result.total, result.error);
        else
            result.ok = fetch_top(config_, job.difficulty, job.limit, result.rows, result.error);

        std::lock_guard<std::mutex> lock(mutex_);
        results_.push_back(std::move(result));
        pending_--;
    }
    mysql_thread_end();
}

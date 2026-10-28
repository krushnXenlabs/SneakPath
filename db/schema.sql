-- Leaderboard schema for Escape the Professor (MySQL 8.0.16+, CHECK constraints need it).
-- Run once as an admin user:   mysql -u root -p < db/schema.sql
-- The test database is made from this same file:
--   sed 's/escape_professor/escape_professor_test/g' db/schema.sql | mysql -u root -p

CREATE DATABASE IF NOT EXISTS escape_professor
    CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
USE escape_professor;

CREATE TABLE IF NOT EXISTS players (
    id         INT UNSIGNED NOT NULL AUTO_INCREMENT,
    name       VARCHAR(12)  NOT NULL,
    created_at TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uq_players_name (name),
    CONSTRAINT ck_players_name CHECK (CHAR_LENGTH(name) BETWEEN 1 AND 12)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS scores (
    id         INT UNSIGNED      NOT NULL AUTO_INCREMENT,
    player_id  INT UNSIGNED      NOT NULL,
    difficulty ENUM('EASY','MEDIUM','HARD') NOT NULL,
    score      INT UNSIGNED      NOT NULL,
    time_ms    INT UNSIGNED      NOT NULL,
    steps      SMALLINT UNSIGNED NOT NULL,
    pickups    TINYINT UNSIGNED  NOT NULL,
    created_at TIMESTAMP         NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    KEY idx_scores_board (difficulty, score DESC, time_ms),
    CONSTRAINT fk_scores_player FOREIGN KEY (player_id) REFERENCES players (id),
    -- The best possible score is (2000 + 3 * 100) * 2.0 = 4600
    CONSTRAINT ck_scores_score   CHECK (score <= 5000),
    CONSTRAINT ck_scores_pickups CHECK (pickups <= 3)
) ENGINE=InnoDB;

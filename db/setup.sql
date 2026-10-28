-- Creates the game's database account. Run as an admin user AFTER schema.sql:
--   mysql -u root -p < db/setup.sql
--
-- 1. Replace CHANGE_ME below with a password you choose.
-- 2. Put the same password in db.conf (copy db.conf.example) or the
--    ESCAPE_DB_PASSWORD environment variable.
--
-- The game only ever needs to read the board and add scores, so the account
-- gets SELECT and INSERT on the real database and nothing else.

CREATE USER IF NOT EXISTS 'escape_game'@'localhost' IDENTIFIED BY 'CHANGE_ME';
CREATE USER IF NOT EXISTS 'escape_game'@'127.0.0.1' IDENTIFIED BY 'CHANGE_ME';

GRANT SELECT, INSERT ON escape_professor.* TO 'escape_game'@'localhost';
GRANT SELECT, INSERT ON escape_professor.* TO 'escape_game'@'127.0.0.1';

-- Only for `make test-db`: the tests need to clean up after themselves, and
-- they run against a separate throwaway database.
GRANT SELECT, INSERT, DELETE ON escape_professor_test.* TO 'escape_game'@'localhost';
GRANT SELECT, INSERT, DELETE ON escape_professor_test.* TO 'escape_game'@'127.0.0.1';

FLUSH PRIVILEGES;

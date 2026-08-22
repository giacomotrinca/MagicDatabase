#include "database.h"
#include <sqlite3.h>
#include <iostream>
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <ctime>
#include <cstring>
#include <dirent.h>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <mutex>

namespace {

bool cards_fts_exists(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    const char* query = "SELECT name FROM sqlite_master WHERE type='table' AND name='cards_fts'";
    bool exists = false;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            exists = true;
        }
        sqlite3_finalize(stmt);
    }
    return exists;
}

void drop_cards_fts_triggers(sqlite3* db) {
    sqlite3_exec(db, "DROP TRIGGER IF EXISTS trg_cards_ai; DROP TRIGGER IF EXISTS trg_cards_au; DROP TRIGGER IF EXISTS trg_cards_ad;", nullptr, nullptr, nullptr);
}

void create_cards_fts_triggers(sqlite3* db) {
    if (!cards_fts_exists(db)) {
        return;
    }
    const char* trig_ins =
        "CREATE TRIGGER trg_cards_ai AFTER INSERT ON cards BEGIN "
        "INSERT INTO cards_fts(rowid, name, type, mana_cost, rarity, set_code) VALUES (new.id, COALESCE(new.english_name, new.localized_name, new.name), new.type, new.mana_cost, new.rarity, new.set_code); "
        "END;";
    const char* trig_upd =
        "CREATE TRIGGER trg_cards_au AFTER UPDATE ON cards BEGIN "
        "INSERT INTO cards_fts(cards_fts, rowid) VALUES('delete', old.id); "
        "INSERT INTO cards_fts(rowid, name, type, mana_cost, rarity, set_code) VALUES (new.id, COALESCE(new.english_name, new.localized_name, new.name), new.type, new.mana_cost, new.rarity, new.set_code); "
        "END;";
    const char* trig_del =
        "CREATE TRIGGER trg_cards_ad AFTER DELETE ON cards BEGIN "
        "INSERT INTO cards_fts(cards_fts, rowid) VALUES('delete', old.id); "
        "END;";
    sqlite3_exec(db, trig_ins, nullptr, nullptr, nullptr);
    sqlite3_exec(db, trig_upd, nullptr, nullptr, nullptr);
    sqlite3_exec(db, trig_del, nullptr, nullptr, nullptr);
}

void recreate_cards_fts_triggers(sqlite3* db) {
    drop_cards_fts_triggers(db);
    create_cards_fts_triggers(db);
}

}

Database::Database(const std::string& db_path) : db(nullptr) {
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Impossibile aprire il database: " << sqlite3_errmsg(db) << std::endl;
        db = nullptr;
    }
    // If the "cards" table already exists but lacks the added_date column, add it.
    if (db) {
        sqlite3_stmt* stmt = nullptr;
        const char* tbl_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='cards'";
        if (sqlite3_prepare_v2(db, tbl_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                sqlite3_finalize(stmt);
                // Check columns
                sqlite3_stmt* pi = nullptr;
                const char* pragma = "PRAGMA table_info(cards)";
                bool has_added_date = false;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "added_date") == 0) {
                            has_added_date = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_added_date) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN added_date TEXT";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta added_date: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'added_date' alla tabella cards." << std::endl;
                    }
                }
                // Check for price_usd column
                bool has_price_usd = false;
                pi = nullptr;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "price_usd") == 0) {
                            has_price_usd = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_price_usd) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN price_usd TEXT";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta price_usd: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'price_usd' alla tabella cards." << std::endl;
                    }
                }
                // Check for english_name column
                bool has_english_name = false;
                pi = nullptr;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "english_name") == 0) {
                            has_english_name = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_english_name) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN english_name TEXT";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta english_name: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'english_name' alla tabella cards." << std::endl;
                    }
                }
                // Check for localized_name column
                bool has_localized_name = false;
                pi = nullptr;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "localized_name") == 0) {
                            has_localized_name = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_localized_name) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN localized_name TEXT";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta localized_name: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'localized_name' alla tabella cards." << std::endl;
                    }
                }
                // Check for localized_type column
                bool has_localized_type = false;
                pi = nullptr;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "localized_type") == 0) {
                            has_localized_type = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_localized_type) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN localized_type TEXT";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta localized_type: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'localized_type' alla tabella cards." << std::endl;
                    }
                }
                // Check for scryfall_id column
                bool has_scryfall_id = false;
                pi = nullptr;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "scryfall_id") == 0) {
                            has_scryfall_id = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_scryfall_id) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN scryfall_id TEXT";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta scryfall_id: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'scryfall_id' alla tabella cards." << std::endl;
                    }
                }
                // Check for oracle_id column
                bool has_oracle_id_col = false;
                pi = nullptr;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "oracle_id") == 0) {
                            has_oracle_id_col = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_oracle_id_col) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN oracle_id TEXT";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta oracle_id: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'oracle_id' alla tabella cards." << std::endl;
                    }
                }
                // Check for collector_number column
                bool has_collector_number = false;
                pi = nullptr;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "collector_number") == 0) {
                            has_collector_number = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_collector_number) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN collector_number TEXT";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta collector_number: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'collector_number' alla tabella cards." << std::endl;
                    }
                }
                // Check for decks table
                bool has_decks_table = false;
                sqlite3_stmt* stmt_decks = nullptr;
                const char* decks_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='decks'";
                if (sqlite3_prepare_v2(db, decks_sql, -1, &stmt_decks, nullptr) == SQLITE_OK) {
                    if (sqlite3_step(stmt_decks) == SQLITE_ROW) {
                        has_decks_table = true;
                    }
                    sqlite3_finalize(stmt_decks);
                }
                if (!has_decks_table) {
                    char* err = nullptr;
                    const char* create_decks = "CREATE TABLE decks (id INTEGER PRIMARY KEY, name TEXT UNIQUE)";
                    if (sqlite3_exec(db, create_decks, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore creazione tabella decks: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Creata tabella 'decks'." << std::endl;
                    }
                }
                // Check for deck_id column
                bool has_deck_id = false;
                pi = nullptr;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "deck_id") == 0) {
                            has_deck_id = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_deck_id) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN deck_id INTEGER REFERENCES decks(id)";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta deck_id: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'deck_id' alla tabella cards." << std::endl;
                    }
                }
                // Check for foil column
                bool has_foil = false;
                pi = nullptr;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "foil") == 0) {
                            has_foil = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_foil) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN foil INTEGER DEFAULT 0";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta foil: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'foil' alla tabella cards." << std::endl;
                    }
                }
                // Check for sideboard column
                bool has_sideboard = false;
                pi = nullptr;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "sideboard") == 0) {
                            has_sideboard = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_sideboard) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN sideboard INTEGER DEFAULT 0";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta sideboard: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'sideboard' alla tabella cards." << std::endl;
                    }
                }
                bool has_oracle_text = false;
                pi = nullptr;
                if (sqlite3_prepare_v2(db, pragma, -1, &pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(pi, 1);
                        if (colname && strcmp((const char*)colname, "oracle_text") == 0) {
                            has_oracle_text = true;
                            break;
                        }
                    }
                    sqlite3_finalize(pi);
                }
                if (!has_oracle_text) {
                    char* err = nullptr;
                    const char* alter = "ALTER TABLE cards ADD COLUMN oracle_text TEXT";
                    if (sqlite3_exec(db, alter, nullptr, nullptr, &err) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta oracle_text: " << (err ? err : (char*)"unknown") << std::endl;
                        if (err) sqlite3_free(err);
                    } else {
                        std::cout << "Aggiunta colonna 'oracle_text' alla tabella cards." << std::endl;
                    }
                }
                // Create auxiliary tables for snapshots, tags, notes and full-text search (if not present)
                // deck_snapshots
                const char* create_snapshots = "CREATE TABLE IF NOT EXISTS deck_snapshots (id INTEGER PRIMARY KEY, deck_id INTEGER, name TEXT, created_at TEXT)";
                char* errx = nullptr;
                if (sqlite3_exec(db, create_snapshots, nullptr, nullptr, &errx) != SQLITE_OK) {
                    std::cerr << "Errore creazione tabella deck_snapshots: " << (errx ? errx : (char*)"unknown") << std::endl;
                    if (errx) sqlite3_free(errx);
                }
                const char* create_snapshot_rows = "CREATE TABLE IF NOT EXISTS deck_snapshot_rows (id INTEGER PRIMARY KEY, snapshot_id INTEGER, original_card_id INTEGER, english_name TEXT, localized_name TEXT, type TEXT, colors TEXT, set_code TEXT, mana_cost TEXT, rarity TEXT, quantity INTEGER, foil INTEGER, sideboard INTEGER, oracle_text TEXT)";
                errx = nullptr;
                if (sqlite3_exec(db, create_snapshot_rows, nullptr, nullptr, &errx) != SQLITE_OK) {
                    std::cerr << "Errore creazione tabella deck_snapshot_rows: " << (errx ? errx : (char*)"unknown") << std::endl;
                    if (errx) sqlite3_free(errx);
                }
                bool has_snapshot_oracle = false;
                sqlite3_stmt* snap_pi = nullptr;
                if (sqlite3_prepare_v2(db, "PRAGMA table_info(deck_snapshot_rows)", -1, &snap_pi, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(snap_pi) == SQLITE_ROW) {
                        const unsigned char* colname = sqlite3_column_text(snap_pi, 1);
                        if (colname && strcmp((const char*)colname, "oracle_text") == 0) {
                            has_snapshot_oracle = true;
                            break;
                        }
                    }
                    sqlite3_finalize(snap_pi);
                }
                if (!has_snapshot_oracle) {
                    char* serr = nullptr;
                    if (sqlite3_exec(db, "ALTER TABLE deck_snapshot_rows ADD COLUMN oracle_text TEXT", nullptr, nullptr, &serr) != SQLITE_OK) {
                        std::cerr << "Errore alter table aggiunta oracle_text su deck_snapshot_rows: " << (serr ? serr : (char*)"unknown") << std::endl;
                        if (serr) sqlite3_free(serr);
                    }
                }
                // Ensure snapshot rows carry the full card identity so restore_snapshot
                // can rebuild cards without losing metadata (price, image, Scryfall ids).
                {
                    const char* snap_cols[] = {"localized_type", "image_url", "price_usd", "scryfall_id", "oracle_id", "collector_number"};
                    for (const char* col : snap_cols) {
                        sqlite3_stmt* spi = nullptr;
                        bool has_col = false;
                        if (sqlite3_prepare_v2(db, "PRAGMA table_info(deck_snapshot_rows)", -1, &spi, nullptr) == SQLITE_OK) {
                            while (sqlite3_step(spi) == SQLITE_ROW) {
                                const unsigned char* colname = sqlite3_column_text(spi, 1);
                                if (colname && strcmp((const char*)colname, col) == 0) { has_col = true; break; }
                            }
                            sqlite3_finalize(spi);
                        }
                        if (!has_col) {
                            std::string alter_sql = std::string("ALTER TABLE deck_snapshot_rows ADD COLUMN ") + col + " TEXT";
                            char* serr = nullptr;
                            if (sqlite3_exec(db, alter_sql.c_str(), nullptr, nullptr, &serr) != SQLITE_OK) {
                                std::cerr << "Errore alter table aggiunta " << col << " su deck_snapshot_rows: " << (serr ? serr : (char*)"unknown") << std::endl;
                                if (serr) sqlite3_free(serr);
                            }
                        }
                    }
                }
                // Tags and notes
                const char* create_card_tags = "CREATE TABLE IF NOT EXISTS card_tags (id INTEGER PRIMARY KEY, card_id INTEGER, tag TEXT)";
                errx = nullptr;
                if (sqlite3_exec(db, create_card_tags, nullptr, nullptr, &errx) != SQLITE_OK) {
                    std::cerr << "Errore creazione tabella card_tags: " << (errx ? errx : (char*)"unknown") << std::endl;
                    if (errx) sqlite3_free(errx);
                }
                const char* create_deck_tags = "CREATE TABLE IF NOT EXISTS deck_tags (id INTEGER PRIMARY KEY, deck_id INTEGER, tag TEXT)";
                errx = nullptr;
                if (sqlite3_exec(db, create_deck_tags, nullptr, nullptr, &errx) != SQLITE_OK) {
                    std::cerr << "Errore creazione tabella deck_tags: " << (errx ? errx : (char*)"unknown") << std::endl;
                    if (errx) sqlite3_free(errx);
                }
                const char* create_card_notes = "CREATE TABLE IF NOT EXISTS card_notes (id INTEGER PRIMARY KEY, card_id INTEGER, note TEXT)";
                errx = nullptr;
                if (sqlite3_exec(db, create_card_notes, nullptr, nullptr, &errx) != SQLITE_OK) {
                    std::cerr << "Errore creazione tabella card_notes: " << (errx ? errx : (char*)"unknown") << std::endl;
                    if (errx) sqlite3_free(errx);
                }
                const char* create_deck_notes = "CREATE TABLE IF NOT EXISTS deck_notes (id INTEGER PRIMARY KEY, deck_id INTEGER, note TEXT)";
                errx = nullptr;
                if (sqlite3_exec(db, create_deck_notes, nullptr, nullptr, &errx) != SQLITE_OK) {
                    std::cerr << "Errore creazione tabella deck_notes: " << (errx ? errx : (char*)"unknown") << std::endl;
                    if (errx) sqlite3_free(errx);
                }
                // Create a simple FTS5 table for cards if available
                const char* create_fts = "CREATE VIRTUAL TABLE IF NOT EXISTS cards_fts USING fts5(name, type, mana_cost, rarity, set_code, content='')";
                errx = nullptr;
                if (sqlite3_exec(db, create_fts, nullptr, nullptr, &errx) != SQLITE_OK) {
                    // FTS5 may not be available on some builds - ignore error
                    if (errx) {
                        std::cerr << "FTS5 not available or error creating cards_fts: " << errx << std::endl;
                        sqlite3_free(errx);
                    }
                } else {
                    // Populate cards_fts from existing cards table (best effort)
                    const char* populate_fts = "INSERT INTO cards_fts (rowid, name, type, mana_cost, rarity, set_code) SELECT id, COALESCE(english_name, localized_name, name), type, mana_cost, rarity, set_code FROM cards WHERE id NOT IN (SELECT rowid FROM cards_fts)";
                    errx = nullptr;
                    if (sqlite3_exec(db, populate_fts, nullptr, nullptr, &errx) != SQLITE_OK) {
                        if (errx) sqlite3_free(errx);
                    }
                }
                // Performance tuning: set safe pragmas and create useful indexes
                // These are non-breaking and use IF NOT EXISTS where applicable.
                errx = nullptr;
                // Use WAL for better concurrency and usually faster writes; ignore failures on older SQLite builds
                if (sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, &errx) != SQLITE_OK) {
                    if (errx) { std::cerr << "Warning: could not set journal_mode=WAL: " << errx << std::endl; sqlite3_free(errx); }
                } else {
                    // reset errx if successful
                    errx = nullptr;
                }
                // Make trade-offs for desktop app: faster sync
                sqlite3_exec(db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);
                sqlite3_exec(db, "PRAGMA temp_store = MEMORY;", nullptr, nullptr, nullptr);

                // Create indexes to speed up common queries (name lookups, filters by deck/set/date/foil)
                const char* idxs[] = {
                    "CREATE INDEX IF NOT EXISTS idx_cards_deck_id ON cards (deck_id)",
                    "CREATE INDEX IF NOT EXISTS idx_cards_deck_side ON cards (deck_id, sideboard)",
                    "CREATE INDEX IF NOT EXISTS idx_cards_set_code ON cards (set_code)",
                    "CREATE INDEX IF NOT EXISTS idx_cards_added_date ON cards (added_date)",
                    "CREATE INDEX IF NOT EXISTS idx_cards_foil ON cards (foil)",
                    "CREATE INDEX IF NOT EXISTS idx_cards_name ON cards (name)",
                    "CREATE INDEX IF NOT EXISTS idx_cards_english_name ON cards (english_name)",
                    "CREATE INDEX IF NOT EXISTS idx_cards_localized_name ON cards (localized_name)",
                    "CREATE INDEX IF NOT EXISTS idx_cards_oracle_id ON cards (oracle_id)",
                    "CREATE INDEX IF NOT EXISTS idx_cards_scryfall_id ON cards (scryfall_id)",
                    "CREATE INDEX IF NOT EXISTS idx_cards_collector_number ON cards (collector_number)"
                };
                for (const char* idx_sql : idxs) {
                    char* ierr = nullptr;
                    if (sqlite3_exec(db, idx_sql, nullptr, nullptr, &ierr) != SQLITE_OK) {
                        if (ierr) {
                            std::cerr << "Warning creating index: " << ierr << " for stmt: " << idx_sql << std::endl;
                            sqlite3_free(ierr);
                        }
                    }
                }
                // Run ANALYZE once to collect statistics for the query planner (best-effort)
                errx = nullptr;
                if (sqlite3_exec(db, "ANALYZE;", nullptr, nullptr, &errx) != SQLITE_OK) {
                    if (errx) { sqlite3_free(errx); }
                }

                // --- Ensure migrations table exists (to track applied .sql files) ---
                const char* create_migrations_tbl = "CREATE TABLE IF NOT EXISTS migrations (name TEXT PRIMARY KEY, applied_at TEXT)";
                errx = nullptr;
                if (sqlite3_exec(db, create_migrations_tbl, nullptr, nullptr, &errx) != SQLITE_OK) {
                    if (errx) { std::cerr << "Warning: could not create migrations table: " << errx << std::endl; sqlite3_free(errx); }
                }

                // --- Apply any SQL migration files found under data/migrations/ (lexicographic order) ---
                const std::string migrations_dir = "data/migrations";
                DIR* d = opendir(migrations_dir.c_str());
                if (d) {
                    std::vector<std::string> files;
                    struct dirent* de;
                    while ((de = readdir(d)) != nullptr) {
                        std::string name = de->d_name;
                        if (name.size() > 4 && name.substr(name.size()-4) == ".sql") files.push_back(name);
                    }
                    closedir(d);
                    std::sort(files.begin(), files.end());
                    for (const auto &fname : files) {
                        // Check if already applied
                        sqlite3_stmt* chk = nullptr;
                        std::string chk_sql = "SELECT name FROM migrations WHERE name = ? LIMIT 1";
                        if (sqlite3_prepare_v2(db, chk_sql.c_str(), -1, &chk, nullptr) != SQLITE_OK) continue;
                        sqlite3_bind_text(chk, 1, fname.c_str(), -1, SQLITE_TRANSIENT);
                        int rc = sqlite3_step(chk);
                        bool applied = (rc == SQLITE_ROW);
                        sqlite3_finalize(chk);
                        if (applied) continue;
                        // Read file content
                        std::string path = migrations_dir + "/" + fname;
                        std::ifstream ifs(path);
                        if (!ifs) {
                            std::cerr << "Warning: could not open migration file: " << path << std::endl;
                            continue;
                        }
                        std::stringstream buf;
                        buf << ifs.rdbuf();
                        std::string sql = buf.str();
                        if (sql.empty()) continue;
                        // Run each migration file inside its own transaction so a
                        // mid-file failure leaves no partially-applied schema.
                        char* berr = nullptr;
                        if (sqlite3_exec(db, "BEGIN", nullptr, nullptr, &berr) != SQLITE_OK) {
                            std::cerr << "Error starting transaction for migration " << fname << ": " << (berr ? berr : (char*)"unknown") << std::endl;
                            if (berr) sqlite3_free(berr);
                            break;
                        }
                        char* merr = nullptr;
                        bool mig_ok = (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &merr) == SQLITE_OK);
                        if (!mig_ok) {
                            std::cerr << "Error applying migration " << fname << ": " << (merr ? merr : (char*)"unknown") << std::endl;
                            if (merr) sqlite3_free(merr);
                            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                            // stop on first failing migration to avoid partial state
                            break;
                        }
                        sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
                        // Record applied migration with timestamp
                        char timebuf[32] = {0};
                        time_t now = time(nullptr);
                        struct tm local_tm{};
#if defined(_MSC_VER)
                        localtime_s(&local_tm, &now);
#else
                        localtime_r(&now, &local_tm);
#endif
                        strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", &local_tm);
                        const char* ins = "INSERT INTO migrations (name, applied_at) VALUES (?, ?)";
                        sqlite3_stmt* pins = nullptr;
                        if (sqlite3_prepare_v2(db, ins, -1, &pins, nullptr) == SQLITE_OK) {
                            sqlite3_bind_text(pins, 1, fname.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(pins, 2, timebuf, -1, SQLITE_TRANSIENT);
                            int irc = sqlite3_step(pins);
                            if (irc != SQLITE_DONE) {
                                std::cerr << "Warning: could not record migration " << fname << " as applied: " << sqlite3_errmsg(db) << std::endl;
                            }
                            sqlite3_finalize(pins);
                        }
                    }
                }

                // --- If FTS table exists, ensure triggers keep it in sync (create if missing) ---
                bool has_fts2 = false;
                sqlite3_stmt* s2 = nullptr;
                const char* q2 = "SELECT name FROM sqlite_master WHERE type='table' AND name='cards_fts'";
                if (sqlite3_prepare_v2(db, q2, -1, &s2, nullptr) == SQLITE_OK) {
                    if (sqlite3_step(s2) == SQLITE_ROW) has_fts2 = true;
                    sqlite3_finalize(s2);
                }
                if (has_fts2) {
                    recreate_cards_fts_triggers(db);
                }
            } else {
                sqlite3_finalize(stmt);
            }
        }
    }
}

Database::~Database() {
    if (db) {
        sqlite3_close(db);
    }
}

bool Database::is_open() const {
    return db != nullptr;
}

bool Database::execute(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Errore SQL: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool Database::insert_card(const std::string& english_name, const std::string& localized_name, const std::string& type, const std::string& localized_type, const std::string& colors, const std::string& set_code, const std::string& mana_cost, const std::string& rarity, int quantity, const std::string& image_url, const std::string& price_usd, const std::string& oracle_text, int deck_id, int foil, int sideboard, const std::string& scryfall_id, const std::string& oracle_id, const std::string& collector_number) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    // Normalize deck_id: callers may pass 0 for "no deck"; internally we treat <=0 as -1 (NULL)
    if (deck_id <= 0) deck_id = -1;
    if (sideboard <= 0) sideboard = 0; // normalize any non-positive to 0
    // Prima controlla se la carta esiste già (stesso english_name o localized_name, stesso foil, e nello stesso deck se specificato)
    sqlite3_stmt* check_stmt;
    // Uniqueness check: consider that some rows may have english_name empty and use localized_name instead.
    // Match if either english_name OR localized_name equals the provided value, same foil, and same deck (or NULL).
    // NOTE: we intentionally do NOT use set_code for uniqueness so cards from different sets with the same name
    // are merged into the same row (as requested).
    if (deck_id != -1) {
        // Use trimmed, case-insensitive comparisons so 'Anello Solare' == 'anello solare' and
        // leading/trailing spaces don't cause duplicates. Compare both input names against stored
        // columns english_name, localized_name and the legacy name column so older rows match.
        const char* check_sql =
            "SELECT id, quantity FROM cards WHERE ("
            " LOWER(TRIM(COALESCE(english_name,''))) = LOWER(TRIM(?))"
            " OR LOWER(TRIM(COALESCE(localized_name,''))) = LOWER(TRIM(?))"
            " OR LOWER(TRIM(COALESCE(name,''))) = LOWER(TRIM(?))"
            " OR LOWER(TRIM(COALESCE(english_name,''))) = LOWER(TRIM(?))"
            " OR LOWER(TRIM(COALESCE(localized_name,''))) = LOWER(TRIM(?))"
            " OR LOWER(TRIM(COALESCE(name,''))) = LOWER(TRIM(?)) )"
            " AND foil = ? AND sideboard = ? AND deck_id = ?";
        if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Errore prepare check: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        // Bind: first three compare stored cols to the provided english_name,
        // next three compare stored cols to the provided localized_name.
        sqlite3_bind_text(check_stmt, 1, english_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 2, english_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 3, english_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 4, localized_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 5, localized_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 6, localized_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(check_stmt, 7, foil);
        sqlite3_bind_int(check_stmt, 8, sideboard);
        sqlite3_bind_int(check_stmt, 9, deck_id);
    } else {
        const char* check_sql =
            "SELECT id, quantity FROM cards WHERE ("
            " LOWER(TRIM(COALESCE(english_name,''))) = LOWER(TRIM(?))"
            " OR LOWER(TRIM(COALESCE(localized_name,''))) = LOWER(TRIM(?))"
            " OR LOWER(TRIM(COALESCE(name,''))) = LOWER(TRIM(?))"
            " OR LOWER(TRIM(COALESCE(english_name,''))) = LOWER(TRIM(?))"
            " OR LOWER(TRIM(COALESCE(localized_name,''))) = LOWER(TRIM(?))"
            " OR LOWER(TRIM(COALESCE(name,''))) = LOWER(TRIM(?)) )"
            " AND foil = ? AND sideboard = ? AND deck_id IS NULL";
        if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Errore prepare check: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        sqlite3_bind_text(check_stmt, 1, english_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 2, english_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 3, english_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 4, localized_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 5, localized_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 6, localized_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(check_stmt, 7, foil);
        sqlite3_bind_int(check_stmt, 8, sideboard);
    }
    int rc = sqlite3_step(check_stmt);
    if (rc == SQLITE_ROW) {
        // Esiste già, aggiorna quantità
        int existing_id = sqlite3_column_int(check_stmt, 0);
        int existing_qty = sqlite3_column_int(check_stmt, 1);
        sqlite3_finalize(check_stmt);
        
        const char* update_sql = "UPDATE cards SET quantity = ? WHERE id = ?";
        sqlite3_stmt* update_stmt;
        if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Errore prepare update: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        sqlite3_bind_int(update_stmt, 1, existing_qty + quantity);
        sqlite3_bind_int(update_stmt, 2, existing_id);
        // Some SQLite builds with contentless FTS5 may raise errors inside triggers
        // (e.g. "cannot DELETE from contentless fts5 table"). To avoid failing the
        // user's save operation, drop the FTS triggers temporarily, perform the
        // update, then recreate the triggers if the FTS table exists.
    drop_cards_fts_triggers(db);
        rc = sqlite3_step(update_stmt);
        sqlite3_finalize(update_stmt);
        if (rc != SQLITE_DONE) {
            std::cerr << "Errore update: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        std::cout << "Updated card " << english_name << " quantity to " << (existing_qty + quantity) << std::endl;
        recreate_cards_fts_triggers(db);
        return true;
    } else {
        sqlite3_finalize(check_stmt);
        // Non esiste, inserisci nuova (imposta added_date alla data/ora corrente in formato ISO)
    const char* insert_sql = "INSERT INTO cards (english_name, localized_name, name, canonical_name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, image_url, added_date, price_usd, foil, sideboard, oracle_text, deck_id, scryfall_id, oracle_id, collector_number) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
        sqlite3_stmt* insert_stmt;
        if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Errore prepare insert: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
    // Compute canonical_name similar to migration: lower(trim(coalesce(english_name, localized_name, name)))
    std::string base_name = !english_name.empty() ? english_name : (!localized_name.empty() ? localized_name : std::string());
    // trim
    auto trim = [](std::string s) {
        size_t start = 0;
        while (start < s.size() && isspace((unsigned char)s[start])) ++start;
        size_t end = s.size();
        while (end > start && isspace((unsigned char)s[end-1])) --end;
        return s.substr(start, end - start);
    };
    std::string canonical = trim(base_name);
    // tolower
    for (auto &c : canonical) c = (char)tolower((unsigned char)c);

    sqlite3_bind_text(insert_stmt, 1, english_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 2, localized_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 3, localized_name.c_str(), -1, SQLITE_TRANSIENT); // name = localized for backward
    sqlite3_bind_text(insert_stmt, 4, canonical.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 5, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 6, localized_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 7, colors.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 8, set_code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 9, mana_cost.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 10, rarity.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_stmt, 11, quantity);
    sqlite3_bind_text(insert_stmt, 12, image_url.c_str(), -1, SQLITE_TRANSIENT);
        // Calcola timestamp ISO locale: YYYY-MM-DDTHH:MM:SS
        char timebuf[32] = {0};
        time_t now = time(nullptr);
        struct tm local_tm{};
#if defined(_MSC_VER)
        localtime_s(&local_tm, &now);
#else
        localtime_r(&now, &local_tm);
#endif
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", &local_tm);
        sqlite3_bind_text(insert_stmt, 13, timebuf, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 14, price_usd.c_str(), -1, SQLITE_TRANSIENT);
        // bind foil
        sqlite3_bind_int(insert_stmt, 15, foil);
        // bind sideboard
        sqlite3_bind_int(insert_stmt, 16, sideboard);
        sqlite3_bind_text(insert_stmt, 17, oracle_text.c_str(), -1, SQLITE_TRANSIENT);
        if (deck_id != -1) {
            sqlite3_bind_int(insert_stmt, 18, deck_id);
        } else {
            sqlite3_bind_null(insert_stmt, 18);
        }
        if (!scryfall_id.empty()) {
            sqlite3_bind_text(insert_stmt, 19, scryfall_id.c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(insert_stmt, 19);
        }
        if (!oracle_id.empty()) {
            sqlite3_bind_text(insert_stmt, 20, oracle_id.c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(insert_stmt, 20);
        }
        if (!collector_number.empty()) {
            sqlite3_bind_text(insert_stmt, 21, collector_number.c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(insert_stmt, 21);
        }
    // As above, drop FTS triggers before insert to avoid FTS5 trigger errors
    drop_cards_fts_triggers(db);
        rc = sqlite3_step(insert_stmt);
        sqlite3_finalize(insert_stmt);
        if (rc != SQLITE_DONE) {
            std::cerr << "Errore insert: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        // recreate triggers if FTS table exists (best-effort)
        recreate_cards_fts_triggers(db);
        std::cout << "Inserted new card " << english_name << std::endl;
        return true;
    }
}

bool Database::delete_card(int id) {
    const char* sql = "DELETE FROM cards WHERE id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Errore prepare delete: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Errore delete: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}



bool Database::update_quantity(int id, int new_quantity) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    const char* sql = "UPDATE cards SET quantity = ? WHERE id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Errore prepare update_quantity: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, new_quantity);
    sqlite3_bind_int(stmt, 2, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Errore update_quantity: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool Database::get_card_quantity(int id, int& quantity) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    const char* sql = "SELECT quantity FROM cards WHERE id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Errore prepare get_quantity: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        quantity = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return true;
    } else {
        sqlite3_finalize(stmt);
        return false;
    }
}

bool Database::update_card_info(int id, const std::string& english_name, const std::string& localized_name, const std::string& type, const std::string& localized_type, const std::string& colors, const std::string& mana_cost, const std::string& rarity, const std::string& image_url, const std::string& price_usd, const std::string& oracle_text, const std::string& scryfall_id, const std::string& oracle_id, const std::string& collector_number) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    std::string set_code;
    bool set_code_is_null = true;
    {
        sqlite3_stmt* code_stmt = nullptr;
        const char* code_sql = "SELECT set_code FROM cards WHERE id = ?";
        if (sqlite3_prepare_v2(db, code_sql, -1, &code_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(code_stmt, 1, id);
            if (sqlite3_step(code_stmt) == SQLITE_ROW) {
                const unsigned char* txt = sqlite3_column_text(code_stmt, 0);
                if (txt) {
                    set_code.assign(reinterpret_cast<const char*>(txt));
                    set_code_is_null = false;
                }
            }
            sqlite3_finalize(code_stmt);
        }
    }

    drop_cards_fts_triggers(db);

    const char* sql = "UPDATE cards SET english_name = ?, localized_name = ?, name = ?, type = ?, localized_type = ?, colors = ?, mana_cost = ?, rarity = ?, image_url = ?, price_usd = ?, oracle_text = ?, scryfall_id = ?, oracle_id = ?, collector_number = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    bool success = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Errore prepare update_card_info: " << sqlite3_errmsg(db) << std::endl;
    } else {
        sqlite3_bind_text(stmt, 1, english_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, localized_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, localized_name.c_str(), -1, SQLITE_TRANSIENT); // name = localized
        sqlite3_bind_text(stmt, 4, type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, localized_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, colors.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, mana_cost.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, rarity.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, image_url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 10, price_usd.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 11, oracle_text.c_str(), -1, SQLITE_TRANSIENT);
        if (!scryfall_id.empty()) {
            sqlite3_bind_text(stmt, 12, scryfall_id.c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 12);
        }
        if (!oracle_id.empty()) {
            sqlite3_bind_text(stmt, 13, oracle_id.c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 13);
        }
        if (!collector_number.empty()) {
            sqlite3_bind_text(stmt, 14, collector_number.c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 14);
        }
        sqlite3_bind_int(stmt, 15, id);
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::cerr << "Errore update_card_info: " << sqlite3_errmsg(db) << std::endl;
        } else {
            success = true;
        }
    }
    if (stmt) {
        sqlite3_finalize(stmt);
    }

    bool has_fts = cards_fts_exists(db);
    if (has_fts && success) {
        sqlite3_stmt* fts_del = nullptr;
        const char* fts_del_sql = "INSERT INTO cards_fts(cards_fts, rowid) VALUES('delete', ?)";
        if (sqlite3_prepare_v2(db, fts_del_sql, -1, &fts_del, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(fts_del, 1, id);
            sqlite3_step(fts_del);
        }
        if (fts_del) {
            sqlite3_finalize(fts_del);
        }

        sqlite3_stmt* fts_ins = nullptr;
        const char* fts_ins_sql = "INSERT INTO cards_fts(rowid, name, type, mana_cost, rarity, set_code) VALUES (?, ?, ?, ?, ?, ?)";
        if (sqlite3_prepare_v2(db, fts_ins_sql, -1, &fts_ins, nullptr) == SQLITE_OK) {
            std::string fts_name = english_name;
            if (fts_name.empty()) {
                fts_name = localized_name;
            }
            sqlite3_bind_int(fts_ins, 1, id);
            sqlite3_bind_text(fts_ins, 2, fts_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(fts_ins, 3, type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(fts_ins, 4, mana_cost.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(fts_ins, 5, rarity.c_str(), -1, SQLITE_TRANSIENT);
            if (set_code_is_null) {
                sqlite3_bind_null(fts_ins, 6);
            } else {
                sqlite3_bind_text(fts_ins, 6, set_code.c_str(), -1, SQLITE_TRANSIENT);
            }
            sqlite3_step(fts_ins);
        }
        if (fts_ins) {
            sqlite3_finalize(fts_ins);
        }
    }

    if (has_fts) {
        recreate_cards_fts_triggers(db);
    }

    return success;
}

bool Database::create_deck(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    const char* sql = "INSERT INTO decks (name) VALUES (?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Errore prepare create_deck: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Errore create_deck: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool Database::query_decks(const std::function<void(const std::map<std::string, std::string>&)>& callback) {
    return query("SELECT id, name FROM decks", callback);
}

bool Database::set_card_deck(int card_id, int deck_id, int sideboard) {
    const char* sql = "UPDATE cards SET deck_id = ?, sideboard = ? WHERE id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Errore prepare set_card_deck: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    if (deck_id != -1) {
        sqlite3_bind_int(stmt, 1, deck_id);
        std::cout << "DEBUG: set_card_deck binding deck_id=" << deck_id << " for card_id=" << card_id << std::endl;
    } else {
        sqlite3_bind_null(stmt, 1);
        std::cout << "DEBUG: set_card_deck binding deck_id=NULL for card_id=" << card_id << std::endl;
    }
    // bind sideboard flag
    sqlite3_bind_int(stmt, 2, sideboard);
    sqlite3_bind_int(stmt, 3, card_id);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Errore set_card_deck: " << sqlite3_errmsg(db) << " (rc=" << rc << ")" << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    std::cout << "DEBUG: set_card_deck succeeded for card_id=" << card_id << " deck_id=" << (deck_id == -1 ? -1 : deck_id) << " sideboard=" << sideboard << std::endl;
    return true;
}

bool Database::query(const std::string& sql, const std::function<void(const std::map<std::string, std::string>&)>& callback, const std::vector<std::string>& params) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Errore prepare query: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    // Bind params
    for (size_t i = 0; i < params.size(); ++i) {
        sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_STATIC);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::map<std::string, std::string> row;
        int col_count = sqlite3_column_count(stmt);
        for (int i = 0; i < col_count; ++i) {
            const char* col_name = sqlite3_column_name(stmt, i);
            const unsigned char* col_value = sqlite3_column_text(stmt, i);
            row[col_name ? col_name : ""] = col_value ? (const char*)col_value : "";
        }
        callback(row);
    }
    sqlite3_finalize(stmt);
    return true;
}

// Snapshot & tagging implementations
bool Database::create_snapshot(int deck_id, const std::string& name, int& out_snapshot_id) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!db) return false;
    char timebuf[32] = {0};
    time_t now = time(nullptr);
    struct tm local_tm{};
#if defined(_MSC_VER)
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", &local_tm);
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    const char* ins_snap = "INSERT INTO deck_snapshots (deck_id, name, created_at) VALUES (?, ?, ?)";
    sqlite3_stmt* sstmt = nullptr;
    if (sqlite3_prepare_v2(db, ins_snap, -1, &sstmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_int(sstmt, 1, deck_id);
    sqlite3_bind_text(sstmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(sstmt, 3, timebuf, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(sstmt);
    sqlite3_finalize(sstmt);
    if (rc != SQLITE_DONE) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    int snap_id = (int)sqlite3_last_insert_rowid(db);
    // Copy current deck rows into deck_snapshot_rows
    const char* sel = "SELECT id, english_name, localized_name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, foil, sideboard, oracle_text, image_url, price_usd, scryfall_id, oracle_id, collector_number FROM cards WHERE deck_id = ?";
    sqlite3_stmt* psel = nullptr;
    if (sqlite3_prepare_v2(db, sel, -1, &psel, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_int(psel, 1, deck_id);
    const char* ins_row = "INSERT INTO deck_snapshot_rows (snapshot_id, original_card_id, english_name, localized_name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, foil, sideboard, oracle_text, image_url, price_usd, scryfall_id, oracle_id, collector_number) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* pins = nullptr;
    if (sqlite3_prepare_v2(db, ins_row, -1, &pins, nullptr) != SQLITE_OK) {
        sqlite3_finalize(psel);
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    while (sqlite3_step(psel) == SQLITE_ROW) {
        int cid = sqlite3_column_int(psel, 0);
        const unsigned char* en = sqlite3_column_text(psel, 1);
        const unsigned char* ln = sqlite3_column_text(psel, 2);
        const unsigned char* ty = sqlite3_column_text(psel, 3);
        const unsigned char* lty = sqlite3_column_text(psel, 4);
        const unsigned char* cols = sqlite3_column_text(psel, 5);
        const unsigned char* setc = sqlite3_column_text(psel, 6);
        const unsigned char* mana = sqlite3_column_text(psel, 7);
        const unsigned char* rar = sqlite3_column_text(psel, 8);
        int qty = sqlite3_column_int(psel, 9);
        int foil = sqlite3_column_int(psel, 10);
    int side = sqlite3_column_int(psel, 11);
    const unsigned char* oracle = sqlite3_column_text(psel, 12);
    const unsigned char* imgurl = sqlite3_column_text(psel, 13);
    const unsigned char* price = sqlite3_column_text(psel, 14);
    const unsigned char* sfid = sqlite3_column_text(psel, 15);
    const unsigned char* orid = sqlite3_column_text(psel, 16);
    const unsigned char* colnum = sqlite3_column_text(psel, 17);
    sqlite3_bind_int(pins, 1, snap_id);
    sqlite3_bind_int(pins, 2, cid);
    const char* en_s = en ? reinterpret_cast<const char*>(en) : "";
    const char* ln_s = ln ? reinterpret_cast<const char*>(ln) : "";
    const char* ty_s = ty ? reinterpret_cast<const char*>(ty) : "";
    const char* lty_s = lty ? reinterpret_cast<const char*>(lty) : "";
    const char* cols_s = cols ? reinterpret_cast<const char*>(cols) : "";
    const char* setc_s = setc ? reinterpret_cast<const char*>(setc) : "";
    const char* mana_s = mana ? reinterpret_cast<const char*>(mana) : "";
    const char* rar_s = rar ? reinterpret_cast<const char*>(rar) : "";
    sqlite3_bind_text(pins, 3, en_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 4, ln_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 5, ty_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 6, lty_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 7, cols_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 8, setc_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 9, mana_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 10, rar_s, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pins, 11, qty);
        sqlite3_bind_int(pins, 12, foil);
    sqlite3_bind_int(pins, 13, side);
    const char* oracle_s = oracle ? reinterpret_cast<const char*>(oracle) : "";
    sqlite3_bind_text(pins, 14, oracle_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 15, imgurl ? reinterpret_cast<const char*>(imgurl) : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 16, price ? reinterpret_cast<const char*>(price) : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 17, sfid ? reinterpret_cast<const char*>(sfid) : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 18, orid ? reinterpret_cast<const char*>(orid) : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 19, colnum ? reinterpret_cast<const char*>(colnum) : "", -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(pins);
        sqlite3_reset(pins);
        if (rc != SQLITE_DONE) {
            sqlite3_finalize(pins);
            sqlite3_finalize(psel);
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
    }
    sqlite3_finalize(pins);
    sqlite3_finalize(psel);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    out_snapshot_id = snap_id;
    return true;
}

bool Database::list_snapshots(int deck_id, const std::function<void(const std::map<std::string,std::string>&)>& callback) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    const char* sql = "SELECT id, name, created_at FROM deck_snapshots WHERE deck_id = ? ORDER BY created_at DESC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, deck_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::map<std::string,std::string> row;
        row["id"] = std::to_string(sqlite3_column_int(stmt, 0));
        const unsigned char* nm = sqlite3_column_text(stmt, 1);
        const unsigned char* ca = sqlite3_column_text(stmt, 2);
        row["name"] = nm ? (const char*)nm : "";
        row["created_at"] = ca ? (const char*)ca : "";
        callback(row);
    }
    sqlite3_finalize(stmt);
    return true;
}

bool Database::get_snapshot_rows(int snapshot_id, const std::function<void(const std::map<std::string,std::string>&)>& callback) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    const char* sql = "SELECT id, original_card_id, english_name, localized_name, type, colors, set_code, mana_cost, rarity, quantity, foil, sideboard, oracle_text, localized_type, image_url, price_usd, scryfall_id, oracle_id, collector_number FROM deck_snapshot_rows WHERE snapshot_id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, snapshot_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::map<std::string,std::string> row;
        row["id"] = std::to_string(sqlite3_column_int(stmt, 0));
        row["original_card_id"] = std::to_string(sqlite3_column_int(stmt, 1));
        const char* cols[] = {"english_name","localized_name","type","colors","set_code","mana_cost","rarity"};
        for (int i=0;i<7;++i) {
            const unsigned char* txt = sqlite3_column_text(stmt, i+2);
            row[cols[i]] = txt ? (const char*)txt : "";
        }
    row["quantity"] = std::to_string(sqlite3_column_int(stmt, 9));
    row["foil"] = std::to_string(sqlite3_column_int(stmt, 10));
    row["sideboard"] = std::to_string(sqlite3_column_int(stmt, 11));
    const unsigned char* oracle = sqlite3_column_text(stmt, 12);
    row["oracle_text"] = oracle ? (const char*)oracle : "";
    const char* extra_cols[] = {"localized_type","image_url","price_usd","scryfall_id","oracle_id","collector_number"};
    for (int i=0;i<6;++i) {
        const unsigned char* txt = sqlite3_column_text(stmt, i+13);
        row[extra_cols[i]] = txt ? (const char*)txt : "";
    }
        callback(row);
    }
    sqlite3_finalize(stmt);
    return true;
}

bool Database::restore_snapshot(int snapshot_id) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!db) return false;
    // Find deck_id for this snapshot
    int deck_id = -1;
    const char* q = "SELECT deck_id FROM deck_snapshots WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, q, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, snapshot_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) deck_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    if (deck_id == -1) return false;
    // Begin transaction
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    // Delete current cards in deck
    const char* del = "DELETE FROM cards WHERE deck_id = ?";
    sqlite3_stmt* sdel = nullptr;
    if (sqlite3_prepare_v2(db, del, -1, &sdel, nullptr) != SQLITE_OK) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    sqlite3_bind_int(sdel, 1, deck_id);
    if (sqlite3_step(sdel) != SQLITE_DONE) { sqlite3_finalize(sdel); sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    sqlite3_finalize(sdel);
    // Insert snapshot rows as new cards for this deck
    const char* ins = "INSERT INTO cards (english_name, localized_name, name, canonical_name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, image_url, added_date, price_usd, foil, sideboard, oracle_text, deck_id, scryfall_id, oracle_id, collector_number) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* pins = nullptr;
    if (sqlite3_prepare_v2(db, ins, -1, &pins, nullptr) != SQLITE_OK) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    // iterate snapshot rows
    const char* sel = "SELECT english_name, localized_name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, foil, sideboard, oracle_text, image_url, price_usd, scryfall_id, oracle_id, collector_number FROM deck_snapshot_rows WHERE snapshot_id = ?";
    sqlite3_stmt* ssel = nullptr;
    if (sqlite3_prepare_v2(db, sel, -1, &ssel, nullptr) != SQLITE_OK) { sqlite3_finalize(pins); sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    sqlite3_bind_int(ssel, 1, snapshot_id);
    while (sqlite3_step(ssel) == SQLITE_ROW) {
        const unsigned char* en = sqlite3_column_text(ssel, 0);
        const unsigned char* ln = sqlite3_column_text(ssel, 1);
        const unsigned char* ty = sqlite3_column_text(ssel, 2);
        const unsigned char* lty = sqlite3_column_text(ssel, 3);
        const unsigned char* cols = sqlite3_column_text(ssel, 4);
        const unsigned char* setc = sqlite3_column_text(ssel, 5);
        const unsigned char* mana = sqlite3_column_text(ssel, 6);
        const unsigned char* rar = sqlite3_column_text(ssel, 7);
        int qty = sqlite3_column_int(ssel, 8);
        int foil = sqlite3_column_int(ssel, 9);
    int side = sqlite3_column_int(ssel, 10);
    const unsigned char* oracle = sqlite3_column_text(ssel, 11);
    const unsigned char* imgurl = sqlite3_column_text(ssel, 12);
    const unsigned char* price = sqlite3_column_text(ssel, 13);
    const unsigned char* sfid = sqlite3_column_text(ssel, 14);
    const unsigned char* orid = sqlite3_column_text(ssel, 15);
    const unsigned char* colnum = sqlite3_column_text(ssel, 16);
    const char* en_s = en ? reinterpret_cast<const char*>(en) : "";
    const char* ln_s = ln ? reinterpret_cast<const char*>(ln) : "";
    const char* ty_s = ty ? reinterpret_cast<const char*>(ty) : "";
    const char* lty_s = lty ? reinterpret_cast<const char*>(lty) : "";
    const char* cols_s = cols ? reinterpret_cast<const char*>(cols) : "";
    const char* setc_s = setc ? reinterpret_cast<const char*>(setc) : "";
    const char* mana_s = mana ? reinterpret_cast<const char*>(mana) : "";
    const char* rar_s = rar ? reinterpret_cast<const char*>(rar) : "";
    sqlite3_bind_text(pins, 1, en_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 2, ln_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 3, ln_s, -1, SQLITE_TRANSIENT);
    // compute canonical_name from english/localized/name (coalesce)
    std::string base_name2 = en_s && strlen(en_s) ? std::string(en_s) : (ln_s && strlen(ln_s) ? std::string(ln_s) : std::string());
    auto trim2 = [](std::string s) {
        size_t start = 0;
        while (start < s.size() && isspace((unsigned char)s[start])) ++start;
        size_t end = s.size();
        while (end > start && isspace((unsigned char)s[end-1])) --end;
        return s.substr(start, end - start);
    };
    std::string canonical2 = trim2(base_name2);
    for (auto &c : canonical2) c = (char)tolower((unsigned char)c);
    sqlite3_bind_text(pins, 4, canonical2.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 5, ty_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 6, lty_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 7, cols_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 8, setc_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 9, mana_s, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(pins, 10, rar_s, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pins, 11, qty);
        sqlite3_bind_text(pins, 12, imgurl ? reinterpret_cast<const char*>(imgurl) : "", -1, SQLITE_TRANSIENT);
        char timebuf2[32] = {0}; time_t now = time(nullptr); struct tm ltm{}; localtime_r(&now, &ltm); strftime(timebuf2, sizeof(timebuf2), "%Y-%m-%dT%H:%M:%S", &ltm);
        sqlite3_bind_text(pins, 13, timebuf2, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(pins, 14, price ? reinterpret_cast<const char*>(price) : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pins, 15, foil);
    sqlite3_bind_int(pins, 16, side);
    const char* oracle_s = oracle ? reinterpret_cast<const char*>(oracle) : "";
    sqlite3_bind_text(pins, 17, oracle_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(pins, 18, deck_id);
    if (sfid && strlen(reinterpret_cast<const char*>(sfid))) {
        sqlite3_bind_text(pins, 19, reinterpret_cast<const char*>(sfid), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(pins, 19);
    }
    if (orid && strlen(reinterpret_cast<const char*>(orid))) {
        sqlite3_bind_text(pins, 20, reinterpret_cast<const char*>(orid), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(pins, 20);
    }
    if (colnum && strlen(reinterpret_cast<const char*>(colnum))) {
        sqlite3_bind_text(pins, 21, reinterpret_cast<const char*>(colnum), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(pins, 21);
    }
        int rc = sqlite3_step(pins);
        sqlite3_reset(pins);
        if (rc != SQLITE_DONE) { sqlite3_finalize(pins); sqlite3_finalize(ssel); sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    }
    sqlite3_finalize(pins);
    sqlite3_finalize(ssel);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    return true;
}

bool Database::delete_snapshot(int snapshot_id) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!db) return false;
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    const char* del_rows = "DELETE FROM deck_snapshot_rows WHERE snapshot_id = ?";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, del_rows, -1, &s, nullptr) != SQLITE_OK) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    sqlite3_bind_int(s, 1, snapshot_id);
    if (sqlite3_step(s) != SQLITE_DONE) { sqlite3_finalize(s); sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    sqlite3_finalize(s);
    const char* del_snap = "DELETE FROM deck_snapshots WHERE id = ?";
    if (sqlite3_prepare_v2(db, del_snap, -1, &s, nullptr) != SQLITE_OK) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    sqlite3_bind_int(s, 1, snapshot_id);
    if (sqlite3_step(s) != SQLITE_DONE) { sqlite3_finalize(s); sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    sqlite3_finalize(s);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    return true;
}

bool Database::add_tag_to_card(int card_id, const std::string& tag) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!db) return false;
    const char* sql = "INSERT INTO card_tags (card_id, tag) VALUES (?, ?)";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(s, 1, card_id);
    sqlite3_bind_text(s, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE;
}

bool Database::remove_tag_from_card(int card_id, const std::string& tag) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!db) return false;
    const char* sql = "DELETE FROM card_tags WHERE card_id = ? AND tag = ?";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(s, 1, card_id);
    sqlite3_bind_text(s, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE;
}

bool Database::get_tags_for_card(int card_id, const std::function<void(const std::string&)>& callback) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!db) return false;
    const char* sql = "SELECT tag FROM card_tags WHERE card_id = ?";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(s, 1, card_id);
    while (sqlite3_step(s) == SQLITE_ROW) {
        const unsigned char* t = sqlite3_column_text(s, 0);
        callback(t ? (const char*)t : "");
    }
    sqlite3_finalize(s);
    return true;
}

bool Database::add_note_to_card(int card_id, const std::string& note) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!db) return false;
    // Replace existing note
    const char* del = "DELETE FROM card_notes WHERE card_id = ?";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, del, -1, &s, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(s, 1, card_id);
        sqlite3_step(s);
        sqlite3_finalize(s);
    }
    const char* ins = "INSERT INTO card_notes (card_id, note) VALUES (?, ?)";
    if (sqlite3_prepare_v2(db, ins, -1, &s, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(s, 1, card_id);
    sqlite3_bind_text(s, 2, note.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE;
}

bool Database::get_note_for_card(int card_id, std::string& out_note) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!db) return false;
    const char* sql = "SELECT note FROM card_notes WHERE card_id = ? LIMIT 1";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(s, 1, card_id);
    if (sqlite3_step(s) == SQLITE_ROW) {
        const unsigned char* n = sqlite3_column_text(s, 0);
        out_note = n ? (const char*)n : "";
        sqlite3_finalize(s);
        return true;
    }
    sqlite3_finalize(s);
    return false;
}

bool Database::add_tag_to_deck(int deck_id, const std::string& tag) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!db) return false;
    const char* sql = "INSERT INTO deck_tags (deck_id, tag) VALUES (?, ?)";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(s, 1, deck_id);
    sqlite3_bind_text(s, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE;
}

bool Database::get_tags_for_deck(int deck_id, const std::function<void(const std::string&)>& callback) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!db) return false;
    const char* sql = "SELECT tag FROM deck_tags WHERE deck_id = ?";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(s, 1, deck_id);
    while (sqlite3_step(s) == SQLITE_ROW) {
        const unsigned char* t = sqlite3_column_text(s, 0);
        callback(t ? (const char*)t : "");
    }
    sqlite3_finalize(s);
    return true;
}

bool Database::search_fulltext(const std::string& query, const std::function<void(const std::map<std::string,std::string>&)>& callback) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!db) return false;
    // Check if cards_fts exists
    bool has_fts = false;
    sqlite3_stmt* s = nullptr;
    const char* q = "SELECT name FROM sqlite_master WHERE type='table' AND name='cards_fts'";
    if (sqlite3_prepare_v2(db, q, -1, &s, nullptr) == SQLITE_OK) {
        if (sqlite3_step(s) == SQLITE_ROW) has_fts = true;
        sqlite3_finalize(s);
    }
    if (has_fts) {
        // Use MATCH
        std::string sql = "SELECT rowid AS id, name, type, set_code FROM cards_fts WHERE cards_fts MATCH ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::map<std::string,std::string> row;
            row["id"] = std::to_string(sqlite3_column_int(stmt, 0));
            const unsigned char* nm = sqlite3_column_text(stmt, 1);
            const unsigned char* ty = sqlite3_column_text(stmt, 2);
            const unsigned char* sc = sqlite3_column_text(stmt, 3);
            row["name"] = nm ? (const char*)nm : "";
            row["type"] = ty ? (const char*)ty : "";
            row["set_code"] = sc ? (const char*)sc : "";
            callback(row);
        }
        sqlite3_finalize(stmt);
        return true;
    } else {
        // Fallback to LIKE on english_name/localized_name/name
        std::string sql = "SELECT id, COALESCE(english_name, localized_name, name) AS name, type, set_code FROM cards WHERE LOWER(COALESCE(english_name, localized_name, name)) LIKE ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
        std::string param = "%" + query + "%";
        // lowercase the param
        for (auto &c : param) c = tolower((unsigned char)c);
        sqlite3_bind_text(stmt, 1, param.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::map<std::string,std::string> row;
            row["id"] = std::to_string(sqlite3_column_int(stmt, 0));
            const unsigned char* nm = sqlite3_column_text(stmt, 1);
            const unsigned char* ty = sqlite3_column_text(stmt, 2);
            const unsigned char* sc = sqlite3_column_text(stmt, 3);
            row["name"] = nm ? (const char*)nm : "";
            row["type"] = ty ? (const char*)ty : "";
            row["set_code"] = sc ? (const char*)sc : "";
            callback(row);
        }
        sqlite3_finalize(stmt);
        return true;
    }
}

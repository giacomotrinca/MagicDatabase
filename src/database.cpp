#include "database.h"
#include <sqlite3.h>
#include <iostream>
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <ctime>
#include <cstring>


#include "database.h"
#include <iostream>

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
                // Create auxiliary tables for snapshots, tags, notes and full-text search (if not present)
                // deck_snapshots
                const char* create_snapshots = "CREATE TABLE IF NOT EXISTS deck_snapshots (id INTEGER PRIMARY KEY, deck_id INTEGER, name TEXT, created_at TEXT)";
                char* errx = nullptr;
                if (sqlite3_exec(db, create_snapshots, nullptr, nullptr, &errx) != SQLITE_OK) {
                    std::cerr << "Errore creazione tabella deck_snapshots: " << (errx ? errx : (char*)"unknown") << std::endl;
                    if (errx) sqlite3_free(errx);
                }
                const char* create_snapshot_rows = "CREATE TABLE IF NOT EXISTS deck_snapshot_rows (id INTEGER PRIMARY KEY, snapshot_id INTEGER, original_card_id INTEGER, english_name TEXT, localized_name TEXT, type TEXT, colors TEXT, set_code TEXT, mana_cost TEXT, rarity TEXT, quantity INTEGER, foil INTEGER, sideboard INTEGER)";
                errx = nullptr;
                if (sqlite3_exec(db, create_snapshot_rows, nullptr, nullptr, &errx) != SQLITE_OK) {
                    std::cerr << "Errore creazione tabella deck_snapshot_rows: " << (errx ? errx : (char*)"unknown") << std::endl;
                    if (errx) sqlite3_free(errx);
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

bool Database::execute(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Errore SQL: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool Database::insert_card(const std::string& english_name, const std::string& localized_name, const std::string& type, const std::string& localized_type, const std::string& colors, const std::string& set_code, const std::string& mana_cost, const std::string& rarity, int quantity, const std::string& image_url, const std::string& price_usd, int deck_id, int foil, int sideboard) {
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
        rc = sqlite3_step(update_stmt);
        sqlite3_finalize(update_stmt);
        if (rc != SQLITE_DONE) {
            std::cerr << "Errore update: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        std::cout << "Updated card " << english_name << " quantity to " << (existing_qty + quantity) << std::endl;
        return true;
    } else {
        sqlite3_finalize(check_stmt);
        // Non esiste, inserisci nuova (imposta added_date alla data/ora corrente in formato ISO)
        const char* insert_sql = "INSERT INTO cards (english_name, localized_name, name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, image_url, added_date, price_usd, foil, sideboard, deck_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
        sqlite3_stmt* insert_stmt;
        if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Errore prepare insert: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
    sqlite3_bind_text(insert_stmt, 1, english_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 2, localized_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 3, localized_name.c_str(), -1, SQLITE_TRANSIENT); // name = localized for backward
    sqlite3_bind_text(insert_stmt, 4, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 5, localized_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 6, colors.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 7, set_code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 8, mana_cost.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 9, rarity.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_stmt, 10, quantity);
    sqlite3_bind_text(insert_stmt, 11, image_url.c_str(), -1, SQLITE_TRANSIENT);
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
        sqlite3_bind_text(insert_stmt, 12, timebuf, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 13, price_usd.c_str(), -1, SQLITE_TRANSIENT);
        // bind foil
        sqlite3_bind_int(insert_stmt, 14, foil);
        // bind sideboard
        sqlite3_bind_int(insert_stmt, 15, sideboard);
        if (deck_id != -1) {
            sqlite3_bind_int(insert_stmt, 16, deck_id);
        } else {
            sqlite3_bind_null(insert_stmt, 16);
        }
        rc = sqlite3_step(insert_stmt);
        sqlite3_finalize(insert_stmt);
        if (rc != SQLITE_DONE) {
            std::cerr << "Errore insert: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
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

bool Database::update_card_info(int id, const std::string& english_name, const std::string& localized_name, const std::string& type, const std::string& localized_type, const std::string& colors, const std::string& mana_cost, const std::string& rarity, const std::string& image_url, const std::string& price_usd) {
    const char* sql = "UPDATE cards SET english_name = ?, localized_name = ?, name = ?, type = ?, localized_type = ?, colors = ?, mana_cost = ?, rarity = ?, image_url = ?, price_usd = ? WHERE id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Errore prepare update_card_info: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, english_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, localized_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, localized_name.c_str(), -1, SQLITE_STATIC); // name = localized
    sqlite3_bind_text(stmt, 4, type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, localized_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, colors.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, mana_cost.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, rarity.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, image_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, price_usd.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 11, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Errore update_card_info: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool Database::create_deck(const std::string& name) {
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
    const char* sel = "SELECT id, english_name, localized_name, type, colors, set_code, mana_cost, rarity, quantity, foil, sideboard FROM cards WHERE deck_id = ?";
    sqlite3_stmt* psel = nullptr;
    if (sqlite3_prepare_v2(db, sel, -1, &psel, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_int(psel, 1, deck_id);
    const char* ins_row = "INSERT INTO deck_snapshot_rows (snapshot_id, original_card_id, english_name, localized_name, type, colors, set_code, mana_cost, rarity, quantity, foil, sideboard) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
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
        const unsigned char* cols = sqlite3_column_text(psel, 4);
        const unsigned char* setc = sqlite3_column_text(psel, 5);
        const unsigned char* mana = sqlite3_column_text(psel, 6);
        const unsigned char* rar = sqlite3_column_text(psel, 7);
        int qty = sqlite3_column_int(psel, 8);
        int foil = sqlite3_column_int(psel, 9);
        int side = sqlite3_column_int(psel, 10);
    sqlite3_bind_int(pins, 1, snap_id);
    sqlite3_bind_int(pins, 2, cid);
    const char* en_s = en ? reinterpret_cast<const char*>(en) : "";
    const char* ln_s = ln ? reinterpret_cast<const char*>(ln) : "";
    const char* ty_s = ty ? reinterpret_cast<const char*>(ty) : "";
    const char* cols_s = cols ? reinterpret_cast<const char*>(cols) : "";
    const char* setc_s = setc ? reinterpret_cast<const char*>(setc) : "";
    const char* mana_s = mana ? reinterpret_cast<const char*>(mana) : "";
    const char* rar_s = rar ? reinterpret_cast<const char*>(rar) : "";
    sqlite3_bind_text(pins, 3, en_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 4, ln_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 5, ty_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 6, cols_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 7, setc_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 8, mana_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 9, rar_s, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pins, 10, qty);
        sqlite3_bind_int(pins, 11, foil);
        sqlite3_bind_int(pins, 12, side);
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
    const char* sql = "SELECT id, original_card_id, english_name, localized_name, type, colors, set_code, mana_cost, rarity, quantity, foil, sideboard FROM deck_snapshot_rows WHERE snapshot_id = ?";
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
        callback(row);
    }
    sqlite3_finalize(stmt);
    return true;
}

bool Database::restore_snapshot(int snapshot_id) {
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
    const char* ins = "INSERT INTO cards (english_name, localized_name, name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, image_url, added_date, price_usd, foil, sideboard, deck_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* pins = nullptr;
    if (sqlite3_prepare_v2(db, ins, -1, &pins, nullptr) != SQLITE_OK) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    // iterate snapshot rows
    const char* sel = "SELECT english_name, localized_name, type, colors, set_code, mana_cost, rarity, quantity, foil, sideboard FROM deck_snapshot_rows WHERE snapshot_id = ?";
    sqlite3_stmt* ssel = nullptr;
    if (sqlite3_prepare_v2(db, sel, -1, &ssel, nullptr) != SQLITE_OK) { sqlite3_finalize(pins); sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); return false; }
    sqlite3_bind_int(ssel, 1, snapshot_id);
    while (sqlite3_step(ssel) == SQLITE_ROW) {
        const unsigned char* en = sqlite3_column_text(ssel, 0);
        const unsigned char* ln = sqlite3_column_text(ssel, 1);
        const unsigned char* ty = sqlite3_column_text(ssel, 2);
        const unsigned char* cols = sqlite3_column_text(ssel, 3);
        const unsigned char* setc = sqlite3_column_text(ssel, 4);
        const unsigned char* mana = sqlite3_column_text(ssel, 5);
        const unsigned char* rar = sqlite3_column_text(ssel, 6);
        int qty = sqlite3_column_int(ssel, 7);
        int foil = sqlite3_column_int(ssel, 8);
        int side = sqlite3_column_int(ssel, 9);
    const char* en_s = en ? reinterpret_cast<const char*>(en) : "";
    const char* ln_s = ln ? reinterpret_cast<const char*>(ln) : "";
    const char* ty_s = ty ? reinterpret_cast<const char*>(ty) : "";
    const char* cols_s = cols ? reinterpret_cast<const char*>(cols) : "";
    const char* setc_s = setc ? reinterpret_cast<const char*>(setc) : "";
    const char* mana_s = mana ? reinterpret_cast<const char*>(mana) : "";
    const char* rar_s = rar ? reinterpret_cast<const char*>(rar) : "";
    sqlite3_bind_text(pins, 1, en_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 2, ln_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 3, ln_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 4, ty_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 5, ty_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 6, cols_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 7, setc_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 8, mana_s, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pins, 9, rar_s, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pins, 10, qty);
        sqlite3_bind_text(pins, 11, "", -1, SQLITE_TRANSIENT);
        char timebuf2[32] = {0}; time_t now = time(nullptr); struct tm ltm{}; localtime_r(&now, &ltm); strftime(timebuf2, sizeof(timebuf2), "%Y-%m-%dT%H:%M:%S", &ltm);
        sqlite3_bind_text(pins, 12, timebuf2, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(pins, 13, "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pins, 14, foil);
        sqlite3_bind_int(pins, 15, side);
        sqlite3_bind_int(pins, 16, deck_id);
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

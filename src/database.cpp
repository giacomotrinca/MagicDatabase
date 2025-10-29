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

bool Database::insert_card(const std::string& english_name, const std::string& localized_name, const std::string& type, const std::string& localized_type, const std::string& colors, const std::string& set_code, const std::string& mana_cost, const std::string& rarity, int quantity, const std::string& image_url, const std::string& price_usd, int deck_id, int foil) {
    // Prima controlla se la carta esiste già (stesso english_name, set_code, foil e nello stesso deck se specificato)
    sqlite3_stmt* check_stmt;
    if (deck_id != -1) {
        const char* check_sql = "SELECT id, quantity FROM cards WHERE english_name = ? AND set_code = ? AND foil = ? AND deck_id = ?";
        if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Errore prepare check: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        sqlite3_bind_text(check_stmt, 1, english_name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(check_stmt, 2, set_code.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(check_stmt, 3, foil);
        sqlite3_bind_int(check_stmt, 4, deck_id);
    } else {
        const char* check_sql = "SELECT id, quantity FROM cards WHERE english_name = ? AND set_code = ? AND foil = ? AND deck_id IS NULL";
        if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Errore prepare check: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        sqlite3_bind_text(check_stmt, 1, english_name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(check_stmt, 2, set_code.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(check_stmt, 3, foil);
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
        const char* insert_sql = "INSERT INTO cards (english_name, localized_name, name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, image_url, added_date, price_usd, foil, deck_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
        sqlite3_stmt* insert_stmt;
        if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Errore prepare insert: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        sqlite3_bind_text(insert_stmt, 1, english_name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 2, localized_name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 3, localized_name.c_str(), -1, SQLITE_STATIC); // name = localized for backward
        sqlite3_bind_text(insert_stmt, 4, type.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 5, localized_type.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 6, colors.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 7, set_code.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 8, mana_cost.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 9, rarity.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(insert_stmt, 10, quantity);
        sqlite3_bind_text(insert_stmt, 11, image_url.c_str(), -1, SQLITE_STATIC);
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
        sqlite3_bind_text(insert_stmt, 13, price_usd.c_str(), -1, SQLITE_STATIC);
        // bind foil
        sqlite3_bind_int(insert_stmt, 14, foil);
        if (deck_id != -1) {
            sqlite3_bind_int(insert_stmt, 15, deck_id);
        } else {
            sqlite3_bind_null(insert_stmt, 15);
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

bool Database::set_card_deck(int card_id, int deck_id) {
    const char* sql = "UPDATE cards SET deck_id = ? WHERE id = ?";
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
    sqlite3_bind_int(stmt, 2, card_id);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Errore set_card_deck: " << sqlite3_errmsg(db) << " (rc=" << rc << ")" << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    std::cout << "DEBUG: set_card_deck succeeded for card_id=" << card_id << " deck_id=" << (deck_id == -1 ? -1 : deck_id) << std::endl;
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

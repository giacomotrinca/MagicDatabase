#include "database.h"
#include <sqlite3.h>
#include <iostream>
#include <functional>
#include <map>
#include <vector>
#include <string>


#include "database.h"
#include <iostream>

Database::Database(const std::string& db_path) : db(nullptr) {
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Impossibile aprire il database: " << sqlite3_errmsg(db) << std::endl;
        db = nullptr;
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

bool Database::insert_card(const std::string& name, const std::string& type, const std::string& colors, const std::string& set_code, const std::string& mana_cost, const std::string& rarity, int quantity, const std::string& image_url) {
    // Prima controlla se la carta esiste già (stesso name e set_code)
    const char* check_sql = "SELECT id, quantity FROM cards WHERE name = ? AND set_code = ?";
    sqlite3_stmt* check_stmt;
    if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Errore prepare check: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(check_stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(check_stmt, 2, set_code.c_str(), -1, SQLITE_STATIC);
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
        std::cout << "Updated card " << name << " quantity to " << (existing_qty + quantity) << std::endl;
        return true;
    } else {
        sqlite3_finalize(check_stmt);
        // Non esiste, inserisci nuova
        const char* insert_sql = "INSERT INTO cards (name, type, colors, set_code, mana_cost, rarity, quantity, image_url) VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
        sqlite3_stmt* insert_stmt;
        if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Errore prepare insert: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        sqlite3_bind_text(insert_stmt, 1, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 2, type.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 3, colors.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 4, set_code.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 5, mana_cost.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 6, rarity.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(insert_stmt, 7, quantity);
        sqlite3_bind_text(insert_stmt, 8, image_url.c_str(), -1, SQLITE_STATIC);
        rc = sqlite3_step(insert_stmt);
        sqlite3_finalize(insert_stmt);
        if (rc != SQLITE_DONE) {
            std::cerr << "Errore insert: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        std::cout << "Inserted new card " << name << std::endl;
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

bool Database::query(const std::string& sql, const std::function<void(const std::map<std::string, std::string>&)>& callback) {
    if (!db) return false;
    struct CallbackData {
        std::function<void(const std::map<std::string, std::string>&)>* cb;
        std::vector<std::string> colNames;
    } data;
    data.cb = const_cast<std::function<void(const std::map<std::string, std::string>&)>*>(&callback);
    auto rowCallback = [](void* userData, int argc, char** argv, char** azColName) -> int {
        CallbackData* d = static_cast<CallbackData*>(userData);
        std::map<std::string, std::string> row;
        for (int i = 0; i < argc; ++i) {
            row[azColName[i] ? azColName[i] : ""] = argv[i] ? argv[i] : "";
        }
        (*d->cb)(row);
        return 0;
    };
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), rowCallback, &data, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Errore SQL: " << errMsg << std::endl;
        sqlite3_free(errMsg);
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

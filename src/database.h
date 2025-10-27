#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include <string>
#include <functional>
#include <map>

class Database {
public:
    Database(const std::string& db_path);
    ~Database();
    bool execute(const std::string& sql);
    bool insert_card(const std::string& name, const std::string& type, const std::string& colors, const std::string& set_code, const std::string& mana_cost, const std::string& rarity, int quantity, const std::string& image_url = "");
    bool delete_card(int id);
    bool update_quantity(int id, int new_quantity);
    bool get_card_quantity(int id, int& quantity);
    // Esegue una query SELECT e chiama la callback per ogni riga
    bool query(const std::string& sql, const std::function<void(const std::map<std::string, std::string>&)>& callback);
private:
    sqlite3* db;
};

#endif // DATABASE_H

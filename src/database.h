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
    // foil: 0 = non-foil, 1 = foil
    bool insert_card(const std::string& english_name, const std::string& localized_name, const std::string& type, const std::string& localized_type, const std::string& colors, const std::string& set_code, const std::string& mana_cost, const std::string& rarity, int quantity, const std::string& image_url = "", const std::string& price_usd = "", int deck_id = -1, int foil = 0);
    bool create_deck(const std::string& name);
    bool query_decks(const std::function<void(const std::map<std::string, std::string>&)>& callback);
    bool delete_card(int id);
    bool set_card_deck(int card_id, int deck_id);
    bool update_quantity(int id, int new_quantity);
    bool get_card_quantity(int id, int& quantity);
    bool update_card_info(int id, const std::string& english_name, const std::string& localized_name, const std::string& type, const std::string& localized_type, const std::string& colors, const std::string& mana_cost, const std::string& rarity, const std::string& image_url, const std::string& price_usd);
    // Esegue una query SELECT e chiama la callback per ogni riga
    bool query(const std::string& sql, const std::function<void(const std::map<std::string, std::string>&)>& callback, const std::vector<std::string>& params = {});
private:
    sqlite3* db;
};

#endif // DATABASE_H

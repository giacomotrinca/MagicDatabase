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
    bool is_open() const;
    bool execute(const std::string& sql);
    // foil: 0 = non-foil, 1 = foil; sideboard: 0 = main, 1 = sideboard
    bool insert_card(const std::string& english_name, const std::string& localized_name, const std::string& type, const std::string& localized_type, const std::string& colors, const std::string& set_code, const std::string& mana_cost, const std::string& rarity, int quantity, const std::string& image_url = "", const std::string& price_usd = "", const std::string& oracle_text = "", int deck_id = -1, int foil = 0, int sideboard = 0, const std::string& scryfall_id = "", const std::string& oracle_id = "", const std::string& collector_number = "");
    bool create_deck(const std::string& name);
    bool query_decks(const std::function<void(const std::map<std::string, std::string>&)>& callback);
    bool delete_card(int id);
    bool set_card_deck(int card_id, int deck_id, int sideboard = 0);
    bool update_quantity(int id, int new_quantity);
    bool get_card_quantity(int id, int& quantity);
    bool update_card_info(int id, const std::string& english_name, const std::string& localized_name, const std::string& type, const std::string& localized_type, const std::string& colors, const std::string& mana_cost, const std::string& rarity, const std::string& image_url, const std::string& price_usd, const std::string& oracle_text, const std::string& scryfall_id = "", const std::string& oracle_id = "", const std::string& collector_number = "");
    // Esegue una query SELECT e chiama la callback per ogni riga
    bool query(const std::string& sql, const std::function<void(const std::map<std::string, std::string>&)>& callback, const std::vector<std::string>& params = {});
    // Snapshots / versioning API
    bool create_snapshot(int deck_id, const std::string& name, int& out_snapshot_id);
    bool list_snapshots(int deck_id, const std::function<void(const std::map<std::string,std::string>&)>& callback);
    bool restore_snapshot(int snapshot_id);
    bool delete_snapshot(int snapshot_id);
    bool get_snapshot_rows(int snapshot_id, const std::function<void(const std::map<std::string,std::string>&)>& callback);

    // Tagging / notes API
    bool add_tag_to_card(int card_id, const std::string& tag);
    bool remove_tag_from_card(int card_id, const std::string& tag);
    bool get_tags_for_card(int card_id, const std::function<void(const std::string&)>& callback);
    bool add_note_to_card(int card_id, const std::string& note);
    bool get_note_for_card(int card_id, std::string& out_note);

    bool add_tag_to_deck(int deck_id, const std::string& tag);
    bool get_tags_for_deck(int deck_id, const std::function<void(const std::string&)>& callback);

    // Simple full-text search (uses FTS5 table if available, otherwise falls back to LIKE)
    bool search_fulltext(const std::string& query, const std::function<void(const std::map<std::string,std::string>&)>& callback);
private:
    sqlite3* db;
};

#endif // DATABASE_H

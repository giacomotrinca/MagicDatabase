#ifndef SCRYFALL_H
#define SCRYFALL_H
#include <string>
#include <map>
#include <vector>
#include <optional>

// Struttura per rappresentare una carta trovata
struct ScryfallCard {
    std::string english_name;
    std::string localized_name;
    std::string name; // Keep for backward compatibility, but use english/localized
    std::string type;
    std::string localized_type;
    std::string colors;
    std::string oracle_text;
    std::string set_name;
    std::string set_code;
    std::string mana_cost;
    std::string rarity;
    std::string image_url;
    std::string price_usd; // Prezzo in USD, stringa per gestire null/empty
    std::string scryfall_id;
    std::string oracle_id;
    std::string collector_number;
    std::string lang;
    bool is_exact_match;
};

// Restituisce una carta singola se trovata esattamente, oppure una lista di nomi se multiple corrispondenze
std::vector<ScryfallCard> search_cards_from_scryfall(const std::string& query);

// Fetch a single card using Scryfall's named endpoint with optional set/language hints.
std::optional<ScryfallCard> fetch_card_named_exact(const std::string& name, const std::string& set_code = "", const std::string& language = "");

// Fetch a card by its stable Scryfall UUID (optionally requesting a specific language printing).
std::optional<ScryfallCard> fetch_card_by_id(const std::string& scryfall_id, const std::string& language = "");

// Fetch a card by set code and collector number (optionally specifying language).
std::optional<ScryfallCard> fetch_card_by_set_number(const std::string& set_code, const std::string& collector_number, const std::string& language = "");

// Fetch a specific printing using the oracle id, optionally hinting language, set, and collector number.
std::optional<ScryfallCard> fetch_print(const std::string& oracle_id, const std::string& language = "", const std::string& preferred_set = "", const std::string& preferred_collector = "");

// Scarica i dati binari dell'immagine dall'URL
std::vector<unsigned char> download_image_data(const std::string& url);

// Carica i dati binari dell'immagine da file
std::vector<unsigned char> load_image_from_file(const std::string& path);

#endif // SCRYFALL_H

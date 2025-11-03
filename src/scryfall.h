#ifndef SCRYFALL_H
#define SCRYFALL_H
#include <string>
#include <map>
#include <vector>

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
    bool is_exact_match;
};

// Restituisce una carta singola se trovata esattamente, oppure una lista di nomi se multiple corrispondenze
std::vector<ScryfallCard> search_cards_from_scryfall(const std::string& query);

// Scarica i dati binari dell'immagine dall'URL
std::vector<unsigned char> download_image_data(const std::string& url);

// Carica i dati binari dell'immagine da file
std::vector<unsigned char> load_image_from_file(const std::string& path);

#endif // SCRYFALL_H

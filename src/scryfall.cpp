#include "scryfall.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iostream>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static size_t WriteDataCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::vector<unsigned char>* data = (std::vector<unsigned char>*)userp;
    size_t total_size = size * nmemb;
    data->insert(data->end(), (unsigned char*)contents, (unsigned char*)contents + total_size);
    return total_size;
}

// Funzione helper per cercare carte
static std::vector<ScryfallCard> perform_search(const std::string& query, bool require_italian = true) {
    std::vector<ScryfallCard> results;
    
    // Costruisci la query per Scryfall
    std::string search_query = "name:" + query;
    if (require_italian) {
        search_query += " lang:it";
    }
    
    char* escaped = curl_easy_escape(nullptr, search_query.c_str(), search_query.length());
    std::string url = "https://api.scryfall.com/cards/search?q=" + std::string(escaped);
    curl_free(escaped);
    
    std::cout << "Scryfall search URL: " << url << std::endl;
    
    CURL* curl = curl_easy_init();
    std::string readBuffer;
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "MagicDatabase/1.0");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        CURLcode res = curl_easy_perform(curl);
        std::cout << "CURL result: " << res << std::endl;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (res == CURLE_OK) {
            std::cout << "Response length: " << readBuffer.length() << std::endl;
            try {
                auto j = json::parse(readBuffer);
                if (j.contains("data") && j["data"].is_array()) {
                    auto& data = j["data"];
                    std::cout << "Found " << data.size() << " cards" << std::endl;
                    
                    for (const auto& card : data) {
                        ScryfallCard result;
                        result.is_exact_match = false;
                        
                        // Controlla se è una corrispondenza esatta
                        std::string card_name = card.value("name", "");
                        std::transform(card_name.begin(), card_name.end(), card_name.begin(), ::tolower);
                        std::string query_lower = query;
                        std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(), ::tolower);
                        
                        if (card_name.find(query_lower) != std::string::npos) {
                            result.is_exact_match = true;
                        }
                        
                        // Se richiede italiano, usa i campi italiani
                        if (require_italian) {
                            result.name = card.value("printed_name", card.value("name", ""));
                            result.type = card.value("printed_type_line", card.value("type_line", ""));
                            result.oracle_text = card.value("printed_text", card.value("oracle_text", ""));
                        } else {
                            result.name = card.value("name", "");
                            result.type = card.value("type_line", "");
                            result.oracle_text = card.value("oracle_text", "");
                        }
                        
                        result.colors = card.value("colors", json::array()).dump();
                        result.set_name = card.value("set_name", "");
                        result.mana_cost = card.value("mana_cost", "");
                        result.rarity = card.value("rarity", "");
                        result.image_url = card.value("image_uris", json::object()).value("normal", "");
                        
                        results.push_back(result);
                        
                        // Limita a 10 risultati per non sovraccaricare
                        if (results.size() >= 10) break;
                    }
                } else if (j.contains("object") && j["object"] == "error") {
                    std::cout << "Scryfall error: " << j.value("details", "") << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "JSON parse error: " << e.what() << std::endl;
            }
        } else {
            std::cout << "CURL error: " << curl_easy_strerror(res) << std::endl;
        }
    }
    return results;
}

std::vector<ScryfallCard> search_cards_from_scryfall(const std::string& query) {
    std::vector<ScryfallCard> results;
    
    // Prima cerca in italiano
    results = perform_search(query, true);
    
    // Se non trova risultati italiani, cerca in inglese
    if (results.empty()) {
        std::cout << "No Italian results, searching in English..." << std::endl;
        results = perform_search(query, false);
        
        // Per i risultati inglesi, cerca se esiste una versione italiana
        for (auto& card : results) {
            if (!card.is_exact_match) continue;
            
            // Cerca la versione italiana di questa carta
            std::string italian_query = "name:" + card.name + " lang:it";
            char* escaped = curl_easy_escape(nullptr, italian_query.c_str(), italian_query.length());
            std::string url = "https://api.scryfall.com/cards/search?q=" + std::string(escaped);
            curl_free(escaped);
            
            CURL* curl = curl_easy_init();
            std::string buffer;
            if (curl) {
                struct curl_slist *headers = NULL;
                headers = curl_slist_append(headers, "Accept: application/json");
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "MagicDatabase/1.0");
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
                CURLcode res = curl_easy_perform(curl);
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                
                if (res == CURLE_OK) {
                    try {
                        auto j = json::parse(buffer);
                        if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
                            auto& italian_card = j["data"][0];
                            card.name = italian_card.value("printed_name", card.name);
                            card.type = italian_card.value("printed_type_line", card.type);
                            card.oracle_text = italian_card.value("printed_text", card.oracle_text);
                        }
                    } catch (...) {
                        // Ignora errori, mantieni la versione inglese
                    }
                }
            }
        }
    }
    
    return results;
}

std::vector<unsigned char> download_image_data(const std::string& url) {
    std::vector<unsigned char> image_data;
    
    CURL* curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "User-Agent: MagicDatabase/1.0");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDataCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &image_data);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Segui redirect
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            std::cout << "CURL download error: " << curl_easy_strerror(res) << std::endl;
            image_data.clear();
        }
    }
    return image_data;
}

std::vector<unsigned char> load_image_from_file(const std::string& path) {
    std::vector<unsigned char> data;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (file) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        data.resize(size);
        file.read((char*)data.data(), size);
    }
    return data;
}

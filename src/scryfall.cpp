#include "scryfall.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <cctype>

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

namespace {

constexpr std::chrono::minutes kCacheTtl{10};
constexpr size_t kMaxResults = 50;

struct CurlHandle {
    CurlHandle() : handle(curl_easy_init()) {}
    ~CurlHandle() {
        if (handle) {
            curl_easy_cleanup(handle);
        }
    }
    CURL* get() const { return handle; }
    explicit operator bool() const { return handle != nullptr; }

private:
    CURL* handle;
};

struct CurlHeaders {
    ~CurlHeaders() {
        if (list) {
            curl_slist_free_all(list);
        }
    }

    void append(const std::string& header) {
        list = curl_slist_append(list, header.c_str());
    }

    struct curl_slist* get() const { return list; }

private:
    struct curl_slist* list = nullptr;
};

using CacheEntry = std::pair<std::chrono::steady_clock::time_point, std::vector<ScryfallCard>>;
static std::mutex g_cache_mutex;
static std::unordered_map<std::string, CacheEntry> g_cache;

static std::string make_cache_key(const std::string& query, bool require_italian) {
    return query + (require_italian ? "|it" : "|any");
}

static std::string sanitize_query(const std::string& query) {
    std::string trimmed = query;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), trimmed.end());
    return trimmed;
}

static bool perform_json_request(const std::string& url, std::string& out_buffer) {
    CurlHandle curl;
    if (!curl) {
        std::cout << "Failed to initialize CURL" << std::endl;
        return false;
    }

    CurlHeaders headers;
    headers.append("Accept: application/json");

    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "MagicDatabase/1.0");
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &out_buffer);

    CURLcode res = curl_easy_perform(curl.get());
    std::cout << "CURL result: " << res << std::endl;
    if (res != CURLE_OK) {
        std::cout << "CURL error: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    std::cout << "Response length: " << out_buffer.length() << std::endl;
    return true;
}

static bool parse_json(const std::string& payload, json& out_json) {
    try {
        out_json = json::parse(payload);
        return true;
    } catch (const std::exception& e) {
        std::cout << "JSON parse error: " << e.what() << std::endl;
        return false;
    }
}

static void store_in_cache(const std::string& key, const std::vector<ScryfallCard>& cards) {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    g_cache[key] = {std::chrono::steady_clock::now(), cards};
}

static bool try_cache_lookup(const std::string& key, std::vector<ScryfallCard>& out_cards) {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    auto it = g_cache.find(key);
    if (it == g_cache.end()) {
        return false;
    }
    auto age = std::chrono::steady_clock::now() - it->second.first;
    if (age > kCacheTtl) {
        g_cache.erase(it);
        return false;
    }
    out_cards = it->second.second;
    return true;
}

} // namespace

// Funzione helper per cercare carte
static std::vector<ScryfallCard> perform_search(const std::string& query, bool require_italian = true) {
    std::vector<ScryfallCard> results;

    const std::string trimmed_query = sanitize_query(query);
    if (trimmed_query.empty()) {
        return results;
    }

    const std::string cache_key = make_cache_key(trimmed_query, require_italian);
    if (try_cache_lookup(cache_key, results)) {
        std::cout << "Scryfall cache hit: " << cache_key << std::endl;
        return results;
    }

    std::string search_query = "name:" + trimmed_query;
    if (require_italian) {
        search_query += " lang:it";
    }

    char* escaped = curl_easy_escape(nullptr, search_query.c_str(), static_cast<int>(search_query.length()));
    std::string url = "https://api.scryfall.com/cards/search?q=" + std::string(escaped ? escaped : "");
    if (escaped) {
        curl_free(escaped);
    }

    std::cout << "Scryfall search URL: " << url << std::endl;

    std::string read_buffer;
    if (!perform_json_request(url, read_buffer)) {
        store_in_cache(cache_key, results);
        return results;
    }

    json payload;
    if (!parse_json(read_buffer, payload)) {
        store_in_cache(cache_key, results);
        return results;
    }

    if (payload.contains("data") && payload["data"].is_array()) {
        const auto& data = payload["data"];
        std::cout << "Found " << data.size() << " cards" << std::endl;

        std::string query_lower = trimmed_query;
        std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(), ::tolower);

        for (const auto& card : data) {
            ScryfallCard result;
            result.is_exact_match = false;

            std::string card_name = card.value("name", "");
            std::transform(card_name.begin(), card_name.end(), card_name.begin(), ::tolower);

            if (card_name.find(query_lower) != std::string::npos) {
                result.is_exact_match = true;
            }

            if (require_italian) {
                result.english_name = card.value("name", "");
                result.localized_name = card.value("printed_name", card.value("name", ""));
                result.name = result.localized_name;
                result.type = card.value("type_line", "");
                result.localized_type = card.value("printed_type_line", card.value("type_line", ""));
                result.oracle_text = card.value("printed_text", card.value("oracle_text", ""));
            } else {
                result.english_name = card.value("name", "");
                result.localized_name = card.value("printed_name", card.value("name", ""));
                result.name = result.english_name;
                result.type = card.value("type_line", "");
                result.localized_type = card.value("printed_type_line", card.value("type_line", ""));
                result.oracle_text = card.value("oracle_text", "");
            }

            auto colors_json = card.value("colors", json::array());
            if (colors_json.is_null()) {
                result.colors = "[]";
            } else {
                result.colors = colors_json.dump();
            }

            result.set_name = card.value("set_name", "");
            result.mana_cost = card.value("mana_cost", "");
            result.rarity = card.value("rarity", "");

            auto image_uris = card.value("image_uris", json::object());
            if (!image_uris.is_null()) {
                result.image_url = image_uris.value("normal", "");
            } else {
                result.image_url = "";
            }

            auto prices = card.value("prices", json::object());
            if (!prices.is_null() && prices.contains("usd") && !prices["usd"].is_null()) {
                result.price_usd = prices["usd"];
            } else {
                result.price_usd = "";
            }

            std::cout << "Parsed card: " << result.name << ", price_usd: '" << result.price_usd << "'" << std::endl;

            results.push_back(result);
            if (results.size() >= kMaxResults) {
                break;
            }
        }
    } else if (payload.contains("object") && payload["object"] == "error") {
        std::cout << "Scryfall error: " << payload.value("details", "") << std::endl;
    }

    store_in_cache(cache_key, results);
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
            auto italian_matches = perform_search(card.name, true);
            const auto preferred = std::find_if(italian_matches.begin(), italian_matches.end(), [](const ScryfallCard& c) {
                return c.is_exact_match;
            });

            const ScryfallCard* italian_card = nullptr;
            if (preferred != italian_matches.end()) {
                italian_card = &(*preferred);
            } else if (!italian_matches.empty()) {
                italian_card = &italian_matches.front();
            }

            if (italian_card) {
                card.english_name = card.name;
                card.localized_name = italian_card->localized_name;
                card.name = card.localized_name;
                card.type = italian_card->type;
                card.localized_type = italian_card->localized_type;
                card.oracle_text = italian_card->oracle_text;
            }
        }
    }
    
    return results;
}

std::vector<unsigned char> download_image_data(const std::string& url) {
    std::vector<unsigned char> image_data;
    if (url.empty()) {
        return image_data;
    }

    CurlHandle curl;
    if (!curl) {
        std::cout << "Failed to initialize CURL" << std::endl;
        return image_data;
    }

    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "MagicDatabase/1.0");
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteDataCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &image_data);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        std::cout << "CURL download error: " << curl_easy_strerror(res) << std::endl;
        image_data.clear();
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

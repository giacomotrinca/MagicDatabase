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
#include <optional>
#include <thread>

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

using PriceCacheEntry = std::pair<std::chrono::steady_clock::time_point, std::string>;
static std::mutex g_price_cache_mutex;
static std::unordered_map<std::string, PriceCacheEntry> g_price_cache;

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
    constexpr int kMaxAttempts = 3;
    constexpr long kConnectTimeoutSec = 10L;
    constexpr long kTransferTimeoutSec = 25L;

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        CurlHandle curl;
        if (!curl) {
            std::cout << "Failed to initialize CURL" << std::endl;
            return false;
        }

        CurlHeaders headers;
        headers.append("Accept: application/json");

        out_buffer.clear();

        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "MagicDatabase/1.0");
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &out_buffer);
    curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "gzip,deflate");
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, kTransferTimeoutSec);
#ifdef CURLOPT_TCP_KEEPALIVE
    curl_easy_setopt(curl.get(), CURLOPT_TCP_KEEPALIVE, 1L);
#endif
#ifdef CURLOPT_TCP_KEEPIDLE
    curl_easy_setopt(curl.get(), CURLOPT_TCP_KEEPIDLE, 30L);
#endif
#ifdef CURLOPT_TCP_KEEPINTVL
    curl_easy_setopt(curl.get(), CURLOPT_TCP_KEEPINTVL, 15L);
#endif

        CURLcode res = curl_easy_perform(curl.get());
        std::cout << "CURL result: " << res << std::endl;

        if (res == CURLE_OK) {
            long response_code = 0;
            curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
            std::cout << "HTTP status: " << response_code << std::endl;
            if (response_code >= 200 && response_code < 300) {
                std::cout << "Response length: " << out_buffer.length() << std::endl;
                return true;
            }

            if (response_code == 429 || response_code >= 500) {
                if (attempt + 1 < kMaxAttempts) {
                    std::cout << "HTTP " << response_code << " received, retrying..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(200 * (attempt + 1)));
                    continue;
                }
            }

            std::cout << "HTTP error: " << response_code << std::endl;
            return false;
        }

        std::cout << "CURL error: " << curl_easy_strerror(res) << std::endl;
        if ((res == CURLE_OPERATION_TIMEDOUT || res == CURLE_COULDNT_RESOLVE_HOST || res == CURLE_COULDNT_CONNECT) && attempt + 1 < kMaxAttempts) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200 * (attempt + 1)));
            continue;
        }
        return false;
    }

    return false;
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

static std::string normalize_price_string(std::string price) {
    price.erase(price.begin(), std::find_if(price.begin(), price.end(), [](unsigned char ch){ return !std::isspace(ch); }));
    price.erase(std::find_if(price.rbegin(), price.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), price.end());
    return price;
}

static std::string pick_price_field(const json& prices, const std::initializer_list<const char*>& keys) {
    if (prices.is_null() || !prices.is_object()) return "";
    auto pick = [&](const char* key) -> std::string {
        if (!prices.contains(key)) return std::string();
        const json& node = prices.at(key);
        if (node.is_null()) return std::string();
        if (node.is_string()) return normalize_price_string(node.get<std::string>());
        if (node.is_number_float() || node.is_number_integer()) {
            std::ostringstream oss;
            oss.setf(std::ios::fixed, std::ios::floatfield);
            oss.precision(2);
            oss << node.get<double>();
            return oss.str();
        }
        return std::string();
    };

    for (const char* key : keys) {
        std::string value = pick(key);
        if (!value.empty()) return value;
    }
    return "";
}

static std::string select_best_price_usd(const json& prices) {
    // prefer regular USD, then foil variants. If none present, return empty string.
    return pick_price_field(prices, {"usd", "usd_foil", "usd_etched"});
}

static bool try_price_cache_lookup(const std::string& key, std::string& out_price) {
    std::lock_guard<std::mutex> lock(g_price_cache_mutex);
    auto it = g_price_cache.find(key);
    if (it == g_price_cache.end()) return false;
    auto age = std::chrono::steady_clock::now() - it->second.first;
    if (age > kCacheTtl) {
        g_price_cache.erase(it);
        return false;
    }
    out_price = it->second.second;
    return true;
}

static void store_price_cache_entry(const std::string& key, const std::string& price) {
    std::lock_guard<std::mutex> lock(g_price_cache_mutex);
    g_price_cache[key] = {std::chrono::steady_clock::now(), price};
}

static std::string fetch_price_from_named(const std::string& exact_name) {
    const std::string trimmed = sanitize_query(exact_name);
    if (trimmed.empty()) return "";
    std::string cached;
    if (try_price_cache_lookup(trimmed, cached)) {
        return cached;
    }

    char* escaped = curl_easy_escape(nullptr, trimmed.c_str(), static_cast<int>(trimmed.length()));
    std::string url = "https://api.scryfall.com/cards/named?exact=" + std::string(escaped ? escaped : "");
    if (escaped) curl_free(escaped);

    std::string buffer;
    if (!perform_json_request(url, buffer)) {
        store_price_cache_entry(trimmed, "");
        return "";
    }

    json payload;
    if (!parse_json(buffer, payload)) {
        store_price_cache_entry(trimmed, "");
        return "";
    }

    if (payload.contains("object") && payload["object"] == "error") {
        store_price_cache_entry(trimmed, "");
        return "";
    }

    std::string price = select_best_price_usd(payload.value("prices", json::object()));
    store_price_cache_entry(trimmed, price);
    return price;
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
        return results;
    }

    json payload;
    if (!parse_json(read_buffer, payload)) {
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
            result.set_code = card.value("set", "");
            result.mana_cost = card.value("mana_cost", "");
            result.rarity = card.value("rarity", "");

            auto image_uris = card.value("image_uris", json::object());
            if (!image_uris.is_null()) {
                result.image_url = image_uris.value("normal", "");
            } else {
                result.image_url = "";
            }

            auto prices = card.value("prices", json::object());
            result.price_usd = select_best_price_usd(prices);
            if (result.price_usd.empty()) {
                result.price_usd = fetch_price_from_named(result.english_name.empty() ? card.value("name", "") : result.english_name);
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

std::optional<ScryfallCard> fetch_card_named_exact(const std::string& name, const std::string& set_code, const std::string& language) {
    const std::string trimmed = sanitize_query(name);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    char* escaped_name = curl_easy_escape(nullptr, trimmed.c_str(), static_cast<int>(trimmed.length()));
    std::string url = "https://api.scryfall.com/cards/named?exact=" + std::string(escaped_name ? escaped_name : "");
    if (escaped_name) {
        curl_free(escaped_name);
    }

    if (!set_code.empty()) {
        std::string lowered = set_code;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
        char* esc_set = curl_easy_escape(nullptr, lowered.c_str(), static_cast<int>(lowered.length()));
        url += "&set=" + std::string(esc_set ? esc_set : "");
        if (esc_set) {
            curl_free(esc_set);
        }
    }

    if (!language.empty()) {
        std::string lowered_lang = language;
        std::transform(lowered_lang.begin(), lowered_lang.end(), lowered_lang.begin(), ::tolower);
        char* esc_lang = curl_easy_escape(nullptr, lowered_lang.c_str(), static_cast<int>(lowered_lang.length()));
        url += "&lang=" + std::string(esc_lang ? esc_lang : "");
        if (esc_lang) {
            curl_free(esc_lang);
        }
    }

    std::string buffer;
    if (!perform_json_request(url, buffer)) {
        return std::nullopt;
    }

    json payload;
    if (!parse_json(buffer, payload)) {
        return std::nullopt;
    }

    const std::string object_type = payload.value("object", "");
    if (object_type == "error" || object_type != "card") {
        return std::nullopt;
    }

    ScryfallCard result;
    result.english_name = payload.value("name", "");
    result.localized_name = payload.value("printed_name", result.english_name);
    result.name = result.localized_name.empty() ? result.english_name : result.localized_name;
    result.type = payload.value("type_line", "");
    result.localized_type = payload.value("printed_type_line", result.type);
    result.oracle_text = payload.value("printed_text", payload.value("oracle_text", ""));

    auto colors_json = payload.value("colors", json::array());
    result.colors = colors_json.is_null() ? "[]" : colors_json.dump();

    result.set_name = payload.value("set_name", "");
    result.set_code = payload.value("set", "");
    result.mana_cost = payload.value("mana_cost", "");
    result.rarity = payload.value("rarity", "");

    auto image_uris = payload.value("image_uris", json::object());
    if (!image_uris.is_null()) {
        result.image_url = image_uris.value("normal", "");
    } else if (payload.contains("card_faces") && payload["card_faces"].is_array() && !payload["card_faces"].empty()) {
        const auto& face = payload["card_faces"].front();
        auto face_images = face.value("image_uris", json::object());
        if (!face_images.is_null()) {
            result.image_url = face_images.value("normal", "");
        }
        if (result.localized_name.empty()) {
            result.localized_name = face.value("printed_name", "");
        }
        if (result.localized_type.empty()) {
            result.localized_type = face.value("printed_type_line", result.localized_type);
        }
        if (result.oracle_text.empty()) {
            result.oracle_text = face.value("printed_text", face.value("oracle_text", ""));
        }
    } else {
        result.image_url.clear();
    }

    auto prices = payload.value("prices", json::object());
    result.price_usd = select_best_price_usd(prices);
    if (result.price_usd.empty()) {
        const std::string price_name = result.english_name.empty() ? payload.value("name", "") : result.english_name;
        result.price_usd = fetch_price_from_named(price_name);
    }

    result.is_exact_match = true;
    return result;
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

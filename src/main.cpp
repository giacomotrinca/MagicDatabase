
#include <fstream>
#include <iostream>
#include <iostream>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cairo/cairo.h>
#include <cairo/cairo-pdf.h>
#include <cctype>
#include <string>
#include <ctime>
#include <map>
#include <vector>
#include <set>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "database.h"
#include "scryfall.h"
#include "utils.h"
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <atomic>

#include <sstream>
// Forward declarations to allow scheduling helpers to be referenced before their definitions.
struct AppState;
struct ManaStatsExportCtx;
static void schedule_focus_retries(GtkWidget* entry, AppState* state);
static void schedule_focus_retries_custom(GtkWidget* entry, int tries, int interval_ms);
// Forward-declare thumbnail prefetcher (defined later) so callers earlier in the file can use it
static void prefetch_thumbnails_async(const std::vector<std::map<std::string,std::string>>& rows);
    static void on_refresh_clicked(GtkButton* button, gpointer user_data);
    struct RefreshDialogContext;
    struct RefreshProgressPayload;
    struct RefreshFinalPayload;
    static gboolean refresh_dialog_progress_cb(gpointer user_data);
    static gboolean refresh_dialog_finish_cb(gpointer user_data);
    static void refresh_dialog_cancel(GtkButton* button, gpointer user_data);
    static void start_refresh_cards_async(GtkWindow* window, AppState* state);
    static std::vector<std::map<std::string, std::string>> load_cards_from_db(Database* db, const std::string& filter, int deck_filter, bool only_no_deck);

// Struct per dialog widgets
typedef struct {
    GtkWidget *dialog;
    GtkWidget *entry;
} DialogWidgets;
typedef struct _CardRow {
    GObject parent_instance;
    int id;
    gchar *name;
    gchar *type;
    gchar *colors;
    gchar *set_code;
    gchar *mana_cost;
    gchar *rarity;
    int quantity;
    gchar *quantity_display;
    gchar *translated_colors;
    int total_mana_cost;
    gchar *image_url;
    gchar *added_date;
    gchar *price_usd;
    int foil;
} CardRow;

typedef struct _CardRowClass {
    GObjectClass parent_class;
} CardRowClass;

enum {
    PROP_0,
    PROP_NAME,
    PROP_TYPE,
    PROP_TRANSLATED_COLORS,
    PROP_TOTAL_MANA_COST,
    PROP_RARITY,
    PROP_QUANTITY,
    N_PROPERTIES
};

G_DEFINE_TYPE(CardRow, card_row, G_TYPE_OBJECT)

static void card_row_finalize(GObject *object) {
    CardRow *self = (CardRow*)object;
    g_free(self->name);
    g_free(self->type);
    g_free(self->colors);
    g_free(self->set_code);
    g_free(self->mana_cost);
    g_free(self->rarity);
    g_free(self->translated_colors);
    g_free(self->image_url);
    g_free(self->added_date);
    g_free(self->price_usd);
    g_free(self->quantity_display);
    G_OBJECT_CLASS(card_row_parent_class)->finalize(object);
}

static void card_row_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    CardRow *self = (CardRow*)object;
    switch (property_id) {
        case PROP_NAME:
            g_value_set_string(value, self->name);
            break;
        case PROP_TYPE:
            g_value_set_string(value, self->type);
            break;
        case PROP_TRANSLATED_COLORS:
            g_value_set_string(value, self->translated_colors);
            break;
        case PROP_TOTAL_MANA_COST:
            g_value_set_int(value, self->total_mana_cost);
            break;
        case PROP_RARITY:
            g_value_set_string(value, self->rarity);
            break;
        case PROP_QUANTITY:
            g_value_set_int(value, self->quantity);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void card_row_class_init(CardRowClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = card_row_finalize;
    object_class->get_property = card_row_get_property;

    GParamSpec *pspec[N_PROPERTIES] = { NULL, };
    pspec[PROP_NAME] = g_param_spec_string("name", "Name", "Card name", NULL, G_PARAM_READABLE);
    pspec[PROP_TYPE] = g_param_spec_string("type", "Type", "Card type", NULL, G_PARAM_READABLE);
    pspec[PROP_TRANSLATED_COLORS] = g_param_spec_string("translated-colors", "Translated Colors", "Colors translated to Italian", NULL, G_PARAM_READABLE);
    pspec[PROP_TOTAL_MANA_COST] = g_param_spec_int("total-mana-cost", "Total Mana Cost", "Total mana cost as int", 0, G_MAXINT, 0, G_PARAM_READABLE);
    pspec[PROP_RARITY] = g_param_spec_string("rarity", "Rarity", "Card rarity", NULL, G_PARAM_READABLE);
    pspec[PROP_QUANTITY] = g_param_spec_int("quantity", "Quantity", "Card quantity", 0, G_MAXINT, 0, G_PARAM_READABLE);
    g_object_class_install_properties(object_class, N_PROPERTIES, pspec);
}

static void card_row_init(CardRow *self) {
    self->id = 0;
    self->name = NULL;
    self->type = NULL;
    self->colors = NULL;
    self->set_code = NULL;
    self->mana_cost = NULL;
    self->rarity = NULL;
    self->quantity = 0;
    self->quantity_display = NULL;
    self->translated_colors = NULL;
    self->total_mana_cost = 0;
    self->image_url = NULL;
    self->added_date = NULL;
    self->price_usd = NULL;
    self->foil = 0;
}

#define ROW_ID_SEPARATOR_TITLE    -1001
#define ROW_ID_HEADER             -1002

static int calculate_total_mana_cost(const std::string &mana) {
    if (mana.empty()) return 0;
    int total = 0;
    std::string num;
    for (size_t i = 0; i < mana.size(); ++i) {
        char c = mana[i];
        if (std::isdigit((unsigned char)c)) {
            num.push_back(c);
            if (i + 1 >= mana.size() || !std::isdigit((unsigned char)mana[i+1])) {
                try { total += std::stoi(num); } catch(...) { }
                num.clear();
            }
        } else if (std::isalpha((unsigned char)c)) {
            total += 1;
        }
    }
    return total;
}

static const std::string& get_current_language();

static std::string format_datetime(const char* iso) {
    if (!iso) return std::string();
    std::string s = iso;
    if (s.empty()) return s;
    int year = 0, mon = 0, day = 0, hour = 0, minute = 0, second = 0;
    bool parsed = false;
    try {
        if (s.size() >= 19 && s[4] == '-' && s[7] == '-' && (s[10] == 'T' || s[10] == ' ')) {
            year = std::stoi(s.substr(0,4));
            mon = std::stoi(s.substr(5,2));
            day = std::stoi(s.substr(8,2));
            hour = std::stoi(s.substr(11,2));
            minute = std::stoi(s.substr(14,2));
            second = std::stoi(s.substr(17,2));
            parsed = true;
        } else if (s.size() >= 16 && s[4] == '-' && s[7] == '-' && (s[10] == 'T' || s[10] == ' ')) {
            year = std::stoi(s.substr(0,4));
            mon = std::stoi(s.substr(5,2));
            day = std::stoi(s.substr(8,2));
            hour = std::stoi(s.substr(11,2));
            minute = std::stoi(s.substr(14,2));
            second = 0;
            parsed = true;
        }
    } catch(...) { parsed = false; }

    if (!parsed) {
        return s;
    }

    struct tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    if (t == (time_t)-1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", year, mon, day, hour, minute, second);
        return std::string(buf);
    }

    struct tm *lt = localtime(&t);
    if (!lt) return s;

    const char* days_it[] = {"Dom","Lun","Mar","Mer","Gio","Ven","Sab"};
    const char* months_it[] = {"Gen","Feb","Mar","Apr","Mag","Giu","Lug","Ago","Set","Ott","Nov","Dic"};
    const char* days_en[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    const char* months_en[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

    char out[128];
    int wday = lt->tm_wday;
    int monidx = lt->tm_mon;
    if (get_current_language() == "it") {
        snprintf(out, sizeof(out), "%s %02d %s %04d %02d:%02d:%02d",
                 days_it[wday], lt->tm_mday, months_it[monidx], lt->tm_year + 1900, lt->tm_hour, lt->tm_min, lt->tm_sec);
    } else {
        snprintf(out, sizeof(out), "%s %02d %s %04d %02d:%02d:%02d",
                 days_en[wday], lt->tm_mday, months_en[monidx], lt->tm_year + 1900, lt->tm_hour, lt->tm_min, lt->tm_sec);
    }
    return std::string(out);
}

static std::string translate_colors(const char* colors);
static std::string translate_type(const char* type);

static CardRow* card_row_new(int id, const char* name, const char* type, const char* colors, const char* set_code, const char* mana_cost, const char* rarity, int quantity, const char* image_url, const char* added_date, const char* price_usd, int foil, const char* quantity_display = NULL) {
    CardRow* r = (CardRow*)g_object_new(card_row_get_type(), NULL);
    r->id = id;
    r->name = g_strdup(name ? name : "");
    r->type = g_strdup(type ? type : "");
    r->colors = g_strdup(colors ? colors : "");
    r->set_code = g_strdup(set_code ? set_code : "");
    r->mana_cost = g_strdup(mana_cost ? mana_cost : "");
    r->rarity = g_strdup(rarity ? rarity : "");
    r->quantity = quantity;
    r->image_url = g_strdup(image_url ? image_url : "");
    r->added_date = g_strdup(added_date ? added_date : "");
    r->price_usd = g_strdup(price_usd ? price_usd : "");
    r->foil = foil;
    r->total_mana_cost = calculate_total_mana_cost(r->mana_cost ? r->mana_cost : std::string());
    r->translated_colors = g_strdup(translate_colors(r->colors).c_str());
    if (quantity_display && *quantity_display) r->quantity_display = g_strdup(quantity_display); else {
        char buf[64]; snprintf(buf, sizeof(buf), "%d", quantity);
        r->quantity_display = g_strdup(buf);
    }
    return r;
}

static GtkCssProvider* separator_css_provider = NULL;

static void ensure_separator_css_provider() {
    if (separator_css_provider) return;
    separator_css_provider = gtk_css_provider_new();
    const char *css =
        ".separator-row { background-color: rgba(255,165,0,1.0); padding: 6px 8px; }\n"
        ".separator-row { color: #ffffff; }\n"
        ".header-row { background-color: rgba(245,245,245,1.0); padding: 4px 6px; color: #000000; font-weight: bold; border-bottom: 1px solid rgba(0,0,0,0.06); border-radius: 0px; }\n";
    gtk_css_provider_load_from_data(separator_css_provider, css, -1);
}

static std::map<std::string, std::map<std::string, std::string>> translations = {
    {"Nome", {{"it","Nome"}, {"en","Name"}}},
    {"Tipo", {{"it","Tipo"}, {"en","Type"}}},
    {"Colori", {{"it","Colori"}, {"en","Colors"}}},
    {"Costo Mana", {{"it","Costo Mana"}, {"en","Mana Cost"}}},
    {"Rarità", {{"it","Rarità"}, {"en","Rarity"}}},
    {"Data di aggiunta", {{"it","Data di aggiunta"}, {"en","Added Date"}}},
    {"Quantità", {{"it","Quantità"}, {"en","Quantity"}}},
    {"Nuova Carta", {{"it","Nuova Carta"}, {"en","New Card"}}},
    {"Cerca per nome...", {{"it","Cerca per nome..."}, {"en","Search by name..."}}},
    {"File", {{"it","File"}, {"en","File"}}},
    {"Visualizza", {{"it","Visualizza"}, {"en","View"}}},
    {"Lingua", {{"it","Lingua"}, {"en","Language"}}},
    {"Nuovo Database", {{"it","Nuovo Database"}, {"en","New Database"}}},
    {"Apri Database", {{"it","Apri Database"}, {"en","Open Database"}}},
    {"Crea Deck", {{"it","Crea Deck"}, {"en","Create Deck"}}},
    {"Esporta Database", {{"it","Esporta Database"}, {"en","Export Database"}}},
    {"Esporta Deck", {{"it","Esporta Deck"}, {"en","Export Deck"}}},
    {"Seleziona Deck", {{"it","Seleziona Deck"}, {"en","Select Deck"}}},
    {"Filtri...", {{"it","Filtri..."}, {"en","Filters..."}}},
    {"Elimina Deck", {{"it","Elimina Deck"}, {"en","Delete Deck"}}},
    {"Torna al Database Principale", {{"it","Torna al Database Principale"}, {"en","Back to Main Database"}}},
    {"Ordina Crescente", {{"it","Ordina Crescente"}, {"en","Sort Ascending"}}},
    {"Ordina Decrescente", {{"it","Ordina Decrescente"}, {"en","Sort Descending"}}},
    {"Elimina", {{"it","Elimina"}, {"en","Delete"}}},
    {"Totale carte", {{"it","Totale carte"}, {"en","Total cards"}}},
    {"Valore totale", {{"it","Valore totale"}, {"en","Total Value"}}}
};

__attribute__((unused)) static void __add_tendina_translations() {
    translations["Rimuovi"] = {{"it","Rimuovi"}, {"en","Remove"}};
}

__attribute__((unused)) static void __add_settings_translations() {
    translations["Notifiche"] = {{"it","Notifiche"}, {"en","Notifications"}};
    translations["Preferenze"] = {{"it","Preferenze"}, {"en","Preferences"}};
}

static std::string current_language = "it";

static std::map<std::string, std::string> type_translations = {
    {"Creature", "Creatura"},
    {"Instant", "Istantaneo"},
    {"Sorcery", "Stregoneria"},
    {"Artifact", "Artefatto"},
    {"Enchantment", "Incantesimo"},
    {"Land", "Terra"},
    {"Planeswalker", "Planeswalker"},
    {"Legendary Creature", "Creatura Leggendaria"},
    {"Legendary Artifact", "Artefatto Leggendario"},
    {"Basic Land", "Terra Base"},
    {"Snow Land", "Terra Neve"},
    {"Token", "Token"},
    {"Emblem", "Emblema"}
};

static std::string english_for_localized_type(const std::string &loc) {
    if (loc.empty()) return "";
    std::string lower_loc = loc;
    std::transform(lower_loc.begin(), lower_loc.end(), lower_loc.begin(), ::tolower);
    for (const auto &p : type_translations) {
        std::string v = p.second;
        std::string lowv = v;
        std::transform(lowv.begin(), lowv.end(), lowv.begin(), ::tolower);
        if (lowv == lower_loc) return p.first;
    }
    return "";
}

static const std::string& get_current_language() { return current_language; }

// Add translation for the "Add Cards" button
static void __add_more_translations() {
    translations["Aggiungi Carte"] = {{"it", "Aggiungi Carte"}, {"en", "Add Cards"}};
    translations["Disponibili"] = {{"it", "Disponibili"}, {"en", "Available"}};
    translations["Aggiungi"] = {{"it", "Aggiungi"}, {"en", "Add"}};
    translations["Database"] = {{"it", "Database"}, {"en", "Database"}};
    translations["Sideboard"] = {{"it", "Sideboard"}, {"en", "Sideboard"}};
    translations["Deck"] = {{"it", "Deck"}, {"en", "Deck"}};
}

// Translation entry for the new filter: not in any deck
static void __add_extra_translations() {
    translations["Non in alcun mazzo"] = {{"it", "Non in alcun mazzo"}, {"en", "Not in any deck"}};
}

std::string translate(const std::string& key) {
    auto it = translations.find(key);
    if (it != translations.end()) {
        auto lang_it = it->second.find(current_language);
        if (lang_it != it->second.end()) return lang_it->second;
    }
    return key;
}

static std::string translate_colors(const char* colors) {
    // Robust color formatter: accepts several formats stored in the DB:
    // - JSON array (e.g. ["W","R"] or ["Bianco","Rosso"]) -> join with ", "
    // - Compact codes string (e.g. "WUR" or "WU") -> map each letter to localized name
    // - Comma/space separated names -> normalize and return
    // If no colors or empty -> return localized "Incolore" / "Colorless".
    if (!colors) {
        return (current_language == "it") ? std::string("Incolore") : std::string("Colorless");
    }
    std::string c = colors;
    // Trim whitespace
    auto trim = [](std::string s) {
        size_t a = 0, b = s.size();
        while (a < b && isspace((unsigned char)s[a])) ++a;
        while (b > a && isspace((unsigned char)s[b-1])) --b;
        return s.substr(a, b-a);
    };
    c = trim(c);
    if (c.empty() || c == "[]" || c == "null") {
        return (current_language == "it") ? std::string("Incolore") : std::string("Colorless");
    }

    // Color name translations for single-letter codes
    std::map<std::string, std::string> color_trans;
    if (current_language == "it") {
        color_trans = {{"W", "Bianco"}, {"U", "Blu"}, {"B", "Nero"}, {"R", "Rosso"}, {"G", "Verde"}};
    } else {
        color_trans = {{"W", "White"}, {"U", "Blue"}, {"B", "Black"}, {"R", "Red"}, {"G", "Green"}};
    }

    // If it looks like JSON array, try to parse
    if (!c.empty() && c.front() == '[') {
        try {
            auto j = nlohmann::json::parse(c);
            if (j.is_array()) {
                std::vector<std::string> parts;
                for (auto &el : j) {
                    if (el.is_string()) {
                        std::string v = el.get<std::string>();
                        // If element is a single-letter code, translate it
                        if (v.size() == 1) {
                            std::string key(1, v[0]);
                            auto it = color_trans.find(key);
                            if (it != color_trans.end()) parts.push_back(it->second);
                            else parts.push_back(v);
                        } else {
                            // element looks like a full name; accept as-is
                            parts.push_back(v);
                        }
                    }
                }
                if (parts.empty()) return (current_language == "it") ? std::string("Incolore") : std::string("Colorless");
                std::string out;
                for (size_t i = 0; i < parts.size(); ++i) {
                    if (i) out += ", ";
                    out += parts[i];
                }
                return out;
            }
        } catch(...) {
            // fallthrough to other heuristics
        }
    }

    // If string is short and contains only letters (like "WUR"), treat as codes
    bool all_letters = !c.empty();
    for (char ch : c) if (!std::isalpha((unsigned char)ch)) { all_letters = false; break; }
    if (all_letters && c.size() <= 6) {
        std::string out;
        for (size_t i = 0; i < c.size(); ++i) {
            std::string key(1, c[i]);
            auto it = color_trans.find(key);
            if (it != color_trans.end()) {
                if (!out.empty()) out += ", ";
                out += it->second;
            }
        }
        if (out.empty()) return (current_language == "it") ? std::string("Incolore") : std::string("Colorless");
        return out;
    }

    // Otherwise, try to remove surrounding quotes/brackets and commas then normalize separators
    // Remove leading/trailing brackets and quotes
    std::string s = c;
    if (!s.empty() && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size()-2);
    }
    // Replace '"', '[', ']' characters
    std::string cleaned;
    for (char ch : s) {
        if (ch == '"' || ch == '[' || ch == ']') continue;
        cleaned.push_back(ch);
    }
    // Split on commas
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < cleaned.size()) {
        size_t comma = cleaned.find(',', pos);
        std::string tok;
        if (comma == std::string::npos) { tok = cleaned.substr(pos); pos = cleaned.size(); }
        else { tok = cleaned.substr(pos, comma - pos); pos = comma + 1; }
        tok = trim(tok);
        if (!tok.empty()) parts.push_back(tok);
    }
    if (parts.empty()) return (current_language == "it") ? std::string("Incolore") : std::string("Colorless");
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += ", ";
        out += parts[i];
    }
    return out;
}

// Translate internal rarity codes/strings to localized, nicely-capitalized labels.
// Accepts values like "common", "uncommon", "rare", "mythic" (any case) and
// returns "Comune"/"Common", etc. If input is empty or null, returns an empty string.
static std::string translate_rarity(const char* rarity) {
    if (!rarity) return std::string();
    std::string r = rarity;
    // lowercase for matching
    std::string low;
    low.reserve(r.size());
    for (char ch : r) low.push_back(std::tolower((unsigned char)ch));
    if (low.empty()) return std::string();
    if (current_language == "it") {
        if (low == "common") return std::string("Comune");
        if (low == "uncommon") return std::string("Non Comune");
        if (low == "rare") return std::string("Rara");
        if (low == "mythic") return std::string("Mitica");
        // fallback: capitalize first letter
        std::string out = r;
        out[0] = std::toupper((unsigned char)out[0]);
        return out;
    } else {
        // English (default): capitalize first letter
        if (low == "common") return std::string("Common");
        if (low == "uncommon") return std::string("Uncommon");
        if (low == "rare") return std::string("Rare");
        if (low == "mythic") return std::string("Mythic");
        std::string out = r;
        out[0] = std::toupper((unsigned char)out[0]);
        return out;
    }
}

// Helper to grab focus on a widget from an idle callback (used to ensure focus
// is applied after transient dialogs close).
static gboolean grab_focus_idle(gpointer data) {
    GtkWidget* w = GTK_WIDGET(data);
    // Diagnostic: print which widget currently has focus in the top-level window
    auto debug_print_focus = [](GtkWidget* ref) {
        if (!ref) { std::cout << "DEBUG: debug_print_focus called with NULL ref\n"; return; }
        GtkWindow* win = GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(ref), GTK_TYPE_WINDOW));
        if (!win) { std::cout << "DEBUG: no ancestor window for ref=" << ref << "\n"; return; }
        GtkWidget* f = gtk_window_get_focus(win);
        if (!f) std::cout << "DEBUG: window " << win << " has NO focus widget\n";
        else std::cout << "DEBUG: window " << win << " focus=" << f << " type=" << G_OBJECT_TYPE_NAME(f) << "\n";
    };
    if (w && GTK_IS_WIDGET(w)) {
        std::cout << "DEBUG: grab_focus_idle trying to focus entry=" << w << " type=" << G_OBJECT_TYPE_NAME(w) << "\n";
        // Print current focus before attempting
        debug_print_focus(w);
        // Try to grab focus
        gtk_widget_grab_focus(w);
        // Re-check focus
        debug_print_focus(w);
    } else {
        std::cout << "DEBUG: grab_focus_idle called with invalid widget pointer=" << w << "\n";
    }
    return G_SOURCE_REMOVE;
}

// Helper used to reliably restore focus to the Add-Card entry after a transient
// dialog closes. Using a short timeout (instead of an idle) avoids races where
// another widget (or the window manager) steals focus immediately after the
// dialog destruction.
struct FocusTarget {
    GtkWidget* entry;
    int tries;
    int interval_ms; // current interval between retries (ms)
    int attempt; // how many attempts done
};

// Forward declaration for send_notification (defined later)
static void send_notification(const std::string& title, const std::string& body, const std::string& icon);

// Try to grab focus multiple times (short retries) to overcome races where the
// window manager or other widgets steal focus when dialogs close.
static gboolean grab_focus_to_entry(gpointer data) {
    FocusTarget* ft = (FocusTarget*)data;
    if (!ft) return G_SOURCE_REMOVE;
    GtkWidget* entry = ft->entry;
    if (!entry || !GTK_IS_WIDGET(entry)) {
        std::cout << "DEBUG: grab_focus_to_entry called with invalid entry pointer=" << entry << "\n";
        delete ft;
        return G_SOURCE_REMOVE;
    }
    // Ensure parent window is presented (helps when WM focus policy is odd)
    GtkWindow* win = GTK_WINDOW(gtk_widget_get_ancestor(entry, GTK_TYPE_WINDOW));
    if (win && GTK_IS_WINDOW(win)) gtk_window_present(win);

    if (ft->attempt == 0) {
        const char* sess = getenv("XDG_SESSION_TYPE");
        const char* disp = gdk_display_get_name(gdk_display_get_default());
        std::cout << "DEBUG: grab_focus_to_entry initial attempt; XDG_SESSION_TYPE=" << (sess?sess:"(null)") << ", display=" << (disp?disp:"(null)") << "\n";
    }

    std::cout << "DEBUG: grab_focus_to_entry attempt=" << (ft->attempt+1) << " tries_left=" << ft->tries << " interval_ms=" << ft->interval_ms << " focusing entry=" << entry << " type=" << G_OBJECT_TYPE_NAME(entry) << "\n";

    // If already focused, we're done
    if (win && GTK_IS_WINDOW(win)) {
        GtkWidget* cur = gtk_window_get_focus(win);
        if (cur == entry) {
            if (GTK_IS_EDITABLE(entry)) gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
            delete ft;
            return G_SOURCE_REMOVE;
        }
    }

    // Try to grab focus now
    gtk_widget_grab_focus(entry);
    if (GTK_IS_EDITABLE(entry)) gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);

    // Diagnostics: print which widget holds focus after the grab attempt
    GtkWidget* cur_after = NULL;
    if (win && GTK_IS_WINDOW(win)) cur_after = gtk_window_get_focus(win);
    std::cout << "DEBUG: grab_focus_to_entry after grab: current_focus=" << cur_after << " type=" << (cur_after ? G_OBJECT_TYPE_NAME(cur_after) : "NULL") << " window=" << win << "\n";

    if (cur_after == entry) {
        delete ft;
        return G_SOURCE_REMOVE;
    }

    // Not focused yet; schedule another attempt using exponential/backoff
    ft->tries -= 1;
    ft->attempt += 1;
    if (ft->tries <= 0) {
    std::cout << "DEBUG: grab_focus_to_entry exhausted retries, giving up and stopping\n";
    // Informational only on stdout; per user preference do NOT send desktop notification on focus failures.
        delete ft;
        return G_SOURCE_REMOVE;
    }

    // Increase interval by 1.5x up to a cap
    int next_interval = ft->interval_ms + (ft->interval_ms / 2);
    if (next_interval < 50) next_interval = 50;
    if (next_interval > 2000) next_interval = 2000;
    ft->interval_ms = next_interval;
    // Reschedule this FocusTarget with the new interval
    g_timeout_add(ft->interval_ms, grab_focus_to_entry, ft);
    return G_SOURCE_REMOVE;
}

// (schedule helpers are defined later, after AppState)

// Global toggle for desktop notifications. Defaults to enabled.
static bool g_notifications_enabled = true;
static int g_focus_retry_tries_default = 12;
static int g_focus_retry_interval_ms_default = 100;
// Default page size persisted in settings
static int g_page_size_default = 50;

// Persist settings to a simple INI-like file under data/settings.ini
static void save_settings() {
    ensure_data_dir_exists("data");
    std::ofstream out("data/settings.ini");
    if (!out) return;
    out << "notifications=" << (g_notifications_enabled ? "1" : "0") << "\n";
    out << "focus_retry_tries=" << g_focus_retry_tries_default << "\n";
    out << "focus_retry_interval_ms=" << g_focus_retry_interval_ms_default << "\n";
    out << "page_size=" << g_page_size_default << "\n";
    out.close();
}

static void load_settings() {
    ensure_data_dir_exists("data");
    std::ifstream in("data/settings.ini");
    if (!in) {
        // use defaults and create file
        g_notifications_enabled = true;
        g_focus_retry_tries_default = 12;
        g_focus_retry_interval_ms_default = 100;
        g_page_size_default = 50;
        save_settings();
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq+1);
        if (key == "notifications") {
            g_notifications_enabled = (val == "1" || val == "true");
        } else if (key == "focus_retry_tries") {
            try { g_focus_retry_tries_default = std::stoi(val); } catch(...) { g_focus_retry_tries_default = 12; }
        } else if (key == "focus_retry_interval_ms") {
            try { g_focus_retry_interval_ms_default = std::stoi(val); } catch(...) { g_focus_retry_interval_ms_default = 100; }
        } else if (key == "page_size") {
            try { g_page_size_default = std::stoi(val); } catch(...) { g_page_size_default = 50; }
        }
    }
    in.close();
}

// Send a desktop notification using notify-send. Uses g_shell_quote to safely
// quote title/body and g_spawn_command_line_async to invoke the command.
static void send_notification(const std::string& title, const std::string& body, const std::string& icon) {
    if (!g_notifications_enabled) return;
    GError* error = NULL;
    gchar* qtitle = g_shell_quote(title.c_str());
    gchar* qbody = g_shell_quote(body.c_str());
    gchar* qicon = NULL;
    if (!icon.empty()) qicon = g_shell_quote(icon.c_str());
    gchar* cmd = NULL;
    if (qicon) {
        cmd = g_strdup_printf("notify-send --icon %s %s %s", qicon, qtitle, qbody);
    } else {
        cmd = g_strdup_printf("notify-send %s %s", qtitle, qbody);
    }
    gboolean ok = g_spawn_command_line_async(cmd, &error);
    if (!ok) {
        std::cout << "DEBUG: notify-send failed: " << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
    }
    g_free(cmd);
    g_free(qtitle);
    g_free(qbody);
    if (qicon) g_free(qicon);
}


struct AppState {
    std::string db_path;
    Database* db;
    GtkWidget* db_name_label;
    GtkWidget* total_cards_label;
    GtkWidget* filter_label;
    GListStore* card_store;
    GtkColumnView* column_view;
    GtkSelectionModel* selection;
    GtkWidget* search_entry;
    // Inline add controls (when viewing main DB)
    GtkWidget* inline_add_entry;
    GtkWidget* inline_add_spin;
    GtkWidget* inline_add_foil;
    GtkColumnViewColumn *name_col, *type_col, *colors_col, *mana_col, *rarity_col, *date_col, *qty_col;
    GtkWidget* add_card_button;
    GtkWidget* file_button;
    GtkWidget* view_button;
    GtkWidget* refresh_button;
    int selected_deck_id;
    GtkWidget* deck_button;
    GtkWidget* deck_label;
    GtkWidget* deck_delete_button;
    GtkWidget* db_button; // button to return to main Database when viewing a deck
    GMenu *deck_menu;
    GMenu *file_menu;
    GMenu *view_menu;
    // Filters
    std::set<std::string> filter_colors; // set of color codes, e.g. "W", "U"
    std::set<std::string> filter_rarities; // set of rarities: "common","uncommon","rare","mythic"
    std::set<std::string> filter_types; // set of types (English keys like "Creature", "Instant")
    int filter_foil; // -1 = any, 0 = non-foil only, 1 = foil only
    bool filter_no_deck; // true = only show cards not in any deck
    // Focus retry configuration for restoring focus after dialogs
    int focus_retry_tries;
    int focus_retry_interval_ms;
    // Pagination state for large DBs
    int page_size;       // rows per page
    int current_page;    // zero-based
    int total_rows;      // total rows matching current filters
    // Pagination widgets (created during UI init)
    GtkWidget* page_label;
    GtkWidget* prev_page_button;
    GtkWidget* next_page_button;
    GtkWidget* page_size_combo;
    GtkWidget* view_all_toggle;
    // Debounce timer id for search entry changes (0 = none)
    guint search_debounce_id;
    // Main window layout widgets
    GtkWidget* main_window;
    GtkWidget* main_stack;
    GtkWidget* cards_page;
    GtkWidget* stats_page;
    GtkWidget* stats_container;
    GtkWidget* stats_placeholder;
    ManaStatsExportCtx* stats_ctx;
    bool has_oracle_text_column;
};

static bool g_cards_table_has_oracle_text = false;

static void update_cards_schema_flags(AppState* state) {
    if (!state) return;
    bool has_oracle = false;
    if (state->db) {
        state->db->query("PRAGMA table_info(cards);", [&](const std::map<std::string,std::string>& row){
            auto it = row.find("name");
            if (it != row.end() && it->second == "oracle_text") has_oracle = true;
        });
    }
    state->has_oracle_text_column = has_oracle;
    g_cards_table_has_oracle_text = has_oracle;
}

struct ManaStatsExportCtx {
    GtkWidget* da = nullptr;
    GtkWindow* parent = nullptr;
    cairo_surface_t* surf = nullptr;
    cairo_surface_t* surf_total = nullptr;
    cairo_surface_t* surf_type = nullptr;
    cairo_surface_t* surf_color = nullptr;
    int w = 0;
    int h = 0;
    std::map<int,int> buckets;
    int total = 0;
    int cap = 0;
    std::string deck_name;
    std::map<int, std::vector<std::pair<std::string,int>>> bucket_details;
    std::map<std::string,int> color_counts;
    double avg = 0.0;
    int median = 0;
    int mode_bucket = 0;
    int mode_count = 0;
    int max_cost = 0;
    int land_cards = 0;
    int spells_count = 0;
    double avg_total_cmc = 0.0;
    double avg_non_land_cmc = 0.0;
    double avg_creature_cmc = 0.0;
    double avg_non_creature_cmc = 0.0;
    int recommended_lands = 0;
    int land_delta = 0;
    int removal_spells = 0;
    int board_wipes = 0;
    int ramp_spells = 0;
    int card_draw_spells = 0;
    std::map<int,int> curve_targets;
    std::map<int,int> curve_actual_summary;
    std::map<std::string,int> color_sources;
    std::map<std::string,int> color_source_targets;
    std::map<std::string,int> pip_counts;
    double total_pips = 0.0;
};

struct RefreshDialogContext {
    AppState* state = nullptr;
    GtkWidget* dialog = nullptr;
    GtkWidget* spinner = nullptr;
    GtkProgressBar* progress = nullptr;
    GtkLabel* summary_label = nullptr;
    GtkLabel* detail_label = nullptr;
    GtkWidget* cancel_button = nullptr;
    GtkWidget* refresh_button = nullptr;
    std::atomic<bool> cancel_requested{false};
    std::vector<std::map<std::string,std::string>> cards;
    int updated_count = 0;
    int processed_count = 0;
};

struct RefreshProgressPayload {
    RefreshDialogContext* ctx;
    double fraction;
    std::string summary;
    std::string detail;
};

struct RefreshFinalPayload {
    RefreshDialogContext* ctx;
    bool cancelled;
    int updated;
    int processed;
    int total;
};

static void destroy_mana_stats_context(ManaStatsExportCtx* ctx) {
    if (!ctx) return;
    std::set<cairo_surface_t*> surfaces;
    if (ctx->surf_total) surfaces.insert(ctx->surf_total);
    if (ctx->surf_type) surfaces.insert(ctx->surf_type);
    if (ctx->surf_color) surfaces.insert(ctx->surf_color);
    for (auto* surface : surfaces) cairo_surface_destroy(surface);
    delete ctx;
}

static void stats_clear(AppState* state, bool restore_placeholder) {
    if (!state) return;
    if (state->stats_ctx) {
        destroy_mana_stats_context(state->stats_ctx);
        state->stats_ctx = nullptr;
    }
    if (!state->stats_container) return;
    GtkWidget* child = gtk_widget_get_first_child(state->stats_container);
    while (child) {
        gtk_widget_unparent(child);
        child = gtk_widget_get_first_child(state->stats_container);
    }
    state->stats_placeholder = nullptr;
    if (restore_placeholder) {
        GtkWidget* placeholder = gtk_label_new("Seleziona \"Curva Mana\" per visualizzare le statistiche del deck.");
        gtk_label_set_wrap(GTK_LABEL(placeholder), TRUE);
        gtk_label_set_xalign(GTK_LABEL(placeholder), 0.0);
        gtk_widget_set_margin_top(placeholder, 24);
        gtk_widget_set_margin_bottom(placeholder, 24);
        gtk_widget_set_margin_start(placeholder, 32);
        gtk_widget_set_margin_end(placeholder, 32);
        gtk_widget_add_css_class(placeholder, "muted");
        gtk_box_append(GTK_BOX(state->stats_container), placeholder);
        state->stats_placeholder = placeholder;
    }
}

static void stats_set_surface(ManaStatsExportCtx* ctx, cairo_surface_t* surf) {
    if (!ctx || !ctx->da) return;
    ctx->surf = surf;
    g_object_set_data(G_OBJECT(ctx->da), "mana_surface", surf);
    gtk_widget_queue_draw(ctx->da);
}

static void on_stats_back_clicked(GtkButton*, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state) return;
    stats_clear(state, true);
    if (state->main_stack) gtk_stack_set_visible_child_name(GTK_STACK(state->main_stack), "cards");
}

static void on_stats_surface_total(GtkButton*, gpointer user_data) {
    ManaStatsExportCtx* ctx = (ManaStatsExportCtx*)user_data;
    if (!ctx) return;
    stats_set_surface(ctx, ctx->surf_total ? ctx->surf_total : ctx->surf);
}

static void on_stats_surface_type(GtkButton*, gpointer user_data) {
    ManaStatsExportCtx* ctx = (ManaStatsExportCtx*)user_data;
    if (!ctx || !ctx->surf_type) return;
    stats_set_surface(ctx, ctx->surf_type);
}

static void on_stats_surface_color(GtkButton*, gpointer user_data) {
    ManaStatsExportCtx* ctx = (ManaStatsExportCtx*)user_data;
    if (!ctx || !ctx->surf_color) return;
    stats_set_surface(ctx, ctx->surf_color);
}

// Schedule focus retries using the app-configured retries/interval.
static void schedule_focus_retries(GtkWidget* entry, AppState* state) {
    if (!entry) return;
    FocusTarget* ft = new FocusTarget();
    ft->entry = entry;
    if (state && state->focus_retry_tries > 0) ft->tries = state->focus_retry_tries;
    else ft->tries = 12;
    int interval = (state && state->focus_retry_interval_ms > 0) ? state->focus_retry_interval_ms : 100;
    g_timeout_add(interval, grab_focus_to_entry, ft);
}

// Schedule focus retries with explicit small values (used for short-lived dialogs)
static void schedule_focus_retries_custom(GtkWidget* entry, int tries, int interval_ms) {
    if (!entry) return;
    FocusTarget* ft = new FocusTarget();
    ft->entry = entry;
    ft->tries = tries > 0 ? tries : 6;
    int interval = interval_ms > 0 ? interval_ms : 50;
    g_timeout_add(interval, grab_focus_to_entry, ft);
}

// Forward declaration needed by search_debounce_cb
void refresh_card_list(AppState* state);

// Debounced timeout callback for search field. When fired, it calls
// refresh_card_list and clears the stored timer id.
static gboolean search_debounce_cb(gpointer user_data) {
    AppState* state = (AppState*)user_data;
    if (!state) return G_SOURCE_REMOVE;
    state->search_debounce_id = 0;
    refresh_card_list(state);
    return G_SOURCE_REMOVE; // do not repeat
}

// Forward declaration for refresh used by deck helpers
void refresh_card_list(AppState* state);
// forward-declare populate_deck_menu so helpers can call it
static void populate_deck_menu(AppState *state);
// Forward declaration for card-to-deck helper
// sideboard: 0 = main, 1 = sideboard
static bool add_card_to_deck(AppState* state, int card_id, int to_move, int target_deck_id, int sideboard = 0);
// Forward declarations for deck-delete handlers (defined later)
static void on_deck_delete_clicked(GtkButton* button, gpointer user_data);
static void on_delete_deck_confirmed(GtkButton* button, gpointer user_data);
static void on_delete_deck_cancel(GtkButton* button, gpointer user_data);

// Forward declarations for export handlers
static void on_export_database_action(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void on_export_deck_action(GSimpleAction *action, GVariant *parameter, gpointer user_data);
// Forward declaration for filters dialog
static void on_filters_action(GSimpleAction *action, GVariant *parameter, gpointer user_data);
// Forward declaration for preferences dialog
static void on_preferences_action(GSimpleAction *action, GVariant *parameter, gpointer user_data);

// Forward declaration for menu rebuild helper
static void rebuild_menus_for_language(AppState* state);

// Helper: sanitize a filename (basic), replace spaces with underscore and remove problematic chars
static std::string sanitize_filename(const std::string &s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-' ) out.push_back(c);
        else if (std::isspace((unsigned char)c)) out.push_back('_');
        // else skip
    }
    if (out.empty()) out = "export";
    return out;
}

// Create a styled dialog/window consistent with app theme.
static GtkWidget* create_styled_dialog(GtkWindow* parent, int width = -1, int height = -1) {
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_modal(GTK_WINDOW(dialog), true);
    if (parent && GTK_IS_WINDOW(parent)) gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    if (width > 0 && height > 0) gtk_window_set_default_size(GTK_WINDOW(dialog), width, height);
    // Apply CSS class used earlier for popovers
    gtk_widget_add_css_class(dialog, "small-popover");
    return dialog;
}

static gboolean refresh_dialog_progress_cb(gpointer user_data) {
    auto payload = static_cast<RefreshProgressPayload*>(user_data);
    if (!payload || !payload->ctx) return G_SOURCE_REMOVE;
    auto ctx = payload->ctx;
    if (ctx->progress && GTK_IS_PROGRESS_BAR(ctx->progress)) {
        double fraction = std::clamp(payload->fraction, 0.0, 1.0);
        gtk_progress_bar_set_fraction(ctx->progress, fraction);
        int percent = static_cast<int>(std::round(fraction * 100.0));
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        std::string pct = std::to_string(percent) + "%";
        gtk_progress_bar_set_text(ctx->progress, pct.c_str());
    }
    if (ctx->summary_label && GTK_IS_LABEL(ctx->summary_label)) {
        gtk_label_set_text(ctx->summary_label, payload->summary.c_str());
    }
    if (ctx->detail_label && GTK_IS_LABEL(ctx->detail_label)) {
        gtk_label_set_text(ctx->detail_label, payload->detail.c_str());
    }
    return G_SOURCE_REMOVE;
}

static gboolean refresh_dialog_finish_cb(gpointer user_data) {
    auto payload = static_cast<RefreshFinalPayload*>(user_data);
    if (!payload || !payload->ctx) return G_SOURCE_REMOVE;
    auto ctx = payload->ctx;
    double fraction = payload->total > 0 ? static_cast<double>(payload->processed) / payload->total : 1.0;
    fraction = std::clamp(fraction, 0.0, 1.0);
    if (!payload->cancelled) fraction = 1.0;
    if (ctx->progress && GTK_IS_PROGRESS_BAR(ctx->progress)) {
        gtk_progress_bar_set_fraction(ctx->progress, fraction);
        int percent = static_cast<int>(std::round(fraction * 100.0));
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        std::string pct = std::to_string(percent) + "%";
        gtk_progress_bar_set_text(ctx->progress, pct.c_str());
    }
    if (ctx->spinner && GTK_IS_SPINNER(ctx->spinner)) {
        gtk_spinner_stop(GTK_SPINNER(ctx->spinner));
    }
    if (ctx->cancel_button && GTK_IS_WIDGET(ctx->cancel_button)) {
        gtk_widget_set_sensitive(ctx->cancel_button, FALSE);
    }
    if (ctx->summary_label && GTK_IS_LABEL(ctx->summary_label)) {
        std::ostringstream summary;
        if (payload->cancelled) {
            summary << "Refresh annullato";
        } else {
            summary << "Aggiornamento completato";
        }
        summary << " • " << payload->processed << "/" << payload->total;
        gtk_label_set_text(ctx->summary_label, summary.str().c_str());
    }
    if (ctx->detail_label && GTK_IS_LABEL(ctx->detail_label)) {
        std::ostringstream detail;
        if (payload->cancelled) {
            detail << "Aggiornate " << payload->updated << " carte prima dell'annullamento.";
        } else {
            detail << "Aggiornate " << payload->updated << " carte su " << payload->total << ".";
        }
        gtk_label_set_text(ctx->detail_label, detail.str().c_str());
    }
    if (ctx->refresh_button && GTK_IS_WIDGET(ctx->refresh_button)) {
        gtk_widget_set_sensitive(ctx->refresh_button, TRUE);
    }
    if (ctx->state) {
        refresh_card_list(ctx->state);
    }
    g_timeout_add(420, +[](gpointer data) -> gboolean {
        auto ctx = static_cast<RefreshDialogContext*>(data);
        if (ctx) {
            if (ctx->dialog && GTK_IS_WINDOW(ctx->dialog)) {
                gtk_window_destroy(GTK_WINDOW(ctx->dialog));
            }
            ctx->dialog = nullptr;
            delete ctx;
        }
        return G_SOURCE_REMOVE;
    }, ctx);
    payload->ctx = nullptr;
    return G_SOURCE_REMOVE;
}

static void refresh_dialog_cancel(GtkButton*, gpointer user_data) {
    auto ctx = static_cast<RefreshDialogContext*>(user_data);
    if (!ctx) return;
    ctx->cancel_requested.store(true);
    if (ctx->cancel_button && GTK_IS_WIDGET(ctx->cancel_button)) {
        gtk_widget_set_sensitive(ctx->cancel_button, FALSE);
    }
    if (ctx->detail_label && GTK_IS_LABEL(ctx->detail_label)) {
        gtk_label_set_text(ctx->detail_label, "Annullamento in corso...");
    }
}

static void start_refresh_cards_async(GtkWindow* window, AppState* state) {
    if (!window || !state || !state->db) return;
    auto cards = load_cards_from_db(state->db, std::string(""), state->selected_deck_id, state->filter_no_deck);
    if (cards.empty()) {
        GtkAlertDialog* alert = gtk_alert_dialog_new("%s", "Nessuna carta da aggiornare.");
        gtk_alert_dialog_show(alert, window);
        g_object_unref(alert);
        return;
    }

    auto* ctx = new RefreshDialogContext();
    ctx->state = state;
    ctx->cards = std::move(cards);
    ctx->refresh_button = state->refresh_button;

    GtkWidget* dialog = create_styled_dialog(window, 440, 220);
    gtk_window_set_title(GTK_WINDOW(dialog), "Aggiornamento carte");
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(dialog), FALSE);

    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(content, "refresh-dialog-content");
    gtk_window_set_child(GTK_WINDOW(dialog), content);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(content), header);

    GtkWidget* spinner = gtk_spinner_new();
    gtk_widget_add_css_class(spinner, "accent-spinner");
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_box_append(GTK_BOX(header), spinner);

    GtkWidget* title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span weight=\"bold\" size=\"large\">Aggiornamento carte</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_widget_add_css_class(title, "refresh-dialog-title");
    gtk_box_append(GTK_BOX(header), title);

    GtkWidget* summary = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(summary), 0.0);
    gtk_widget_add_css_class(summary, "refresh-dialog-subtitle");
    gtk_box_append(GTK_BOX(content), summary);

    GtkWidget* progress = gtk_progress_bar_new();
    gtk_widget_add_css_class(progress, "accent-progress");
    gtk_widget_set_hexpand(progress, TRUE);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress), TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), 0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress), "0%");
    gtk_box_append(GTK_BOX(content), progress);

    GtkWidget* detail = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(detail), 0.0);
    gtk_label_set_wrap(GTK_LABEL(detail), TRUE);
    gtk_widget_add_css_class(detail, "refresh-dialog-detail");
    gtk_box_append(GTK_BOX(content), detail);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget* cancel_btn = gtk_button_new_with_label("Annulla");
    gtk_widget_add_css_class(cancel_btn, "ghost-button");
    gtk_box_append(GTK_BOX(buttons), cancel_btn);
    gtk_box_append(GTK_BOX(content), buttons);

    ctx->dialog = dialog;
    ctx->spinner = spinner;
    ctx->progress = GTK_PROGRESS_BAR(progress);
    ctx->summary_label = GTK_LABEL(summary);
    ctx->detail_label = GTK_LABEL(detail);
    ctx->cancel_button = cancel_btn;

    std::ostringstream initial_summary;
    initial_summary << "0 / " << ctx->cards.size() << " carte elaborate";
    gtk_label_set_text(GTK_LABEL(summary), initial_summary.str().c_str());
    gtk_label_set_text(GTK_LABEL(detail), "Preparazione in corso...");

    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(refresh_dialog_cancel), ctx);

    if (state->refresh_button && GTK_IS_WIDGET(state->refresh_button)) {
        gtk_widget_set_sensitive(state->refresh_button, FALSE);
    }

    gtk_window_present(GTK_WINDOW(dialog));

    std::thread([ctx]() {
        const int total = static_cast<int>(ctx->cards.size());
        int processed = 0;
        for (const auto& row : ctx->cards) {
            if (ctx->cancel_requested.load()) break;

            std::string search_name;
            auto it_en = row.find("english_name");
            if (it_en != row.end()) search_name = it_en->second;
            if (search_name.empty()) {
                auto it_name = row.find("name");
                if (it_name != row.end()) search_name = it_name->second;
            }
            if (search_name.empty()) {
                processed++;
                ctx->processed_count = processed;
                auto* payload = new RefreshProgressPayload{ctx, total > 0 ? static_cast<double>(processed) / total : 1.0, std::to_string(processed) + " / " + std::to_string(total) + " carte elaborate", "Nessun nome disponibile"};
                g_idle_add_full(G_PRIORITY_DEFAULT, refresh_dialog_progress_cb, payload, [](gpointer data){ delete static_cast<RefreshProgressPayload*>(data); });
                continue;
            }

            auto results = search_cards_from_scryfall(search_name);
            const std::string row_set = row.count("set_code") ? row.at("set_code") : std::string();
            ScryfallCard* found = nullptr;
            for (auto& card : results) {
                if (!row_set.empty()) {
                    std::string card_code = card.set_code;
                    if (!card_code.empty()) {
                        std::string row_code_upper = row_set;
                        std::string card_code_upper = card_code;
                        std::transform(row_code_upper.begin(), row_code_upper.end(), row_code_upper.begin(), ::toupper);
                        std::transform(card_code_upper.begin(), card_code_upper.end(), card_code_upper.begin(), ::toupper);
                        if (row_code_upper == card_code_upper) {
                            found = &card;
                            break;
                        }
                    }
                }
            }
            if (!found && !results.empty()) {
                found = &results.front();
            }

            bool success = false;
            if (found && ctx->state && ctx->state->db) {
                int card_id = 0;
                auto it_id = row.find("id");
                if (it_id != row.end()) {
                    try { card_id = std::stoi(it_id->second); } catch(...) { card_id = 0; }
                }
                if (card_id > 0) {
                    success = ctx->state->db->update_card_info(card_id,
                        found->english_name,
                        found->localized_name,
                        found->type,
                        found->localized_type,
                        found->colors,
                        found->mana_cost,
                        found->rarity,
                        found->image_url,
                        found->price_usd,
                        found->oracle_text);
                }
            }
            if (success) ctx->updated_count++;

            processed++;
            ctx->processed_count = processed;

            double fraction = total > 0 ? static_cast<double>(processed) / total : 1.0;
            std::ostringstream summary;
            summary << processed << " / " << total << " carte elaborate";
            std::string detail;
            if (ctx->cancel_requested.load()) {
                detail = "Annullamento in corso...";
            } else if (found) {
                const std::string& display_name = !found->localized_name.empty() ? found->localized_name : found->english_name;
                detail = std::string("Ultima carta: ") + display_name;
                if (!success) detail += " (errore)";
            } else {
                detail = std::string("Nessuna corrispondenza per: ") + search_name;
            }

            auto* payload = new RefreshProgressPayload{ctx, fraction, summary.str(), detail};
            g_idle_add_full(G_PRIORITY_DEFAULT, refresh_dialog_progress_cb, payload, [](gpointer data){ delete static_cast<RefreshProgressPayload*>(data); });

            if (ctx->cancel_requested.load()) {
                break;
            }
        }

        int total_cards = static_cast<int>(ctx->cards.size());
        ctx->cards.clear();
        auto* final_payload = new RefreshFinalPayload{ctx, ctx->cancel_requested.load(), ctx->updated_count, ctx->processed_count, total_cards};
        g_idle_add_full(G_PRIORITY_DEFAULT, refresh_dialog_finish_cb, final_payload, [](gpointer data){ delete static_cast<RefreshFinalPayload*>(data); });
    }).detach();
}

static void on_refresh_clicked(GtkButton* button, gpointer user_data) {
    GtkWindow* window = GTK_WINDOW(user_data);
    if (!window) return;
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
    if (!state || !state->db) return;
    // Store the button pointer for re-enabling if not already registered
    if (!state->refresh_button) {
        state->refresh_button = GTK_WIDGET(button);
    }
    start_refresh_cards_async(window, state);
}

// Export helper: queries cards (optionally filtered by deck) and writes a TXT file.
// If 'deck' is true, deck_id must be provided and file is named <deckname>_data.txt
// Otherwise file is data/tot_database.txt
// format: "bilingual" (default formatted TXT + TSVs), "tsv_en", "tsv_it", "csv_en", "csv_it"
static bool export_cards_to_txt(AppState* state, bool deck, int deck_id, const std::string& lang, const std::string& format = "bilingual") {
    if (!state || !state->db) return false;
    ensure_data_dir_exists("data");
    std::string filename;
    if (deck) {
        // Query deck name
        std::string deck_name = "deck";
        state->db->query("SELECT name FROM decks WHERE id = ?", [&](const std::map<std::string,std::string>& row) {
            deck_name = row.at("name");
        }, std::vector<std::string>{std::to_string(deck_id)});
        std::string safe = sanitize_filename(deck_name);
        filename = std::string("data/") + safe + "_data.txt";
    } else {
        // Build filename based on active filters/search so exporting a filtered view
        // results in a descriptive filename like "W_R_rare_NoDeck_20251029_data.txt"
        std::vector<std::string> parts;
        // Colors
        if (state->filter_colors.size() > 0) {
            std::string cpart;
            bool first = true;
            for (auto &c : state->filter_colors) {
                if (!first) cpart += "-";
                cpart += c;
                first = false;
            }
            parts.push_back(cpart);
        }
        // Rarities
        if (state->filter_rarities.size() > 0) {
            std::string rpart;
            bool first = true;
            for (auto &r : state->filter_rarities) {
                if (!first) rpart += "-";
                rpart += r;
                first = false;
            }
            parts.push_back(rpart);
        }
        // Foil filter
        if (state->filter_foil != -1) {
            parts.push_back(state->filter_foil == 1 ? "Foil" : "NonFoil");
        }
        // Not-in-deck filter
        if (state->filter_no_deck) parts.push_back("NoDeck");
        // Search text
        if (state->search_entry) {
            const char* stext = gtk_editable_get_text(GTK_EDITABLE(state->search_entry));
            std::string st = stext ? stext : "";
            if (!st.empty()) {
                parts.push_back("q-" + sanitize_filename(st));
            }
        }
        // If no filters selected, fallback to tot_database
        std::string base;
        if (parts.empty()) base = "tot_database";
        else {
            base.clear();
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i) base += "_";
                base += parts[i];
            }
        }
        // Append date YYYYMMDD
        char datebuf[16];
        time_t now = time(nullptr);
        struct tm local_tm{};
#if defined(_MSC_VER)
        localtime_s(&local_tm, &now);
#else
        localtime_r(&now, &local_tm);
#endif
        strftime(datebuf, sizeof(datebuf), "%Y%m%d", &local_tm);
        filename = std::string("data/") + base + "_" + datebuf + "_data.txt";
    }

    std::ofstream out;
    if (format == "bilingual") {
        out.open(filename);
        if (!out) return false;
    }
    // Build SQL
    std::string sql = "SELECT id, english_name, localized_name, name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, added_date, price_usd, sideboard, foil FROM cards";
    std::vector<std::string> params;
    if (deck) {
        sql += " WHERE deck_id = ?";
        params.push_back(std::to_string(deck_id));
        // For deck export we want only Name and Type, nicely column aligned.
        struct DeckRow { std::string name; std::string type; int side; };
        std::vector<DeckRow> rows;
        state->db->query(sql, [&](const std::map<std::string,std::string>& row) {
            std::string name = row.count("english_name") && !row.at("english_name").empty() ? row.at("english_name") : row.at("name");
            std::string lname = row.count("localized_name") && !row.at("localized_name").empty() ? row.at("localized_name") : row.at("name");
            std::string type_en = row.count("type") ? row.at("type") : "";
            std::string type_local = row.count("localized_type") && !row.at("localized_type").empty() ? row.at("localized_type") : type_en;
            std::string out_name = (lang == "en") ? name : lname;
            std::string out_type = (lang == "en") ? type_en : type_local;
            int sb = 0;
            if (row.count("sideboard") && !row.at("sideboard").empty()) {
                try { sb = std::stoi(row.at("sideboard")); } catch(...) { sb = 0; }
            }
            rows.push_back(DeckRow{out_name, out_type, sb});
        }, params);
        // Compute column widths
        size_t max_name = 4; // minimum for header 'Name'
        size_t max_type = 4; // minimum for header 'Type'
        for (auto &p : rows) {
            if (p.name.size() > max_name) max_name = p.name.size();
            if (p.type.size() > max_type) max_type = p.type.size();
        }
        // Header (localized)
        std::string header_name = (lang == "en") ? std::string("Name") : std::string("Nome");
        std::string header_type = (lang == "en") ? std::string("Type") : std::string("Tipo");
        if (header_name.size() > max_name) max_name = header_name.size();
        if (header_type.size() > max_type) max_type = header_type.size();
        if (format == "bilingual") {
            // Write header with padding
            out << header_name;
            out << std::string(max_name - header_name.size() + 2, ' ');
            out << header_type << '\n';
            // Write main deck rows first (side == 0)
            for (auto &p : rows) {
                if (p.side != 0) continue;
                out << p.name << std::string(max_name - p.name.size() + 2, ' ') << p.type << '\n';
            }
            // Then append sideboard rows under a localized separator if any
            bool has_side = false;
            for (auto &p : rows) { if (p.side != 0) { has_side = true; break; } }
            if (has_side) {
                out << '\n' << translate("Sideboard") << ":\n";
                for (auto &p : rows) {
                    if (p.side == 0) continue;
                    out << p.name << std::string(max_name - p.name.size() + 2, ' ') << p.type << '\n';
                }
            }
        }
        // Write main deck rows first (side == 0)
        for (auto &p : rows) {
            if (p.side != 0) continue;
            out << p.name << std::string(max_name - p.name.size() + 2, ' ') << p.type << '\n';
        }
        // Then append sideboard rows under a localized separator if any
        bool has_side = false;
        for (auto &p : rows) { if (p.side != 0) { has_side = true; break; } }
        if (has_side) {
            out << '\n' << translate("Sideboard") << ":\n";
            for (auto &p : rows) {
                if (p.side == 0) continue;
                out << p.name << std::string(max_name - p.name.size() + 2, ' ') << p.type << '\n';
            }
        }
    } else {
        // Nicely aligned bilingual table for main DB export (no id column)
        // Columns: name_en, name_it, type_en, type_it, colors_en, colors_it, set_code, mana_cost, rarity_en, rarity_it, quantity, price_usd, foil
        std::string filter = "";
        if (state->search_entry) {
            const char* t = gtk_editable_get_text(GTK_EDITABLE(state->search_entry));
            filter = t ? t : "";
        }
        auto rows = load_cards_from_db(state->db, filter, state->selected_deck_id, state->filter_no_deck);
        // Collect formatted fields
        struct OutRow { std::string name_en, name_it, type_en, type_it, colors_en, colors_it, setc, mana, rarity_en, rarity_it, qty, price, foil; };
        std::vector<OutRow> out_rows;
        out_rows.reserve(rows.size());
        for (const auto &row : rows) {
            OutRow r;
            r.name_en = row.count("english_name") && !row.at("english_name").empty() ? row.at("english_name") : row.at("name");
            r.name_it = row.count("localized_name") && !row.at("localized_name").empty() ? row.at("localized_name") : r.name_en;
            r.type_en = row.count("type") ? row.at("type") : "";
            r.type_it = row.count("localized_type") && !row.at("localized_type").empty() ? row.at("localized_type") : r.type_en;
            std::string colors_raw = row.count("colors") ? row.at("colors") : "";
            std::string prev_lang = current_language;
            current_language = "en";
            r.colors_en = translate_colors(colors_raw.c_str());
            r.rarity_en = translate_rarity(row.count("rarity") ? row.at("rarity").c_str() : "");
            current_language = "it";
            r.colors_it = translate_colors(colors_raw.c_str());
            r.rarity_it = translate_rarity(row.count("rarity") ? row.at("rarity").c_str() : "");
            current_language = prev_lang;
            r.setc = row.count("set_code") ? row.at("set_code") : "";
            r.mana = row.count("mana_cost") ? row.at("mana_cost") : "";
            r.qty = row.count("quantity") ? row.at("quantity") : "0";
            r.price = row.count("price_usd") ? row.at("price_usd") : "";
            r.foil = row.count("foil") ? row.at("foil") : "0";
            out_rows.push_back(std::move(r));
        }
        // Compute column widths with reasonable caps
        size_t w_name_en = std::string("Name_EN").size();
        size_t w_name_it = std::string("Name_IT").size();
        size_t w_type_en = std::string("Type_EN").size();
        size_t w_type_it = std::string("Type_IT").size();
        size_t w_colors_en = std::string("Colors_EN").size();
        size_t w_colors_it = std::string("Colors_IT").size();
        size_t w_set = std::string("Set").size();
        size_t w_mana = std::string("Mana").size();
        size_t w_rarity_en = std::string("Rarity_EN").size();
        size_t w_rarity_it = std::string("Rarity_IT").size();
        size_t w_qty = std::string("Quantity").size();
        size_t w_price = std::string("Price").size();
        size_t w_foil = std::string("Foil").size();
        for (const auto &r : out_rows) {
            w_name_en = std::max(w_name_en, std::min<size_t>(r.name_en.size(), 60));
            w_name_it = std::max(w_name_it, std::min<size_t>(r.name_it.size(), 60));
            w_type_en = std::max(w_type_en, std::min<size_t>(r.type_en.size(), 40));
            w_type_it = std::max(w_type_it, std::min<size_t>(r.type_it.size(), 40));
            w_colors_en = std::max(w_colors_en, std::min<size_t>(r.colors_en.size(), 30));
            w_colors_it = std::max(w_colors_it, std::min<size_t>(r.colors_it.size(), 30));
            w_set = std::max(w_set, std::min<size_t>(r.setc.size(), 12));
            w_mana = std::max(w_mana, std::min<size_t>(r.mana.size(), 20));
            w_rarity_en = std::max(w_rarity_en, std::min<size_t>(r.rarity_en.size(), 12));
            w_rarity_it = std::max(w_rarity_it, std::min<size_t>(r.rarity_it.size(), 12));
            w_qty = std::max(w_qty, r.qty.size());
            w_price = std::max(w_price, r.price.size());
            w_foil = std::max(w_foil, r.foil.size());
        }
        // Header row
        out << std::left << std::setw(w_name_en + 2) << "Name_EN"
            << std::setw(w_name_it + 2) << "Name_IT"
            << std::setw(w_type_en + 2) << "Type_EN"
            << std::setw(w_type_it + 2) << "Type_IT"
            << std::setw(w_colors_en + 2) << "Colors_EN"
            << std::setw(w_colors_it + 2) << "Colors_IT"
            << std::setw(w_set + 2) << "Set"
            << std::setw(w_mana + 2) << "Mana"
            << std::setw(w_rarity_en + 2) << "Rarity_EN"
            << std::setw(w_rarity_it + 2) << "Rarity_IT"
            << std::setw(w_qty + 2) << "Quantity"
            << std::setw(w_price + 2) << "Price"
            << std::setw(w_foil + 2) << "Foil" << '\n';
        // Rows
        for (const auto &r : out_rows) {
            std::string ne = r.name_en.size() > 60 ? r.name_en.substr(0,57) + "..." : r.name_en;
            std::string ni = r.name_it.size() > 60 ? r.name_it.substr(0,57) + "..." : r.name_it;
            std::string te = r.type_en.size() > 40 ? r.type_en.substr(0,37) + "..." : r.type_en;
            std::string ti = r.type_it.size() > 40 ? r.type_it.substr(0,37) + "..." : r.type_it;
            std::string ce = r.colors_en.size() > 30 ? r.colors_en.substr(0,27) + "..." : r.colors_en;
            std::string ci = r.colors_it.size() > 30 ? r.colors_it.substr(0,27) + "..." : r.colors_it;
            out << std::left << std::setw(w_name_en + 2) << ne
                << std::setw(w_name_it + 2) << ni
                << std::setw(w_type_en + 2) << te
                << std::setw(w_type_it + 2) << ti
                << std::setw(w_colors_en + 2) << ce
                << std::setw(w_colors_it + 2) << ci
                << std::setw(w_set + 2) << r.setc
                << std::setw(w_mana + 2) << r.mana
                << std::setw(w_rarity_en + 2) << r.rarity_en
                << std::setw(w_rarity_it + 2) << r.rarity_it
                << std::setw(w_qty + 2) << r.qty
                << std::setw(w_price + 2) << r.price
                << std::setw(w_foil + 2) << r.foil
                << '\n';
        }
    }

    out.close();
    std::cout << "Exported to " << filename << std::endl;

    // Also produce machine-friendly TSV/CSV files (monolingua EN and IT) for AI/automation.
    // Build base filename without _data.txt suffix if present
    std::string base = filename;
    if (base.size() > 9 && base.substr(base.size()-9) == "_data.txt") {
        base = base.substr(0, base.size()-9);
    } else if (base.size() > 4 && base.substr(base.size()-4) == ".txt") {
        base = base.substr(0, base.size()-4);
    }
    std::string tsv_en = base + "_en.tsv";
    std::string tsv_it = base + "_it.tsv";

    // For deck export, reuse earlier collected rows; for main export we reuse 'rows' from load_cards_from_db
    // We'll build a simple TSV with header and one row per aggregated card.
    std::ofstream out_en(tsv_en);
    std::ofstream out_it(tsv_it);
    if (out_en && out_it) {
        // Header (monolingua): name, type, colors, set_code, mana_cost, rarity, quantity, price_usd, foil
        auto write_tsv_header = [](std::ofstream &f){ f << "name\ttype\tcolors\tset_code\tmana_cost\trarity\tquantity\tprice_usd\tfoil\n"; };
        auto write_csv_header = [](std::ofstream &f){ f << "name,type,colors,set_code,mana_cost,rarity,quantity,price_usd,foil\n"; };
        if (format == "bilingual" || format == "tsv_en" || format == "tsv_it") {
            write_tsv_header(out_en);
            write_tsv_header(out_it);
        }
        if (format == "csv_en" || format == "csv_it") {
            // we'll write CSVs down below as needed
        }

        // Use aggregated rows for main DB or deck rows for deck export
        std::string export_filter = "";
        if (!deck) {
            if (state->search_entry) {
                const char* tt = gtk_editable_get_text(GTK_EDITABLE(state->search_entry));
                export_filter = tt ? tt : "";
            }
            auto trows = load_cards_from_db(state->db, export_filter, state->selected_deck_id, state->filter_no_deck);
            for (const auto &row : trows) {
                std::string name_en = row.count("english_name") && !row.at("english_name").empty() ? row.at("english_name") : row.at("name");
                std::string name_it = row.count("localized_name") && !row.at("localized_name").empty() ? row.at("localized_name") : name_en;
                std::string type_en = row.count("type") ? row.at("type") : "";
                std::string type_it = row.count("localized_type") && !row.at("localized_type").empty() ? row.at("localized_type") : type_en;
                std::string colors_raw = row.count("colors") ? row.at("colors") : "";
                // English
                {
                    std::string prev = current_language;
                    current_language = "en";
                    std::string colors_en = translate_colors(colors_raw.c_str());
                    std::string rarity_en = translate_rarity(row.count("rarity") ? row.at("rarity").c_str() : "");
                    current_language = prev;
                    // TSV EN
                    if (format == "bilingual" || format == "tsv_en") {
                        out_en << name_en << '\t' << type_en << '\t' << colors_en << '\t' << (row.count("set_code") ? row.at("set_code") : "") << '\t' << (row.count("mana_cost") ? row.at("mana_cost") : "") << '\t' << rarity_en << '\t' << (row.count("quantity") ? row.at("quantity") : "0") << '\t' << (row.count("price_usd") ? row.at("price_usd") : "") << '\t' << (row.count("foil") ? row.at("foil") : "0") << '\n';
                    }
                }
                // Italian
                {
                    std::string prev = current_language;
                    current_language = "it";
                    std::string colors_it = translate_colors(colors_raw.c_str());
                    std::string rarity_it = translate_rarity(row.count("rarity") ? row.at("rarity").c_str() : "");
                    current_language = prev;
                    if (format == "bilingual" || format == "tsv_it") {
                        out_it << name_it << '\t' << type_it << '\t' << colors_it << '\t' << (row.count("set_code") ? row.at("set_code") : "") << '\t' << (row.count("mana_cost") ? row.at("mana_cost") : "") << '\t' << rarity_it << '\t' << (row.count("quantity") ? row.at("quantity") : "0") << '\t' << (row.count("price_usd") ? row.at("price_usd") : "") << '\t' << (row.count("foil") ? row.at("foil") : "0") << '\n';
                    }
                }
            }
        } else {
            // Deck export: re-run the deck SQL to gather rows
            state->db->query(sql, [&](const std::map<std::string,std::string>& row) {
                std::string name = row.count("english_name") && !row.at("english_name").empty() ? row.at("english_name") : row.at("name");
                std::string lname = row.count("localized_name") && !row.at("localized_name").empty() ? row.at("localized_name") : row.at("name");
                std::string type_en = row.count("type") ? row.at("type") : "";
                std::string type_local = row.count("localized_type") && !row.at("localized_type").empty() ? row.at("localized_type") : type_en;
                // English
                {
                    std::string colors_en = row.count("colors") ? ( (current_language=="en") ? translate_colors(row.at("colors").c_str()) : translate_colors(row.at("colors").c_str()) ) : "";
                    std::string rarity_en = row.count("rarity") ? translate_rarity(row.at("rarity").c_str()) : "";
                    out_en << name << '\t' << type_en << '\t' << colors_en << '\t' << (row.count("set_code") ? row.at("set_code") : "") << '\t' << (row.count("mana_cost") ? row.at("mana_cost") : "") << '\t' << rarity_en << '\t' << (row.count("quantity") ? row.at("quantity") : "0") << '\t' << (row.count("price_usd") ? row.at("price_usd") : "") << '\t' << (row.count("foil") ? row.at("foil") : "0") << '\n';
                }
                // Italian
                {
                    std::string colors_it = row.count("colors") ? translate_colors(row.at("colors").c_str()) : "";
                    std::string rarity_it = row.count("rarity") ? translate_rarity(row.at("rarity").c_str()) : "";
                    out_it << lname << '\t' << type_local << '\t' << colors_it << '\t' << (row.count("set_code") ? row.at("set_code") : "") << '\t' << (row.count("mana_cost") ? row.at("mana_cost") : "") << '\t' << rarity_it << '\t' << (row.count("quantity") ? row.at("quantity") : "0") << '\t' << (row.count("price_usd") ? row.at("price_usd") : "") << '\t' << (row.count("foil") ? row.at("foil") : "0") << '\n';
                }
            }, params);
        }
            // If CSV requested, build CSV by converting TSV fields with quoting
            auto quote_csv = [](const std::string &s) {
                std::string out;
                out.push_back('"');
                for (char ch : s) {
                    if (ch == '"') out += "\"\"";
                    else out.push_back(ch);
                }
                out.push_back('"');
                return out;
            };
            // Close TSV of appropriate types
            if (format == "bilingual" || format == "tsv_en" || format == "tsv_it") {
                out_en.close(); out_it.close();
                std::cout << "Exported TSV files: " << tsv_en << " , " << tsv_it << std::endl;
            }
            // CSV generation for monolingua CSV formats
            if (format == "csv_en" || format == "csv_it") {
                // Re-run trows to write chosen CSV
                std::string csvfile = (format == "csv_en") ? (base + "_en.csv") : (base + "_it.csv");
                std::ofstream out_csv(csvfile);
                if (out_csv) {
                    write_csv_header(out_csv);
                    auto trows2 = load_cards_from_db(state->db, export_filter, state->selected_deck_id, state->filter_no_deck);
                    for (const auto &row : trows2) {
                        std::string name_en = row.count("english_name") && !row.at("english_name").empty() ? row.at("english_name") : row.at("name");
                        std::string name_it = row.count("localized_name") && !row.at("localized_name").empty() ? row.at("localized_name") : name_en;
                        std::string type_en = row.count("type") ? row.at("type") : "";
                        std::string type_it = row.count("localized_type") && !row.at("localized_type").empty() ? row.at("localized_type") : type_en;
                        std::string colors_raw = row.count("colors") ? row.at("colors") : "";
                        if (format == "csv_en") {
                            std::string prev = current_language; current_language = "en";
                            std::string colors_en = translate_colors(colors_raw.c_str());
                            std::string rarity_en = translate_rarity(row.count("rarity") ? row.at("rarity").c_str() : "");
                            current_language = prev;
                            out_csv << quote_csv(name_en) << "," << quote_csv(type_en) << "," << quote_csv(colors_en) << "," << quote_csv(row.count("set_code") ? row.at("set_code") : "") << "," << quote_csv(row.count("mana_cost") ? row.at("mana_cost") : "") << "," << quote_csv(rarity_en) << "," << (row.count("quantity") ? row.at("quantity") : "0") << "," << quote_csv(row.count("price_usd") ? row.at("price_usd") : "") << "," << (row.count("foil") ? row.at("foil") : "0") << "\n";
                        } else {
                            std::string prev = current_language; current_language = "it";
                            std::string colors_it2 = translate_colors(colors_raw.c_str());
                            std::string rarity_it2 = translate_rarity(row.count("rarity") ? row.at("rarity").c_str() : "");
                            current_language = prev;
                            out_csv << quote_csv(name_it) << "," << quote_csv(type_it) << "," << quote_csv(colors_it2) << "," << quote_csv(row.count("set_code") ? row.at("set_code") : "") << "," << quote_csv(row.count("mana_cost") ? row.at("mana_cost") : "") << "," << quote_csv(rarity_it2) << "," << (row.count("quantity") ? row.at("quantity") : "0") << "," << quote_csv(row.count("price_usd") ? row.at("price_usd") : "") << "," << (row.count("foil") ? row.at("foil") : "0") << "\n";
                        }
                    }
                    out_csv.close();
                    std::cout << "Exported CSV file: " << csvfile << std::endl;
                }
            }
    }
    return true;
}

// Types and helpers used by the "Aggiungi Carta al Deck" dialog
struct DeckAddItem {
    GtkWidget* row;
    GtkWidget* checkbox;
    GtkWidget* spin;
    int card_id;
    int available;
};

struct DeckAddOkCtx {
    AppState* state;
    GtkWidget* list_box;
    GtkWindow* dialog;
};

static void on_deck_add_cancel(GtkButton* button, gpointer user_data) {
    std::vector<DeckAddItem*>* items = (std::vector<DeckAddItem*>*)user_data;
    if (!items) return;
    GtkWidget* dialog = NULL;
    if (!items->empty() && items->at(0) && items->at(0)->row) {
        dialog = gtk_widget_get_ancestor(GTK_WIDGET(items->at(0)->row), GTK_TYPE_WINDOW);
    }
    // Unref stored widget refs and delete items
    for (auto p : *items) {
        if (!p) continue;
        if (p->row && G_IS_OBJECT(p->row)) g_object_unref(p->row);
        if (p->checkbox && G_IS_OBJECT(p->checkbox)) g_object_unref(p->checkbox);
        if (p->spin && G_IS_OBJECT(p->spin)) g_object_unref(p->spin);
        delete p;
    }
    delete items;
    if (dialog && GTK_IS_WIDGET(dialog)) gtk_window_destroy(GTK_WINDOW(dialog));
}

// Toggle the check button when a row is activated (double-click or Enter)
static void on_deck_add_row_activated(GtkListBox* box, GtkListBoxRow* row, gpointer user_data) {
    std::cout << "DEBUG: row-activated called for row=" << row << std::endl;
    GtkWidget* check = (GtkWidget*)g_object_get_data(G_OBJECT(row), "check");
    if (check) {
        gboolean sel = FALSE;
        g_object_get(G_OBJECT(check), "active", &sel, NULL);
        g_object_set(G_OBJECT(check), "active", (gboolean)!sel, NULL);
        gboolean after = FALSE;
        g_object_get(G_OBJECT(check), "active", &after, NULL);
        std::cout << "DEBUG: row-activated toggled check -> now=" << (after ? "true" : "false") << std::endl;
        // Also persist logical selection on the row
        g_object_set_data(G_OBJECT(row), "selected", GINT_TO_POINTER(after ? 1 : 0));
    }
}

// Toggle the check button on single click (pressed) using GtkGestureClick for GTK4
static void on_deck_add_row_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    GtkListBoxRow* row = GTK_LIST_BOX_ROW(user_data);
    std::cout << "DEBUG: row-pressed called for row=" << row << std::endl;
    if (!row) return;
    // Flip the persisted selection flag (don't directly toggle the check here to avoid
    // conflicting with the checkbutton's own event processing). We'll sync the visual
    // state in an idle callback to ensure the final state is the persisted one.
    gpointer selptr = g_object_get_data(G_OBJECT(row), "selected");
    gboolean cur = selptr ? (GPOINTER_TO_INT(selptr) != 0) : FALSE;
    gboolean newsel = !cur;
    g_object_set_data(G_OBJECT(row), "selected", GINT_TO_POINTER(newsel ? 1 : 0));
    std::cout << "DEBUG: row-pressed set persisted selected=" << (newsel ? "1" : "0") << " for row=" << row << std::endl;

    // Idle callback will force the check widget to match the persisted flag
    g_idle_add(+[](gpointer data) -> gboolean {
        GtkWidget* r = GTK_WIDGET(data);
        if (!r) return G_SOURCE_REMOVE;
        GtkWidget* ch = (GtkWidget*)g_object_get_data(G_OBJECT(r), "check");
        gpointer sp = g_object_get_data(G_OBJECT(r), "selected");
        gboolean want = sp ? (GPOINTER_TO_INT(sp) != 0) : FALSE;
            if (ch && GTK_IS_WIDGET(ch)) {
            // Use generic property set to avoid casting macros triggering runtime type checks
            g_object_set(G_OBJECT(ch), "active", want ? TRUE : FALSE, NULL);
            std::cout << "DEBUG: idle sync set check active=" << (want ? "true" : "false") << " for row=" << r << std::endl;
        }
        return G_SOURCE_REMOVE;
    }, row);
}

// Keep persisted selection in sync when the check button changes state
static void on_deck_row_check_toggled(GtkToggleButton* toggle, gpointer user_data) {
    GtkWidget* row = GTK_WIDGET(user_data);
    if (!row || !toggle) return;
    gboolean active = gtk_toggle_button_get_active(toggle);
    g_object_set_data(G_OBJECT(row), "selected", GINT_TO_POINTER(active ? 1 : 0));
    std::cout << "DEBUG: check toggled -> set row selected=" << (active ? "1" : "0") << " for row=" << row << std::endl;
}

static void on_deck_add_ok(GtkButton* button, gpointer user_data) {
    DeckAddOkCtx* ctx = (DeckAddOkCtx*)user_data;
    if (!ctx) return;
    AppState* state = ctx->state;
    if (!state || !state->db) return;
    GtkWidget* list_box = ctx->list_box;
    std::cout << "DEBUG: on_deck_add_ok called; selected_deck_id=" << state->selected_deck_id << std::endl;
    // Extra debug: count rows present in the add-dialog and log them so we can see why nothing
    // is being attempted when the user clicks Aggiungi.
    int debug_row_count = 0;
    GtkWidget* tmp_row = gtk_widget_get_first_child(GTK_WIDGET(list_box));
    while (tmp_row) {
        debug_row_count++;
        tmp_row = gtk_widget_get_next_sibling(tmp_row);
    }
    std::cout << "DEBUG: Aggiungi-Dialog contains rows=" << debug_row_count << std::endl;
    // Collect selected card ids
    std::vector<int> selected_ids;
    GtkWidget* row = gtk_widget_get_first_child(GTK_WIDGET(list_box));
    while (row) {
        gboolean checked = FALSE;
        gpointer selptr = g_object_get_data(G_OBJECT(row), "selected");
        if (selptr) checked = GPOINTER_TO_INT(selptr) != 0;
        else {
            GtkWidget* check = (GtkWidget*)g_object_get_data(G_OBJECT(row), "check");
            if (check) {
                gboolean act = FALSE;
                g_object_get(G_OBJECT(check), "active", &act, NULL);
                checked = act;
            }
        }
        if (checked) {
            int card_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "card_id"));
            selected_ids.push_back(card_id);
        }
        row = gtk_widget_get_next_sibling(row);
    }
    // Process selected ids: show a single dialog listing each selected card with Quantity and Sideboard
    if (!selected_ids.empty()) {
        GtkWidget *qdialog = create_styled_dialog(GTK_WINDOW(ctx->dialog), 480, 360);
        gtk_window_set_title(GTK_WINDOW(qdialog), "Aggiungi carte selezionate");
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_window_set_child(GTK_WINDOW(qdialog), vbox);
        GtkWidget *scrolled = gtk_scrolled_window_new();
        gtk_widget_set_vexpand(scrolled, TRUE);
        gtk_box_append(GTK_BOX(vbox), scrolled);
        GtkWidget *list = gtk_list_box_new();
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list);
        // For each selected id, create a row with Name | Spin | Side checkbox
        for (int cid : selected_ids) {
            int available = 0;
            if (!state->db->get_card_quantity(cid, available) || available <= 0) continue;
            std::map<std::string,std::string> crow;
            state->db->query("SELECT english_name, localized_name FROM cards WHERE id = ?", [&](const std::map<std::string,std::string>& r){ crow = r; }, std::vector<std::string>{std::to_string(cid)});
            std::string name_only = crow.count("localized_name") && !crow.at("localized_name").empty() ? crow.at("localized_name") : (crow.count("english_name") ? crow.at("english_name") : std::to_string(cid));
            GtkWidget *roww = gtk_list_box_row_new();
            GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(roww), hbox);
            GtkWidget *label = gtk_label_new(name_only.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_widget_set_hexpand(label, TRUE);
            gtk_box_append(GTK_BOX(hbox), label);
            GtkWidget *spin = gtk_spin_button_new_with_range(1, available, 1);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1);
            gtk_widget_set_size_request(spin, 80, -1);
            gtk_box_append(GTK_BOX(hbox), spin);
            GtkWidget *side = gtk_check_button_new_with_label("Sideboard");
            gtk_box_append(GTK_BOX(hbox), side);
            // store pointers
            g_object_set_data(G_OBJECT(roww), "card_id", GINT_TO_POINTER(cid));
            g_object_set_data(G_OBJECT(roww), "spin", spin);
            g_object_set_data(G_OBJECT(roww), "sidecheck", side);
            gtk_list_box_append(GTK_LIST_BOX(list), roww);
        }
        GtkWidget *btnbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_append(GTK_BOX(vbox), btnbox);
        GtkWidget *ok = gtk_button_new_with_label("Aggiungi");
        GtkWidget *cancel = gtk_button_new_with_label("Annulla");
        gtk_box_append(GTK_BOX(btnbox), ok);
        gtk_box_append(GTK_BOX(btnbox), cancel);
        // OK handler: iterate rows and perform additions
        struct OkCtx { AppState* state; GtkWidget* list; GtkWidget* add_dialog; GtkWindow* parent_add; };
        OkCtx* okctx = new OkCtx{state, list, qdialog, ctx->dialog};
        g_signal_connect(ok, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
            OkCtx* c = (OkCtx*)user_data;
            GtkWidget* row = gtk_widget_get_first_child(GTK_WIDGET(c->list));
            while (row) {
                int cid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "card_id"));
                GtkWidget* spin = (GtkWidget*)g_object_get_data(G_OBJECT(row), "spin");
                GtkWidget* side = (GtkWidget*)g_object_get_data(G_OBJECT(row), "sidecheck");
                int qty = 1;
                if (spin && GTK_IS_SPIN_BUTTON(spin)) qty = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
                gboolean sflag = FALSE;
                if (side) g_object_get(G_OBJECT(side), "active", &sflag, NULL);
                int side_i = sflag ? 1 : 0;
                if (c->state && c->state->db && qty > 0) {
                    bool ok = add_card_to_deck(c->state, cid, qty, c->state->selected_deck_id, side_i);
                    std::cout << "DEBUG: added card " << cid << " qty=" << qty << " side=" << side_i << " ok=" << ok << std::endl;
                }
                row = gtk_widget_get_next_sibling(row);
            }
            // Refresh and close both dialogs
            if (c->state) { refresh_card_list(c->state); populate_deck_menu(c->state); }
            if (c->add_dialog) gtk_window_destroy(GTK_WINDOW(c->add_dialog));
            if (c->parent_add) gtk_window_destroy(GTK_WINDOW(c->parent_add));
            delete c;
        }), okctx);
        // Cancel just close the quantity dialog and keep the add dialog open
        struct CancelCtx { GtkWidget* adddlg; };
        CancelCtx* cancelctx = new CancelCtx{qdialog};
        g_signal_connect(cancel, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
            CancelCtx* c = (CancelCtx*)user_data;
            if (c->adddlg) gtk_window_destroy(GTK_WINDOW(c->adddlg));
            delete c;
        }), cancelctx);
        gtk_window_present(GTK_WINDOW(qdialog));
        return;
    }

    // After processing all rows, refresh UI and deck menu and close dialog
    refresh_card_list(state);
    populate_deck_menu(state);
    // Debug: print deck contents after operation to help diagnose UI vs DB discrepancy
    if (state && state->db) {
        int did = state->selected_deck_id;
        std::cout << "DEBUG: on_deck_add_ok selected_deck_id=" << did << std::endl;
        try {
            state->db->query("SELECT id, name, quantity FROM cards WHERE deck_id = ?", [&](const std::map<std::string,std::string>& r) {
                std::cout << "DEBUG: deck row -> id=" << r.at("id") << ", name='" << r.at("name") << "', qty=" << r.at("quantity") << std::endl;
            }, std::vector<std::string>{std::to_string(did)});
        } catch (...) {
            std::cout << "DEBUG: error querying deck contents" << std::endl;
        }
    }
    // Destroy dialog
    if (ctx->dialog) gtk_window_destroy(GTK_WINDOW(ctx->dialog));
    delete ctx;
}

// Populate the File->Seleziona Deck submenu with decks from the DB
static void populate_deck_menu(AppState *state) {
    if (!state) return;
    if (!state->deck_menu) return;
    // Clear existing
    g_menu_remove_all(state->deck_menu);
    // If no DB, show disabled placeholder
    if (!state->db) {
        g_menu_append(state->deck_menu, "Nessun database", NULL);
        return;
    }
    // Add option to clear filter
    g_menu_append(state->deck_menu, "Torna al Database Principale", "app.clear_deck");
    // Populate decks
    state->db->query_decks([&](const std::map<std::string, std::string>& row) {
        const std::string id = row.at("id");
        const std::string name = row.at("name");
    // Create a submenu for this deck with actions: Apri deck, Curva Mana, Elimina Deck
    GMenu *deck_sub = g_menu_new();
    // Select/open deck (with deck id parameter)
    GMenuItem *sel_item = g_menu_item_new(translate("Apri").c_str(), NULL);
    g_menu_item_set_action_and_target_value(sel_item, "app.select_deck_id", g_variant_new_int32(std::stoi(id)));
    g_menu_append_item(deck_sub, sel_item);
    g_object_unref(sel_item);
    // Mana curve for this specific deck
    GMenuItem *curve_item = g_menu_item_new("Curva Mana", NULL);
    g_menu_item_set_action_and_target_value(curve_item, "app.mana_curve", g_variant_new_int32(std::stoi(id)));
    g_menu_append_item(deck_sub, curve_item);
    g_object_unref(curve_item);
    // Delete deck (calls app.delete_deck with deck id)
    GMenuItem *del_item = g_menu_item_new(translate("Elimina Deck").c_str(), NULL);
    g_menu_item_set_action_and_target_value(del_item, "app.delete_deck", g_variant_new_int32(std::stoi(id)));
    g_menu_append_item(deck_sub, del_item);
    g_object_unref(del_item);
    // Wrap submenu in a top-level item named after the deck
    GMenuItem *parent_item = g_menu_item_new(name.c_str(), NULL);
    g_menu_item_set_submenu(parent_item, G_MENU_MODEL(deck_sub));
    g_menu_append_item(state->deck_menu, parent_item);
    g_object_unref(parent_item);
    g_object_unref(deck_sub);
    });
}

// Action handler: select deck by id (used by the File->Seleziona Deck submenu)
static void on_select_deck_id(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    int deck_id = g_variant_get_int32(parameter);
    GtkWindow *parent = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(parent), "app_state");
    if (!state) return;
    state->selected_deck_id = deck_id;
    std::string deck_name = "";
    if (state->db) {
        state->db->query("SELECT name FROM decks WHERE id = ?", [&](const std::map<std::string, std::string>& row) {
            deck_name = row.at("name");
        }, std::vector<std::string>{std::to_string(deck_id)});
    }
    if (state->deck_button) {
        gtk_button_set_label(GTK_BUTTON(state->deck_button), deck_name.c_str());
        gtk_widget_set_visible(state->deck_button, TRUE);
    }
    if (state->deck_label) {
        std::string lbl = std::string("Filtrando: ") + deck_name;
        gtk_label_set_text(GTK_LABEL(state->deck_label), lbl.c_str());
        gtk_widget_set_visible(state->deck_label, TRUE);
    }
    if (state->deck_delete_button) {
        gtk_widget_set_visible(state->deck_delete_button, TRUE);
    }
    if (state->db_button) {
        gtk_widget_set_visible(state->db_button, TRUE);
    }
    refresh_card_list(state);

    // Update the add-card button label to 'Aggiungi Carte' when a deck is selected
    if (state->add_card_button) gtk_button_set_label(GTK_BUTTON(state->add_card_button), translate("Aggiungi Carte").c_str());
}


static void update_ui_texts(GtkWindow *window, AppState* state) {
    gtk_column_view_column_set_title(state->name_col, translate("Nome").c_str());
    gtk_column_view_column_set_title(state->type_col, translate("Tipo").c_str());
    gtk_column_view_column_set_title(state->colors_col, translate("Colori").c_str());
    gtk_column_view_column_set_title(state->mana_col, translate("Costo Mana").c_str());
    gtk_column_view_column_set_title(state->rarity_col, translate("Rarità").c_str());
    gtk_column_view_column_set_title(state->date_col, translate("Data di aggiunta").c_str());
    gtk_column_view_column_set_title(state->qty_col, translate("Quantità").c_str());
    // Change add button label when viewing a deck
    if (state->selected_deck_id != -1) {
        gtk_button_set_label(GTK_BUTTON(state->add_card_button), translate("Aggiungi Carte").c_str());
    } else {
        gtk_button_set_label(GTK_BUTTON(state->add_card_button), translate("Nuova Carta").c_str());
    }
    gtk_menu_button_set_label(GTK_MENU_BUTTON(state->file_button), translate("File").c_str());
    gtk_menu_button_set_label(GTK_MENU_BUTTON(state->view_button), translate("Visualizza").c_str());
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->search_entry), translate("Cerca per nome...").c_str());
    // Update total label (only show total quantity)
    char buf[128];
    int total_qty = 0;
    if (state->db) {
        state->db->query("SELECT quantity FROM cards", [&](const std::map<std::string, std::string>& row) {
            total_qty += std::stoi(row.at("quantity"));
        });
    }
    snprintf(buf, sizeof(buf), "%s: %d", translate("Totale carte").c_str(), total_qty);
    gtk_label_set_text(GTK_LABEL(state->total_cards_label), buf);
    // Also rebuild menus so menu labels reflect the selected language
    rebuild_menus_for_language(state);
}

// Rebuild menus to reflect current language. This updates the File and View menu models
// so menu item labels appear in the selected language.
static void rebuild_menus_for_language(AppState* state) {
    if (!state) return;
    // Rebuild File menu
    GMenu *new_file = g_menu_new();
    g_menu_append(new_file, translate("Nuovo Database").c_str(), "app.newdb");
    g_menu_append(new_file, translate("Apri Database").c_str(), "app.opendb");
    g_menu_append(new_file, translate("Crea Deck").c_str(), "app.create_deck");
    g_menu_append(new_file, translate("Esporta Database").c_str(), "app.export_db");
    g_menu_append(new_file, translate("Esporta Deck").c_str(), "app.export_deck");
    // Seleziona Deck submenu (reuse existing deck_menu model)
    if (state->deck_menu) {
        g_menu_append_submenu(new_file, translate("Seleziona Deck").c_str(), G_MENU_MODEL(state->deck_menu));
    }
    // Attach to file button
    if (state->file_button) {
        gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(state->file_button), G_MENU_MODEL(new_file));
    }
    if (state->file_menu) g_object_unref(state->file_menu);
    state->file_menu = new_file;

    // Rebuild View menu
    GMenu *new_view = g_menu_new();
    GMenu *lang_menu = g_menu_new();
    g_menu_append(lang_menu, "Italiano", "app.lang.it");
    g_menu_append(lang_menu, "English", "app.lang.en");
    g_menu_append_submenu(new_view, translate("Lingua").c_str(), G_MENU_MODEL(lang_menu));
    g_object_unref(lang_menu);
    // Notifications (checkable) and Preferences
    g_menu_append(new_view, translate("Notifiche").c_str(), "app.notifications");
    g_menu_append(new_view, translate("Preferenze").c_str(), "app.preferences");
    g_menu_append(new_view, translate("Filtri...").c_str(), "app.filters");
    if (state->view_button) gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(state->view_button), G_MENU_MODEL(new_view));
    if (state->view_menu) g_object_unref(state->view_menu);
    state->view_menu = new_view;
}

// Preferences dialog: allows toggling notifications and adjusting focus-retry parameters
static void on_preferences_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    GtkWindow *parent = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(parent), "app_state");
    if (!parent || !state) return;
    GtkWidget *dlg = create_styled_dialog(parent, 420, 220);
    gtk_window_set_title(GTK_WINDOW(dlg), "Preferences");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_window_set_child(GTK_WINDOW(dlg), vbox);

    // Notifications checkbox
    GtkWidget *notif_check = gtk_check_button_new_with_label("Notifiche");
    // Avoid direct GTK cast macros; set property via GObject to be safe
    g_object_set(G_OBJECT(notif_check), "active", g_notifications_enabled ? TRUE : FALSE, NULL);
    gtk_box_append(GTK_BOX(vbox), notif_check);

    // Focus retry spinbuttons
    GtkWidget *h1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label1 = gtk_label_new("Focus retry attempts:");
    gtk_label_set_xalign(GTK_LABEL(label1), 0.0);
    gtk_box_append(GTK_BOX(h1), label1);
    GtkWidget *spin_tries = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_tries), state->focus_retry_tries > 0 ? state->focus_retry_tries : g_focus_retry_tries_default);
    gtk_widget_set_hexpand(spin_tries, FALSE);
    gtk_box_append(GTK_BOX(h1), spin_tries);
    gtk_box_append(GTK_BOX(vbox), h1);

    GtkWidget *h2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label2 = gtk_label_new("Focus retry interval (ms):");
    gtk_label_set_xalign(GTK_LABEL(label2), 0.0);
    gtk_box_append(GTK_BOX(h2), label2);
    GtkWidget *spin_interval = gtk_spin_button_new_with_range(10, 5000, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_interval), state->focus_retry_interval_ms > 0 ? state->focus_retry_interval_ms : g_focus_retry_interval_ms_default);
    gtk_widget_set_hexpand(spin_interval, FALSE);
    gtk_box_append(GTK_BOX(h2), spin_interval);
    gtk_box_append(GTK_BOX(vbox), h2);

    // Buttons
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *save_btn = gtk_button_new_with_label("Save");
    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    gtk_box_append(GTK_BOX(btn_box), save_btn);
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);
    gtk_box_append(GTK_BOX(vbox), btn_box);

    // Cancel handler
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data){ GtkWindow* d = GTK_WINDOW(user_data); if (d) gtk_window_destroy(d); }), dlg);

    // Save handler: allocate context and attach as user_data to the signal
    struct PrefCtx { GtkWindow* dlg; AppState* state; GtkWidget* notif; GtkWidget* spin_tries; GtkWidget* spin_interval; };
    PrefCtx* pref_ctx = new PrefCtx();
    pref_ctx->dlg = GTK_WINDOW(dlg);
    pref_ctx->state = state;
    pref_ctx->notif = notif_check;
    pref_ctx->spin_tries = spin_tries;
    pref_ctx->spin_interval = spin_interval;
    g_signal_connect(save_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data){
        PrefCtx* ctx = (PrefCtx*)user_data;
        if (!ctx) return;
    gboolean n = FALSE;
    g_object_get(G_OBJECT(ctx->notif), "active", &n, NULL);
        int tries = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ctx->spin_tries));
        int interval = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ctx->spin_interval));
        // Update globals and state
        g_notifications_enabled = n ? true : false;
        g_focus_retry_tries_default = tries > 0 ? tries : g_focus_retry_tries_default;
        g_focus_retry_interval_ms_default = interval > 0 ? interval : g_focus_retry_interval_ms_default;
        if (ctx->state) {
            ctx->state->focus_retry_tries = g_focus_retry_tries_default;
            ctx->state->focus_retry_interval_ms = g_focus_retry_interval_ms_default;
        }
        // Persist
        save_settings();
        // Update action state (if present) so menus show the correct check
        GtkApplication* app = GTK_APPLICATION(gtk_window_get_application(ctx->dlg));
        if (app) {
            GSimpleAction* act = (GSimpleAction*)g_action_map_lookup_action(G_ACTION_MAP(app), "notifications");
            if (act) g_simple_action_set_state(act, g_variant_new_boolean(g_notifications_enabled));
        }
        // Rebuild menus to reflect changed labels/state
        if (ctx->state) rebuild_menus_for_language(ctx->state);
        // Close dialog
        if (ctx->dlg) gtk_window_destroy(GTK_WINDOW(ctx->dlg));
        delete ctx;
    }), pref_ctx);

    gtk_window_present(GTK_WINDOW(dlg));
}

// Funzione per ordinare la lista carte
static void sort_card_list(AppState* state, const char* column, bool asc) {
    if (!state || !state->card_store) return;
    guint n = g_list_model_get_n_items(G_LIST_MODEL(state->card_store));
    std::vector<CardRow*> items;
    for (guint i = 0; i < n; ++i) {
        CardRow* row = (CardRow*)g_list_model_get_item(G_LIST_MODEL(state->card_store), i);
        items.push_back(row);
    }
    auto cmp = [column, asc](CardRow* a, CardRow* b) -> bool {
        int res = 0;
        if (strcmp(column, "name") == 0) {
            res = g_strcmp0(a->name, b->name);
        } else if (strcmp(column, "type") == 0) {
            res = g_strcmp0(a->type, b->type);
        } else if (strcmp(column, "translated-colors") == 0) {
            res = g_strcmp0(a->translated_colors, b->translated_colors);
        } else if (strcmp(column, "total-mana-cost") == 0) {
            res = a->total_mana_cost - b->total_mana_cost;
        } else if (strcmp(column, "rarity") == 0) {
            res = g_strcmp0(a->rarity, b->rarity);
        } else if (strcmp(column, "quantity") == 0) {
            res = a->quantity - b->quantity;
        } else if (strcmp(column, "added_date") == 0) {
            const char* da = a->added_date ? a->added_date : "";
            const char* db = b->added_date ? b->added_date : "";
            res = strcmp(da, db);
        } else if (strcmp(column, "price_usd") == 0) {
            // Confronta come float per ordinamento numerico
            float pa = a->price_usd && strlen(a->price_usd) > 0 ? std::atof(a->price_usd) : 0.0f;
            float pb = b->price_usd && strlen(b->price_usd) > 0 ? std::atof(b->price_usd) : 0.0f;
            if (pa < pb) res = -1;
            else if (pa > pb) res = 1;
            else res = 0;
        }
        return asc ? res < 0 : res > 0;
    };
    std::sort(items.begin(), items.end(), cmp);
    g_list_store_remove_all(state->card_store);
    for (auto row : items) {
        g_list_store_append(state->card_store, row);
        g_object_unref(row);
    }
}

// Draw mana histogram onto a cairo surface
static void draw_mana_on_cairo(cairo_t* cr, int width, int height, const std::map<int,int>& counts, int total_cards, int cap_bucket) {
    cairo_pattern_t* bg = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgba(bg, 0.0, 0.07, 0.08, 0.12, 1.0);
    cairo_pattern_add_color_stop_rgba(bg, 1.0, 0.04, 0.05, 0.09, 1.0);
    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);

    const double accent_r = 0.54;
    const double accent_g = 0.43;
    const double accent_b = 1.00;

    const int margin = 48;
    int w = width - margin*2;
    int h = height - margin*2;
    int buckets = cap_bucket + 1;

    std::vector<double> vals(buckets, 0.0);
    int maxc = 1;
    int total = 0;
    for (int i=0;i<buckets;++i) {
        auto it = counts.find(i);
        if (it != counts.end()) vals[i] = (double)it->second;
        if ((int)vals[i] > maxc) maxc = (int)vals[i];
        total += (int)vals[i];
    }
    if (total_cards > 0) total = total_cards;

    double left = margin;
    double top = margin;
    double right = margin + w;
    double bottom = margin + h;

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
    int y_steps = 4;
    for (int s=0;s<=y_steps;++s) {
        double yy = top + (h * (double)s / (double)y_steps);
        cairo_move_to(cr, left, yy);
        cairo_line_to(cr, right, yy);
        cairo_stroke(cr);
    }

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, left, top);
    cairo_line_to(cr, left, bottom);
    cairo_line_to(cr, right, bottom);
    cairo_stroke(cr);

    std::vector<double> xs(buckets, 0.0), ys(buckets, 0.0);
    for (int i=0;i<buckets;++i) {
        double x = left + (w * ((i + 0.0) / (double)(buckets - 1 < 1 ? 1 : buckets - 1)));
        xs[i] = x;
        double v = vals[i];
        double y = bottom;
        if (maxc > 0) y = bottom - ((h - 24) * (v / (double)maxc));
        ys[i] = y;
    }

    cairo_new_path(cr);
    cairo_move_to(cr, xs[0], bottom);
    for (int i=0;i<buckets;++i) cairo_line_to(cr, xs[i], ys[i]);
    cairo_line_to(cr, xs.back(), bottom);
    cairo_close_path(cr);
    cairo_pattern_t* fill = cairo_pattern_create_linear(0, top, 0, bottom);
    cairo_pattern_add_color_stop_rgba(fill, 0.0, accent_r, accent_g, accent_b, 0.32);
    cairo_pattern_add_color_stop_rgba(fill, 1.0, accent_r, accent_g, accent_b, 0.08);
    cairo_set_source(cr, fill);
    cairo_fill(cr);
    cairo_pattern_destroy(fill);

    cairo_new_path(cr);
    cairo_set_source_rgb(cr, accent_r, accent_g, accent_b);
    cairo_set_line_width(cr, 2.8);
    cairo_move_to(cr, xs[0], ys[0]);
    for (int i=1;i<buckets;++i) cairo_line_to(cr, xs[i], ys[i]);
    cairo_stroke(cr);

    for (int i=0;i<buckets;++i) {
        double x = xs[i];
        double y = ys[i];
        cairo_arc(cr, x, y, 5.0, 0, 2*M_PI);
        cairo_set_source_rgba(cr, 0.06, 0.08, 0.12, 1.0);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, accent_r, accent_g, accent_b);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
        if (vals[i] > 0.5) {
            char tb[32]; snprintf(tb, sizeof(tb), "%d", (int)vals[i]);
            cairo_set_source_rgba(cr, 0.88, 0.92, 1.0, 0.9);
            cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 11.0);
            cairo_text_extents_t ext;
            cairo_text_extents(cr, tb, &ext);
            cairo_move_to(cr, x - ext.width/2.0, y - 10);
            cairo_show_text(cr, tb);
        }
    }

    cairo_set_source_rgba(cr, 0.88, 0.92, 1.0, 0.85);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);
    for (int i=0;i<buckets;++i) {
        char lbl[32];
        if (i < cap_bucket) snprintf(lbl, sizeof(lbl), "%d", i);
        else snprintf(lbl, sizeof(lbl), ">=%d", cap_bucket);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, lbl, &ext);
        double lx = xs[i] - ext.width/2.0 - ext.x_bearing;
        double ly = bottom + 20;
        cairo_move_to(cr, lx, ly);
        cairo_show_text(cr, lbl);
    }

    cairo_set_font_size(cr, 11.0);
    for (int s=0;s<=y_steps;++s) {
        double frac = (double)(y_steps - s) / (double)y_steps;
        int val = (int)round(frac * (double)maxc);
        char lb[32]; snprintf(lb, sizeof(lb), "%d", val);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, lb, &ext);
        double yy = top + (h * (double)s / (double)y_steps);
        double lx = left - 10 - ext.width;
        double ly = yy + ext.height/2.0;
        cairo_move_to(cr, lx, ly);
        cairo_show_text(cr, lb);
    }

    cairo_set_source_rgba(cr, 0.9, 0.95, 1.0, 0.92);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 18.0);
    cairo_move_to(cr, left, margin/2);
    cairo_show_text(cr, "Curva Mana");
}

// Create a cairo image surface containing the mana curve for the given counts
static cairo_surface_t* create_mana_surface(const std::map<int,int>& counts, int total_cards, int cap_bucket, int width=800, int height=400) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surface);
    draw_mana_on_cairo(cr, width, height, counts, total_cards, cap_bucket);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    return surface;
}

// Draw multiple series (label -> bucket->count) onto cairo surface with legend
static void draw_mana_multi_on_cairo(cairo_t* cr, int width, int height, const std::map<std::string, std::map<int,int>>& series, int cap_bucket) {
    cairo_pattern_t* bg = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgba(bg, 0.0, 0.07, 0.08, 0.12, 1.0);
    cairo_pattern_add_color_stop_rgba(bg, 0.5, 0.06, 0.07, 0.11, 1.0);
    cairo_pattern_add_color_stop_rgba(bg, 1.0, 0.04, 0.05, 0.09, 1.0);
    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);

    const int margin = 48;
    int w = width - margin*2;
    int h = height - margin*2;
    int buckets = cap_bucket + 1;

    int maxc = 1;
    for (auto &s : series) for (auto &p : s.second) if (p.second > maxc) maxc = p.second;

    double left = margin, top = margin, right = margin + w, bottom = margin + h;

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
    int y_steps = 4;
    for (int s=0;s<=y_steps;++s) {
        double yy = top + (h * (double)s / (double)y_steps);
        cairo_move_to(cr, left, yy);
        cairo_line_to(cr, right, yy);
        cairo_stroke(cr);
    }

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, left, top);
    cairo_line_to(cr, left, bottom);
    cairo_line_to(cr, right, bottom);
    cairo_stroke(cr);

    std::vector<std::array<double,3>> palette = {
        {0.54,0.43,1.00}, {0.96,0.44,0.75}, {0.31,0.79,0.99}, {0.49,0.92,0.61}, {0.99,0.76,0.43}, {0.48,0.66,1.00}, {0.78,0.78,0.90}
    };

    int si = 0;
    double legend_x = right - 180;
    double legend_y = top + 12;
    for (auto &s : series) {
        std::vector<double> xs(buckets), ys(buckets);
        for (int i=0;i<buckets;++i) {
            double x = left + (w * ((i + 0.0) / (double)(buckets - 1 < 1 ? 1 : buckets - 1)));
            xs[i] = x;
            int v = 0;
            auto it = s.second.find(i);
            if (it != s.second.end()) v = it->second;
            double y = bottom;
            if (maxc > 0) y = bottom - ((h - 24) * (v / (double)maxc));
            ys[i] = y;
        }

        std::array<double,3> col = palette[si % palette.size()];
        std::string key = s.first;
        bool dashed = false;
        auto lower_key = key; std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        if (lower_key == "w" || lower_key.find("white") != std::string::npos || lower_key.find("bianco") != std::string::npos) {
            col = {0.88,0.88,0.86};
            dashed = true;
        } else if (lower_key == "u" || lower_key.find("blue") != std::string::npos || lower_key.find("blu") != std::string::npos) {
            col = {0.31,0.79,0.99};
        } else if (lower_key == "b" || lower_key.find("black") != std::string::npos || lower_key.find("nero") != std::string::npos) {
            col = {0.16,0.16,0.18};
        } else if (lower_key == "r" || lower_key.find("red") != std::string::npos || lower_key.find("rosso") != std::string::npos) {
            col = {0.96,0.44,0.30};
        } else if (lower_key == "g" || lower_key.find("green") != std::string::npos || lower_key.find("verde") != std::string::npos) {
            col = {0.49,0.92,0.61};
        } else if (lower_key == "c" || lower_key.find("colorless") != std::string::npos || lower_key.find("incolore") != std::string::npos) {
            col = {0.90,0.52,0.98};
        }

        cairo_new_path(cr);
        cairo_move_to(cr, xs[0], bottom);
        for (int i=0;i<buckets;++i) cairo_line_to(cr, xs[i], ys[i]);
        cairo_line_to(cr, xs.back(), bottom);
        cairo_close_path(cr);
        cairo_pattern_t* fill = cairo_pattern_create_linear(0, top, 0, bottom);
        cairo_pattern_add_color_stop_rgba(fill, 0.0, col[0], col[1], col[2], 0.24);
        cairo_pattern_add_color_stop_rgba(fill, 1.0, col[0], col[1], col[2], 0.04);
        cairo_set_source(cr, fill);
        cairo_fill(cr);
        cairo_pattern_destroy(fill);

        cairo_new_path(cr);
        cairo_set_source_rgb(cr, col[0], col[1], col[2]);
        cairo_set_line_width(cr, 2.6);
        if (dashed) {
            double dashes[] = {6.0,6.0};
            cairo_set_dash(cr, dashes, 2, 0);
        }
        cairo_move_to(cr, xs[0], ys[0]);
        for (int i=1;i<buckets;++i) cairo_line_to(cr, xs[i], ys[i]);
        cairo_stroke(cr);
        if (dashed) cairo_set_dash(cr, NULL, 0, 0);

        for (int i=0;i<buckets;++i) {
            cairo_arc(cr, xs[i], ys[i], 4.0, 0, 2*M_PI);
            cairo_set_source_rgba(cr, 0.06,0.08,0.12,1.0);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, col[0], col[1], col[2]);
            cairo_set_line_width(cr, 1.4);
            cairo_stroke(cr);
        }

        double lx = legend_x;
        double ly = legend_y + si*24;
        cairo_save(cr);
        cairo_translate(cr, lx, ly);
        cairo_new_path(cr);
        cairo_arc(cr, 8, -2, 6, M_PI/2, 3*M_PI/2);
        cairo_arc(cr, 28, -2, 6, 3*M_PI/2, M_PI/2);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, col[0], col[1], col[2], 0.18);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, col[0], col[1], col[2]);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);
        cairo_restore(cr);
    cairo_set_source_rgba(cr, 0.9, 0.95, 1.0, 0.9);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12.0);
        cairo_move_to(cr, lx + 30, ly + 2);
        cairo_show_text(cr, s.first.c_str());
        si++;
    }

    cairo_set_source_rgba(cr, 0.88, 0.92, 1.0, 0.85);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);
    for (int i=0;i<buckets;++i) {
        char lbl[32];
        if (i < cap_bucket) snprintf(lbl, sizeof(lbl), "%d", i);
        else snprintf(lbl, sizeof(lbl), ">=%d", cap_bucket);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, lbl, &ext);
        double lx = left + (w * ((i + 0.0) / (double)(buckets - 1 < 1 ? 1 : buckets - 1))) - ext.width/2.0 - ext.x_bearing;
        double ly = bottom + 20;
        cairo_move_to(cr, lx, ly);
        cairo_show_text(cr, lbl);
    }

    cairo_set_font_size(cr, 11.0);
    for (int s=0;s<=y_steps;++s) {
        double frac = (double)(y_steps - s) / (double)y_steps;
        int val = (int)round(frac * (double)maxc);
        char lb[32]; snprintf(lb, sizeof(lb), "%d", val);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, lb, &ext);
        double yy = top + (h * (double)s / (double)y_steps);
        double lx = left - 10 - ext.width;
        double ly = yy + ext.height/2.0;
        cairo_move_to(cr, lx, ly);
        cairo_show_text(cr, lb);
    }

    cairo_set_source_rgba(cr, 0.9, 0.95, 1.0, 0.92);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 18.0);
    cairo_move_to(cr, left, margin/2);
    cairo_show_text(cr, "Curva Mana");
}

static cairo_surface_t* create_mana_surface_multi(const std::map<std::string, std::map<int,int>>& series, int cap_bucket, int width=800, int height=400) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surface);
    draw_mana_multi_on_cairo(cr, width, height, series, cap_bucket);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    return surface;
}

// Draw function for GtkDrawingArea (GTK4). Paints the pre-rendered cairo surface.
static void on_mana_area_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)user_data;
    gpointer surf_ptr = g_object_get_data(G_OBJECT(area), "mana_surface");
    if (!surf_ptr) {
        std::cout << "DEBUG: on_mana_area_draw called but no surface stored on widget" << std::endl;
        return;
    }
    cairo_surface_t* surf = (cairo_surface_t*)surf_ptr;
    std::cout << "DEBUG: on_mana_area_draw surf=" << (void*)surf << std::endl;
    int sw = cairo_image_surface_get_width(surf);
    int sh = cairo_image_surface_get_height(surf);
    // scale the surface to fit the allocation if needed
    double sx = (double)width / (double)sw;
    double sy = (double)height / (double)sh;
    double s = sx < sy ? sx : sy;
    if (s <= 0) s = 1.0;
    cairo_save(cr);
    cairo_scale(cr, s, s);
    cairo_set_source_surface(cr, surf, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

// Action handler: show mana curve for deck (parameter int deck id) or current selected deck
static void on_mana_curve_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    GtkWindow* parent = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(parent), "app_state");
    if (!state || !state->db) return;
    int deck_id = -1;
    if (parameter && g_variant_is_of_type(parameter, G_VARIANT_TYPE_INT32)) deck_id = g_variant_get_int32(parameter);
    if (deck_id <= 0) {
        if (state->selected_deck_id > 0) deck_id = state->selected_deck_id;
        else deck_id = -1;
    }
    std::map<int,int> counts;
    int total_cards = 0;
    const int cap = 10;
    // detailed info per bucket: cost -> list of (name, qty)
    std::map<int, std::vector<std::pair<std::string,int>>> bucket_details;
    std::map<std::string,int> color_counts;
    std::map<std::string,int> type_summary = {
        {"Land",0}, {"Creature",0}, {"Instant",0}, {"Sorcery",0}, {"Enchantment",0}, {"Artifact",0}, {"Planeswalker",0}, {"Other",0}
    };
    int land_cards = 0;
    int creature_cards = 0;
    int non_creature_spells = 0;
    double sum_all_cmc = 0.0;
    double sum_non_land_cmc = 0.0;
    int count_non_land_cards = 0;
    double sum_creature_cmc = 0.0;
    int count_creature_cards = 0;
    double sum_non_creature_cmc = 0.0;
    int count_non_creature_cards = 0;
    const int INF_TURN = 99;
    std::map<std::string,int> color_sources = {
        {"W",0},{"U",0},{"B",0},{"R",0},{"G",0},{"C",0}
    };
    std::map<std::string,int> pip_counts = {
        {"W",0},{"U",0},{"B",0},{"R",0},{"G",0},{"C",0}
    };
    std::map<std::string,int> pip_single_turn = {
        {"W",INF_TURN},{"U",INF_TURN},{"B",INF_TURN},{"R",INF_TURN},{"G",INF_TURN},{"C",INF_TURN}
    };
    std::map<std::string,int> pip_double_turn = pip_single_turn;
    std::map<std::string,int> pip_triple_turn = pip_single_turn;
    std::vector<std::string> type_keys = {"Creature","Instant","Sorcery","Enchantment","Artifact","Planeswalker","Other"};
    std::map<std::string, std::map<int,int>> series_by_type;
    for (const auto &key : type_keys) series_by_type[key] = std::map<int,int>();
    std::vector<std::string> color_keys = {"W","U","B","R","G","C"};
    std::map<std::string, std::map<int,int>> series_by_color;
    for (const auto &key : color_keys) series_by_color[key] = std::map<int,int>();
    int removal_spells = 0;
    int ramp_spells = 0;
    int card_draw_spells = 0;
    int board_wipes = 0;
    struct CardRoleFlags {
        bool is_land = false;
        bool is_creature = false;
        bool is_planeswalker = false;
        bool is_instant = false;
        bool is_sorcery = false;
        bool is_enchantment = false;
        bool is_artifact = false;
    };

    auto classify_card = [&](const std::string& type_val) {
        CardRoleFlags info;
        std::string lower = type_val;
        lower.reserve(type_val.size());
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        info.is_land = lower.find("land") != std::string::npos || lower.find("terra") != std::string::npos;
        info.is_creature = lower.find("creature") != std::string::npos || lower.find("creatura") != std::string::npos;
        info.is_planeswalker = lower.find("planeswalker") != std::string::npos;
        info.is_instant = lower.find("instant") != std::string::npos || lower.find("istantaneo") != std::string::npos;
        info.is_sorcery = lower.find("sorcery") != std::string::npos || lower.find("stregoneria") != std::string::npos;
        info.is_enchantment = lower.find("enchantment") != std::string::npos || lower.find("incantesimo") != std::string::npos;
        info.is_artifact = lower.find("artifact") != std::string::npos || lower.find("artefatto") != std::string::npos;
        return info;
    };

    auto accumulate_card_stats = [&](const CardRoleFlags& info, int qty, int cost) {
        sum_all_cmc += cost * qty;

        if (info.is_land) {
            land_cards += qty;
            type_summary["Land"] += qty;
            return;
        }

        sum_non_land_cmc += cost * qty;
        count_non_land_cards += qty;

        bool assigned = false;
        if (info.is_creature) {
            creature_cards += qty;
            sum_creature_cmc += cost * qty;
            count_creature_cards += qty;
            type_summary["Creature"] += qty;
            assigned = true;
        } else {
            non_creature_spells += qty;
            sum_non_creature_cmc += cost * qty;
            count_non_creature_cards += qty;
        }

        if (!assigned && info.is_planeswalker) {
            type_summary["Planeswalker"] += qty;
            assigned = true;
        }
        if (!assigned && info.is_instant) {
            type_summary["Instant"] += qty;
            assigned = true;
        }
        if (!assigned && info.is_sorcery) {
            type_summary["Sorcery"] += qty;
            assigned = true;
        }
        if (!assigned && info.is_enchantment) {
            type_summary["Enchantment"] += qty;
            assigned = true;
        }
        if (!assigned && info.is_artifact) {
            type_summary["Artifact"] += qty;
            assigned = true;
        }
        if (!assigned) {
            type_summary["Other"] += qty;
        }
    };
    auto register_color = [&](const std::string& raw, int qty) {
        std::string norm;
        norm.reserve(raw.size());
        for (char ch : raw) {
            if (!std::isspace((unsigned char)ch)) {
                norm.push_back(std::toupper((unsigned char)ch));
            }
        }
        if (norm.empty()) norm = "C";
        if (norm == "WHITE" || norm == "BIANCO") norm = "W";
        else if (norm == "BLUE" || norm == "BLU") norm = "U";
        else if (norm == "BLACK" || norm == "NERO") norm = "B";
        else if (norm == "RED" || norm == "ROSSO") norm = "R";
        else if (norm == "GREEN" || norm == "VERDE") norm = "G";
        else if (norm == "COLORLESS" || norm == "INCOLORE") norm = "C";
        else if (norm == "AZORIUS") norm = "WU";
        else if (norm == "DIMIR") norm = "UB";
        else if (norm == "RAKDOS") norm = "BR";
        else if (norm == "GRUUL") norm = "RG";
        else if (norm == "SELESNYA") norm = "GW";
        else if (norm.size() > 1 && norm != "WU" && norm != "UB" && norm != "BR" && norm != "RG" && norm != "GW") {
            // fallback to first character for unknown multi-color strings
            norm = std::string(1, norm[0]);
        }
        color_counts[norm] += qty;
        return norm;
    };
    auto normalize_color_symbol = [](char ch) -> std::string {
        char up = std::toupper((unsigned char)ch);
        switch (up) {
            case 'W': case 'U': case 'B': case 'R': case 'G': return std::string(1, up);
            case 'C': return std::string("C");
            case 'S': return std::string("C");
            default: return std::string();
        }
    };
    auto update_type_series = [&](const CardRoleFlags& info, int bucket, int qty) {
        if (qty <= 0) return;
        if (bucket < 0) bucket = 0;
        if (info.is_land) return;
        bool matched = false;
        if (info.is_creature) { series_by_type["Creature"][bucket] += qty; matched = true; }
        if (info.is_instant) { series_by_type["Instant"][bucket] += qty; matched = true; }
        if (info.is_sorcery) { series_by_type["Sorcery"][bucket] += qty; matched = true; }
        if (info.is_enchantment) { series_by_type["Enchantment"][bucket] += qty; matched = true; }
        if (info.is_artifact) { series_by_type["Artifact"][bucket] += qty; matched = true; }
        if (info.is_planeswalker) { series_by_type["Planeswalker"][bucket] += qty; matched = true; }
        if (!matched) series_by_type["Other"][bucket] += qty;
    };
    auto append_color_series_from_token = [&](const std::string& token, int bucket, int qty) {
        if (qty <= 0 || token.empty()) return false;
        bool appended = false;
        for (char ch : token) {
            std::string sym = normalize_color_symbol(ch);
            if (!sym.empty()) {
                series_by_color[sym][bucket] += qty;
                appended = true;
            }
        }
        return appended;
    };
    auto to_lower_copy = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return s;
    };
    auto to_upper_copy = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::toupper(c); });
        return s;
    };
    auto contains_any = [](const std::string& haystack, const std::vector<std::string>& needles) {
        for (const auto& needle : needles) {
            if (!needle.empty() && haystack.find(needle) != std::string::npos) return true;
        }
        return false;
    };
    auto parse_mana_symbols = [&](const std::string& mana) {
        std::map<std::string,int> out;
        if (mana.empty()) return out;
        std::string token;
        bool inside = false;
        bool saw_braces = false;
        auto flush = [&](const std::string& raw) {
            for (char ch : raw) {
                std::string symbol = normalize_color_symbol(ch);
                if (!symbol.empty()) out[symbol] += 1;
            }
        };
        for (char ch : mana) {
            if (ch == '{') {
                inside = true;
                saw_braces = true;
                token.clear();
            } else if (ch == '}') {
                flush(token);
                token.clear();
                inside = false;
            } else if (inside) {
                token.push_back(ch);
            } else if (!saw_braces) {
                flush(std::string(1, ch));
            }
        }
        if (inside && !token.empty()) flush(token);
        return out;
    };
    auto note_color_source = [&](const std::string& color, int qty) {
        if (qty <= 0 || color.empty()) return;
        std::string normalized = color.size() == 1 ? normalize_color_symbol(color[0]) : normalize_color_symbol(color.front());
        if (normalized.empty()) return;
        if (!color_sources.count(normalized)) {
            color_sources[normalized] = 0;
            pip_counts[normalized] = 0;
            pip_single_turn[normalized] = INF_TURN;
            pip_double_turn[normalized] = INF_TURN;
            pip_triple_turn[normalized] = INF_TURN;
        }
        color_sources[normalized] += qty;
    };
    auto register_land_sources_from_row = [&](const std::map<std::string,std::string>& row, const CardRoleFlags& info, int qty, const std::string& type_line, const std::string& oracle_text) {
        if (!info.is_land || qty <= 0) return;
        std::set<std::string> produced;
        std::string colors_raw = row.count("colors") ? row.at("colors") : "";
        if (!colors_raw.empty()) {
            try {
                auto parsed = nlohmann::json::parse(colors_raw);
                if (parsed.is_array()) {
                    for (auto &el : parsed) {
                        if (el.is_string()) {
                            std::string token = el.get<std::string>();
                            for (char ch : token) {
                                std::string sym = normalize_color_symbol(ch);
                                if (!sym.empty()) produced.insert(sym);
                            }
                        }
                    }
                }
            } catch (...) {
                for (char ch : colors_raw) {
                    std::string sym = normalize_color_symbol(ch);
                    if (!sym.empty()) produced.insert(sym);
                }
            }
        }
        if (produced.empty()) {
            std::string type_upper = to_upper_copy(type_line);
            auto match_type = [&](const std::vector<std::string>& needles, const std::string& sym) {
                for (const auto& needle : needles) {
                    if (!needle.empty() && type_upper.find(needle) != std::string::npos) {
                        produced.insert(sym);
                        break;
                    }
                }
            };
            match_type({"PLAINS","PIANURA"}, "W");
            match_type({"ISLAND","ISOLA"}, "U");
            match_type({"SWAMP","PALUDE"}, "B");
            match_type({"MOUNTAIN","MONTAGNA"}, "R");
            match_type({"FOREST","FORESTA"}, "G");
        }
        if (produced.empty()) {
            std::string text_upper = to_upper_copy(oracle_text);
            auto match_symbol = [&](char symbol, const std::string& sym) {
                std::string needle = std::string("{") + symbol + "}";
                if (text_upper.find(needle) != std::string::npos) produced.insert(sym);
            };
            match_symbol('W', "W");
            match_symbol('U', "U");
            match_symbol('B', "B");
            match_symbol('R', "R");
            match_symbol('G', "G");
            if (contains_any(text_upper, {"ANY COLOR", "QUALSIASI COLORE", "QUALUNQUE COLORE"})) {
                produced.insert("W"); produced.insert("U"); produced.insert("B"); produced.insert("R"); produced.insert("G");
            }
        }
        if (produced.empty()) produced.insert("C");
        for (const auto& sym : produced) note_color_source(sym, qty);
    };
    auto analyze_oracle_roles = [&](const std::string& oracle_text, const CardRoleFlags& info, int qty) {
        if (qty <= 0 || oracle_text.empty()) return;
        if (info.is_land) return;
        std::string text_lower = to_lower_copy(oracle_text);
        bool boardwipe = contains_any(text_lower, {"destroy all creatures", "destroy all permanents", "tutte le creature", "ogni creatura", "each creature", "all creatures"});
        bool removal_target = contains_any(text_lower, {"destroy target", "distruggi", "exile target", "esilia", "fight target", "lotta con", "damage to target", "infligge", "-x/-x", "sacrifica la creatura bersaglio"});
        bool mentions_target = contains_any(text_lower, {"target creature", "creatura bersaglio", "target planeswalker", "planeswalker bersaglio", "target permanent", "permanente bersaglio", "any target", "bersaglio qualsiasi"});
        if (boardwipe) {
            board_wipes += qty;
            removal_spells += qty;
        } else if (removal_target && mentions_target) {
            removal_spells += qty;
        }
        bool ramp = contains_any(text_lower, {"add {", "aggiungi {", "search your library for a land", "search your library for up to one land", "cerca nel tuo grimorio una carta terra", "cerca nel tuo grimorio fino a una carta terra", "put a land card", "metti una carta terra", "create a treasure", "crea una pedina tesoro", "mana of any color", "mana di qualsiasi colore", "mana di un qualsiasi colore"});
        if (ramp) {
            ramp_spells += qty;
        }
        bool draw = contains_any(text_lower, {"draw a card", "draw two cards", "draw three cards", "pesca una carta", "pesca due carte", "pesca tre carte", "pesca una carta per"});
        if (draw) {
            card_draw_spells += qty;
        }
    };
    if (deck_id == -1) {
        // Build counts from the current filtered main view (use aggregated loader and apply UI filters)
        std::string filter = "";
        if (state->search_entry) {
            const char* text = gtk_editable_get_text(GTK_EDITABLE(state->search_entry));
            filter = text ? text : "";
        }
        auto rows = load_cards_from_db(state->db, filter, -1, state->filter_no_deck);
        for (const auto &row : rows) {
            // Apply UI filters the same way refresh_card_list does
            if (!state->filter_colors.empty()) {
                std::set<std::string> card_colors;
                if (row.count("colors") && !row.at("colors").empty()) {
                    try {
                        auto j = nlohmann::json::parse(row.at("colors"));
                        if (j.is_array()) for (auto &el : j) if (el.is_string()) card_colors.insert(el.get<std::string>());
                    } catch (...) {
                        std::string tmp = row.at("colors");
                        for (char ch : tmp) if (!isspace((unsigned char)ch)) card_colors.insert(std::string(1,ch));
                    }
                }
                if (card_colors != state->filter_colors) continue;
            }
            if (!state->filter_rarities.empty()) {
                std::string card_rarity = row.count("rarity") ? row.at("rarity") : "";
                std::transform(card_rarity.begin(), card_rarity.end(), card_rarity.begin(), ::tolower);
                if (state->filter_rarities.count(card_rarity) == 0) continue;
            }
            if (!state->filter_types.empty()) {
                std::string card_type_en = row.count("type") ? row.at("type") : "";
                std::string card_type_local = row.count("localized_type") && !row.at("localized_type").empty() ? row.at("localized_type") : card_type_en;
                auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
                std::string cte = lower(card_type_en);
                std::string ctl = lower(card_type_local);
                bool matched = false;
                for (const auto &fk : state->filter_types) {
                    std::string prev_lang = current_language;
                    current_language = "it";
                    std::string fk_it = translate_type(fk.c_str());
                    current_language = prev_lang;
                    std::string fk_en_l = lower(fk);
                    std::string fk_it_l = lower(fk_it);
                    if ((cte.find(fk_en_l) != std::string::npos) || (ctl.find(fk_en_l) != std::string::npos) || (cte.find(fk_it_l) != std::string::npos) || (ctl.find(fk_it_l) != std::string::npos)) { matched = true; break; }
                }
                if (!matched) continue;
            }
            if (state->filter_foil != -1) {
                int foil = 0;
                if (row.count("foil")) try { foil = std::stoi(row.at("foil")); } catch(...) { foil = 0; }
                if (state->filter_foil != foil) continue;
            }
            // Quantity: use aggregated quantity fields if present
            int qty = 0;
            if (row.count("quantity_total")) {
                try { qty = std::stoi(row.at("quantity_total")); } catch(...) { qty = 0; }
            } else if (row.count("quantity")) {
                try { qty = std::stoi(row.at("quantity")); } catch(...) { qty = 0; }
            } else {
                qty = 1;
            }
            std::string mana = row.count("mana_cost") ? row.at("mana_cost") : "";
            int cost = calculate_total_mana_cost(mana);
            if (cost < 0) cost = 0;
            int bucket = cost >= cap ? cap : cost;
            counts[cost] += qty;
            total_cards += qty;
            std::string cname = "";
            if (row.count("localized_name") && !row.at("localized_name").empty()) cname = row.at("localized_name");
            else if (row.count("english_name") && !row.at("english_name").empty()) cname = row.at("english_name");
            else if (row.count("name")) cname = row.at("name");
            bucket_details[cost].push_back({cname, qty});
            std::string ctype = row.count("type") ? row.at("type") : "";
            if (row.count("localized_type") && !row.at("localized_type").empty()) {
                if (!ctype.empty()) ctype += " ";
                ctype += row.at("localized_type");
            }
            CardRoleFlags info = classify_card(ctype);
            std::string oracle_text = row.count("oracle_text") ? row.at("oracle_text") : "";
            register_land_sources_from_row(row, info, qty, ctype, oracle_text);
            accumulate_card_stats(info, qty, cost);
            update_type_series(info, bucket, qty);
            if (!info.is_land) {
                auto pip_map = parse_mana_symbols(mana);
                int turn = cost > 0 ? cost : 1;
                for (auto &pip : pip_map) {
                    if (!pip_single_turn.count(pip.first)) {
                        pip_single_turn[pip.first] = INF_TURN;
                        pip_double_turn[pip.first] = INF_TURN;
                        pip_triple_turn[pip.first] = INF_TURN;
                    }
                    pip_counts[pip.first] += pip.second * qty;
                    if (pip.second >= 1) pip_single_turn[pip.first] = std::min(pip_single_turn[pip.first], turn);
                    if (pip.second >= 2) pip_double_turn[pip.first] = std::min(pip_double_turn[pip.first], turn);
                    if (pip.second >= 3) pip_triple_turn[pip.first] = std::min(pip_triple_turn[pip.first], turn);
                }
            }
            analyze_oracle_roles(oracle_text, info, qty);
            // colors
            std::string cols = row.count("colors") ? row.at("colors") : "";
            bool appended_color = false;
            if (!cols.empty()) {
                try {
                    auto j = nlohmann::json::parse(cols);
                    if (j.is_array()) {
                        for (auto &c : j) {
                            if (!c.is_string()) continue;
                            std::string normalized = register_color(c.get<std::string>(), qty);
                            appended_color = append_color_series_from_token(normalized, bucket, qty) || appended_color;
                        }
                    }
                } catch(...) {
                    for (char ch : cols) {
                        if (!std::isalpha((unsigned char)ch)) continue;
                        std::string normalized = register_color(std::string(1, ch), qty);
                        appended_color = append_color_series_from_token(normalized, bucket, qty) || appended_color;
                    }
                }
            }
            if (!appended_color) {
                std::string normalized = register_color("C", qty);
                append_color_series_from_token(normalized, bucket, qty);
            }
        }
    } else {
        // Query DB for mana_cost, name, quantity and colors in this deck
    std::string oracle_field = state->has_oracle_text_column ? "oracle_text" : "'' AS oracle_text";
    std::string deck_sql = "SELECT english_name, localized_name, name, localized_type, type, mana_cost, " + oracle_field + ", quantity, colors FROM cards WHERE deck_id = ?";
    state->db->query(deck_sql, [&](const std::map<std::string,std::string>& row){
            std::string mana = row.count("mana_cost") ? row.at("mana_cost") : "";
            int qty = 1;
            try { if (row.count("quantity") && !row.at("quantity").empty()) qty = std::stoi(row.at("quantity")); } catch(...) { qty = 1; }
            int cost = calculate_total_mana_cost(mana);
            if (cost < 0) cost = 0;
            int bucket = cost >= cap ? cap : cost;
            counts[cost] += qty;
            total_cards += qty;
            // name preference: localized, english, raw name
            std::string cname = "";
            if (row.count("localized_name") && !row.at("localized_name").empty()) cname = row.at("localized_name");
            else if (row.count("english_name") && !row.at("english_name").empty()) cname = row.at("english_name");
            else if (row.count("name")) cname = row.at("name");
            bucket_details[cost].push_back({cname, qty});
            std::string ctype = row.count("type") ? row.at("type") : "";
            if (row.count("localized_type") && !row.at("localized_type").empty()) {
                if (!ctype.empty()) ctype += " ";
                ctype += row.at("localized_type");
            }
            CardRoleFlags info = classify_card(ctype);
            std::string oracle_text = row.count("oracle_text") ? row.at("oracle_text") : "";
            register_land_sources_from_row(row, info, qty, ctype, oracle_text);
            accumulate_card_stats(info, qty, cost);
            update_type_series(info, bucket, qty);
            if (!info.is_land) {
                auto pip_map = parse_mana_symbols(mana);
                int turn = cost > 0 ? cost : 1;
                for (auto &pip : pip_map) {
                    if (!pip_single_turn.count(pip.first)) {
                        pip_single_turn[pip.first] = INF_TURN;
                        pip_double_turn[pip.first] = INF_TURN;
                        pip_triple_turn[pip.first] = INF_TURN;
                    }
                    pip_counts[pip.first] += pip.second * qty;
                    if (pip.second >= 1) pip_single_turn[pip.first] = std::min(pip_single_turn[pip.first], turn);
                    if (pip.second >= 2) pip_double_turn[pip.first] = std::min(pip_double_turn[pip.first], turn);
                    if (pip.second >= 3) pip_triple_turn[pip.first] = std::min(pip_triple_turn[pip.first], turn);
                }
            }
            analyze_oracle_roles(oracle_text, info, qty);
            // colors parsing: either JSON array or compact codes
            std::string cols = row.count("colors") ? row.at("colors") : "";
            bool appended_color = false;
            if (!cols.empty()) {
                try {
                    auto j = nlohmann::json::parse(cols);
                    if (j.is_array()) {
                        for (auto &c : j) {
                            if (!c.is_string()) continue;
                            std::string normalized = register_color(c.get<std::string>(), qty);
                            appended_color = append_color_series_from_token(normalized, bucket, qty) || appended_color;
                        }
                    }
                } catch(...) {
                    // fallback: treat as sequence of letters (e.g., WUBRG or "WU")
                    for (char ch : cols) {
                        if (!std::isalpha((unsigned char)ch)) continue;
                        std::string normalized = register_color(std::string(1, ch), qty);
                        appended_color = append_color_series_from_token(normalized, bucket, qty) || appended_color;
                    }
                }
            }
            if (!appended_color) {
                std::string normalized = register_color("C", qty);
                append_color_series_from_token(normalized, bucket, qty);
            }
        }, std::vector<std::string>{std::to_string(deck_id)});
    }
    // cap bucket at 10
    // Normalize counts into capped buckets and merge detailed lists accordingly
    std::map<int,int> buckets;
    std::map<int, std::vector<std::pair<std::string,int>>> buckets_details_capped;
    for (auto &p : counts) {
        int b = p.first;
        if (b >= cap) b = cap;
        buckets[b] += p.second;
    }
    // fold bucket_details into capped buckets
    for (auto &p : bucket_details) {
        int orig = p.first;
        int b = orig >= cap ? cap : orig;
        for (auto &it : p.second) buckets_details_capped[b].push_back(it);
    }
    // Query deck name for nicer filenames
    std::string deck_name = "";
    if (state->db) {
        state->db->query("SELECT name FROM decks WHERE id = ?", [&](const std::map<std::string,std::string>& r){
            deck_name = r.at("name");
        }, std::vector<std::string>{std::to_string(deck_id)});
    }
    std::cout << "DEBUG: mana_curve deck_id=" << deck_id << " total_cards=" << total_cards
              << " counts_size=" << counts.size() << " buckets_size=" << buckets.size()
              << " series_type=" << series_by_type.size() << " series_color=" << series_by_color.size() << std::endl;

    // create multi-series: Total, By Type, By Color
    int w = 800, h = 400;
    std::map<std::string, std::map<int,int>> series_total;
    series_total["Totale"] = buckets;

    // Remove any empty series (no counts) so we don't plot unused types/colors
    std::vector<std::string> erase_keys;
    for (auto &p : series_by_type) {
        bool any = false;
        for (auto &kv : p.second) if (kv.second > 0) { any = true; break; }
        if (!any) erase_keys.push_back(p.first);
    }
    for (auto &k : erase_keys) series_by_type.erase(k);
    erase_keys.clear();
    for (auto &p : series_by_color) {
        bool any = false;
        for (auto &kv : p.second) if (kv.second > 0) { any = true; break; }
        if (!any) erase_keys.push_back(p.first);
    }
    for (auto &k : erase_keys) series_by_color.erase(k);

    // Create surfaces up-front. Only create type/color surfaces if they contain data.
    cairo_surface_t* surf_total = create_mana_surface_multi(series_total, cap, w, h);
    cairo_surface_t* surf_type = nullptr;
    cairo_surface_t* surf_color = nullptr;
    bool has_type = !series_by_type.empty();
    bool has_color = !series_by_color.empty();
    if (has_type) surf_type = create_mana_surface_multi(series_by_type, cap, w, h);
    if (has_color) surf_color = create_mana_surface_multi(series_by_color, cap, w, h);
    // Debug: always write a PNG of the generated surface so we can inspect it on disk
    try {
        ensure_data_dir_exists("data");
        std::string dbg_fname = std::string("data/debug_mana_preview_") + std::to_string(time(nullptr)) + ".png";
        cairo_surface_write_to_png(surf_total, dbg_fname.c_str());
        std::cout << "DEBUG: wrote debug PNG to " << dbg_fname << std::endl;
    } catch(...) {
        std::cout << "DEBUG: failed to write debug PNG" << std::endl;
    }

    double avg = 0.0;
    int max_cost = 0;
    int mode_bucket = 0;
    int mode_count = 0;
    for (auto &p : buckets) {
        int cost = p.first;
        int cnt = p.second;
        avg += cost * (double)cnt;
        if (cost > max_cost) max_cost = cost;
        if (cnt > mode_count) {
            mode_count = cnt;
            mode_bucket = cost;
        }
    }
    if (total_cards > 0) avg /= (double)total_cards;

    int median = 0;
    if (total_cards > 0) {
        int half = (total_cards + 1) / 2;
        int acc = 0;
        for (auto &p : buckets) {
            acc += p.second;
            if (acc >= half) {
                median = p.first;
                break;
            }
        }
    }

    int spells_count = total_cards - land_cards;
    if (spells_count < 0) spells_count = 0;
    double avg_total_cmc = total_cards > 0 ? (sum_all_cmc / (double)total_cards) : 0.0;
    double avg_non_land_cmc = count_non_land_cards > 0 ? (sum_non_land_cmc / (double)count_non_land_cards) : 0.0;
    double avg_creature_cmc = count_creature_cards > 0 ? (sum_creature_cmc / (double)count_creature_cards) : 0.0;
    double avg_non_creature_cmc = count_non_creature_cards > 0 ? (sum_non_creature_cmc / (double)count_non_creature_cards) : 0.0;
    double creature_ratio = total_cards > 0 ? (double)creature_cards / (double)total_cards : 0.0;
    double non_creature_ratio = total_cards > 0 ? (double)non_creature_spells / (double)total_cards : 0.0;
    double land_ratio = total_cards > 0 ? (double)land_cards / (double)total_cards : 0.0;
    double recommended_land_ratio = (total_cards >= 90) ? 0.37 : (total_cards >= 70 ? 0.38 : 0.40);
    int recommended_lands = total_cards > 0 ? (int)std::round(total_cards * recommended_land_ratio) : 0;
    int land_delta = land_cards - recommended_lands;

    auto compute_curve_targets = [&](int spell_total, int deck_total) {
        std::map<int,int> targets;
        if (spell_total <= 0) {
            targets[0] = targets[1] = targets[2] = targets[3] = targets[4] = targets[5] = targets[6] = 0;
            return targets;
        }
        bool commander_profile = deck_total >= 90;
        std::vector<std::pair<int,double>> ratios = commander_profile ?
            std::vector<std::pair<int,double>>{{1,0.18},{2,0.20},{3,0.19},{4,0.16},{5,0.14},{6,0.13}} :
            std::vector<std::pair<int,double>>{{1,0.24},{2,0.22},{3,0.18},{4,0.16},{5,0.12},{6,0.08}};
        int assigned = 0;
        for (size_t i = 0; i < ratios.size(); ++i) {
            int bucket = ratios[i].first;
            int value = 0;
            if (i + 1 == ratios.size()) {
                value = std::max(0, spell_total - assigned);
            } else {
                value = (int)std::round(spell_total * ratios[i].second);
                assigned += value;
            }
            targets[bucket] = value;
        }
        targets[0] = 0;
        return targets;
    };

    std::map<int,int> curve_targets = compute_curve_targets(spells_count, total_cards);
    std::map<int,int> curve_actual_summary;
    auto get_bucket_count = [&](int bucket) {
        if (bucket == 6) {
            int sum = 0;
            for (int b = 6; b <= cap; ++b) {
                auto it = buckets.find(b);
                if (it != buckets.end()) sum += it->second;
            }
            return sum;
        }
        auto it = buckets.find(bucket);
        return it != buckets.end() ? it->second : 0;
    };
    int bucket_keys[] = {0,1,2,3,4,5,6};
    for (int bucket : bucket_keys) {
        curve_actual_summary[bucket] = get_bucket_count(bucket);
        if (!curve_targets.count(bucket)) curve_targets[bucket] = 0;
    }

    std::set<std::string> color_key_set;
    for (const auto &kv : color_sources) if (!kv.first.empty()) color_key_set.insert(kv.first);
    for (const auto &kv : pip_counts) if (!kv.first.empty()) color_key_set.insert(kv.first);
    if (color_key_set.empty()) color_key_set.insert("C");

    auto lookup_requirement = [&](int turn, const std::vector<std::pair<int,int>>& table) {
        if (table.empty()) return 0;
        if (turn <= 0) return table.front().second;
        for (const auto &entry : table) {
            if (turn <= entry.first) return entry.second;
        }
        return table.back().second;
    };
    std::vector<std::pair<int,int>> single_table = {{1,14},{2,12},{3,11},{4,10},{5,9}};
    std::vector<std::pair<int,int>> double_table = {{2,20},{3,17},{4,15},{5,14}};
    std::vector<std::pair<int,int>> triple_table = {{3,23},{4,21},{5,19}};
    std::map<std::string,int> color_source_targets;
    for (const auto &color : color_key_set) {
        if (color == "C") {
            color_source_targets[color] = 0;
            continue;
        }
        int best = 0;
        int single_turn = pip_single_turn.count(color) ? pip_single_turn[color] : INF_TURN;
        int double_turn_req = pip_double_turn.count(color) ? pip_double_turn[color] : INF_TURN;
        int triple_turn_req = pip_triple_turn.count(color) ? pip_triple_turn[color] : INF_TURN;
        if (single_turn < INF_TURN) best = std::max(best, lookup_requirement(std::max(1, single_turn), single_table));
        if (double_turn_req < INF_TURN) best = std::max(best, lookup_requirement(std::max(1, double_turn_req), double_table));
        if (triple_turn_req < INF_TURN) best = std::max(best, lookup_requirement(std::max(1, triple_turn_req), triple_table));
        color_source_targets[color] = best;
    }

    double total_pips = 0.0;
    for (const auto &kv : pip_counts) total_pips += kv.second;
    // Instead of a dialog, render the mana curve inside the main window stack.
    if (!state->stats_container || !state->main_stack) {
        std::cout << "WARN: stats container not available, skipping inline mana curve view" << std::endl;
        if (surf_total) cairo_surface_destroy(surf_total);
        if (surf_type) cairo_surface_destroy(surf_type);
        if (surf_color) cairo_surface_destroy(surf_color);
        return;
    }

    stats_clear(state, false);

    GtkWidget* container = state->stats_container;
    std::string header_title = deck_name.empty() ? std::string("Curva Mana") : deck_name + " • Curva Mana";

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(header, TRUE);
    gtk_widget_set_margin_bottom(header, 12);
    gtk_box_append(GTK_BOX(container), header);

    GtkWidget* back_btn = gtk_button_new_with_label("← Database");
    gtk_widget_add_css_class(back_btn, "ghost-button");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_stats_back_clicked), state);
    gtk_box_append(GTK_BOX(header), back_btn);

    GtkWidget* title_lbl = gtk_label_new(header_title.c_str());
    gtk_label_set_xalign(GTK_LABEL(title_lbl), 0.0);
    gtk_widget_set_hexpand(title_lbl, TRUE);
    gtk_box_append(GTK_BOX(header), title_lbl);

    std::string info_text = "Carte: " + std::to_string(total_cards);
    GtkWidget* info_lbl = gtk_label_new(info_text.c_str());
    gtk_widget_add_css_class(info_lbl, "muted");
    gtk_box_append(GTK_BOX(header), info_lbl);

    GtkWidget* hmain = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_hexpand(hmain, TRUE);
    gtk_widget_set_vexpand(hmain, TRUE);
    gtk_box_append(GTK_BOX(container), hmain);

    GtkWidget* left_v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(left_v, TRUE);
    gtk_widget_set_vexpand(left_v, TRUE);
    gtk_box_append(GTK_BOX(hmain), left_v);

    GtkWidget* top_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_bottom(top_controls, 8);
    GtkWidget* btn_tot = gtk_button_new_with_label("Totale");
    GtkWidget* btn_type = gtk_button_new_with_label("Tipo");
    GtkWidget* btn_color = gtk_button_new_with_label("Colore");
    gtk_widget_set_size_request(btn_tot, 92, -1);
    gtk_widget_set_size_request(btn_type, 92, -1);
    gtk_widget_set_size_request(btn_color, 92, -1);
    gtk_box_append(GTK_BOX(top_controls), btn_tot);
    gtk_box_append(GTK_BOX(top_controls), btn_type);
    gtk_box_append(GTK_BOX(top_controls), btn_color);
    gtk_box_append(GTK_BOX(left_v), top_controls);

    GtkWidget* chart_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(chart_frame, "stats-chart-frame");
    gtk_widget_set_hexpand(chart_frame, TRUE);
    gtk_widget_set_vexpand(chart_frame, TRUE);
    gtk_box_append(GTK_BOX(left_v), chart_frame);

    GtkWidget* da = gtk_drawing_area_new();
    gtk_widget_set_hexpand(da, TRUE);
    gtk_widget_set_vexpand(da, TRUE);
    gtk_widget_set_size_request(da, w, h);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), (GtkDrawingAreaDrawFunc)on_mana_area_draw, NULL, NULL);
    gtk_frame_set_child(GTK_FRAME(chart_frame), da);

    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(right, TRUE);
    gtk_widget_set_vexpand(right, TRUE);
    gtk_box_append(GTK_BOX(hmain), right);

    auto format_double = [](double value) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", value);
        return std::string(buf);
    };
    auto format_percent = [](double value) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0);
        return std::string(buf);
    };
    auto format_delta_text = [](int delta) {
        if (delta == 0) return std::string("Δ 0");
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Δ %s%d", delta > 0 ? "+" : "", delta);
        return std::string(buf);
    };
    auto create_stats_pill = [&](const std::string& title, const std::string& value, const std::string& subtitle) {
        GtkWidget* pill = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(pill, "stats-pill");
        gtk_widget_set_margin_end(pill, 8);
        gtk_widget_set_margin_bottom(pill, 8);
        GtkWidget* title_label = gtk_label_new(title.c_str());
        gtk_widget_add_css_class(title_label, "stats-pill-title");
        gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
        gtk_box_append(GTK_BOX(pill), title_label);
        GtkWidget* value_label = gtk_label_new(value.c_str());
        gtk_widget_add_css_class(value_label, "stats-pill-value");
        gtk_label_set_xalign(GTK_LABEL(value_label), 0.0);
        gtk_box_append(GTK_BOX(pill), value_label);
        if (!subtitle.empty()) {
            GtkWidget* subtitle_label = gtk_label_new(subtitle.c_str());
            gtk_widget_add_css_class(subtitle_label, "stats-pill-subtitle");
            gtk_label_set_wrap(GTK_LABEL(subtitle_label), TRUE);
            gtk_label_set_xalign(GTK_LABEL(subtitle_label), 0.0);
            gtk_box_append(GTK_BOX(pill), subtitle_label);
        }
        return pill;
    };

    GtkWidget* summary_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(summary_flow), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(summary_flow, "stats-pill-flow");
    gtk_widget_set_margin_bottom(summary_flow, 8);
    gtk_box_append(GTK_BOX(right), summary_flow);
    auto append_pill = [&](GtkWidget* pill) {
        gtk_flow_box_insert(GTK_FLOW_BOX(summary_flow), pill, -1);
    };

    append_pill(create_stats_pill("Totale", std::to_string(total_cards), deck_name.empty() ? std::string() : deck_name));
    std::string land_sub = format_percent(land_ratio) + " • target " + std::to_string(recommended_lands) + " (" + format_delta_text(land_delta) + ")";
    append_pill(create_stats_pill("Terre", std::to_string(land_cards), land_sub));
    std::string spells_sub = std::string("avg ") + format_double(avg_non_land_cmc) + " • " + format_percent(1.0 - land_ratio);
    append_pill(create_stats_pill("Magie", std::to_string(spells_count), spells_sub));
    std::string creature_sub = std::string("avg ") + format_double(avg_creature_cmc) + " • " + format_percent(creature_ratio);
    append_pill(create_stats_pill("Creature", std::to_string(creature_cards), creature_sub));
    std::string non_creature_sub = std::string("avg ") + format_double(avg_non_creature_cmc) + " • " + format_percent(non_creature_ratio);
    append_pill(create_stats_pill("Non Creature", std::to_string(non_creature_spells), non_creature_sub));
    double removal_share = spells_count > 0 ? (double)removal_spells / (double)spells_count : 0.0;
    double ramp_share = spells_count > 0 ? (double)ramp_spells / (double)spells_count : 0.0;
    double draw_share = spells_count > 0 ? (double)card_draw_spells / (double)spells_count : 0.0;
    std::string removal_sub = format_percent(removal_share);
    if (board_wipes > 0) removal_sub += std::string(" • wipe ") + std::to_string(board_wipes);
    append_pill(create_stats_pill("Rimozioni", std::to_string(removal_spells), removal_sub));
    std::string ramp_sub = format_percent(ramp_share);
    append_pill(create_stats_pill("Ramp", std::to_string(ramp_spells), ramp_sub));
    std::string draw_sub = format_percent(draw_share);
    append_pill(create_stats_pill("Pescate", std::to_string(card_draw_spells), draw_sub));

    auto style_delta_label = [&](GtkWidget* lbl, int delta) {
        if (delta > 0) gtk_widget_add_css_class(lbl, "trend-up");
        else if (delta < 0) gtk_widget_add_css_class(lbl, "trend-down");
        else gtk_widget_add_css_class(lbl, "trend-neutral");
    };

    GtkWidget* curve_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(curve_panel, "stats-panel");
    gtk_widget_set_margin_bottom(curve_panel, 12);
    GtkWidget* curve_title = gtk_label_new("Curva vs target");
    gtk_widget_add_css_class(curve_title, "stats-panel-title");
    gtk_label_set_xalign(GTK_LABEL(curve_title), 0.0);
    gtk_box_append(GTK_BOX(curve_panel), curve_title);
    GtkWidget* curve_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(curve_grid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(curve_grid), 4);
    gtk_widget_add_css_class(curve_grid, "stats-grid");
    gtk_box_append(GTK_BOX(curve_panel), curve_grid);
    GtkWidget* curve_hdr0 = gtk_label_new("Costo");
    GtkWidget* curve_hdr1 = gtk_label_new("Reale");
    GtkWidget* curve_hdr2 = gtk_label_new("Target");
    GtkWidget* curve_hdr3 = gtk_label_new("Δ");
    GtkWidget* headers[] = {curve_hdr0, curve_hdr1, curve_hdr2, curve_hdr3};
    for (int i = 0; i < 4; ++i) {
        gtk_widget_add_css_class(headers[i], "stats-grid-header");
        gtk_label_set_xalign(GTK_LABEL(headers[i]), 0.0);
        gtk_grid_attach(GTK_GRID(curve_grid), headers[i], i, 0, 1, 1);
    }
    int curve_row = 1;
    for (int bucket : bucket_keys) {
        int actual = curve_actual_summary[bucket];
        int target = curve_targets.count(bucket) ? curve_targets[bucket] : 0;
        int delta = actual - target;
        std::string bucket_label = bucket == 6 ? ">=6" : std::to_string(bucket);
        GtkWidget* lbl_bucket = gtk_label_new(bucket_label.c_str());
        gtk_label_set_xalign(GTK_LABEL(lbl_bucket), 0.0);
        gtk_grid_attach(GTK_GRID(curve_grid), lbl_bucket, 0, curve_row, 1, 1);
        GtkWidget* lbl_actual = gtk_label_new(std::to_string(actual).c_str());
        gtk_label_set_xalign(GTK_LABEL(lbl_actual), 0.0);
        gtk_grid_attach(GTK_GRID(curve_grid), lbl_actual, 1, curve_row, 1, 1);
        GtkWidget* lbl_target = gtk_label_new(std::to_string(target).c_str());
        gtk_label_set_xalign(GTK_LABEL(lbl_target), 0.0);
        gtk_grid_attach(GTK_GRID(curve_grid), lbl_target, 2, curve_row, 1, 1);
        GtkWidget* lbl_delta = gtk_label_new(format_delta_text(delta).c_str());
        gtk_label_set_xalign(GTK_LABEL(lbl_delta), 0.0);
        style_delta_label(lbl_delta, delta);
        gtk_grid_attach(GTK_GRID(curve_grid), lbl_delta, 3, curve_row, 1, 1);
        curve_row++;
    }
    gtk_box_append(GTK_BOX(right), curve_panel);

    GtkWidget* color_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(color_panel, "stats-panel");
    GtkWidget* color_title = gtk_label_new("Fonti di colore");
    gtk_widget_add_css_class(color_title, "stats-panel-title");
    gtk_label_set_xalign(GTK_LABEL(color_title), 0.0);
    gtk_box_append(GTK_BOX(color_panel), color_title);
    GtkWidget* color_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(color_grid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(color_grid), 4);
    gtk_widget_add_css_class(color_grid, "stats-grid");
    gtk_box_append(GTK_BOX(color_panel), color_grid);
    GtkWidget* color_hdr0 = gtk_label_new("Colore");
    GtkWidget* color_hdr1 = gtk_label_new("Fonti");
    GtkWidget* color_hdr2 = gtk_label_new("Consigliate");
    GtkWidget* color_hdr3 = gtk_label_new("Δ");
    GtkWidget* color_hdr4 = gtk_label_new("Share");
    GtkWidget* color_headers[] = {color_hdr0, color_hdr1, color_hdr2, color_hdr3, color_hdr4};
    for (int i = 0; i < 5; ++i) {
        gtk_widget_add_css_class(color_headers[i], "stats-grid-header");
        gtk_label_set_xalign(GTK_LABEL(color_headers[i]), 0.0);
        gtk_grid_attach(GTK_GRID(color_grid), color_headers[i], i, 0, 1, 1);
    }
    auto get_or_zero = [](const std::map<std::string,int>& m, const std::string& key) {
        auto it = m.find(key);
        return it != m.end() ? it->second : 0;
    };
    std::vector<std::string> ordered_colors = {"W","U","B","R","G","C"};
    for (const auto &kv : color_key_set) {
        if (std::find(ordered_colors.begin(), ordered_colors.end(), kv) == ordered_colors.end()) {
            ordered_colors.push_back(kv);
        }
    }
    int color_row = 1;
    for (const auto &color : ordered_colors) {
        int sources = get_or_zero(color_sources, color);
        int pips = get_or_zero(pip_counts, color);
        if (sources == 0 && pips == 0) continue;
        int needed = color_source_targets.count(color) ? color_source_targets[color] : 0;
        int delta = sources - needed;
        double share_sources = land_cards > 0 ? (double)sources / (double)land_cards : 0.0;
        double share_pips = total_pips > 0 ? (double)pips / total_pips : 0.0;
        int share_delta = (int)std::round((share_sources - share_pips) * 100.0);
        std::string share_text = format_percent(share_sources) + " vs " + format_percent(share_pips);
        GtkWidget* lbl_color = gtk_label_new(color.c_str());
        gtk_label_set_xalign(GTK_LABEL(lbl_color), 0.0);
        gtk_grid_attach(GTK_GRID(color_grid), lbl_color, 0, color_row, 1, 1);
        GtkWidget* lbl_sources = gtk_label_new(std::to_string(sources).c_str());
        gtk_label_set_xalign(GTK_LABEL(lbl_sources), 0.0);
        gtk_grid_attach(GTK_GRID(color_grid), lbl_sources, 1, color_row, 1, 1);
        GtkWidget* lbl_needed = gtk_label_new(std::to_string(needed).c_str());
        gtk_label_set_xalign(GTK_LABEL(lbl_needed), 0.0);
        gtk_grid_attach(GTK_GRID(color_grid), lbl_needed, 2, color_row, 1, 1);
        GtkWidget* lbl_delta = gtk_label_new(format_delta_text(delta).c_str());
        gtk_label_set_xalign(GTK_LABEL(lbl_delta), 0.0);
        style_delta_label(lbl_delta, delta);
        gtk_grid_attach(GTK_GRID(color_grid), lbl_delta, 3, color_row, 1, 1);
        std::string share_delta_text = share_delta == 0 ? std::string("0%") : (share_delta > 0 ? "+" + std::to_string(share_delta) + "%" : std::to_string(share_delta) + "%");
        GtkWidget* lbl_share = gtk_label_new((share_text + " (" + share_delta_text + ")").c_str());
        gtk_label_set_xalign(GTK_LABEL(lbl_share), 0.0);
        if (share_delta > 0) gtk_widget_add_css_class(lbl_share, "trend-up");
        else if (share_delta < 0) gtk_widget_add_css_class(lbl_share, "trend-down");
        else gtk_widget_add_css_class(lbl_share, "trend-neutral");
        gtk_label_set_wrap(GTK_LABEL(lbl_share), TRUE);
        gtk_grid_attach(GTK_GRID(color_grid), lbl_share, 4, color_row, 1, 1);
        color_row++;
    }
    gtk_box_append(GTK_BOX(right), color_panel);

    GtkWidget* btns = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(btns, 12);
    GtkWidget* export_png = gtk_button_new_with_label("Export PNG");
    GtkWidget* export_pdf = gtk_button_new_with_label("Export PDF");
    GtkWidget* export_stats = gtk_button_new_with_label("Export stats");
    GtkWidget* close = gtk_button_new_with_label("Chiudi");
    gtk_widget_set_size_request(export_png, 120, -1);
    gtk_widget_set_size_request(export_pdf, 120, -1);
    gtk_widget_set_size_request(export_stats, 120, -1);
    gtk_widget_set_size_request(close, 120, -1);
    gtk_box_append(GTK_BOX(btns), export_png);
    gtk_box_append(GTK_BOX(btns), export_pdf);
    gtk_box_append(GTK_BOX(btns), export_stats);
    gtk_box_append(GTK_BOX(btns), close);
    gtk_box_append(GTK_BOX(right), btns);

    if (!has_type) gtk_widget_set_sensitive(btn_type, FALSE);
    if (!has_color) gtk_widget_set_sensitive(btn_color, FALSE);

    g_object_set_data(G_OBJECT(da), "mana_surface_type", surf_type);
    g_object_set_data(G_OBJECT(da), "mana_surface_color", surf_color);

    ManaStatsExportCtx* ectx = new ManaStatsExportCtx();
    ectx->da = da;
    ectx->parent = parent;
    ectx->surf_total = surf_total;
    ectx->surf_type = surf_type;
    ectx->surf_color = surf_color;
    ectx->w = w;
    ectx->h = h;
    ectx->buckets = buckets;
    ectx->total = total_cards;
    ectx->cap = cap;
    ectx->deck_name = deck_name;
    ectx->bucket_details = buckets_details_capped;
    ectx->color_counts = color_counts;
    ectx->avg = avg;
    ectx->median = median;
    ectx->mode_bucket = mode_bucket;
    ectx->mode_count = mode_count;
    ectx->max_cost = max_cost;
    ectx->land_cards = land_cards;
    ectx->spells_count = spells_count;
    ectx->avg_total_cmc = avg_total_cmc;
    ectx->avg_non_land_cmc = avg_non_land_cmc;
    ectx->avg_creature_cmc = avg_creature_cmc;
    ectx->avg_non_creature_cmc = avg_non_creature_cmc;
    ectx->recommended_lands = recommended_lands;
    ectx->land_delta = land_delta;
    ectx->removal_spells = removal_spells;
    ectx->board_wipes = board_wipes;
    ectx->ramp_spells = ramp_spells;
    ectx->card_draw_spells = card_draw_spells;
    ectx->curve_targets = curve_targets;
    ectx->curve_actual_summary = curve_actual_summary;
    ectx->color_sources = color_sources;
    ectx->color_source_targets = color_source_targets;
    ectx->pip_counts = pip_counts;
    ectx->total_pips = total_pips;
    state->stats_ctx = ectx;

    g_signal_connect(export_png, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data){
        ManaStatsExportCtx* c = (ManaStatsExportCtx*)user_data;
        if (!c || !c->surf) return;
        ensure_data_dir_exists("data");
        std::string base = c->deck_name.empty() ? (std::string("mana_curve_") + std::to_string(time(nullptr))) : (std::string("mana_curve_") + sanitize_filename(c->deck_name));
        std::string fname = std::string("data/") + base + ".png";
        cairo_surface_write_to_png(c->surf, fname.c_str());
        std::cout << "Exported PNG to " << fname << std::endl;
    }), ectx);
    g_signal_connect(export_pdf, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data){
        ManaStatsExportCtx* c = (ManaStatsExportCtx*)user_data;
        if (!c || !c->surf) return;
        ensure_data_dir_exists("data");
        std::string base = c->deck_name.empty() ? (std::string("mana_curve_") + std::to_string(time(nullptr))) : (std::string("mana_curve_") + sanitize_filename(c->deck_name));
        std::string fname = std::string("data/") + base + ".pdf";
        cairo_surface_t* pdf = cairo_pdf_surface_create(fname.c_str(), c->w, c->h);
        cairo_t* cr = cairo_create(pdf);
        cairo_set_source_surface(cr, c->surf, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_flush(pdf);
        cairo_surface_destroy(pdf);
        std::cout << "Exported PDF to " << fname << std::endl;
    }), ectx);
    g_signal_connect(export_stats, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data){
        ManaStatsExportCtx* c = (ManaStatsExportCtx*)user_data;
        if (!c) return;
        ensure_data_dir_exists("data");
        std::string base = c->deck_name.empty() ? (std::string("mana_stats_") + std::to_string(time(nullptr))) : (std::string("mana_stats_") + sanitize_filename(c->deck_name));
        std::string fname = std::string("data/") + base + ".txt";
        std::ofstream out(fname);
        if (!out) return;
        auto format_double_txt = [](double value) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f", value);
            return std::string(buf);
        };
        out << "Deck: " << c->deck_name << "\n";
        out << "Total cards: " << c->total << "\n";
        out << "Average mana cost (overall): " << format_double_txt(c->avg_total_cmc) << "\n";
        out << "Average mana cost (non-lands): " << format_double_txt(c->avg_non_land_cmc) << "\n";
        out << "Average creature MV: " << format_double_txt(c->avg_creature_cmc) << "\n";
        out << "Average non-creature MV: " << format_double_txt(c->avg_non_creature_cmc) << "\n";
        out << "Median bucket: " << c->median << "\n";
        out << "Mode bucket: " << c->mode_bucket << " (" << c->mode_count << ")\n";
        out << "Max bucket: " << c->max_cost << "\n";
        out << "Lands: " << c->land_cards << " (target " << c->recommended_lands << ", Δ " << c->land_delta << ")\n";
        out << "Spells: " << c->spells_count << "\n";
        out << "Removal spells: " << c->removal_spells << " (board wipes " << c->board_wipes << ")\n";
        out << "Ramp sources: " << c->ramp_spells << "\n";
        out << "Card draw spells: " << c->card_draw_spells << "\n\n";
        out << "Curve summary (bucket -> actual / target / Δ):\n";
        for (const auto &entry : c->curve_actual_summary) {
            int bucket = entry.first;
            int actual = entry.second;
            int target = c->curve_targets.count(bucket) ? c->curve_targets.at(bucket) : 0;
            int delta = actual - target;
            out << "  " << (bucket == 6 ? ">=6" : std::to_string(bucket)) << ": " << actual << " / " << target << " / " << (delta >= 0 ? "+" : "") << delta << "\n";
        }
        out << "\nDistribution detail:\n";
        for (auto &p : c->buckets) out << "  " << p.first << ": " << p.second << "\n";
        out << "\nPer-bucket card lists:\n";
        for (auto &p : c->bucket_details) {
            out << "Bucket " << p.first << ":\n";
            for (auto &it : p.second) {
                out << "    " << it.first << " x" << it.second << "\n";
            }
        }
        out << "\nColor identity breakdown:\n";
        for (auto &p : c->color_counts) out << "  " << p.first << ": " << p.second << "\n";
        out << "\nMana sources by color:\n";
        auto get_or_zero_local = [](const std::map<std::string,int>& m, const std::string& key) {
            auto it = m.find(key);
            return it != m.end() ? it->second : 0;
        };
        std::set<std::string> export_colors;
        for (auto &kv : c->color_sources) if (!kv.first.empty()) export_colors.insert(kv.first);
        for (auto &kv : c->pip_counts) if (!kv.first.empty()) export_colors.insert(kv.first);
        if (export_colors.empty()) export_colors.insert("C");
        for (const auto &color : export_colors) {
            int sources = get_or_zero_local(c->color_sources, color);
            int needed = c->color_source_targets.count(color) ? c->color_source_targets.at(color) : 0;
            int pips = get_or_zero_local(c->pip_counts, color);
            double share_sources = c->land_cards > 0 ? (double)sources / (double)c->land_cards : 0.0;
            double share_pips = c->total_pips > 0 ? (double)pips / c->total_pips : 0.0;
            int share_delta = (int)std::round((share_sources - share_pips) * 100.0);
            char share_buf[64];
            std::snprintf(share_buf, sizeof(share_buf), "%.0f%% vs %.0f%% (Δ %s%d%%)", share_sources * 100.0, share_pips * 100.0, share_delta >= 0 ? "+" : "", share_delta);
            out << "  " << color << ": sources " << sources << ", target " << needed << ", pips " << pips << ", " << share_buf << "\n";
        }
        out.close();
        std::cout << "Exported stats to " << fname << std::endl;
    }), ectx);
    g_signal_connect(close, "clicked", G_CALLBACK(on_stats_back_clicked), state);

    g_signal_connect(btn_tot, "clicked", G_CALLBACK(on_stats_surface_total), ectx);
    g_signal_connect(btn_type, "clicked", G_CALLBACK(on_stats_surface_type), ectx);
    g_signal_connect(btn_color, "clicked", G_CALLBACK(on_stats_surface_color), ectx);

    stats_set_surface(ectx, ectx->surf_total);
    gtk_stack_set_visible_child_name(GTK_STACK(state->main_stack), "stats");
}

// Dialog per ricerca carta Scryfall
struct AddCardContext {
    GtkWidget* entry;
    GtkWidget* spin;
    AppState* state;
    GtkWindow* parent;
    GtkWidget* foil_checkbox;
};

// Find a descendant widget that implements GtkEditable (e.g., the internal
// entry inside a GtkSpinButton). Returns the first match or NULL if none found.
static GtkWidget* find_editable_descendant(GtkWidget* root) {
    if (!root) return NULL;
    GtkWidget* child = gtk_widget_get_first_child(root);
    while (child) {
        if (GTK_IS_EDITABLE(child)) return child;
        GtkWidget* found = find_editable_descendant(child);
        if (found) return found;
        child = gtk_widget_get_next_sibling(child);
    }
    return NULL;
}

// Funzione per caricare le carte dal database
static std::vector<std::map<std::string, std::string>> load_cards_from_db(Database* db, const std::string& filter = "", int deck_filter = -1, bool only_no_deck = false) {
    std::vector<std::map<std::string, std::string>> cards;
    if (!db) return cards;
    // Build SQL and optional params for deck filtering
    std::string oracle_select = g_cards_table_has_oracle_text ? "oracle_text" : "'' AS oracle_text";
    std::string sql = "SELECT id, english_name, localized_name, name, type, localized_type, colors, set_code, mana_cost, " + oracle_select + ", rarity, quantity, image_url, added_date, price_usd, foil, sideboard, deck_id FROM cards";
    std::vector<std::string> params;
    if (deck_filter != -1) {
        sql += " WHERE deck_id = ?";
        params.push_back(std::to_string(deck_filter));
    }
    // Default ordering: newest added first
    sql += " ORDER BY added_date DESC";
    db->query(sql, [&](const std::map<std::string, std::string>& row) {
        if (!filter.empty()) {
            std::string name_lower = row.at("name");
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
            std::string filter_lower = filter;
            std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);
            if (name_lower.find(filter_lower) == std::string::npos) {
                return; // Salta questa carta
            }
        }
        cards.push_back(row);
    }, params);
    // If we're viewing the main database (no deck filter), aggregate rows that represent
    // the same card (case/whitespace-insensitive name match) while keeping foil rows separate.
    // Aggregation rules:
    // - Key by canonical name (english_name, then localized_name, then name), trimmed and lowercased
    //   plus the foil flag so foil/non-foil remain distinct rows.
    // - Sum the quantity across all matching rows.
    // - For display metadata (type, image_url, added_date, price_usd, etc.) pick the row with
    //   the latest added_date (ISO timestamp) as representative.
    if (deck_filter == -1) {
        struct AggItem {
            std::map<std::string,std::string> rep; // representative row
            int total_qty;
            int no_deck_qty;
            std::string latest_date;
        };
        std::map<std::string, AggItem> agg;
        auto norm = [](const std::string &s) {
            size_t start = 0, end = s.size();
            while (start < end && isspace((unsigned char)s[start])) start++;
            while (end > start && isspace((unsigned char)s[end-1])) end--;
            std::string t = s.substr(start, end - start);
            std::transform(t.begin(), t.end(), t.begin(), ::tolower);
            return t;
        };
        for (const auto &r : cards) {
            // If caller requested only cards not in any deck, skip rows that belong to a deck
            if (only_no_deck) {
                if (r.count("deck_id") && !r.at("deck_id").empty()) continue;
            }
            std::string en = r.count("english_name") && !r.at("english_name").empty() ? r.at("english_name") : "";
            std::string ln = r.count("localized_name") && !r.at("localized_name").empty() ? r.at("localized_name") : "";
            std::string nm = r.count("name") ? r.at("name") : "";
            std::string canonical = !en.empty() ? en : (!ln.empty() ? ln : nm);
            std::string key = norm(canonical);
            std::string foil = r.count("foil") ? r.at("foil") : "0";
            key += "|foil=" + foil;
            int qty = 0;
            try { qty = std::stoi(r.count("quantity") ? r.at("quantity") : "0"); } catch(...) { qty = 0; }
            std::string added = r.count("added_date") ? r.at("added_date") : "";
            auto it = agg.find(key);
            bool row_is_deckless = !(r.count("deck_id") && !r.at("deck_id").empty());
            if (it == agg.end()) {
                AggItem ai;
                ai.rep = r;
                ai.total_qty = qty;
                ai.no_deck_qty = row_is_deckless ? qty : 0;
                ai.latest_date = added;
                agg.emplace(key, std::move(ai));
            } else {
                it->second.total_qty += qty;
                if (row_is_deckless) it->second.no_deck_qty += qty;
                // choose the representative with the newest added_date
                if (!added.empty() && (it->second.latest_date.empty() || added > it->second.latest_date)) {
                    it->second.rep = r;
                    it->second.latest_date = added;
                }
            }
        }
        // Build aggregated vector and set representative added_date to the latest seen
        std::vector<std::map<std::string, std::string>> out;
        for (auto &p : agg) {
            auto rep = p.second.rep;
            rep["quantity"] = std::to_string(p.second.total_qty);
            rep["quantity_total"] = std::to_string(p.second.total_qty);
            rep["quantity_no_deck"] = std::to_string(p.second.no_deck_qty);
            // Ensure the representative row carries the latest added_date for sorting
            rep["added_date"] = p.second.latest_date;
            out.push_back(rep);
        }
        // Sort by added_date DESC so newest aggregated items appear first (ISO timestamps sort lexicographically)
        std::sort(out.begin(), out.end(), [](const std::map<std::string,std::string>& a, const std::map<std::string,std::string>& b){
            std::string da = a.count("added_date") ? a.at("added_date") : "";
            std::string db = b.count("added_date") ? b.at("added_date") : "";
            return da > db; // descending
        });
        return out;
    }
    return cards;
}

// Paged loader: uses existing aggregation loader and returns a slice for current page.
static std::vector<std::map<std::string, std::string>> load_cards_page(AppState* state, int &out_total) {
    out_total = 0;
    std::vector<std::map<std::string,std::string>> empty;
    if (!state || !state->db) return empty;
    std::string filter = "";
    if (state->search_entry) {
        const char* text = gtk_editable_get_text(GTK_EDITABLE(state->search_entry));
        filter = text ? text : "";
    }
    // If viewing the main DB (no deck filter) use a DB-backed aggregated query with LIMIT/OFFSET
    if (state->selected_deck_id == -1) {
        // Build COUNT query to get total number of aggregated rows
    // Use canonical_name (created by migration) for grouping if available
    std::string count_sql = "SELECT COUNT(*) AS cnt FROM ( SELECT 1 FROM cards WHERE 1=1 ";
        std::vector<std::string> count_params;
        if (state->filter_no_deck) count_sql += " AND deck_id IS NULL ";
        if (!filter.empty()) {
            // Search on the canonical_name to allow indexed lookup
            count_sql += " AND canonical_name LIKE '%' || LOWER(?) || '%' ";
            count_params.push_back(filter);
        }
        count_sql += " GROUP BY LOWER(TRIM(COALESCE(english_name, localized_name, name))), foil )";
        // Execute count
        state->db->query(count_sql, [&](const std::map<std::string,std::string>& row){
            try { out_total = std::stoi(row.at("cnt")); } catch(...) { out_total = 0; }
        }, count_params);

        bool view_all = (state->page_size == 0);
        int effective_page_size = (state->page_size > 0) ? state->page_size : (g_page_size_default > 0 ? g_page_size_default : 50);
        if (state->current_page < 0) state->current_page = 0;
        int start = 0;
        if (!view_all) {
            start = state->current_page * effective_page_size;
            if (start >= out_total && out_total > 0) {
                int last = (out_total - 1) / effective_page_size;
                state->current_page = last;
                start = state->current_page * effective_page_size;
            }
        } else {
            // viewing all: reset to first page conceptually
            state->current_page = 0;
        }

        // Build paged aggregated query using a CTE to compute aggregated keys and latest_date
        std::string select_sql =
            "WITH agg AS ("
            " SELECT canonical_name AS key, foil, SUM(quantity) AS quantity_total, SUM(CASE WHEN deck_id IS NULL THEN quantity ELSE 0 END) AS quantity_no_deck, MAX(added_date) AS latest_date"
            " FROM cards WHERE 1=1 ";
        std::vector<std::string> sel_params;
        if (state->filter_no_deck) select_sql += " AND deck_id IS NULL ";
        if (!filter.empty()) {
            select_sql += " AND canonical_name LIKE '%' || LOWER(?) || '%' ";
            sel_params.push_back(filter);
        }
        select_sql += " GROUP BY key, foil ) ";
        select_sql +=
            "SELECT "
            " (SELECT id FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS id,"
            " (SELECT COALESCE(english_name,'') FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS english_name,"
            " (SELECT COALESCE(localized_name,'') FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS localized_name,"
            " (SELECT COALESCE(name,'') FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS name,"
            " (SELECT COALESCE(type,'') FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS type,"
            " (SELECT COALESCE(localized_type,'') FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS localized_type,"
            " (SELECT COALESCE(colors,'') FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS colors,"
            " (SELECT COALESCE(set_code,'') FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS set_code,"
            " (SELECT COALESCE(mana_cost,'') FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS mana_cost,"
            " (SELECT COALESCE(rarity,'') FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS rarity,"
            " agg.quantity_total AS quantity,"
            " (SELECT COALESCE(image_url,'') FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS image_url,"
            " agg.latest_date AS added_date,"
            " (SELECT COALESCE(price_usd,'') FROM cards c2 WHERE c2.canonical_name = agg.key AND c2.foil = agg.foil AND c2.added_date = agg.latest_date LIMIT 1) AS price_usd,"
            " agg.foil AS foil, agg.quantity_total AS quantity_total, agg.quantity_no_deck AS quantity_no_deck"
            " FROM agg ORDER BY agg.latest_date DESC";

        // Append limit/offset params only when not viewing all
        if (!view_all) {
            select_sql += " LIMIT ? OFFSET ?";
            sel_params.push_back(std::to_string(effective_page_size));
            int start_off = start;
            sel_params.push_back(std::to_string(start_off));
        }

        std::vector<std::map<std::string,std::string>> page_rows;
        state->db->query(select_sql, [&](const std::map<std::string,std::string>& row){
            page_rows.push_back(row);
        }, sel_params);
        return page_rows;
    }
    // Deck-specific view: use DB-backed pagination (no aggregation across rows)
    {
        std::vector<std::string> count_params;
        std::string count_sql = "SELECT COUNT(*) AS cnt FROM cards WHERE 1=1";
        // Filter to the specific deck
        count_sql += " AND deck_id = ?";
        count_params.push_back(std::to_string(state->selected_deck_id));
        if (state->filter_no_deck) {
            // If user also asked for deckless while viewing a deck, result should be empty
            // (deck_id = X and deck_id IS NULL cannot both be true). We keep the SQL but
            // it's effectively a no-op; leave for clarity.
            count_sql += " AND deck_id IS NULL";
        }
        if (!filter.empty()) {
            count_sql += " AND LOWER(COALESCE(english_name, localized_name, name)) LIKE '%' || LOWER(?) || '%'";
            count_params.push_back(filter);
        }
        // Execute count
        state->db->query(count_sql, [&](const std::map<std::string,std::string>& row){
            try { out_total = std::stoi(row.at("cnt")); } catch(...) { out_total = 0; }
        }, count_params);

        bool view_all = (state->page_size == 0);
        int effective_page_size = (state->page_size > 0) ? state->page_size : (g_page_size_default > 0 ? g_page_size_default : 50);
        if (state->current_page < 0) state->current_page = 0;
        int start = 0;
        if (!view_all) {
            start = state->current_page * effective_page_size;
            if (start >= out_total && out_total > 0) {
                int last = (out_total - 1) / effective_page_size;
                state->current_page = last;
                start = state->current_page * effective_page_size;
            }
        } else {
            state->current_page = 0;
        }

        // Build paged SELECT for deck rows
        std::string select_sql =
            "SELECT id, english_name, localized_name, name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, image_url, added_date, price_usd, foil, sideboard, deck_id FROM cards WHERE 1=1";
        std::vector<std::string> sel_params;
        select_sql += " AND deck_id = ?";
        sel_params.push_back(std::to_string(state->selected_deck_id));
        if (state->filter_no_deck) select_sql += " AND deck_id IS NULL";
        // Server-side search by name
        if (!filter.empty()) {
            select_sql += " AND LOWER(COALESCE(english_name, localized_name, name)) LIKE '%' || LOWER(?) || '%'";
            sel_params.push_back(filter);
        }
        // Foil filter
        if (state->filter_foil != -1) {
            select_sql += " AND foil = ?";
            sel_params.push_back(std::to_string(state->filter_foil));
        }
        // Rarity filter (one or more)
        if (!state->filter_rarities.empty()) {
            select_sql += " AND (";
            bool first = true;
            for (const auto &r : state->filter_rarities) {
                if (!first) select_sql += " OR ";
                select_sql += "LOWER(rarity) = ?";
                sel_params.push_back(r);
                first = false;
            }
            select_sql += ")";
        }
        // Type filters: inclusive substring match on stored type/localized_type
        if (!state->filter_types.empty()) {
            for (const auto &t : state->filter_types) {
                select_sql += " AND (LOWER(type) LIKE '%' || LOWER(?) || '%' OR LOWER(localized_type) LIKE '%' || LOWER(?) || '%')";
                sel_params.push_back(t);
                sel_params.push_back(t);
            }
        }
        // Colors: ensure each requested color appears in the colors column (JSON array or compact codes)
        if (!state->filter_colors.empty()) {
            for (const auto &c : state->filter_colors) {
                select_sql += " AND ((colors LIKE ?) OR (colors LIKE ?))"; // try JSON "C" or compact C
                std::string pat1 = std::string("%\"") + c + std::string("\"%");
                std::string pat2 = std::string("%") + c + std::string("%");
                sel_params.push_back(pat1);
                sel_params.push_back(pat2);
            }
        }

        // Append pagination params only when not viewing all
        if (!view_all) {
            select_sql += " ORDER BY added_date DESC LIMIT ? OFFSET ?";
            sel_params.push_back(std::to_string(effective_page_size));
            sel_params.push_back(std::to_string(start));
        } else {
            select_sql += " ORDER BY added_date DESC";
        }

        std::vector<std::map<std::string,std::string>> page_rows;
        state->db->query(select_sql, [&](const std::map<std::string,std::string>& row){
            page_rows.push_back(row);
        }, sel_params);
        return page_rows;
    }
}

// Pagination control handlers
static void on_prev_page_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    GtkWindow *w = GTK_WINDOW(user_data);
    AppState* st = (AppState*)g_object_get_data(G_OBJECT(w), "app_state");
    if (!st) return;
    // If viewing all rows, pagination not applicable
    if (st->page_size == 0) return;
    if (st->current_page > 0) {
        st->current_page -= 1;
        refresh_card_list(st);
    }
}

static void on_next_page_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    GtkWindow *w = GTK_WINDOW(user_data);
    AppState* st = (AppState*)g_object_get_data(G_OBJECT(w), "app_state");
    if (!st) return;
    // If viewing all rows, pagination not applicable
    if (st->page_size == 0) return;
    int ps = st->page_size > 0 ? st->page_size : 50;
    int total_pages = (st->total_rows + ps - 1) / ps;
    if (st->current_page + 1 < total_pages) {
        st->current_page += 1;
        refresh_card_list(st);
    }
}

static void on_page_size_changed(GtkComboBox *combo, gpointer user_data) {
    GtkWindow *w = GTK_WINDOW(user_data);
    AppState* st = (AppState*)g_object_get_data(G_OBJECT(w), "app_state");
    if (!st) return;
    // If view-all toggle is active, ignore combo changes
    if (st->view_all_toggle && GTK_IS_TOGGLE_BUTTON(st->view_all_toggle) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->view_all_toggle))) {
        return;
    }
    int idx = gtk_combo_box_get_active(combo);
    int sizes[] = {10,25,50,100};
    if (idx >= 0 && idx < 4) st->page_size = sizes[idx];
    st->current_page = 0;
    // Persist chosen page size to settings so it survives restarts
    g_page_size_default = st->page_size > 0 ? st->page_size : g_page_size_default;
    save_settings();
    refresh_card_list(st);
}

// Toggle handler for "View All" button. When active, page_size==0 and pagination is disabled.
static void on_view_all_toggled(GtkToggleButton *toggle, gpointer user_data) {
    GtkWindow *w = GTK_WINDOW(user_data);
    AppState* st = (AppState*)g_object_get_data(G_OBJECT(w), "app_state");
    if (!st) return;
    gboolean active = gtk_toggle_button_get_active(toggle);
    if (active) {
        st->page_size = 0; // special: view all rows
        // disable the page-size combo and navigation
        if (st->page_size_combo) gtk_widget_set_sensitive(st->page_size_combo, FALSE);
        if (st->prev_page_button) gtk_widget_set_sensitive(st->prev_page_button, FALSE);
        if (st->next_page_button) gtk_widget_set_sensitive(st->next_page_button, FALSE);
    } else {
        // restore from combo selection
        if (st->page_size_combo) gtk_widget_set_sensitive(st->page_size_combo, TRUE);
        int idx = -1;
        if (st->page_size_combo) idx = gtk_combo_box_get_active(GTK_COMBO_BOX(st->page_size_combo));
        int sizes[] = {10,25,50,100};
        if (idx >= 0 && idx < 4) st->page_size = sizes[idx]; else st->page_size = g_page_size_default > 0 ? g_page_size_default : 50;
        // navigation buttons will be updated by refresh_card_list
    }
    // Persist
    g_page_size_default = st->page_size;
    save_settings();
    st->current_page = 0;
    refresh_card_list(st);
}

// Funzione globale per aggiornare la lista carte (GListStore)
void refresh_card_list(AppState* state) {
    if (!state || !state->card_store) return;
    std::string filter = "";
    if (state->search_entry) {
        const char* text = gtk_editable_get_text(GTK_EDITABLE(state->search_entry));
        filter = text ? text : "";
    }
    g_list_store_remove_all(state->card_store);
    if (!state->db) return;
    int total_rows = 0;
    auto cards = load_cards_page(state, total_rows);
    state->total_rows = total_rows;
    std::cout << "Loading " << cards.size() << " cards (page " << (state->current_page+1) << ") — total matching=" << total_rows << "\n" << std::endl;
    // Update pagination controls (if present)
    if (state->page_label) {
        if (state->page_size == 0) {
            // Viewing all rows
            char pl[128];
            snprintf(pl, sizeof(pl), "All (%d)", total_rows);
            gtk_label_set_text(GTK_LABEL(state->page_label), pl);
            if (state->prev_page_button) gtk_widget_set_sensitive(state->prev_page_button, FALSE);
            if (state->next_page_button) gtk_widget_set_sensitive(state->next_page_button, FALSE);
        } else {
            int ps = state->page_size > 0 ? state->page_size : 50;
            int total_pages = (total_rows + ps - 1) / ps;
            if (total_pages <= 0) total_pages = 1;
            char pl[128];
            snprintf(pl, sizeof(pl), "%s %d/%d", "Page", state->current_page + 1, total_pages);
            gtk_label_set_text(GTK_LABEL(state->page_label), pl);
            if (state->prev_page_button) gtk_widget_set_sensitive(state->prev_page_button, state->current_page > 0 ? TRUE : FALSE);
            if (state->next_page_button) gtk_widget_set_sensitive(state->next_page_button, ((state->current_page+1) < total_pages) ? TRUE : FALSE);
        }
    }
    int total_quantity = 0;
    int main_count = 0;
    int side_count = 0;
    double total_value = 0.0;
    if (state->selected_deck_id == -1) {
        // Main DB view: behave as before
        for (const auto& row : cards) {
            std::string english = row.count("english_name") && !row.at("english_name").empty() ? row.at("english_name") : row.at("name");
            std::string localized = row.count("localized_name") && !row.at("localized_name").empty() ? row.at("localized_name") : row.at("name");
            std::string display_name = (current_language == "en") ? english : localized;
            std::string english_type = row.at("type");
            std::string localized_type = row.count("localized_type") && !row.at("localized_type").empty() ? row.at("localized_type") : row.at("type");
            std::string display_type = (current_language == "en") ? english_type : localized_type;
            std::cout << "Card: " << display_name << ", " << row.at("set_code") << ", " << row.at("quantity") << std::endl;
            int foil = 0;
            if (row.count("foil")) {
                try { foil = std::stoi(row.at("foil")); } catch(...) { foil = 0; }
            }
            // Apply active filters (AND across categories, OR within a category)
            // Colors: row["colors"] is JSON array like ["W","U"]
            if (!state->filter_colors.empty()) {
                std::set<std::string> card_colors;
                if (row.count("colors") && !row.at("colors").empty()) {
                    try {
                        auto j = nlohmann::json::parse(row.at("colors"));
                        if (j.is_array()) {
                            for (auto &el : j) {
                                if (el.is_string()) card_colors.insert(el.get<std::string>());
                            }
                        }
                    } catch (...) {
                        // fallback: if not JSON, treat as sequence of single-letter codes
                        std::string tmp = row.at("colors");
                        for (char ch : tmp) {
                            if (!isspace((unsigned char)ch)) {
                                std::string s(1, ch);
                                card_colors.insert(s);
                            }
                        }
                    }
                }
                if (card_colors != state->filter_colors) continue;
            }
            if (!state->filter_rarities.empty()) {
                std::string card_rarity = row.count("rarity") ? row.at("rarity") : "";
                std::transform(card_rarity.begin(), card_rarity.end(), card_rarity.begin(), ::tolower);
                if (state->filter_rarities.count(card_rarity) == 0) continue;
            }
            // Type filters: inclusive substring match. If the user selected "Creature",
            // we should match both "Creature" and "Legendary Creature" etc.
            if (!state->filter_types.empty()) {
                std::string card_type_en = row.count("type") ? row.at("type") : "";
                std::string card_type_local = row.count("localized_type") && !row.at("localized_type").empty() ? row.at("localized_type") : card_type_en;
                auto lower = [](std::string s) {
                    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                    return s;
                };
                std::string cte = lower(card_type_en);
                std::string ctl = lower(card_type_local);
                bool matched = false;
                for (const auto &fk : state->filter_types) {
                    std::string fk_en = fk;
                    std::string prev_lang = current_language;
                    // Get Italian translation of the filter key (if any) so we can match localized types too
                    current_language = "it";
                    std::string fk_it = translate_type(fk_en.c_str());
                    current_language = prev_lang;
                    std::string fk_en_l = lower(fk_en);
                    std::string fk_it_l = lower(fk_it);
                    if ((cte.find(fk_en_l) != std::string::npos) || (ctl.find(fk_en_l) != std::string::npos) || (cte.find(fk_it_l) != std::string::npos) || (ctl.find(fk_it_l) != std::string::npos)) {
                        matched = true;
                        break;
                    }
                }
                if (!matched) continue;
            }
            if (state->filter_foil != -1) {
                if (state->filter_foil != foil) continue;
            }
            if (state->filter_no_deck) {
                if (row.count("deck_id") && !row.at("deck_id").empty()) continue;
            }
            // Build a human-friendly quantity display. When aggregation produced both
            // quantity_total and quantity_no_deck we prefer to show "no-deck / total"
            // unless the user specifically filtered to only deckless, in which case
            // show only the deckless count.
            std::string qty_display;
            if (row.count("quantity_total") && row.count("quantity_no_deck")) {
                int total_q = 0;
                int no_deck_q = 0;
                try { total_q = std::stoi(row.at("quantity_total")); } catch(...) { total_q = 0; }
                try { no_deck_q = std::stoi(row.at("quantity_no_deck")); } catch(...) { no_deck_q = 0; }
                if (state->filter_no_deck) {
                    qty_display = std::to_string(no_deck_q);
                } else if (no_deck_q > 0 && no_deck_q != total_q) {
                    qty_display = std::to_string(no_deck_q) + " / " + std::to_string(total_q);
                } else {
                    qty_display = std::to_string(total_q);
                }
            } else {
                qty_display = row.count("quantity") ? row.at("quantity") : std::string("0");
            }

            CardRow* crow = card_row_new(std::stoi(row.at("id")),
                                         display_name.c_str(),
                                         display_type.c_str(),
                                         row.at("colors").c_str(),
                                         row.at("set_code").c_str(),
                                         row.at("mana_cost").c_str(),
                                         row.at("rarity").c_str(),
                                         std::stoi(row.count("quantity") ? row.at("quantity") : std::string("0")),
                                         row.at("image_url").c_str(),
                                         row.count("added_date") ? row.at("added_date").c_str() : "",
                                         row.count("price_usd") ? row.at("price_usd").c_str() : "",
                                         foil,
                                         qty_display.c_str());
            g_list_store_append(state->card_store, crow);
            g_object_unref(crow);
            // Accumulate totals
            try {
                int qty = 0;
                if (row.count("quantity_no_deck") && state->filter_no_deck) {
                    try { qty = std::stoi(row.at("quantity_no_deck")); } catch(...) { qty = 0; }
                } else {
                    try { qty = std::stoi(row.count("quantity") ? row.at("quantity") : std::string("0")); } catch(...) { qty = 0; }
                }
                total_quantity += qty;
                if (row.count("price_usd") && !row.at("price_usd").empty()) {
                    try {
                        double price = std::stod(row.at("price_usd"));
                        total_value += price * qty;
                    } catch (...) {}
                }
            } catch (...) {}
        }
    } else {
        // Deck view: separate main deck and sideboard
        std::vector<std::map<std::string,std::string>> main_rows;
        std::vector<std::map<std::string,std::string>> side_rows;
        for (const auto& row : cards) {
            int sb = 0;
            if (row.count("sideboard")) {
                try { sb = std::stoi(row.at("sideboard")); } catch(...) { sb = 0; }
            }
            if (sb) side_rows.push_back(row); else main_rows.push_back(row);
        }
        // First append main rows
        for (const auto& row : main_rows) {
            std::string english = row.count("english_name") && !row.at("english_name").empty() ? row.at("english_name") : row.at("name");
            std::string localized = row.count("localized_name") && !row.at("localized_name").empty() ? row.at("localized_name") : row.at("english_name");
            std::string display_name = (current_language == "en") ? english : localized;
            std::string display_type = "";
            if (row.count("type")) display_type = (current_language == "en") ? row.at("type") : (row.count("localized_type") && !row.at("localized_type").empty() ? row.at("localized_type") : row.at("type"));
            int foil = 0;
            if (row.count("foil")) {
                try { foil = std::stoi(row.at("foil")); } catch(...) { foil = 0; }
            }
            CardRow* crow = card_row_new(std::stoi(row.at("id")), display_name.c_str(), display_type.c_str(), row.count("colors") ? row.at("colors").c_str() : "", row.count("set_code") ? row.at("set_code").c_str() : "", row.count("mana_cost") ? row.at("mana_cost").c_str() : "", row.count("rarity") ? row.at("rarity").c_str() : "", std::stoi(row.at("quantity")), row.count("image_url") ? row.at("image_url").c_str() : "", row.count("added_date") ? row.at("added_date").c_str() : "", row.count("price_usd") ? row.at("price_usd").c_str() : "", foil);
            g_list_store_append(state->card_store, crow);
            g_object_unref(crow);
            try { main_count += std::stoi(row.at("quantity")); } catch(...) {}
        }
        // If sideboard rows exist, append a separator and then side rows
        if (!side_rows.empty()) {
            std::string sep_label = translate("Sideboard");
            // Separator visual row (visual-only title row)
            CardRow* sep = card_row_new(ROW_ID_SEPARATOR_TITLE, sep_label.c_str(), "", "", "", "", "", 0, "", "", "", 0);
            g_list_store_append(state->card_store, sep);
            // Also add separator style to the list item widget when rendered via factories
            g_object_unref(sep);
            // After the separator, add a visual header row that repeats column names (id = -1)
            std::string header_line;
            // Build a compact header string that lists the column names
            header_line += translate("Nome"); header_line += " | ";
            header_line += translate("Tipo"); header_line += " | ";
            header_line += translate("Colori"); header_line += " | ";
            header_line += translate("Costo Mana"); header_line += " | ";
            header_line += translate("Rarità"); header_line += " | ";
            header_line += translate("Data di aggiunta"); header_line += " | ";
            header_line += translate("Quantità");
            CardRow* hdr = card_row_new(ROW_ID_HEADER, header_line.c_str(), "", "", "", "", "", 0, "", "", "", 0);
            g_list_store_append(state->card_store, hdr);
            g_object_unref(hdr);
            for (const auto& row : side_rows) {
                std::string english = row.count("english_name") && !row.at("english_name").empty() ? row.at("english_name") : row.at("name");
                std::string localized = row.count("localized_name") && !row.at("localized_name").empty() ? row.at("localized_name") : row.at("english_name");
                std::string display_name = (current_language == "en") ? english : localized;
                std::string display_type = "";
                if (row.count("type")) display_type = (current_language == "en") ? row.at("type") : (row.count("localized_type") && !row.at("localized_type").empty() ? row.at("localized_type") : row.at("type"));
                int foil = 0;
                if (row.count("foil")) {
                    try { foil = std::stoi(row.at("foil")); } catch(...) { foil = 0; }
                }
                CardRow* crow = card_row_new(std::stoi(row.at("id")), display_name.c_str(), display_type.c_str(), row.count("colors") ? row.at("colors").c_str() : "", row.count("set_code") ? row.at("set_code").c_str() : "", row.count("mana_cost") ? row.at("mana_cost").c_str() : "", row.count("rarity") ? row.at("rarity").c_str() : "", std::stoi(row.at("quantity")), row.count("image_url") ? row.at("image_url").c_str() : "", row.count("added_date") ? row.at("added_date").c_str() : "", row.count("price_usd") ? row.at("price_usd").c_str() : "", foil);
                g_list_store_append(state->card_store, crow);
                g_object_unref(crow);
                try { side_count += std::stoi(row.at("quantity")); } catch(...) {}
            }
        }
    }
    // Update total cards label
    char buf[256];
    if (state->selected_deck_id == -1) {
        // Main database: show overall total (accumulated in total_quantity)
        snprintf(buf, sizeof(buf), "%s: %d", translate("Totale carte").c_str(), total_quantity);
    } else {
        // Deck view: show main deck count and sideboard count separately
        snprintf(buf, sizeof(buf), "%s: %d    %s: %d", translate("Deck").c_str(), main_count, translate("Sideboard").c_str(), side_count);
    }
    gtk_label_set_text(GTK_LABEL(state->total_cards_label), buf);
    // Update filter summary label
    std::string fsummary;
    if (!state->filter_colors.empty()) {
        fsummary += "Colori:";
        for (auto it = state->filter_colors.begin(); it != state->filter_colors.end(); ++it) {
            if (it != state->filter_colors.begin()) fsummary += ",";
            fsummary += *it;
        }
        fsummary += " ";
    }
    if (!state->filter_rarities.empty()) {
        fsummary += "Rarità:";
        bool first = true;
        for (const auto &r : state->filter_rarities) {
            if (!first) fsummary += ",";
            fsummary += r;
            first = false;
        }
        fsummary += " ";
    }
    if (!state->filter_types.empty()) {
        fsummary += "Tipo:";
        bool firstt = true;
        for (const auto &t : state->filter_types) {
            if (!firstt) fsummary += ",";
            // Show localized label for the type
            std::string prev = current_language;
            current_language = "it";
            std::string lab_it = translate_type(t.c_str());
            current_language = prev;
            fsummary += lab_it;
            firstt = false;
        }
        fsummary += " ";
    }
    if (state->filter_foil != -1) {
        fsummary += (state->filter_foil == 1) ? "Solo Foil" : "Solo Non-Foil";
    }
    if (fsummary.empty()) fsummary = "";
    gtk_label_set_text(GTK_LABEL(state->filter_label), fsummary.c_str());
    // Debug log to help track down cases where UI shows zero
    std::cout << "DEBUG: totals computed -> quantity=" << total_quantity << ", value=$" << total_value << std::endl;
    // Update the columnview model
    // GtkSelectionModel *selection = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(state->card_store)));
    // gtk_column_view_set_model(state->column_view, selection);
    // g_object_unref(selection);
    // Force redraw
    gtk_widget_queue_draw(GTK_WIDGET(state->column_view));
    // Start background thumbnail prefetch for the rows we just loaded (best-effort)
    try { prefetch_thumbnails_async(cards); } catch(...) {}
    // gtk_widget_queue_draw(state->listview);
}

// Helper: remove (move) cards from a deck back to the main database (deck_id NULL).
// If to_remove >= current quantity -> move entire row to main (merging if main already has it).
// Otherwise -> reduce deck row quantity and insert/merge the removed quantity into main DB.
static bool remove_card_from_deck(AppState* state, int card_id, int to_remove) {
    if (!state || !state->db) return false;
    int current_qty = 0;
    if (!state->db->get_card_quantity(card_id, current_qty)) return false;
    if (to_remove <= 0) return false;
    // Fetch full row data
    std::map<std::string,std::string> crow;
    if (!state->db->query("SELECT english_name, localized_name, type, localized_type, colors, set_code, mana_cost, rarity, image_url, price_usd, foil, oracle_text FROM cards WHERE id = ?", [&](const std::map<std::string,std::string>& r){ crow = r; }, std::vector<std::string>{std::to_string(card_id)})) {
        return false;
    }
    if (crow.empty()) return false;
    std::string en = crow.count("english_name") ? crow["english_name"] : "";
    std::string ln = crow.count("localized_name") ? crow["localized_name"] : en;
    std::string ty = crow.count("type") ? crow["type"] : "";
    std::string lty = crow.count("localized_type") ? crow["localized_type"] : ty;
    std::string cols = crow.count("colors") ? crow["colors"] : "";
    std::string setc = crow.count("set_code") ? crow["set_code"] : "";
    std::string mana = crow.count("mana_cost") ? crow["mana_cost"] : "";
    std::string rar = crow.count("rarity") ? crow["rarity"] : "";
    std::string img = crow.count("image_url") ? crow["image_url"] : "";
    std::string price = crow.count("price_usd") ? crow["price_usd"] : "";
    std::string oracle = crow.count("oracle_text") ? crow["oracle_text"] : "";
    int foil = 0;
    if (crow.count("foil")) {
        try { foil = std::stoi(crow["foil"]); } catch(...) { foil = 0; }
    }

    if (to_remove >= current_qty) {
        // Move entire row: insert into main (deck_id = -1) merged, then delete original
    bool ok = state->db->insert_card(en, ln, ty, lty, cols, setc, mana, rar, current_qty, img, price, oracle, -1, foil);
        if (!ok) return false;
        return state->db->delete_card(card_id);
    }
    // Partial remove: decrease original and insert into main deckless rows
    if (!state->db->update_quantity(card_id, current_qty - to_remove)) return false;
    return state->db->insert_card(en, ln, ty, lty, cols, setc, mana, rar, to_remove, img, price, oracle, -1, foil);
}

// Asynchronously prefetch thumbnails for the given page rows.
// This runs in a detached thread and saves images into data/img/ if missing.
static void prefetch_thumbnails_async(const std::vector<std::map<std::string,std::string>>& rows) {
    // Spawn worker thread
    std::thread([rows]() {
        try {
            ensure_data_dir_exists("data");
            ensure_data_dir_exists("data/img");
        } catch(...) {}
        for (const auto &r : rows) {
            try {
                if (!r.count("image_url") || r.at("image_url").empty()) continue;
                std::string url = r.at("image_url");
                std::filesystem::path url_path(url);
                std::string filename = url_path.filename().string();
                if (filename.empty()) continue;
                std::string filepath = std::string("data/img/") + filename;
                if (std::filesystem::exists(filepath)) continue;
                auto data = download_image_data(url);
                if (!data.empty()) {
                    std::ofstream out(filepath, std::ios::binary);
                    if (out) {
                        out.write((char*)data.data(), data.size());
                        out.close();
                    }
                }
                // Small throttle so we don't hammer remote servers
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } catch(...) {
                // Swallow network/cache errors; prefetch is best-effort
            }
        }
    }).detach();
}

// Helper: move or split a card into a deck. If to_move >= current quantity, the card's deck_id
// is set to target_deck_id. Otherwise original quantity is reduced and a new/merged card row
// is inserted for the target deck using Database::insert_card.
static bool add_card_to_deck(AppState* state, int card_id, int to_move, int target_deck_id, int sideboard) {
    if (!state || !state->db) return false;
    int current_qty = 0;
    if (!state->db->get_card_quantity(card_id, current_qty)) return false;
    if (to_move <= 0) return false;
    if (to_move >= current_qty) {
        return state->db->set_card_deck(card_id, target_deck_id, sideboard);
    }
    // Need to split: reduce original quantity and insert/move 'to_move' to target deck
    if (!state->db->update_quantity(card_id, current_qty - to_move)) return false;
    // Fetch full row for insertion
    std::map<std::string,std::string> crow;
    if (!state->db->query("SELECT english_name, localized_name, type, localized_type, colors, set_code, mana_cost, rarity, image_url, price_usd, foil, oracle_text FROM cards WHERE id = ?", [&](const std::map<std::string,std::string>& r){ crow = r; }, std::vector<std::string>{std::to_string(card_id)})) {
        return false;
    }
    if (crow.empty()) return false;
    std::string en = crow.count("english_name") ? crow["english_name"] : "";
    std::string ln = crow.count("localized_name") ? crow["localized_name"] : en;
    std::string ty = crow.count("type") ? crow["type"] : "";
    std::string lty = crow.count("localized_type") ? crow["localized_type"] : ty;
    std::string cols = crow.count("colors") ? crow["colors"] : "";
    std::string setc = crow.count("set_code") ? crow["set_code"] : "";
    std::string mana = crow.count("mana_cost") ? crow["mana_cost"] : "";
    std::string rar = crow.count("rarity") ? crow["rarity"] : "";
    std::string img = crow.count("image_url") ? crow["image_url"] : "";
    std::string price = crow.count("price_usd") ? crow["price_usd"] : "";
    std::string oracle = crow.count("oracle_text") ? crow["oracle_text"] : "";
    int foil = 0;
    if (crow.count("foil")) {
        try { foil = std::stoi(crow["foil"]); } catch(...) { foil = 0; }
    }
    // Use Database::insert_card which will merge into existing identical card in the target deck
    return state->db->insert_card(en, ln, ty, lty, cols, setc, mana, rar, to_move, img, price, oracle, target_deck_id, foil, sideboard);
}

static void on_add_card_ok_clicked(GtkButton *button, gpointer user_data) {
    AddCardContext* ctx = (AddCardContext*)user_data;
    GtkWidget *entry = ctx->entry;
    GtkWidget *spin = ctx->spin;
    AppState* state = ctx->state;
    const char *card_name = gtk_editable_get_text(GTK_EDITABLE(entry));
    int quantity = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
    
    auto cards = search_cards_from_scryfall(card_name);
    
    // If no matches, just show an alert and keep the dialog open so the user can refine the query
    if (cards.empty()) {
        GtkWindow* parent = ctx->parent;
        std::string msg = "Nessuna carta trovata per: " + std::string(card_name);
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", msg.c_str());
        gtk_alert_dialog_show(alert, parent);
        g_object_unref(alert);
        // keep dialog open, do not delete ctx
        // Put keyboard focus back on the search entry so the user can type a new query quickly
            if (entry && GTK_IS_WIDGET(entry)) {
            // Select current text so typing replaces it immediately
            gtk_widget_grab_focus(entry);
            gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
            // Schedule a short timeout to reliably restore focus after transient dialogs close.
            schedule_focus_retries(entry, state);
        }
        return;
    }

    // Single result: if Scryfall returned exactly one card, insert it directly
    // (no need to show the selection dialog even for localized/Italian names).
    if (cards.size() == 1) {
        auto& card = cards[0];
        std::cout << "Single card: " << card.name << ", price_usd: '" << card.price_usd << "'" << std::endl;
        std::string msg = "Nome: " + card.name + "\n";
        msg += "Tipo: " + card.type + "\n";
        msg += "Colori: " + card.colors + "\n";
        msg += "Set: " + card.set_name + "\n";
        msg += "Costo Mana: " + card.mana_cost + "\n";
        msg += "Rarità: " + card.rarity + "\n";
        msg += "Prezzo USD: " + (card.price_usd.empty() ? "N/A" : card.price_usd) + "\n";
        msg += "Testo: " + card.oracle_text;

        if (state && state->db) {
            int foil = 0;
            if (ctx->foil_checkbox) {
                gboolean f = FALSE;
                g_object_get(G_OBJECT(ctx->foil_checkbox), "active", &f, NULL);
                foil = f ? 1 : 0;
            }
            bool success = state->db->insert_card(card.english_name, card.localized_name, card.type, card.localized_type, card.colors, card.set_code, card.mana_cost, card.rarity, quantity, card.image_url, card.price_usd, card.oracle_text, -1, foil);
            std::cout << "Inserted card: " << card.english_name << " in set " << card.set_name << " qty " << quantity << " success: " << success << std::endl;
            if (success) {
                refresh_card_list(state);
                g_main_context_iteration(NULL, FALSE);
                msg += "\n\n[Salvata nel database]";
            } else {
                msg += "\n\n[Errore nel salvare]";
            }
        } else {
            msg += "\n\n[ERRORE: Nessun database aperto]";
        }

        // If insertion succeeded, send a brief desktop notification instead of
        // showing a details dialog. On failure, show the error details dialog.
        if (msg.find("Salvata nel database") != std::string::npos) {
            // success path: send notify
            std::filesystem::path p(state->db_path);
            std::string dbname = p.filename().string();
            std::string body = card.name + " aggiunta a " + dbname;
            send_notification("Carta aggiunta", body, "#file:magicdb-icon.png");
            // Reset inputs so user can add another card quickly
            gtk_editable_set_text(GTK_EDITABLE(entry), "");
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1);
            // If there was a foil checkbox, reset it as well
            if (ctx->foil_checkbox) {
                g_object_set(G_OBJECT(ctx->foil_checkbox), "active", FALSE, NULL);
            }
            // Try to grab focus now and also schedule the reliable timeout (configured)
            gtk_widget_grab_focus(entry);
            schedule_focus_retries(entry, state);
            return;
        } else {
            // show error details dialog
            GtkWindow* add_win = GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(entry), GTK_TYPE_WINDOW));
            GtkWidget* det = create_styled_dialog(add_win ? add_win : ctx->parent, 480, 320);
            gtk_window_set_title(GTK_WINDOW(det), "Dettagli Carta");
            GtkWidget* dv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
            gtk_window_set_child(GTK_WINDOW(det), dv);
            GtkWidget* lab = gtk_label_new(msg.c_str());
            gtk_label_set_wrap(GTK_LABEL(lab), TRUE);
            gtk_widget_set_hexpand(lab, TRUE);
            gtk_box_append(GTK_BOX(dv), lab);
            GtkWidget* btn_close = gtk_button_new_with_label("Chiudi");
            gtk_box_append(GTK_BOX(dv), btn_close);
            // When closed, restore focus to the entry via a short timeout (avoid focus races)
            g_signal_connect(det, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer user_data){
                // Use a short custom schedule for quick retries after closing details
                GtkWidget* ent = (GtkWidget*)user_data;
                schedule_focus_retries_custom(ent, 6, 50);
            }), entry);
            g_signal_connect(btn_close, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data){
                std::cout << "DEBUG: details dialog close button clicked, destroying window=" << user_data << "\n";
                gtk_window_destroy(GTK_WINDOW(user_data));
            }), det);
            // Allow Enter to close the details dialog (GTK4-style using GtkEventControllerKey)
            {
                GtkEventController *key = gtk_event_controller_key_new();
                g_signal_connect(key, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType mods, gpointer user_data) -> gboolean {
                    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
                        std::cout << "DEBUG: details dialog key-pressed Enter, destroying window=" << user_data << "\n";
                        gtk_window_destroy(GTK_WINDOW(user_data));
                        return TRUE;
                    }
                    return FALSE;
                }), det);
                gtk_widget_add_controller(det, key);
            }
            gtk_window_set_default_widget(GTK_WINDOW(det), btn_close);
            gtk_window_present(GTK_WINDOW(det));
            return;
        }
    }

    // Multiple matches - show selection dialog; keep original search dialog open
    GtkWidget *dialog = create_styled_dialog(GTK_WINDOW(ctx->parent), 400, 300);
    gtk_window_set_title(GTK_WINDOW(dialog), "Seleziona Carta");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    GtkWidget *label = gtk_label_new("Seleziona la carta desiderata:");
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_append(GTK_BOX(box), scrolled);

    GtkWidget *list_box = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_box);

    for (size_t i = 0; i < cards.size(); ++i) {
        const auto& card = cards[i];
        std::string display_text = card.name + " (" + card.set_name + ") - " + card.type;
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lbl = gtk_label_new(display_text.c_str());
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        gtk_list_box_append(GTK_LIST_BOX(list_box), row);
        g_object_set_data(G_OBJECT(row), "card_index", (gpointer)i);
    }

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), button_box);

    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    struct SelectCardContext {
        GtkWidget* list_box;
        std::vector<ScryfallCard>* cards;
        AppState* state;
        int quantity;
        GtkWindow* parent;
        AddCardContext* original_ctx;
    };

    SelectCardContext* select_ctx = new SelectCardContext{
        list_box,
        new std::vector<ScryfallCard>(cards),
        state,
        quantity,
        ctx->parent,
        ctx
    };

    g_signal_connect(cancel_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        SelectCardContext* sctx = (SelectCardContext*)user_data;
        GtkWidget* select_dialog = gtk_widget_get_ancestor(GTK_WIDGET(sctx->list_box), GTK_TYPE_WINDOW);
        if (select_dialog && GTK_IS_WIDGET(select_dialog)) {
            gtk_window_destroy(GTK_WINDOW(select_dialog));
        }
        delete sctx->cards;
        delete sctx;
    }), select_ctx);

    g_signal_connect(list_box, "row-activated", G_CALLBACK(+[](GtkWidget* list_box, GtkListBoxRow* row, gpointer user_data) {
        SelectCardContext* sctx = (SelectCardContext*)user_data;
        std::cout << "DEBUG: row-activated handler called; sctx=" << sctx << ", row=" << row << std::endl;
        size_t index = 0;
        if (row) {
            gpointer v = g_object_get_data(G_OBJECT(row), "card_index");
            index = (size_t)v;
            std::cout << "DEBUG: activated row index=" << index << std::endl;
        } else {
            std::cout << "DEBUG: activated row is NULL" << std::endl;
        }
        if (index < sctx->cards->size()) {
            auto& card = (*sctx->cards)[index];
            std::string msg = "Nome: " + card.name + "\n";
            msg += "Tipo: " + card.type + "\n";
            msg += "Colori: " + card.colors + "\n";
            msg += "Set: " + card.set_name + "\n";
            msg += "Costo Mana: " + card.mana_cost + "\n";
            msg += "Rarità: " + card.rarity + "\n";
            msg += "Testo: " + card.oracle_text;

            if (sctx->state && sctx->state->db) {
                int foil = 0;
                if (sctx->original_ctx && sctx->original_ctx->foil_checkbox) {
                    gboolean f = FALSE;
                    g_object_get(G_OBJECT(sctx->original_ctx->foil_checkbox), "active", &f, NULL);
                    foil = f ? 1 : 0;
                }
                bool success = sctx->state->db->insert_card(card.english_name, card.localized_name, card.type, card.localized_type, card.colors, card.set_code, card.mana_cost, card.rarity, sctx->quantity, card.image_url, card.price_usd, card.oracle_text, -1, foil);
                std::cout << "Inserted card: " << card.english_name << " in set " << card.set_name << " qty " << sctx->quantity << " success: " << success << std::endl;
                if (success) {
                    refresh_card_list(sctx->state);
                    g_main_context_iteration(NULL, FALSE);
                    // Send brief desktop notification instead of showing details dialog
                    std::filesystem::path p(sctx->state->db_path);
                    std::string dbname = p.filename().string();
                    std::string body = card.name + " aggiunta a " + dbname;
                    send_notification("Carta aggiunta", body, "#file:magicdb-icon.png");
                    // Close the selection dialog (it will be closed later in this handler)
                } else {
                    msg += "\n\n[Errore nel salvare]";
                }
            } else {
                msg += "\n\n[ERRORE: Nessun database aperto]";
            }
            // If success we already sent a notification; otherwise show a details dialog with the error message
            if (msg.find("Errore") != std::string::npos || msg.find("ERRORE") != std::string::npos) {
                GtkWindow* orig_win = NULL;
                if (sctx->original_ctx && sctx->original_ctx->entry) orig_win = GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(sctx->original_ctx->entry), GTK_TYPE_WINDOW));
                GtkWidget* det = create_styled_dialog(orig_win ? orig_win : sctx->parent, 480, 320);
                gtk_window_set_title(GTK_WINDOW(det), "Dettagli Carta");
                GtkWidget* dv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
                gtk_window_set_child(GTK_WINDOW(det), dv);
                GtkWidget* lab = gtk_label_new(msg.c_str());
                gtk_label_set_wrap(GTK_LABEL(lab), TRUE);
                gtk_box_append(GTK_BOX(dv), lab);
                GtkWidget* btn_close = gtk_button_new_with_label("Chiudi");
                gtk_box_append(GTK_BOX(dv), btn_close);
                if (sctx->original_ctx) {
                    g_signal_connect(det, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer user_data){
                        GtkWidget* ent = (GtkWidget*)user_data;
                        schedule_focus_retries(ent, nullptr);
                    }), sctx->original_ctx->entry);
                }
                g_signal_connect(btn_close, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data){
                    std::cout << "DEBUG: details dialog close button clicked, destroying window=" << user_data << "\n";
                    gtk_window_destroy(GTK_WINDOW(user_data));
                }), det);
                // Allow Enter to close the details dialog (GTK4-style using GtkEventControllerKey)
                {
                    GtkEventController *key = gtk_event_controller_key_new();
                    g_signal_connect(key, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType mods, gpointer user_data) -> gboolean {
                        if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
                            std::cout << "DEBUG: details dialog key-pressed Enter, destroying window=" << user_data << "\n";
                            gtk_window_destroy(GTK_WINDOW(user_data));
                            return TRUE;
                        }
                        return FALSE;
                    }), det);
                    gtk_widget_add_controller(det, key);
                }
                gtk_window_set_default_widget(GTK_WINDOW(det), btn_close);
                gtk_window_present(GTK_WINDOW(det));
            }

            // Close the select dialog if present. Instead of destroying it immediately and
            // trying to grab focus (which races with the window manager and the dialog
            // destruction), attach a destroy handler that schedules the reliable focus
            // retry. That way the focus attempts happen after the selection window is
            // fully torn down.
            GtkWidget* select_dialog = gtk_widget_get_ancestor(sctx->list_box, GTK_TYPE_WINDOW);
            if (select_dialog && GTK_IS_WIDGET(select_dialog)) {
                if (sctx->original_ctx && sctx->original_ctx->entry) {
                    // When the select dialog is destroyed, schedule focus retries for the
                    // original add-card entry (this avoids races with the WM).
                    g_signal_connect(select_dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer user_data){
                        GtkWidget* ent = (GtkWidget*)user_data;
                        schedule_focus_retries(ent, nullptr);
                    }), sctx->original_ctx->entry);
                }
                gtk_window_destroy(GTK_WINDOW(select_dialog));
            }

            if (sctx->original_ctx) {
                // Reset inputs immediately (so the dialog below shows cleared fields)
                gtk_editable_set_text(GTK_EDITABLE(sctx->original_ctx->entry), "");
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(sctx->original_ctx->spin), 1);
                if (sctx->original_ctx->foil_checkbox) {
                    g_object_set(G_OBJECT(sctx->original_ctx->foil_checkbox), "active", FALSE, NULL);
                }
                // Also try an immediate grab (best-effort) — the scheduled retries will
                // perform the reliable focus restore after the selection dialog is
                // destroyed.
                gtk_widget_grab_focus(sctx->original_ctx->entry);
            }
        }
        delete sctx->cards;
        delete sctx;
    }), select_ctx);

    // Allow pressing Enter to activate the currently selected row even if focus
    // is on the dialog or was moved by a mouse click. This handler emits the
    // same "row-activated" signal so the existing selection logic (which
    // schedules focus restore on destroy) runs in the same code path.
    {
        GtkEventController *key = gtk_event_controller_key_new();
        g_signal_connect(key, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType mods, gpointer user_data) -> gboolean {
            if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
                std::cout << "DEBUG: Enter pressed in selection dialog" << std::endl;
                GtkWidget* list = GTK_WIDGET(user_data);
                GtkListBoxRow* sel = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
                if (sel) {
                    gpointer v = g_object_get_data(G_OBJECT(sel), "card_index");
                    size_t idx = (size_t)v;
                    std::cout << "DEBUG: Enter handler selected row index=" << idx << " sel=" << sel << std::endl;
                    // Emit row-activated so the handler above runs
                    g_signal_emit_by_name(list, "row-activated", sel);
                    return TRUE;
                } else {
                    std::cout << "DEBUG: Enter pressed but no row selected" << std::endl;
                }
            }
            return FALSE;
        }), list_box);
        // Attach controller to the dialog so it receives key events
        gtk_widget_add_controller(dialog, key);
    }

    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_add_card_clicked(GtkButton *button, gpointer user_data) {
    GtkWindow *parent = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(parent), "app_state");
    std::cout << "DEBUG: on_add_card_clicked called; parent=" << parent << ", state=" << state << std::endl;
    
    // Controlla se c'è un database aperto
    if (!state->db) {
        // Cerca database esistenti in data/
        std::vector<std::string> dbs;
        if (std::filesystem::exists("data") && std::filesystem::is_directory("data")) {
            for (const auto& entry : std::filesystem::directory_iterator("data")) {
                if (entry.is_regular_file() && entry.path().extension() == ".db") {
                    dbs.push_back(entry.path().string());
                }
            }
        }
        
        if (dbs.empty()) {
            // Crea nuovo database automaticamente
            std::cout << "DEBUG: no db files found; creating new DB at data/collection.db" << std::endl;
            std::string db_path = "data/collection.db";
            if (state->db) delete state->db;
            state->db = new Database(db_path);
            state->db_path = db_path;
            update_cards_schema_flags(state);
            gtk_label_set_text(GTK_LABEL(state->db_name_label), db_path.c_str());
                    Database db(db_path);
                    db.execute("CREATE TABLE IF NOT EXISTS cards (id INTEGER PRIMARY KEY, name TEXT, type TEXT, colors TEXT, set_code TEXT, mana_cost TEXT, rarity TEXT, quantity INTEGER, image_url TEXT, added_date TEXT, price_usd TEXT, foil INTEGER DEFAULT 0, oracle_text TEXT)");
            std::ofstream lastdb("lastdb.txt");
            lastdb << db_path << std::endl;
            refresh_card_list(state);
            // Populate deck menu for the auto-created DB
            populate_deck_menu(state);
        } else {
            // Mostra dialogo per scegliere database
            std::cout << "DEBUG: showing DB selection dialog" << std::endl;
            GtkWidget *dialog = create_styled_dialog(parent, 300, 200);
            gtk_window_set_title(GTK_WINDOW(dialog), "Scegli Database");
            
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
            gtk_window_set_child(GTK_WINDOW(dialog), box);
            
            GtkWidget *label = gtk_label_new("Scegli un database esistente:");
            gtk_box_append(GTK_BOX(box), label);
            
            GtkWidget *scrolled = gtk_scrolled_window_new();
            gtk_widget_set_vexpand(scrolled, TRUE);
            gtk_box_append(GTK_BOX(box), scrolled);
            
            GtkWidget *list_box = gtk_list_box_new();
            gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_box);
            
            for (const auto& db : dbs) {
                GtkWidget *row = gtk_list_box_row_new();
                GtkWidget *label = gtk_label_new(db.c_str());
                gtk_label_set_xalign(GTK_LABEL(label), 0.0);
                gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
                gtk_list_box_append(GTK_LIST_BOX(list_box), row);
                g_object_set_data(G_OBJECT(row), "db_path", g_strdup(db.c_str()));
            }
            
            GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_box_append(GTK_BOX(box), button_box);
            
            GtkWidget *ok_button = gtk_button_new_with_label("Apri");
            GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
            gtk_box_append(GTK_BOX(button_box), ok_button);
            gtk_box_append(GTK_BOX(button_box), cancel_button);
            
            gtk_window_set_default_widget(GTK_WINDOW(dialog), ok_button);
            
            struct SelectDbContext {
                AppState* state;
                GtkWidget* list_box;
                GtkWidget* dialog;
            };
            
            SelectDbContext* ctx = new SelectDbContext{state, list_box, dialog};
            
            g_signal_connect(ok_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
                SelectDbContext* ctx = (SelectDbContext*)user_data;
                GtkListBoxRow* selected_row = gtk_list_box_get_selected_row(GTK_LIST_BOX(ctx->list_box));
                if (selected_row) {
                    char* db_path = (char*)g_object_get_data(G_OBJECT(selected_row), "db_path");
                    if (ctx->state->db) delete ctx->state->db;
                    ctx->state->db = new Database(db_path);
                    ctx->state->db_path = db_path;
                    update_cards_schema_flags(ctx->state);
                    gtk_label_set_text(GTK_LABEL(ctx->state->db_name_label), db_path);
                    std::ofstream lastdb("lastdb.txt");
                    lastdb << db_path << std::endl;
                    refresh_card_list(ctx->state);
                }
                gtk_window_destroy(GTK_WINDOW(ctx->dialog));
                delete ctx;
            }), ctx);
            
            g_signal_connect(cancel_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
                SelectDbContext* ctx = (SelectDbContext*)user_data;
                gtk_window_destroy(GTK_WINDOW(ctx->dialog));
                delete ctx;
            }), ctx);
            
            gtk_window_present(GTK_WINDOW(dialog));
            std::cout << "DEBUG: DB selection dialog presented" << std::endl;
            return;  // Non continuare se stiamo mostrando il dialogo di selezione
        }
    }
    
    // If we're currently viewing a specific deck, open the "Aggiungi Carta" dialog
    if (state && state->selected_deck_id != -1) {
        std::cout << "DEBUG: in deck view, opening 'Aggiungi Carta al Deck' dialog; selected_deck_id=" << state->selected_deck_id << std::endl;
        GtkWidget *dialog = create_styled_dialog(parent, 500, 400);
        gtk_window_set_title(GTK_WINDOW(dialog), "Aggiungi Carta al Deck");

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_window_set_child(GTK_WINDOW(dialog), vbox);

        GtkWidget *scrolled = gtk_scrolled_window_new();
        gtk_widget_set_vexpand(scrolled, TRUE);
        gtk_box_append(GTK_BOX(vbox), scrolled);

        GtkWidget *list_box = gtk_list_box_new();
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_box);

        // Query cards with deck_id IS NULL
        state->db->query("SELECT id, english_name, localized_name, name, set_code, quantity FROM cards WHERE deck_id IS NULL ORDER BY name", [&](const std::map<std::string, std::string>& row) {
            int cid = std::stoi(row.at("id"));
            int qty = 0;
            try { qty = std::stoi(row.at("quantity")); } catch(...) { qty = 0; }
            if (qty <= 0) return; // skip
            std::string display = row.count("localized_name") && !row.at("localized_name").empty() ? row.at("localized_name") : row.at("english_name");
            display += " (" + row.at("set_code") + ") - Disponibili: " + std::to_string(qty);
                GtkWidget *roww = gtk_list_box_row_new();
                // Use a grid to align columns: Name | Quantity | Side
                GtkWidget *grid = gtk_grid_new();
                gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
                gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(roww), grid);

                // Selection checkbox (left edge) and Name only (no set shown)
                GtkWidget *check = gtk_check_button_new();
                gtk_widget_set_hexpand(check, FALSE);
                gtk_grid_attach(GTK_GRID(grid), check, 0, 0, 1, 1);
                g_signal_connect(check, "toggled", G_CALLBACK(on_deck_row_check_toggled), roww);

                // Name label - only name is shown (no set or available text)
                std::string name_only = row.count("localized_name") && !row.at("localized_name").empty() ? row.at("localized_name") : row.at("english_name");
                GtkWidget *label = gtk_label_new(name_only.c_str());
                gtk_label_set_xalign(GTK_LABEL(label), 0.0);
                gtk_widget_set_hexpand(label, TRUE);
                gtk_grid_attach(GTK_GRID(grid), label, 1, 0, 1, 1);

                // Store the card id on the row so we can read it later from the list box
                g_object_set_data(G_OBJECT(roww), "card_id", GINT_TO_POINTER(cid));
                // Save checkbox on the row so handlers can toggle/inspect it
                g_object_set_data(G_OBJECT(roww), "check", check);
                // Use a GtkGestureClick to toggle the checkbox on press (GTK4)
                GtkGesture *gesture = gtk_gesture_click_new();
                g_signal_connect(gesture, "pressed", G_CALLBACK(on_deck_add_row_pressed), roww);
                // Attach gesture to the row widget so clicks anywhere on the row trigger it
                gtk_widget_add_controller(roww, GTK_EVENT_CONTROLLER(gesture));
                gtk_list_box_append(GTK_LIST_BOX(list_box), roww);
        });

        // Allow activating (double-click / Enter) a row to toggle its checkbox
        g_signal_connect(list_box, "row-activated", G_CALLBACK(on_deck_add_row_activated), NULL);

        GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_append(GTK_BOX(vbox), button_box);
        GtkWidget *ok_button = gtk_button_new_with_label("Aggiungi");
        GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
        gtk_box_append(GTK_BOX(button_box), ok_button);
        gtk_box_append(GTK_BOX(button_box), cancel_button);

        // Handle Cancel / OK
        g_signal_connect_swapped(cancel_button, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    DeckAddOkCtx* okctx = new DeckAddOkCtx;
    okctx->state = state;
    okctx->list_box = list_box;
    okctx->dialog = GTK_WINDOW(dialog);
    g_signal_connect(ok_button, "clicked", G_CALLBACK(on_deck_add_ok), okctx);

        gtk_window_present(GTK_WINDOW(dialog));
        std::cout << "DEBUG: Aggiungi Carta al Deck dialog presented" << std::endl;
        return;
    }
    // Ora che abbiamo un database aperto, mostra il dialogo di ricerca
    GtkWidget *dialog = create_styled_dialog(parent, 350, 100);
    gtk_window_set_title(GTK_WINDOW(dialog), "Cerca carta su Scryfall");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Nome carta");
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget *spin = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1);
    gtk_box_append(GTK_BOX(box), spin);

    GtkWidget *foil_check = gtk_check_button_new_with_label("Foil");
    gtk_box_append(GTK_BOX(box), foil_check);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), button_box);
    GtkWidget *ok_button = gtk_button_new_with_label("Cerca");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    // Imposta pulsante OK come default
    gtk_window_set_default_widget(GTK_WINDOW(dialog), ok_button);

    // Recupera lo stato globale
    AddCardContext* ctx = new AddCardContext{entry, spin, state, parent, foil_check};
    g_signal_connect(ok_button, "clicked", G_CALLBACK(on_add_card_ok_clicked), ctx);
    g_signal_connect_swapped(cancel_button, "clicked", G_CALLBACK(gtk_window_destroy), dialog);

    // Collega activate dell'entry al click su OK
    g_signal_connect(entry, "activate", G_CALLBACK(on_add_card_ok_clicked), ctx);

    // Ensure the AddCardContext is deleted when the dialog is destroyed
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
        AddCardContext* c = (AddCardContext*)user_data;
        delete c;
    }), ctx);

    gtk_window_present(GTK_WINDOW(dialog));
    std::cout << "DEBUG: Cerca carta su Scryfall dialog presented" << std::endl;
}

static void on_new_db_ok_clicked(GtkButton *button, gpointer user_data) {
    // Aggiorna lista carte dopo creazione nuovo db
    DialogWidgets *widgets = (DialogWidgets*)user_data;
    GtkWindow* main_win = GTK_WINDOW(gtk_window_get_transient_for(GTK_WINDOW(widgets->dialog)));
    if (main_win) {
        AppState* state2 = (AppState*)g_object_get_data(G_OBJECT(main_win), "app_state");
        if (state2) refresh_card_list(state2);
    }
    const char *name = gtk_editable_get_text(GTK_EDITABLE(widgets->entry));
    if (name && *name) {
        std::string db_path = "data/" + std::string(name) + ".db";
        // Aggiorna stato globale se presente
        GtkWindow* main_win = GTK_WINDOW(gtk_window_get_transient_for(GTK_WINDOW(widgets->dialog)));
        AppState* state = nullptr;
        if (main_win) state = (AppState*)g_object_get_data(G_OBJECT(main_win), "app_state");
        if (state) {
            if (state->db) delete state->db;
            state->db = new Database(db_path);
            state->db_path = db_path;
            update_cards_schema_flags(state);
            gtk_label_set_text(GTK_LABEL(state->db_name_label), db_path.c_str());
            // Salva percorso su file
            std::ofstream lastdb("lastdb.txt");
            lastdb << db_path << std::endl;
            // Populate deck menu for the new database
            populate_deck_menu(state);
        }
    Database db(db_path);
    db.execute("CREATE TABLE IF NOT EXISTS cards (id INTEGER PRIMARY KEY, name TEXT, type TEXT, colors TEXT, set_code TEXT, mana_cost TEXT, rarity TEXT, quantity INTEGER, image_url TEXT, added_date TEXT, price_usd TEXT, foil INTEGER DEFAULT 0, oracle_text TEXT)");
    }
    gtk_window_destroy(GTK_WINDOW(widgets->dialog));
    delete widgets;
}

static void on_new_db_cancel_clicked(GtkButton *button, gpointer user_data) {
    DialogWidgets *widgets = (DialogWidgets*)user_data;
    gtk_window_destroy(GTK_WINDOW(widgets->dialog));
    delete widgets;
}

static void on_new_db_clicked(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWindow *parent = GTK_WINDOW(user_data);
    GtkWidget *dialog = create_styled_dialog(parent, 320, 140);
    gtk_window_set_title(GTK_WINDOW(dialog), "Nuovo Database");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Nome database");
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), button_box);

    GtkWidget *ok_button = gtk_button_new_with_label("OK");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    // Imposta pulsante OK come default
    gtk_window_set_default_widget(GTK_WINDOW(dialog), ok_button);

    DialogWidgets *widgets = new DialogWidgets{dialog, entry};
    g_signal_connect(ok_button, "clicked", G_CALLBACK(on_new_db_ok_clicked), widgets);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_new_db_cancel_clicked), widgets);

    // Collega activate dell'entry al click su OK
    g_signal_connect(entry, "activate", G_CALLBACK(on_new_db_ok_clicked), widgets);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void leave_handler(GtkEventControllerMotion*, gpointer user_data) {
    GtkWidget *window = GTK_WIDGET(user_data);
    if (window && GTK_IS_WIDGET(window)) {
        g_signal_handlers_disconnect_by_func(window, (gpointer)leave_handler, window);
        gtk_window_close(GTK_WINDOW(window));
    }
}

static void on_row_enter(GtkEventControllerMotion*, double x, double y, gpointer user_data) {
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    CardRow *row = (CardRow*)gtk_list_item_get_item(list_item);
    if (!row || !row->image_url || strlen(row->image_url) == 0) return;
    GtkWidget *label = gtk_list_item_get_child(list_item);
    GtkWidget *picture = gtk_picture_new();
    // Controlla se l'immagine è già in cache
    std::filesystem::path url_path(row->image_url);
    std::string filename = url_path.filename().string();
    std::string filepath = "data/img/" + filename;
    std::vector<unsigned char> image_data;
    if (std::filesystem::exists(filepath)) {
        image_data = load_image_from_file(filepath);
    } else {
        image_data = download_image_data(row->image_url);
        if (!image_data.empty()) {
            // Salva in cache
            std::ofstream file(filepath, std::ios::binary);
            if (file) {
                file.write((char*)image_data.data(), image_data.size());
            }
        }
    }
    if (!image_data.empty()) {
        GBytes *bytes = g_bytes_new(image_data.data(), image_data.size());
        GdkTexture *texture = gdk_texture_new_from_bytes(bytes, nullptr);
        if (texture) {
            gtk_picture_set_paintable(GTK_PICTURE(picture), GDK_PAINTABLE(texture));
            g_object_unref(texture);
        }
        g_bytes_unref(bytes);
    }
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
    gtk_widget_set_size_request(picture, 60, 84);
    gtk_widget_set_margin_start(picture, 0);
    gtk_widget_set_margin_end(picture, 0);
    gtk_widget_set_margin_top(picture, 0);
    gtk_widget_set_margin_bottom(picture, 0);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), picture);
    gtk_widget_set_size_request(box, 60, 84);
    GtkWidget *window = gtk_window_new();
    // Style the preview window to match app theme
    gtk_widget_add_css_class(window, "small-popover");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_modal(GTK_WINDOW(window), FALSE);
    {
        GtkWidget *parent_win = gtk_widget_get_ancestor(label, GTK_TYPE_WINDOW);
        if (parent_win && GTK_IS_WIDGET(parent_win)) {
            gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parent_win));
        }
    }
    gtk_window_set_child(GTK_WINDOW(window), box);
    // Posiziona vicino al label (in GTK4, la posizione è gestita dal WM)
    gtk_window_present(GTK_WINDOW(window));
    // Aggiungi controller per chiudere su leave del window
    GtkEventController *motion_window = gtk_event_controller_motion_new();
    g_signal_connect(motion_window, "leave", G_CALLBACK(leave_handler), window);
    gtk_widget_add_controller(window, motion_window);
}

static GMenu* create_context_menu(CardRow *row, AppState *state);

static void on_row_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    CardRow *row = (CardRow*)gtk_list_item_get_item(list_item);
    if (!row) return;
    std::cout << "Right click on row id: " << row->id << ", name: " << (row->name ? row->name : "NULL") << std::endl;
    // Trova lo state dal widget
    GtkWidget *label = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkWidget *column_view = gtk_widget_get_ancestor(label, GTK_TYPE_COLUMN_VIEW);
    if (!column_view) return;
    /* Safely obtain the application state stored on the root/toplevel object. */
    GtkRoot *root = gtk_widget_get_root(column_view);
    AppState *state = NULL;
    if (root && G_IS_OBJECT(root)) {
        state = (AppState*)g_object_get_data(G_OBJECT(root), "app_state");
    }
    if (!state) return;
    // Mostra menu
    GtkWidget *menu = gtk_popover_menu_new_from_model(G_MENU_MODEL(create_context_menu(row, state)));
    gtk_widget_set_parent(menu, column_view);
    // Converti coordinate
    graphene_point_t point = { (float)x, (float)y };
    graphene_point_t out_point;
    __attribute__((unused)) gboolean success = gtk_widget_compute_point(label, column_view, &point, &out_point);
    double wx = out_point.x, wy = out_point.y;
    GdkRectangle rect = { (int)wx, (int)wy, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(menu), &rect);
    gtk_popover_popup(GTK_POPOVER(menu));
}

// Double-click handler: toggles the detail revealer attached to the list item.
static void on_row_double_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)x; (void)y;
    if (n_press != 2) return;
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    if (!list_item) return;
    /* Debug: log double-click invocations */
    CardRow *crow = (CardRow*)gtk_list_item_get_item(list_item);
    const char *nm = (crow && crow->name) ? crow->name : "(no-name)";
    g_print("on_row_double_click: n_press=2 list_item=%p name=%s\n", (void*)list_item, nm);
    /* Debounce/deduplicate double-clicks coming from multiple per-column
     * controllers: store a small timestamp on the shared CardRow and ignore
     * subsequent toggles for the same row within a short interval. This
     * prevents two controllers (attached to different column ListItems) from
     * toggling the revealer twice and cancelling the user's action. */
    gint64 now_ms = g_get_monotonic_time() / 1000; /* ms */
    gboolean too_soon = FALSE;
    if (crow) {
        gpointer prevp = g_object_get_data(G_OBJECT(crow), "last_dbl_time");
        if (prevp) {
            gint prev = GPOINTER_TO_INT(prevp);
            if (now_ms - (gint64)prev < 350) {
                too_soon = TRUE;
            }
        }
        /* store truncated ms (fits in gint) */
        g_object_set_data(G_OBJECT(crow), "last_dbl_time", GINT_TO_POINTER((gint)(now_ms & 0x7fffffff)));
    }
    if (too_soon) {
        g_print("  ignoring duplicate dblclick for row=%p\n", (void*)crow);
        return;
    }
    /* Try to obtain the revealer reference. Note: ColumnView creates separate
     * GtkListItem objects per column, so the per-column ListItem may not hold
     * the "detail_revealer" data — the name column's ListItem does. Fallback
     * to the CardRow GObject which we populate during bind. */
    GtkWidget *revealer = (GtkWidget*)g_object_get_data(G_OBJECT(list_item), "detail_revealer");
    if (!revealer && crow) {
        revealer = (GtkWidget*)g_object_get_data(G_OBJECT(crow), "detail_revealer");
    }
    if (!revealer || !GTK_IS_REVEALER(revealer)) {
        g_print("  no valid revealer found for list_item=%p, row=%p\n", (void*)list_item, (void*)crow);
        return;
    }
    gboolean currently = gtk_revealer_get_reveal_child(GTK_REVEALER(revealer));
    g_print("  toggling revealer=%p currently=%d -> new=%d\n", (void*)revealer, (int)currently, (int)!currently);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), !currently);
}

// Remove button handler: deletes the underlying card (single action) and refreshes the view.
static void on_remove_button_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    if (!list_item) return;
    CardRow *row = (CardRow*)gtk_list_item_get_item(list_item);
    if (!row) return;
    // Find AppState via the column view ancestor
    GtkWidget *w = GTK_WIDGET(list_item);
    GtkWidget *column_view = gtk_widget_get_ancestor(w, GTK_TYPE_COLUMN_VIEW);
    if (!column_view) return;
    GtkRoot *root = gtk_widget_get_root(column_view);
    AppState *state = NULL;
    if (root && G_IS_OBJECT(root)) state = (AppState*)g_object_get_data(G_OBJECT(root), "app_state");
    if (!state || !state->db) return;
    // Use existing DB helper to delete the card (this will handle quantities)
    if (state->db->delete_card(row->id)) {
        std::cout << "Deleted card id=" << row->id << std::endl;
    } else {
        std::cout << "Error deleting card id=" << row->id << std::endl;
    }
    // Collapse revealer (if any) and refresh list
    GtkWidget *revealer = (GtkWidget*)g_object_get_data(G_OBJECT(list_item), "detail_revealer");
    if (revealer && GTK_IS_REVEALER(revealer)) gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), FALSE);
    refresh_card_list(state);
}

static void on_delete_card(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWindow *window = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
    if (!state || !state->db) return;
    int id = g_variant_get_int32(parameter);
    int qty;
    if (!state->db->get_card_quantity(id, qty)) {
        std::cout << "Error getting quantity for card " << id << std::endl;
        return;
    }
    bool in_deck_view = state->selected_deck_id != -1;
    if (qty == 1) {
        if (in_deck_view) {
            // Remove single copy from this deck (move back to main/merge)
            if (remove_card_from_deck(state, id, 1)) {
                std::cout << "Removed card id " << id << " from deck " << state->selected_deck_id << std::endl;
                refresh_card_list(state);
            } else {
                std::cout << "Error removing card from deck" << std::endl;
            }
        } else {
            if (state->db->delete_card(id)) {
                std::cout << "Deleted card with id " << id << std::endl;
                refresh_card_list(state);
            } else {
                std::cout << "Error deleting card" << std::endl;
            }
        }
    } else {
    // Show dialog to choose how many to delete
    GtkWidget *dialog = create_styled_dialog(window, 320, 120);
    gtk_window_set_title(GTK_WINDOW(dialog), "Elimina Carte");

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_window_set_child(GTK_WINDOW(dialog), box);

        GtkWidget *label = gtk_label_new("Quante carte eliminare?");
        gtk_box_append(GTK_BOX(box), label);

        GtkWidget *spin = gtk_spin_button_new_with_range(1, qty, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1);
        gtk_box_append(GTK_BOX(box), spin);

        GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_append(GTK_BOX(box), button_box);

        GtkWidget *ok_button = gtk_button_new_with_label("Elimina");
        GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
        gtk_box_append(GTK_BOX(button_box), ok_button);
        gtk_box_append(GTK_BOX(button_box), cancel_button);

        gtk_window_set_default_widget(GTK_WINDOW(dialog), ok_button);

        struct DeleteContext {
            AppState* state;
            int id;
            int qty;
            GtkWidget* spin;
            GtkWidget* dialog;
        };

        DeleteContext* ctx = new DeleteContext{state, id, qty, spin, dialog};

        g_signal_connect(ok_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
            DeleteContext* ctx = (DeleteContext*)user_data;
            int to_delete = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ctx->spin));
            bool in_deck_view = ctx->state->selected_deck_id != -1;
            if (in_deck_view) {
                // Remove from deck (move back to main) — helper handles full or partial
                if (remove_card_from_deck(ctx->state, ctx->id, to_delete)) {
                    std::cout << "Removed " << to_delete << " of card id " << ctx->id << " from deck " << ctx->state->selected_deck_id << std::endl;
                } else {
                    std::cout << "Error removing card from deck" << std::endl;
                }
            } else {
                if (to_delete >= ctx->qty) {
                    // Delete completely from main DB
                    if (ctx->state->db->delete_card(ctx->id)) {
                        std::cout << "Deleted card with id " << ctx->id << std::endl;
                    } else {
                        std::cout << "Error deleting card" << std::endl;
                    }
                } else {
                    // Update quantity in main DB
                    int new_qty = ctx->qty - to_delete;
                    if (ctx->state->db->update_quantity(ctx->id, new_qty)) {
                        std::cout << "Updated card id " << ctx->id << " quantity to " << new_qty << std::endl;
                    } else {
                        std::cout << "Error updating quantity" << std::endl;
                    }
                }
            }
            refresh_card_list(ctx->state);
            gtk_window_destroy(GTK_WINDOW(ctx->dialog));
            delete ctx;
        }), ctx);

        g_signal_connect(cancel_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
            DeleteContext* ctx = (DeleteContext*)user_data;
            gtk_window_destroy(GTK_WINDOW(ctx->dialog));
            delete ctx;
        }), ctx);

        gtk_window_present(GTK_WINDOW(dialog));
    }
}

// Context for deleting a deck (used by the dialog handlers)
struct DeleteDeckCtx {
    AppState* state;
    int deck_id;
    GtkWidget* dialog;
};

// Small structs and handlers used by the Add-to-Deck flow so callbacks can be static
struct AddToDeckCtx_s {
    AppState* state;
    GtkWidget* list_box;
    GtkWidget* dialog;
    int card_id;
};

struct QuantityCtx_s {
    AddToDeckCtx_s* parent;
    int deck_id;
    GtkWidget* qdialog;
    GtkWidget* spin;
};

// Called when user confirms quantity in the quantity dialog
static void on_quantity_ok_clicked(GtkButton* button, gpointer user_data) {
    QuantityCtx_s* qc = (QuantityCtx_s*)user_data;
    if (!qc || !qc->parent) { delete qc; return; }
    int to_move = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(qc->spin));
    AddToDeckCtx_s* parent = qc->parent;
    if (to_move > 0) {
        if (add_card_to_deck(parent->state, parent->card_id, to_move, qc->deck_id, 0)) {
            std::cout << "Moved " << to_move << " of card " << parent->card_id << " to deck " << qc->deck_id << std::endl;
        } else {
            std::cout << "Error moving cards to deck" << std::endl;
        }
    }
    refresh_card_list(parent->state);
    if (qc->qdialog) gtk_window_destroy(GTK_WINDOW(qc->qdialog));
    if (parent->dialog) gtk_window_destroy(GTK_WINDOW(parent->dialog));
    delete qc;
    delete parent;
}

static void on_quantity_cancel_clicked(GtkButton* button, gpointer user_data) {
    QuantityCtx_s* qc = (QuantityCtx_s*)user_data;
    if (!qc) return;
    if (qc->qdialog) gtk_window_destroy(GTK_WINDOW(qc->qdialog));
    // Do not delete parent here: keep parent dialog active so user can select another deck
    delete qc;
}

// Called when user selects a deck and presses "Aggiungi" in the deck selection dialog.
// This creates a quantity dialog capped to the available copies and wires its handlers.
static void on_add_to_deck_select_ok_clicked(GtkButton* button, gpointer user_data) {
    AddToDeckCtx_s* c = (AddToDeckCtx_s*)user_data;
    if (!c) return;
    GtkListBoxRow* sel = gtk_list_box_get_selected_row(GTK_LIST_BOX(c->list_box));
    if (!sel) return;
    char* idstr = (char*)g_object_get_data(G_OBJECT(sel), "deck_id");
    if (!idstr) return;
    int deck_id = atoi(idstr);
    int current_qty = 0;
    if (!c->state->db->get_card_quantity(c->card_id, current_qty) || current_qty <= 0) {
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", "Errore: impossibile leggere la quantità disponibile");
        gtk_alert_dialog_show(alert, GTK_WINDOW(c->dialog));
        g_object_unref(alert);
        return;
    }
    // Create quantity dialog
    GtkWidget *qdialog = create_styled_dialog(GTK_WINDOW(c->dialog), 320, 120);
    gtk_window_set_title(GTK_WINDOW(qdialog), "Quante carte aggiungere?");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_window_set_child(GTK_WINDOW(qdialog), box);
    GtkWidget *label = gtk_label_new("Seleziona la quantità da spostare:");
    gtk_box_append(GTK_BOX(box), label);
    GtkWidget *spin = gtk_spin_button_new_with_range(1, current_qty, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1);
    gtk_box_append(GTK_BOX(box), spin);
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(box), button_box);
    GtkWidget *qok = gtk_button_new_with_label("OK");
    GtkWidget *qcancel = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), qok);
    gtk_box_append(GTK_BOX(button_box), qcancel);

    QuantityCtx_s* qctx = new QuantityCtx_s{c, deck_id, qdialog, spin};
    g_signal_connect(qok, "clicked", G_CALLBACK(on_quantity_ok_clicked), qctx);
    g_signal_connect(qcancel, "clicked", G_CALLBACK(on_quantity_cancel_clicked), qctx);
    gtk_window_present(GTK_WINDOW(qdialog));
}


static void on_delete_deck_confirmed(GtkButton* button, gpointer user_data) {
    DeleteDeckCtx* ctx = (DeleteDeckCtx*)user_data;
    AppState* state = ctx->state;
    int deck_id = ctx->deck_id;
    if (!state || !state->db) {
        if (ctx->dialog) gtk_window_destroy(GTK_WINDOW(ctx->dialog));
        delete ctx;
        return;
    }
    // Move all cards from this deck back to main collection
    state->db->query("SELECT id, quantity FROM cards WHERE deck_id = ?", [&](const std::map<std::string,std::string>& row) {
        int cid = 0;
        int qty = 0;
        try { cid = std::stoi(row.at("id")); } catch(...) { cid = 0; }
        try { qty = std::stoi(row.at("quantity")); } catch(...) { qty = 0; }
        if (cid > 0 && qty > 0) {
            if (!remove_card_from_deck(state, cid, qty)) {
                std::cout << "Error moving card id " << cid << " from deck " << deck_id << std::endl;
            }
        }
    }, std::vector<std::string>{std::to_string(deck_id)});
    // Delete the deck record
    std::string delsql = "DELETE FROM decks WHERE id = " + std::to_string(deck_id);
    state->db->execute(delsql);
    // Clear selection and hide controls
    state->selected_deck_id = -1;
    if (state->deck_button) {
        gtk_widget_set_visible(state->deck_button, FALSE);
        gtk_button_set_label(GTK_BUTTON(state->deck_button), "");
    }
    if (state->deck_label) gtk_widget_set_visible(state->deck_label, FALSE);
    if (state->deck_delete_button) gtk_widget_set_visible(state->deck_delete_button, FALSE);
    if (state->db_button) gtk_widget_set_visible(state->db_button, FALSE);
    populate_deck_menu(state);
    refresh_card_list(state);
    if (ctx->dialog) gtk_window_destroy(GTK_WINDOW(ctx->dialog));
    delete ctx;
}

static void on_delete_deck_cancel(GtkButton* button, gpointer user_data) {
    DeleteDeckCtx* ctx = (DeleteDeckCtx*)user_data;
    if (ctx->dialog) gtk_window_destroy(GTK_WINDOW(ctx->dialog));
    delete ctx;
}

static void on_deck_delete_clicked(GtkButton* button, gpointer user_data) {
    GtkWindow *window = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
    if (!state || !state->db) return;
    int deck_id = state->selected_deck_id;
    if (deck_id == -1) return;
    // Confirmation dialog
    GtkWidget *dialog = create_styled_dialog(window, 360, 140);
    gtk_window_set_title(GTK_WINDOW(dialog), "Elimina Deck");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    GtkWidget *label = gtk_label_new("Sei sicuro di voler eliminare il deck? Le carte torneranno nella collezione principale.");
    gtk_box_append(GTK_BOX(box), label);
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), button_box);
    GtkWidget *ok_button = gtk_button_new_with_label("Elimina");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    DeleteDeckCtx* ctx = new DeleteDeckCtx{state, deck_id, dialog};
    g_signal_connect(ok_button, "clicked", G_CALLBACK(on_delete_deck_confirmed), ctx);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_delete_deck_cancel), ctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

// Handler per aggiungere una carta a un mazzo (action: app.add_to_deck, parameter: int card_id)
static void on_add_to_deck(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    int card_id = g_variant_get_int32(parameter);
    GtkWindow *window = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
    if (!state || !state->db) return;
    // Dialog to select deck
    GtkWidget *dialog = create_styled_dialog(window, 320, 320);
    gtk_window_set_title(GTK_WINDOW(dialog), "Aggiungi a Deck");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_append(GTK_BOX(box), scrolled);
    GtkWidget *list_box = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_box);
    // Populate decks
    state->db->query_decks([&](const std::map<std::string, std::string>& row) {
        const std::string id = row.at("id");
        const std::string name = row.at("name");
        GtkWidget *roww = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new(name.c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(roww), label);
        g_object_set_data_full(G_OBJECT(roww), "deck_id", g_strdup(id.c_str()), g_free);
        gtk_list_box_append(GTK_LIST_BOX(list_box), roww);
    });
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), button_box);
    GtkWidget *ok_button = gtk_button_new_with_label("Aggiungi");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);
    AddToDeckCtx_s* ctx = new AddToDeckCtx_s{state, list_box, dialog, card_id};
    g_signal_connect(ok_button, "clicked", G_CALLBACK(on_add_to_deck_select_ok_clicked), ctx);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        AddToDeckCtx_s* c = (AddToDeckCtx_s*)user_data;
        if (c->dialog) gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), ctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

// Handler per tornare al database principale (cancella il filtro mazzo)
static void on_clear_deck(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWindow *window = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
    if (!state) return;
    state->selected_deck_id = -1;
    // Hide deck indicator/button
    if (state->deck_button) {
        gtk_widget_set_visible(state->deck_button, FALSE);
        // clear label
        gtk_button_set_label(GTK_BUTTON(state->deck_button), "");
    }
    if (state->deck_delete_button) {
        gtk_widget_set_visible(state->deck_delete_button, FALSE);
    }
    if (state->db_button) {
        gtk_widget_set_visible(state->db_button, FALSE);
    }
    refresh_card_list(state);
}

// Forward declarations for global key handler actions
static void on_create_deck_action(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void on_select_deck_action(GSimpleAction *action, GVariant *parameter, gpointer user_data);

static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    GtkApplication *app = GTK_APPLICATION(user_data);
    // Check Ctrl+Shift
    if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK)) {
        if (keyval == GDK_KEY_d || keyval == GDK_KEY_D) {
            // Open create deck
            GtkWindow *win = gtk_application_get_active_window(app);
            on_create_deck_action(NULL, NULL, win);
            return TRUE;
        } else if (keyval == GDK_KEY_s || keyval == GDK_KEY_S) {
            // Open select deck
            GtkWindow *win = gtk_application_get_active_window(app);
            on_select_deck_action(NULL, NULL, win);
            return TRUE;
        }
    }
    return FALSE;
}

// Action handler: Crea Deck (menu)
static void on_create_deck_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWindow *parent = GTK_WINDOW(user_data);
    GtkWidget *dialog = create_styled_dialog(parent, 360, 140);
    gtk_window_set_title(GTK_WINDOW(dialog), "Crea Deck");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Nome deck");
    gtk_box_append(GTK_BOX(box), entry);
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), button_box);
    GtkWidget *ok_button = gtk_button_new_with_label("Crea");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);
    struct CDContext { GtkWidget* dialog; GtkWidget* entry; GtkWindow* parent; };
    CDContext* cctx = new CDContext{dialog, entry, parent};
    g_signal_connect(ok_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        CDContext* c = (CDContext*)user_data;
        const char* name = gtk_editable_get_text(GTK_EDITABLE(c->entry));
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(c->parent), "app_state");
        if (state && state->db && name && *name) {
            state->db->create_deck(name);
            // Refresh deck submenu
            populate_deck_menu(state);
        }
        gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), cctx);
    // Also trigger the same create behavior when pressing Enter in the entry
    g_signal_connect(entry, "activate", G_CALLBACK(+[](GtkEditable*, gpointer user_data) {
        CDContext* c = (CDContext*)user_data;
        const char* name = gtk_editable_get_text(GTK_EDITABLE(c->entry));
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(c->parent), "app_state");
        if (state && state->db && name && *name) {
            state->db->create_deck(name);
            populate_deck_menu(state);
            gtk_window_destroy(GTK_WINDOW(c->dialog));
            delete c;
        } else {
            // Force focus back to entry and hint
            gtk_entry_set_placeholder_text(GTK_ENTRY(c->entry), "Devi inserire un nome");
            gtk_widget_grab_focus(c->entry);
        }
    }), cctx);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        CDContext* c = (CDContext*)user_data;
        gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), cctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

// Show a dialog to choose language and then export the whole database
static void on_export_database_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWindow *parent = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(parent), "app_state");
    if (!state || !state->db) {
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", "Nessun database aperto");
        gtk_alert_dialog_show(alert, parent);
        g_object_unref(alert);
        return;
    }
    // Dialog: choose language
    GtkWidget *dialog = create_styled_dialog(parent, 360, 140);
    gtk_window_set_title(GTK_WINDOW(dialog), "Esporta Database");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    GtkWidget *label = gtk_label_new("Scegli lingua per l'esportazione:");
    gtk_box_append(GTK_BOX(box), label);
    GtkWidget *combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Italiano");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "English");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_append(GTK_BOX(box), combo);
    // Format selection for deck export
    GtkWidget *fmt_label = gtk_label_new("Formato file:");
    gtk_box_append(GTK_BOX(box), fmt_label);
    GtkWidget *fmt_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt_combo), "Bilingue (formattato)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt_combo), "TSV - English");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt_combo), "TSV - Italiano");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt_combo), "CSV - English");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt_combo), "CSV - Italiano");
    gtk_combo_box_set_active(GTK_COMBO_BOX(fmt_combo), 0);
    gtk_box_append(GTK_BOX(box), fmt_combo);
    
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(box), button_box);
    GtkWidget *ok_button = gtk_button_new_with_label("Esporta");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    struct ExportCtx { AppState* state; GtkWidget* combo; GtkWidget* fmt_combo; GtkWidget* dialog; };
    ExportCtx* ctx = new ExportCtx{state, combo, fmt_combo, dialog};

    g_signal_connect(ok_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        ExportCtx* c = (ExportCtx*)user_data;
        int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(c->combo));
        std::string lang = (idx == 1) ? "en" : "it";
        int fidx = gtk_combo_box_get_active(GTK_COMBO_BOX(c->fmt_combo));
        std::string format_str = "bilingual";
        switch(fidx) {
            case 1: format_str = "tsv_en"; break;
            case 2: format_str = "tsv_it"; break;
            case 3: format_str = "csv_en"; break;
            case 4: format_str = "csv_it"; break;
            default: format_str = "bilingual"; break;
        }
        bool ok = export_cards_to_txt(c->state, false, -1, lang, format_str);
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", ok ? "Esportazione completata" : "Errore durante esportazione");
        gtk_alert_dialog_show(alert, GTK_WINDOW(c->dialog));
        g_object_unref(alert);
        gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), ctx);

    g_signal_connect(cancel_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        ExportCtx* c = (ExportCtx*)user_data;
        gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), ctx);

    gtk_window_present(GTK_WINDOW(dialog));
}

// Export selected deck (uses state->selected_deck_id)
static void on_export_deck_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWindow *parent = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(parent), "app_state");
    if (!state || !state->db) {
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", "Nessun database aperto");
        gtk_alert_dialog_show(alert, parent);
        g_object_unref(alert);
        return;
    }
    if (state->selected_deck_id == -1) {
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", "Nessun deck selezionato");
        gtk_alert_dialog_show(alert, parent);
        g_object_unref(alert);
        return;
    }
    // Language selection dialog similar to database export
    GtkWidget *dialog = create_styled_dialog(parent, 360, 140);
    gtk_window_set_title(GTK_WINDOW(dialog), "Esporta Deck");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    GtkWidget *label = gtk_label_new("Scegli lingua per l'esportazione:");
    gtk_box_append(GTK_BOX(box), label);
    GtkWidget *combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Italiano");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "English");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_append(GTK_BOX(box), combo);
    // Format selection for deck export
    GtkWidget *fmt_label = gtk_label_new("Formato file:");
    gtk_box_append(GTK_BOX(box), fmt_label);
    GtkWidget *fmt_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt_combo), "Bilingue (formattato)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt_combo), "TSV - English");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt_combo), "TSV - Italiano");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt_combo), "CSV - English");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt_combo), "CSV - Italiano");
    gtk_combo_box_set_active(GTK_COMBO_BOX(fmt_combo), 0);
    gtk_box_append(GTK_BOX(box), fmt_combo);
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(box), button_box);
    GtkWidget *ok_button = gtk_button_new_with_label("Esporta");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    struct ExportDeckCtx { AppState* state; GtkWidget* combo; GtkWidget* fmt_combo; GtkWidget* dialog; int deck_id; };
    ExportDeckCtx* ctx = new ExportDeckCtx{state, combo, fmt_combo, dialog, state->selected_deck_id};

    g_signal_connect(ok_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        ExportDeckCtx* c = (ExportDeckCtx*)user_data;
        int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(c->combo));
        std::string lang = (idx == 1) ? "en" : "it";
        int fidx = gtk_combo_box_get_active(GTK_COMBO_BOX(c->fmt_combo));
        std::string format_str = "bilingual";
        switch(fidx) {
            case 1: format_str = "tsv_en"; break;
            case 2: format_str = "tsv_it"; break;
            case 3: format_str = "csv_en"; break;
            case 4: format_str = "csv_it"; break;
            default: format_str = "bilingual"; break;
        }
        bool ok = export_cards_to_txt(c->state, true, c->deck_id, lang, format_str);
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", ok ? "Esportazione completata" : "Errore durante esportazione");
        gtk_alert_dialog_show(alert, GTK_WINDOW(c->dialog));
        g_object_unref(alert);
        gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), ctx);

    g_signal_connect(cancel_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        ExportDeckCtx* c = (ExportDeckCtx*)user_data;
        gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), ctx);

    gtk_window_present(GTK_WINDOW(dialog));
}

// Action handler: open Filters dialog (View->Filtri)
static void on_filters_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWindow *parent = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(parent), "app_state");
    if (!state) return;
    GtkWidget *dialog = create_styled_dialog(parent, 420, 360);
    gtk_window_set_title(GTK_WINDOW(dialog), "Filtri");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    // Colors
    GtkWidget *colors_frame = gtk_frame_new("Colori");
    GtkWidget *colors_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_frame_set_child(GTK_FRAME(colors_frame), colors_box);
    GtkWidget *chk_w = gtk_check_button_new_with_label("Bianco (W)");
    GtkWidget *chk_u = gtk_check_button_new_with_label("Blu (U)");
    GtkWidget *chk_b = gtk_check_button_new_with_label("Nero (B)");
    GtkWidget *chk_r = gtk_check_button_new_with_label("Rosso (R)");
    GtkWidget *chk_g = gtk_check_button_new_with_label("Verde (G)");
    gtk_box_append(GTK_BOX(colors_box), chk_w);
    gtk_box_append(GTK_BOX(colors_box), chk_u);
    gtk_box_append(GTK_BOX(colors_box), chk_b);
    gtk_box_append(GTK_BOX(colors_box), chk_r);
    gtk_box_append(GTK_BOX(colors_box), chk_g);
    // Initialize from state
    if (state->filter_colors.count("W")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_w), TRUE);
    if (state->filter_colors.count("U")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_u), TRUE);
    if (state->filter_colors.count("B")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_b), TRUE);
    if (state->filter_colors.count("R")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_r), TRUE);
    if (state->filter_colors.count("G")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_g), TRUE);

    // Rarities
    GtkWidget *rar_frame = gtk_frame_new("Rarità");
    GtkWidget *rar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_frame_set_child(GTK_FRAME(rar_frame), rar_box);
    GtkWidget *chk_common = gtk_check_button_new_with_label("Comune");
    GtkWidget *chk_uncommon = gtk_check_button_new_with_label("Non Comune");
    GtkWidget *chk_rare = gtk_check_button_new_with_label("Rara");
    GtkWidget *chk_mythic = gtk_check_button_new_with_label("Mitica");
    gtk_box_append(GTK_BOX(rar_box), chk_common);
    gtk_box_append(GTK_BOX(rar_box), chk_uncommon);
    gtk_box_append(GTK_BOX(rar_box), chk_rare);
    gtk_box_append(GTK_BOX(rar_box), chk_mythic);
    if (state->filter_rarities.count("common")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_common), TRUE);
    if (state->filter_rarities.count("uncommon")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_uncommon), TRUE);
    if (state->filter_rarities.count("rare")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_rare), TRUE);
    if (state->filter_rarities.count("mythic")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_mythic), TRUE);

    // Types
    GtkWidget *type_frame = gtk_frame_new("Tipo");
    GtkWidget *type_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_frame_set_child(GTK_FRAME(type_frame), type_box);
    // We store filter keys in English internally; labels are localized via translate_type
    GtkWidget *chk_creature = gtk_check_button_new_with_label(translate_type("Creature").c_str());
    GtkWidget *chk_legendary_creature = gtk_check_button_new_with_label(translate_type("Legendary Creature").c_str());
    GtkWidget *chk_instant = gtk_check_button_new_with_label(translate_type("Instant").c_str());
    GtkWidget *chk_sorcery = gtk_check_button_new_with_label(translate_type("Sorcery").c_str());
    GtkWidget *chk_artifact = gtk_check_button_new_with_label(translate_type("Artifact").c_str());
    GtkWidget *chk_enchantment = gtk_check_button_new_with_label(translate_type("Enchantment").c_str());
    GtkWidget *chk_land = gtk_check_button_new_with_label(translate_type("Land").c_str());
    GtkWidget *chk_planeswalker = gtk_check_button_new_with_label(translate_type("Planeswalker").c_str());
    GtkWidget *chk_token = gtk_check_button_new_with_label(translate_type("Token").c_str());
    GtkWidget *chk_emblem = gtk_check_button_new_with_label(translate_type("Emblem").c_str());
    gtk_box_append(GTK_BOX(type_box), chk_creature);
    gtk_box_append(GTK_BOX(type_box), chk_legendary_creature);
    gtk_box_append(GTK_BOX(type_box), chk_instant);
    gtk_box_append(GTK_BOX(type_box), chk_sorcery);
    gtk_box_append(GTK_BOX(type_box), chk_artifact);
    gtk_box_append(GTK_BOX(type_box), chk_enchantment);
    gtk_box_append(GTK_BOX(type_box), chk_land);
    gtk_box_append(GTK_BOX(type_box), chk_planeswalker);
    gtk_box_append(GTK_BOX(type_box), chk_token);
    gtk_box_append(GTK_BOX(type_box), chk_emblem);
    // Initialize from state (filter keys are English)
    if (state->filter_types.count("Creature")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_creature), TRUE);
    if (state->filter_types.count("Legendary Creature")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_legendary_creature), TRUE);
    if (state->filter_types.count("Instant")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_instant), TRUE);
    if (state->filter_types.count("Sorcery")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_sorcery), TRUE);
    if (state->filter_types.count("Artifact")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_artifact), TRUE);
    if (state->filter_types.count("Enchantment")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_enchantment), TRUE);
    if (state->filter_types.count("Land")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_land), TRUE);
    if (state->filter_types.count("Planeswalker")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_planeswalker), TRUE);
    if (state->filter_types.count("Token")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_token), TRUE);
    if (state->filter_types.count("Emblem")) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_emblem), TRUE);

    // Custom type entry (allows adding arbitrary type substrings to filter)
    GtkWidget *custom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *custom_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(custom_entry), "Aggiungi tipo (es. 'Vehicle' o 'Creatura')");
    GtkWidget *custom_add = gtk_button_new_with_label("Aggiungi");
    gtk_box_append(GTK_BOX(custom_box), custom_entry);
    gtk_box_append(GTK_BOX(custom_box), custom_add);
    // A small label showing currently active custom types (not in the built-in list)
    GtkWidget *custom_label = gtk_label_new("");
    gtk_widget_set_halign(custom_label, GTK_ALIGN_START);
    // Pack the custom controls under the types frame
    gtk_box_append(GTK_BOX(type_box), custom_box);
    gtk_box_append(GTK_BOX(type_box), custom_label);

    // Foil selector (All / Only Foil / Only Non-Foil)
    GtkWidget *foil_frame = gtk_frame_new("Foil");
    GtkWidget *foil_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_frame_set_child(GTK_FRAME(foil_frame), foil_box);
    GtkWidget *foil_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(foil_combo), "Tutti");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(foil_combo), "Solo Foil");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(foil_combo), "Solo Non-Foil");
    gtk_box_append(GTK_BOX(foil_box), foil_combo);
    if (state->filter_foil == -1) gtk_combo_box_set_active(GTK_COMBO_BOX(foil_combo), 0);
    else if (state->filter_foil == 1) gtk_combo_box_set_active(GTK_COMBO_BOX(foil_combo), 1);
    else gtk_combo_box_set_active(GTK_COMBO_BOX(foil_combo), 2);

    // Pack frames
    gtk_box_append(GTK_BOX(vbox), colors_frame);
    gtk_box_append(GTK_BOX(vbox), rar_frame);
    // Types frame
    gtk_box_append(GTK_BOX(vbox), type_frame);
    gtk_box_append(GTK_BOX(vbox), foil_frame);

    // Deck membership filter (only show cards not in any deck)
    GtkWidget *deckless_frame = gtk_frame_new(translate("Non in alcun mazzo").c_str());
    GtkWidget *deckless_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_frame_set_child(GTK_FRAME(deckless_frame), deckless_box);
    GtkWidget *chk_not_in_deck = gtk_check_button_new_with_label(translate("Non in alcun mazzo").c_str());
    gtk_box_append(GTK_BOX(deckless_box), chk_not_in_deck);
    if (state->filter_no_deck) gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_not_in_deck), TRUE);
    gtk_box_append(GTK_BOX(vbox), deckless_frame);

    // Buttons
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(vbox), button_box);
    GtkWidget *ok_button = gtk_button_new_with_label("Applica");
    GtkWidget *clear_button = gtk_button_new_with_label("Azzera");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), clear_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    struct FiltersCtx {
        AppState* state;
        GtkWidget* dialog;
        GtkWidget* chk_w; GtkWidget* chk_u; GtkWidget* chk_b; GtkWidget* chk_r; GtkWidget* chk_g;
        GtkWidget* chk_common; GtkWidget* chk_uncommon; GtkWidget* chk_rare; GtkWidget* chk_mythic;
        // Type checkbuttons
        GtkWidget* chk_creature; GtkWidget* chk_legendary_creature; GtkWidget* chk_instant; GtkWidget* chk_sorcery; GtkWidget* chk_artifact; GtkWidget* chk_enchantment; GtkWidget* chk_land; GtkWidget* chk_planeswalker; GtkWidget* chk_token; GtkWidget* chk_emblem;
        GtkWidget* foil_combo;
        GtkWidget* chk_not_in_deck;
        // custom type widgets
        GtkWidget* custom_entry;
        GtkWidget* custom_label;
    };
    FiltersCtx* ctx = new FiltersCtx{state, dialog, chk_w, chk_u, chk_b, chk_r, chk_g, chk_common, chk_uncommon, chk_rare, chk_mythic, chk_creature, chk_legendary_creature, chk_instant, chk_sorcery, chk_artifact, chk_enchantment, chk_land, chk_planeswalker, chk_token, chk_emblem, foil_combo, chk_not_in_deck, custom_entry, custom_label};
    g_signal_connect(ok_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        FiltersCtx* c = (FiltersCtx*)user_data;
        AppState* st = c->state;
        // Colors
        st->filter_colors.clear();
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_w))) st->filter_colors.insert("W");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_u))) st->filter_colors.insert("U");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_b))) st->filter_colors.insert("B");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_r))) st->filter_colors.insert("R");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_g))) st->filter_colors.insert("G");
        // Rarities (use raw keys)
        st->filter_rarities.clear();
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_common))) st->filter_rarities.insert("common");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_uncommon))) st->filter_rarities.insert("uncommon");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_rare))) st->filter_rarities.insert("rare");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_mythic))) st->filter_rarities.insert("mythic");
        // Types (store English keys)
        st->filter_types.clear();
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_creature))) st->filter_types.insert("Creature");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_legendary_creature))) st->filter_types.insert("Legendary Creature");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_instant))) st->filter_types.insert("Instant");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_sorcery))) st->filter_types.insert("Sorcery");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_artifact))) st->filter_types.insert("Artifact");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_enchantment))) st->filter_types.insert("Enchantment");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_land))) st->filter_types.insert("Land");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_planeswalker))) st->filter_types.insert("Planeswalker");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_token))) st->filter_types.insert("Token");
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_emblem))) st->filter_types.insert("Emblem");
    // Foil
    int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(c->foil_combo));
    if (idx == 0) st->filter_foil = -1;
    else if (idx == 1) st->filter_foil = 1;
    else st->filter_foil = 0;
        // Deckless filter
        gboolean no_deck_active = gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_not_in_deck));
        st->filter_no_deck = no_deck_active ? true : false;
        // Also accept any custom type text in the entry as an additional filter
        if (c->custom_entry) {
            const char* txt = gtk_editable_get_text(GTK_EDITABLE(c->custom_entry));
            if (txt && *txt) {
                std::string s = txt;
                // Trim
                while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
                while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
                if (!s.empty()) {
                    // If user typed localized name (Italian), try to find English key
                    std::string key = s;
                    if (get_current_language() == "it") {
                        std::string eng = english_for_localized_type(s);
                        if (!eng.empty()) key = eng;
                    }
                    st->filter_types.insert(key);
                }
            }
        }
        refresh_card_list(st);
        if (c->dialog) gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), ctx);

    g_signal_connect(clear_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        FiltersCtx* c = (FiltersCtx*)user_data;
        AppState* st = c->state;
        st->filter_colors.clear();
        st->filter_rarities.clear();
        st->filter_types.clear();
        st->filter_foil = -1;
        st->filter_no_deck = false;
        // clear custom label
        if (c->custom_label) gtk_label_set_text(GTK_LABEL(c->custom_label), "");
        refresh_card_list(st);
        if (c->dialog) gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), ctx);

    g_signal_connect(cancel_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        FiltersCtx* c = (FiltersCtx*)user_data;
        if (c->dialog) gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), ctx);

    // Wire the Add button: insert custom type immediately and update label
    g_signal_connect(custom_add, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data){
        FiltersCtx* c = (FiltersCtx*)user_data;
        if (!c) return;
        const char* txt = gtk_editable_get_text(GTK_EDITABLE(c->custom_entry));
        if (!txt || !*txt) return;
        std::string s = txt;
        // Trim
        while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
        while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
        if (s.empty()) return;
        std::string key = s;
        if (get_current_language() == "it") {
            std::string eng = english_for_localized_type(s);
            if (!eng.empty()) key = eng;
        }
        c->state->filter_types.insert(key);
        // Update custom label to show active custom types
        std::string list;
        for (const auto &t : c->state->filter_types) {
            // skip built-ins
            if (type_translations.count(t)) continue;
            if (!list.empty()) list += ", ";
            list += t;
        }
        if (list.empty()) gtk_label_set_text(GTK_LABEL(c->custom_label), ""); else gtk_label_set_text(GTK_LABEL(c->custom_label), list.c_str());
        // Clear entry and refresh view
        gtk_editable_set_text(GTK_EDITABLE(c->custom_entry), "");
        refresh_card_list(c->state);
    }), ctx);

    gtk_window_present(GTK_WINDOW(dialog));
}

// Action handler: Seleziona Deck (menu)
static void on_select_deck_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWindow *parent = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(parent), "app_state");
    if (!state || !state->db) return;
    GtkWidget *dialog = create_styled_dialog(parent, 360, 320);
    gtk_window_set_title(GTK_WINDOW(dialog), "Seleziona Deck");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_append(GTK_BOX(box), scrolled);
    GtkWidget *list_box = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_box);
    // Populate decks from DB
    state->db->query_decks([&](const std::map<std::string, std::string>& row) {
        const std::string id = row.at("id");
        const std::string name = row.at("name");
        GtkWidget *roww = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new(name.c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(roww), label);
        g_object_set_data_full(G_OBJECT(roww), "deck_id", g_strdup(id.c_str()), g_free);
        g_object_set_data_full(G_OBJECT(roww), "deck_name", g_strdup(name.c_str()), g_free);
        gtk_list_box_append(GTK_LIST_BOX(list_box), roww);
    });
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), button_box);
    GtkWidget *ok_button = gtk_button_new_with_label("Seleziona");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);
    struct SDContext { AppState* state; GtkWidget* list_box; GtkWidget* dialog; };
    SDContext* sctx = new SDContext{state, list_box, dialog};
    g_signal_connect(ok_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        SDContext* c = (SDContext*)user_data;
        GtkListBoxRow* selected = gtk_list_box_get_selected_row(GTK_LIST_BOX(c->list_box));
        if (selected) {
            char* idstr = (char*)g_object_get_data(G_OBJECT(selected), "deck_id");
            char* namestr = (char*)g_object_get_data(G_OBJECT(selected), "deck_name");
            if (idstr) {
                int id = atoi(idstr);
                c->state->selected_deck_id = id;
                // Update visible deck button with name
                if (c->state->deck_button) {
                    gtk_button_set_label(GTK_BUTTON(c->state->deck_button), namestr ? namestr : "");
                    gtk_widget_set_visible(c->state->deck_button, TRUE);
                }
                if (c->state->deck_delete_button) {
                    gtk_widget_set_visible(c->state->deck_delete_button, TRUE);
                }
                if (c->state->db_button) {
                    gtk_widget_set_visible(c->state->db_button, TRUE);
                }
                if (c->state->deck_label) {
                    std::string lbl = std::string("Filtrando: ") + (namestr ? namestr : "");
                    gtk_label_set_text(GTK_LABEL(c->state->deck_label), lbl.c_str());
                    gtk_widget_set_visible(c->state->deck_label, TRUE);
                }
                // Refresh list with deck filter
                refresh_card_list(c->state);
            }
        }
        gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), sctx);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        SDContext* c = (SDContext*)user_data;
        gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), sctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

static GdkRGBA get_color_for_mana(const char* colors) {
    // Parse colors and compute average RGB
    if (!colors || strlen(colors) == 0) {
        // Colorless: gray
        return {0.5, 0.5, 0.5, 1.0};
    }
    std::vector<GdkRGBA> color_list;
    for (char c : std::string(colors)) {
        if (c == 'W') color_list.push_back({1.0, 0.95, 0.8, 1.0}); // White
        else if (c == 'U') color_list.push_back({0.2, 0.4, 0.8, 1.0}); // Blue
        else if (c == 'B') color_list.push_back({0.2, 0.1, 0.2, 1.0}); // Black
        else if (c == 'R') color_list.push_back({0.8, 0.2, 0.1, 1.0}); // Red
        else if (c == 'G') color_list.push_back({0.1, 0.6, 0.2, 1.0}); // Green
    }
    if (color_list.empty()) {
        return {0.5, 0.5, 0.5, 1.0};
    }
    double r = 0, g = 0, b = 0;
    for (auto& col : color_list) {
        r += col.red;
        g += col.green;
        b += col.blue;
    }
    r /= color_list.size();
    g /= color_list.size();
    b /= color_list.size();
    return {(float)r, (float)g, (float)b, 1.0f};
}

static void on_add_card_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    GtkWindow *window = GTK_WINDOW(user_data);
    if (!window) return;
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
    // If we're in the main DB view (no deck selected) and the inline controls
    // exist, focus the inline add entry instead of opening the modal dialog.
    if (state && state->selected_deck_id == -1 && state->inline_add_entry && GTK_IS_WIDGET(state->inline_add_entry)) {
        // Try an immediate grab and schedule reliable retries to overcome WM focus policies
        gtk_widget_grab_focus(state->inline_add_entry);
        if (GTK_IS_EDITABLE(state->inline_add_entry)) gtk_editable_select_region(GTK_EDITABLE(state->inline_add_entry), 0, -1);
        // Per user request: perform a single focus attempt only (do not schedule retries).
        // This avoids repeated notifications and makes Ctrl+N perform one focused attempt.
        return;
    }
    // Fallback: open the legacy add dialog (used when viewing a deck)
    on_add_card_clicked(NULL, user_data);
}

static std::string translate_type(const char* type) {
    if (!type) return "";
    std::string t = type;
    if (current_language == "en") return t;
    auto it = type_translations.find(t);
    if (it != type_translations.end()) return it->second;
    return t; // Se non trovato, restituisci originale
}

/* PicCtx used to pass picture + raw data to the main-loop invoker */
struct PicCtx { GtkWidget* pic; std::vector<unsigned char>* data; };
/* Text update context used to set oracle text on the main thread */
struct TextCtx { GtkWidget* lbl; char* text; };
static gboolean on_picture_update_invoke(gpointer user_data);
static gboolean on_oracle_update_invoke(gpointer user_data);

/* Named callbacks to avoid passing inline lambdas through G_CALLBACK (preprocessor-safe) */
static void name_factory_setup_cb(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data) {
    (void)factory; (void)user_data;
    /* Structure per list item:
     * outer_vbox
     *  ├ visible_box (contains name_label, meta_label)  <-- always visible and receives gestures
     *  └ revealer (contains detail panel with Remove button)
     */
    GtkWidget *outer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(outer_vbox, "db-row");

    GtkWidget *visible_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *name_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(name_label, "name-label");

    GtkWidget *meta_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(meta_label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(meta_label, "meta-label");

    gtk_box_append(GTK_BOX(visible_box), name_label);
    gtk_box_append(GTK_BOX(visible_box), meta_label);

    /* Also attach double-click gestures to the two visible labels inside the
     * name column so clicking anywhere on the row (including the name area)
     * toggles the tendina. We attach them to the labels to avoid colliding
     * with other per-column controllers. */
    GtkGesture *name_dbl = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(name_dbl), GDK_BUTTON_PRIMARY);
    g_signal_connect(name_dbl, "pressed", G_CALLBACK(on_row_double_click), item);
    if (name_label) gtk_widget_add_controller(name_label, GTK_EVENT_CONTROLLER(name_dbl));
    if (meta_label) gtk_widget_add_controller(meta_label, GTK_EVENT_CONTROLLER(name_dbl));

    /* Double-click gesture (primary button) used to toggle the revealer is
     * attached to each column's cell widgets instead of the name column's
     * visible_box to avoid duplicate invocations when multiple controllers
     * are present. (Per-column controllers are created in the column
     * factories' setup callbacks.) */

    /* Right-click gesture for context menu attached to visible_box as well */
    GtkGesture *rclk = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclk), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclk, "pressed", G_CALLBACK(on_row_right_click), item);
    gtk_widget_add_controller(visible_box, GTK_EVENT_CONTROLLER(rclk));

    /* The detail revealer (collapsed by default) placed below the visible row */
    GtkWidget *revealer = gtk_revealer_new();
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), FALSE);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 260);
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);

    GtkWidget *detail_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(detail_box, "detail-panel");
    gtk_widget_set_margin_start(detail_box, 8);
    gtk_widget_set_margin_end(detail_box, 8);
    gtk_widget_set_margin_top(detail_box, 6);
    gtk_widget_set_margin_bottom(detail_box, 6);

    /* Left: picture area */
    GtkWidget *picture = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_size_request(picture, 200, 280); // approximate card aspect
    gtk_widget_set_hexpand(picture, FALSE);
    gtk_widget_set_vexpand(picture, FALSE);
    gtk_box_append(GTK_BOX(detail_box), picture);
    /* Small Remove button just to the right of the image */
    GtkWidget *remove_btn = gtk_button_new_with_label(translate("Rimuovi").c_str());
    gtk_widget_add_css_class(remove_btn, "danger-button");
    gtk_widget_add_css_class(remove_btn, "compact-button");
    gtk_widget_set_size_request(remove_btn, 92, -1);
    gtk_widget_set_valign(remove_btn, GTK_ALIGN_START);
    gtk_widget_set_halign(remove_btn, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(detail_box), remove_btn);

    /* Middle: info vbox */
    GtkWidget *info_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(info_vbox, TRUE);
    gtk_widget_set_vexpand(info_vbox, FALSE);
    GtkWidget *title_label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(title_label), 0.0); gtk_widget_add_css_class(title_label, "detail-title");
    GtkWidget *type_label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(type_label), 0.0); gtk_widget_add_css_class(type_label, "detail-meta");
    GtkWidget *colors_label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(colors_label), 0.0); gtk_widget_add_css_class(colors_label, "detail-meta");
    GtkWidget *rarity_label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(rarity_label), 0.0); gtk_widget_add_css_class(rarity_label, "detail-meta");
    GtkWidget *set_label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(set_label), 0.0); gtk_widget_add_css_class(set_label, "detail-meta");
    GtkWidget *mana_label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(mana_label), 0.0); gtk_widget_add_css_class(mana_label, "detail-meta");
    /* Oracle / rules text (wrapping, may be long) */
    GtkWidget *oracle_label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(oracle_label), 0.0); gtk_widget_add_css_class(oracle_label, "detail-text"); gtk_label_set_wrap(GTK_LABEL(oracle_label), TRUE);
    GtkWidget *qty_label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(qty_label), 0.0); gtk_widget_add_css_class(qty_label, "detail-meta");
    GtkWidget *deck_label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(deck_label), 0.0); gtk_widget_add_css_class(deck_label, "detail-meta");
    gtk_box_append(GTK_BOX(info_vbox), title_label);
    gtk_box_append(GTK_BOX(info_vbox), type_label);
    gtk_box_append(GTK_BOX(info_vbox), colors_label);
    gtk_box_append(GTK_BOX(info_vbox), rarity_label);
    gtk_box_append(GTK_BOX(info_vbox), set_label);
    gtk_box_append(GTK_BOX(info_vbox), mana_label);
    gtk_box_append(GTK_BOX(info_vbox), oracle_label);
    gtk_box_append(GTK_BOX(info_vbox), qty_label);
    gtk_box_append(GTK_BOX(info_vbox), deck_label);
    gtk_box_append(GTK_BOX(detail_box), info_vbox);

    gtk_revealer_set_child(GTK_REVEALER(revealer), detail_box);

    /* Make outer structure */
    gtk_box_append(GTK_BOX(outer_vbox), visible_box);
    gtk_box_append(GTK_BOX(outer_vbox), revealer);

    /* Keep convenient references on the ListItem for handlers */
    g_object_set_data(G_OBJECT(item), "detail_revealer", revealer);
    /* Also store the revealer on the underlying CardRow (set during bind) so
     * double-click handlers attached to other column ListItems can locate it. */
    /* Note: we set this in bind when we have access to the CardRow instance. */
    g_object_set_data(G_OBJECT(item), "visible_box", visible_box);
    g_object_set_data(G_OBJECT(item), "remove_btn", remove_btn);

    /* Wire remove handler to the ListItem (user_data = item) */
    g_signal_connect(remove_btn, "clicked", G_CALLBACK(on_remove_button_clicked), item);

    /* Watch revealer open/close so we can mark per-item tendina state and
     * also store the flag on the shared CardRow object. Storing on the
     * CardRow makes the state available to bind callbacks for other column
     * ListItem instances. We still set it on the ListItem for the current
     * widget instance. */
    g_signal_connect(revealer, "notify::reveal-child", G_CALLBACK(+[](GObject *obj, GParamSpec*, gpointer user_data){
        GtkListItem *item = GTK_LIST_ITEM(user_data);
        GtkRevealer *r = GTK_REVEALER(obj);
        gboolean reveal = gtk_revealer_get_reveal_child(r);
        g_object_set_data(G_OBJECT(item), "tendina_open", GINT_TO_POINTER(reveal ? 1 : 0));
        /* Also set on the CardRow (shared model) so other column ListItems
         * can consult the row-level flag when deciding visibility. */
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        if (row) {
            g_object_set_data(G_OBJECT(row), "tendina_open", GINT_TO_POINTER(reveal ? 1 : 0));
        }
    }), item);

    gtk_list_item_set_child(item, outer_vbox);
}

static void name_factory_bind_cb(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data) {
    (void)factory; (void)user_data;
    CardRow *row = (CardRow*)gtk_list_item_get_item(item);
    GtkWidget *outer = gtk_list_item_get_child(item);
    if (!outer) return;
    /* visible_box is first child of outer_vbox */
    GtkWidget *visible_box = gtk_widget_get_first_child(outer);
    if (!visible_box) return;
    GtkWidget *name_label = gtk_widget_get_first_child(visible_box);
    GtkWidget *meta_label = name_label ? gtk_widget_get_next_sibling(name_label) : NULL;
    /* Detail widgets: picture + info labels are children of the detail_box (second child of outer_vbox) */
    GtkWidget *revealer = gtk_widget_get_next_sibling(visible_box);
    GtkWidget *detail_box = revealer ? gtk_revealer_get_child(GTK_REVEALER(revealer)) : NULL;
    GtkWidget *picture = NULL, *info_vbox = NULL, *title_label = NULL, *type_label = NULL, *colors_label = NULL, *rarity_label = NULL, *set_label = NULL, *mana_label = NULL, *oracle_label = NULL, *qty_label = NULL, *deck_label = NULL, *remove_btn = NULL;
    if (detail_box) {
        /* detail_box children: picture, remove_btn, info_vbox */
        picture = gtk_widget_get_first_child(detail_box);
        remove_btn = picture ? gtk_widget_get_next_sibling(picture) : NULL;
        info_vbox = remove_btn ? gtk_widget_get_next_sibling(remove_btn) : NULL;
        if (info_vbox) {
            title_label = gtk_widget_get_first_child(info_vbox);
            type_label = title_label ? gtk_widget_get_next_sibling(title_label) : NULL;
            colors_label = type_label ? gtk_widget_get_next_sibling(type_label) : NULL;
            rarity_label = colors_label ? gtk_widget_get_next_sibling(colors_label) : NULL;
            set_label = rarity_label ? gtk_widget_get_next_sibling(rarity_label) : NULL;
            mana_label = set_label ? gtk_widget_get_next_sibling(set_label) : NULL;
            oracle_label = mana_label ? gtk_widget_get_next_sibling(mana_label) : NULL;
            qty_label = oracle_label ? gtk_widget_get_next_sibling(oracle_label) : NULL;
            deck_label = qty_label ? gtk_widget_get_next_sibling(qty_label) : NULL;
            /* remove_btn already is the middle child (picture -> remove_btn -> info_vbox) */
        }
    }
    if (!row) {
        if (name_label) gtk_label_set_text(GTK_LABEL(name_label), "");
        if (meta_label) gtk_label_set_text(GTK_LABEL(meta_label), "");
        gtk_list_item_set_selectable(item, FALSE);
        return;
    }
    if (row->id == ROW_ID_SEPARATOR_TITLE) {
        std::string sep = translate("Sideboard");
        std::string markup = "<span weight='bold'>" + sep + "</span>";
        if (name_label) gtk_label_set_markup(GTK_LABEL(name_label), markup.c_str());
        if (meta_label) gtk_widget_set_visible(meta_label, FALSE);
        gtk_label_set_xalign(GTK_LABEL(name_label), 0.5);
        gtk_widget_set_hexpand(name_label, TRUE);
        gtk_list_item_set_selectable(item, FALSE);
        gtk_widget_add_css_class(outer, "separator-row");
        ensure_separator_css_provider();
        if (separator_css_provider) {
            gtk_style_context_add_provider(gtk_widget_get_style_context(outer), GTK_STYLE_PROVIDER(separator_css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        }
        gtk_widget_remove_css_class(GTK_WIDGET(name_label), "foil");
        return;
    }
    if (row->id == ROW_ID_HEADER) {
        std::string hdr = translate("Nome");
        std::string markup = "<span weight='bold'>" + hdr + "</span>";
        if (name_label) gtk_label_set_markup(GTK_LABEL(name_label), markup.c_str());
        if (meta_label) gtk_widget_set_visible(meta_label, FALSE);
        gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);
        gtk_list_item_set_selectable(item, FALSE);
        gtk_widget_add_css_class(outer, "header-row");
        ensure_separator_css_provider();
        if (separator_css_provider) {
            gtk_style_context_add_provider(gtk_widget_get_style_context(outer), GTK_STYLE_PROVIDER(separator_css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        }
        return;
    }
    // Normal row: populate name and meta (type + set)
    if (name_label) gtk_label_set_text(GTK_LABEL(name_label), row->name ? row->name : "");
    std::string meta;
    if (row->type && row->set_code) {
        meta = std::string(row->type) + " • " + (row->set_code ? row->set_code : "");
    } else if (row->type) meta = row->type;
    if (meta_label) gtk_label_set_text(GTK_LABEL(meta_label), meta.c_str());
    /* Remove any visual-only classes that may have been applied when this ListItem was reused */
    gtk_widget_remove_css_class(outer, "separator-row");
    gtk_widget_remove_css_class(outer, "header-row");
    // Apply foil styling
    if (row->foil) {
        if (name_label) gtk_widget_add_css_class(name_label, "foil");
    } else {
        if (name_label) gtk_widget_remove_css_class(name_label, "foil");
    }
    // Populate detail panel (if present)
    if (title_label) gtk_label_set_text(GTK_LABEL(title_label), row->name ? row->name : "");
    if (type_label) gtk_label_set_text(GTK_LABEL(type_label), row->type ? row->type : "");
    if (colors_label) gtk_label_set_text(GTK_LABEL(colors_label), row->translated_colors ? row->translated_colors : "");
    if (rarity_label) gtk_label_set_text(GTK_LABEL(rarity_label), translate_rarity(row->rarity).c_str());
    if (set_label) gtk_label_set_text(GTK_LABEL(set_label), row->set_code ? row->set_code : "");
    if (mana_label) gtk_label_set_text(GTK_LABEL(mana_label), row->mana_cost ? row->mana_cost : "");
    if (qty_label) gtk_label_set_text(GTK_LABEL(qty_label), row->quantity_display ? row->quantity_display : std::to_string(row->quantity).c_str());

    /* Ensure the CardRow object stores a reference to the detail revealer so
     * gestures coming from other column ListItem objects can find and toggle it.
     * We store a plain pointer; lifetime of the revealer is tied to the ListItem
     * UI so this is safe while the row is visible. */
    if (row && revealer) {
        g_object_set_data(G_OBJECT(row), "detail_revealer", revealer);
    }

    /* Deck membership: query DB for decks that contain cards with same name */
    if (deck_label) {
        gtk_label_set_text(GTK_LABEL(deck_label), translate("Non in alcun mazzo").c_str());
        /* Try to find the AppState to access DB */
        GtkWidget *colv = gtk_widget_get_ancestor(outer, GTK_TYPE_COLUMN_VIEW);
        if (colv) {
            GtkRoot *root = gtk_widget_get_root(colv);
            if (root && G_IS_OBJECT(root)) {
                AppState *state = (AppState*)g_object_get_data(G_OBJECT(root), "app_state");
                if (state && state->db) {
                    std::vector<std::string> params;
                    params.push_back(std::string(row->name ? row->name : ""));
                    std::vector<std::string> deck_names;
                    state->db->query("SELECT DISTINCT d.name AS deck_name FROM decks d JOIN cards c ON c.deck_id = d.id WHERE LOWER(COALESCE(c.english_name,c.localized_name,c.name)) = LOWER(?)", [&](const std::map<std::string,std::string>& r){
                        if (r.count("deck_name")) deck_names.push_back(r.at("deck_name"));
                    }, params);
                    if (!deck_names.empty()) {
                        std::string joined;
                        for (size_t i=0;i<deck_names.size();++i) {
                            if (i) joined += ", ";
                            joined += deck_names[i];
                        }
                        gtk_label_set_text(GTK_LABEL(deck_label), joined.c_str());
                        /* store deck names on the ListItem so other column binds can hide themselves */
                        char* old = (char*)g_object_get_data(G_OBJECT(item), "in_deck_names");
                        if (old) g_free(old);
                        g_object_set_data(G_OBJECT(item), "in_deck_names", g_strdup(joined.c_str()));
                    }
                    else {
                        char* old = (char*)g_object_get_data(G_OBJECT(item), "in_deck_names");
                        if (old) { g_free(old); g_object_set_data(G_OBJECT(item), "in_deck_names", NULL); }
                    }
                }
            }
        }
    }

    /* Load picture (async if needed) */
    if (picture && row->image_url && strlen(row->image_url)>0) {
        std::string url = row->image_url;
        // If cached file exists, load synchronously; otherwise spawn async loader
        std::filesystem::path up(url);
        std::string filename = up.filename().string();
        std::string filepath = std::string("data/img/") + filename;
        if (std::filesystem::exists(filepath)) {
            std::vector<unsigned char> data = load_image_from_file(filepath);
            if (!data.empty()) {
                GBytes *bytes = g_bytes_new(data.data(), data.size());
                GdkTexture *texture = gdk_texture_new_from_bytes(bytes, nullptr);
                if (texture) {
                    gtk_picture_set_paintable(GTK_PICTURE(picture), GDK_PAINTABLE(texture));
                    g_object_unref(texture);
                }
                g_bytes_unref(bytes);
            }
        } else {
            // spawn thread to download and update picture when ready
            std::string u = url;
            GtkWidget *pic = picture;
            std::thread([u, pic]() {
                auto data = download_image_data(u);
                if (data.empty()) return;
                // save into cache
                try {
                    std::filesystem::path upath(u);
                    std::string fname = upath.filename().string();
                    std::string fpath = std::string("data/img/") + fname;
                    std::ofstream out(fpath, std::ios::binary);
                    if (out) { out.write((char*)data.data(), data.size()); out.close(); }
                } catch(...) {}
                // create texture on main thread
                PicCtx* pctx = new PicCtx();
                pctx->pic = pic;
                pctx->data = new std::vector<unsigned char>(data.begin(), data.end());
                g_main_context_invoke(NULL, (GSourceFunc)on_picture_update_invoke, pctx);
            }).detach();
        }
    }
}

static gboolean on_picture_update_invoke(gpointer user_data) {
    PicCtx* ctx = (PicCtx*)user_data;
    if (!ctx) return G_SOURCE_REMOVE;
    GBytes *bytes = g_bytes_new(ctx->data->data(), ctx->data->size());
    GdkTexture *texture = gdk_texture_new_from_bytes(bytes, nullptr);
    if (texture) {
        gtk_picture_set_paintable(GTK_PICTURE(ctx->pic), GDK_PAINTABLE(texture));
        g_object_unref(texture);
    }
    g_bytes_unref(bytes);
    delete ctx->data;
    delete ctx;
    return G_SOURCE_REMOVE;
}

static gboolean on_oracle_update_invoke(gpointer user_data) {
    TextCtx* tc = (TextCtx*)user_data;
    if (!tc) return G_SOURCE_REMOVE;
    if (tc->lbl) {
        gtk_label_set_text(GTK_LABEL(tc->lbl), tc->text ? tc->text : "");
    }
    if (tc) { g_free(tc->text); delete tc; }
    return G_SOURCE_REMOVE;
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    // Carica CSS personalizzato
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "@define-color bg_base #0f121a;\n"
        "@define-color bg_surface #151b26;\n"
        "@define-color bg_panel #1c2432;\n"
        "@define-color accent_primary #8a6cff;\n"
        "@define-color accent_secondary #f56fbb;\n"
        "@define-color accent_tertiary #3dc9ff;\n"
        "@define-color text_primary #ecf0ff;\n"
        "@define-color text_muted #9aa7bd;\n"
        "@define-color outline_color rgba(138,108,255,0.45);\n"
        ".small-popover { padding: 0; margin: 0; border: 1px solid rgba(255,255,255,0.08); background-color: @bg_panel; color: @text_primary; }\n"
        "window {\n"
        "    background-color: @bg_base;\n"
        "    background-image: radial-gradient(circle at 12% 20%, rgba(138,108,255,0.22), transparent 55%),\n"
        "                      radial-gradient(circle at 88% 10%, rgba(61,201,255,0.12), transparent 50%),\n"
        "                      radial-gradient(circle at 50% 100%, rgba(245,111,187,0.10), transparent 60%);\n"
        "    color: @text_primary;\n"
        "    font-family: 'Inter', 'Segoe UI', 'Ubuntu', sans-serif;\n"
        "    font-size: 13px;\n"
        "}\n"
        "headerbar,\n"
        ".topbar {\n"
        "    padding: 1px 6px;\n"
        "    min-height: 24px;\n"
        "    background-image: linear-gradient(180deg, rgba(28,36,50,0.95), rgba(19,25,36,0.92));\n"
        "    border-bottom: 1px solid rgba(255,255,255,0.06);\n"
        "    box-shadow: 0 1px 0 rgba(255,255,255,0.04);\n"
        "}\n"
        "headerbar button,\n"
        "headerbar menubutton,\n"
        "button,\n"
        "menubutton {\n"
        "    border-radius: 6px;\n"
        "    padding: 1px 6px;\n"
        "    background-color: rgba(21,27,38,0.8);\n"
        "    color: @text_primary;\n"
        "    border: 1px solid rgba(255,255,255,0.06);\n"
        "    transition: all 0.14s ease;\n"
        "}\n"
        "button:hover,\n"
        "menubutton:hover {\n"
        "    background-color: rgba(138,108,255,0.18);\n"
        "    border-color: outline_color;\n"
        "    box-shadow: 0 3px 10px rgba(138,108,255,0.25);\n"
        "}\n"
        "button:active,\n"
        "menubutton:active {\n"
        "    background-color: rgba(138,108,255,0.28);\n"
        "    border-color: rgba(138,108,255,0.55);\n"
        "    box-shadow: inset 0 0 0 1px rgba(0,0,0,0.4);\n"
        "}\n"
        ".accent-button {\n"
        "    background-image: linear-gradient(135deg, rgba(138,108,255,0.85), rgba(245,111,187,0.85));\n"
        "    border-color: rgba(138,108,255,0.55);\n"
        "}\n"
        ".accent-button:hover {\n"
        "    background-image: linear-gradient(135deg, rgba(148,118,255,0.95), rgba(247,125,197,0.95));\n"
        "    box-shadow: 0 6px 18px rgba(138,108,255,0.45);\n"
        "}\n"
        ".ghost-button {\n"
        "    background-color: transparent;\n"
        "    border-color: rgba(255,255,255,0.08);\n"
        "}\n"
        ".ghost-button:hover {\n"
        "    background-color: rgba(255,255,255,0.05);\n"
        "}\n"
        ".danger-button {\n"
        "    color: #ff8097;\n"
        "    border-color: rgba(255,128,151,0.35);\n"
        "}\n"
        ".danger-button:hover {\n"
        "    background-color: rgba(255,64,115,0.18);\n"
        "    border-color: rgba(255,128,151,0.6);\n"
        "}\n"
    ".compact-button {\n"
    "    padding: 2px 10px;\n"
    "    min-height: 22px;\n"
    "    font-size: 12px;\n"
    "}\n"
    ".compact-button:hover {\n"
    "    background-color: rgba(255,255,255,0.06);\n"
    "}\n"
        "headerbar entry,\n"
        "entry,\n"
        "spinbutton {\n"
        "    border-radius: 6px;\n"
        "    padding: 1px 6px;\n"
        "    background-color: rgba(10,14,22,0.95);\n"
        "    color: @text_primary;\n"
        "    border: 1px solid rgba(255,255,255,0.08);\n"
        "    transition: all 0.16s ease;\n"
        "}\n"
        "entry:hover,\n"
        "spinbutton:hover {\n"
        "    border-color: outline_color;\n"
        "    background-color: rgba(28,36,50,0.95);\n"
        "}\n"
        "entry:focus,\n"
        "spinbutton:focus {\n"
        "    outline: none;\n"
        "    border-color: rgba(138,108,255,0.75);\n"
        "    box-shadow: 0 0 0 3px rgba(138,108,255,0.25);\n"
        "}\n"
        ".inline-field {\n"
        "    background-color: rgba(18,23,34,0.95);\n"
        "}\n"
        ".inline-toggle {\n"
        "    padding: 2px 8px;\n"
        "}\n"
        ".search-field {\n"
        "    min-width: 220px;\n"
        "    background-color: rgba(18,24,36,0.92);\n"
        "}\n"
        ".search-field:focus {\n"
        "    border-color: rgba(61,201,255,0.65);\n"
        "    box-shadow: 0 0 0 3px rgba(61,201,255,0.18);\n"
        "}\n"
        "columnview {\n"
        "    background-color: @bg_surface;\n"
        "    border: 1px solid rgba(255,255,255,0.05);\n"
        "    border-radius: 10px;\n"
        "    color: @text_primary;\n"
        "}\n"
        "columnview row {\n"
        "    padding: 8px 12px;\n"
        "    background-color: transparent;\n"
        "}\n"
        "columnview row:nth-child(even) {\n"
        "    background-color: rgba(21,27,38,0.4);\n"
        "}\n"
        "columnview row:hover {\n"
        "    background-color: rgba(138,108,255,0.18);\n"
        "}\n"
        "columnview row:selected,\n"
        "columnview row:selected:hover {\n"
        "    background-image: linear-gradient(135deg, rgba(138,108,255,0.4), rgba(61,201,255,0.28));\n"
        "    border-radius: 8px;\n"
        "    color: @text_primary;\n"
        "}\n"
        "columnview row:selected label {\n"
        "    color: @text_primary;\n"
        "}\n"
    ".db-row {\n"
    "    padding: 4px 0;\n"
    "    border-radius: 10px;\n"
    "}\n"
    ".db-row:hover {\n"
    "    background-color: rgba(138,108,255,0.12);\n"
    "}\n"
        "scrolledwindow.card-scroller {\n"
        "    border-radius: 12px;\n"
        "    box-shadow: 0 18px 40px rgba(5,10,20,0.55);\n"
        "    background-color: rgba(16,20,28,0.8);\n"
        "    border: 1px solid rgba(255,255,255,0.05);\n"
        "}\n"
        "label { color: @text_primary; }\n"
    ".name-label { font-weight: 600; letter-spacing: 0.2px; color: @text_primary; }\n"
    ".meta-label { font-size: 12px; color: @text_muted; }\n"
        "label.muted { color: @text_muted; }\n"
    ".detail-panel {\n"
    "    background-color: rgba(14,20,32,0.92);\n"
    "    border: 1px solid rgba(138,108,255,0.18);\n"
    "    border-radius: 12px;\n"
    "    box-shadow: inset 0 0 0 1px rgba(0,0,0,0.35), 0 14px 28px rgba(6,10,18,0.55);\n"
    "}\n"
    ".detail-title { font-size: 15px; font-weight: 600; color: @text_primary; }\n"
    ".detail-meta { font-size: 12px; color: @text_muted; }\n"
    ".detail-text { font-size: 12px; line-height: 1.4; color: @text_primary; opacity: 0.88; }\n"
        ".stat-label { color: @accent_primary; font-weight: 600; }\n"
        ".foil { color: #ffd977; text-shadow: 0 0 4px rgba(255,217,119,0.42); }\n"
        "menu,\n"
        "popover,\n"
        ".popover {\n"
        "    background-color: @bg_panel;\n"
        "    color: @text_primary;\n"
        "}\n"
        "menuitem,\n"
        "popover modelbutton {\n"
        "    padding: 6px 12px;\n"
    "    border-radius: 6px;\n"
        "}\n"
        "menuitem:hover,\n"
        "popover modelbutton:hover {\n"
        "    background-color: rgba(138,108,255,0.18);\n"
        "}\n"
    "menuitem:selected,\n"
    "popover modelbutton:selected {\n"
    "    background-image: linear-gradient(135deg, rgba(138,108,255,0.28), rgba(61,201,255,0.22));\n"
    "}\n"
        "checkbutton,\n"
        "radiobutton {\n"
        "    color: @text_primary;\n"
        "}\n"
        "separator {\n"
        "    background-color: rgba(255,255,255,0.08);\n"
        "}\n"
        "scrollbar slider {\n"
        "    background-color: rgba(255,255,255,0.18);\n"
        "    border-radius: 999px;\n"
        "}\n"
        "scrollbar slider:hover {\n"
        "    background-color: rgba(138,108,255,0.45);\n"
        "}\n"
        ".toolbar {\n"
        "    gap: 6px;\n"
        "    padding: 1px 0;\n"
        "}\n"
        "headerbar button,\n"
        "headerbar menubutton,\n"
        "headerbar entry {\n"
        "    min-height: 22px;\n"
        "}\n"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
    ensure_data_dir_exists("data");
    ensure_data_dir_exists("data/img");
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Magic: The Gathering Collection");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    GMenu *file_menu = g_menu_new();
    g_menu_append(file_menu, "Nuovo Database", "app.newdb");
    g_menu_append(file_menu, "Apri Database", "app.opendb");
    g_menu_append(file_menu, "Crea Deck", "app.create_deck");
    g_menu_append(file_menu, "Esporta Database", "app.export_db");
    g_menu_append(file_menu, "Esporta Deck", "app.export_deck");
    // Create a submenu for selecting decks dynamically
    GMenu *deck_menu = g_menu_new();
    g_menu_append_submenu(file_menu, "Seleziona Deck", G_MENU_MODEL(deck_menu));

    GtkWidget *file_button = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(file_button), "File");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(file_button), G_MENU_MODEL(file_menu));

    // View menu with language submenu
    GMenu *view_menu = g_menu_new();
    GMenu *lang_menu = g_menu_new();
    g_menu_append(lang_menu, "Italiano", "app.lang.it");
    g_menu_append(lang_menu, "English", "app.lang.en");
    g_menu_append_submenu(view_menu, "Lingua", G_MENU_MODEL(lang_menu));
    // Filter entry
    g_menu_append(view_menu, "Filtri...", "app.filters");
    // Mana curve for selected deck
    g_menu_append(view_menu, "Curva Mana", "app.mana_curve");
    g_object_unref(lang_menu);

    GtkWidget *view_button = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(view_button), "Visualizza");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(view_button), G_MENU_MODEL(view_menu));
    // view_menu is stored in state->view_menu so we keep a reference to it


    // Box per il nome del database attualmente aperto
    GtkWidget *db_name_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *db_name_label = gtk_label_new("Nessun database aperto");
    gtk_widget_add_css_class(db_name_label, "muted");
    gtk_box_append(GTK_BOX(db_name_box), db_name_label);

    // Stato globale dell'applicazione
    AppState* state = new AppState;
    state->db_path = "";
    state->db = nullptr;
    state->db_name_label = db_name_label;
    state->total_cards_label = gtk_label_new("Totale carte: 0");
    gtk_widget_add_css_class(state->total_cards_label, "stat-label");
    state->filter_label = gtk_label_new("");
    gtk_widget_add_css_class(state->filter_label, "muted");
    state->selected_deck_id = -1;
    state->deck_button = NULL;
    state->deck_label = NULL;
    state->deck_delete_button = NULL;
    state->db_button = NULL;
    state->refresh_button = NULL;
    state->search_debounce_id = 0;
    state->main_window = window;
    state->main_stack = NULL;
    state->cards_page = NULL;
    state->stats_page = NULL;
    state->stats_container = NULL;
    state->stats_placeholder = NULL;
    state->stats_ctx = NULL;
    state->has_oracle_text_column = false;
    state->filter_colors.clear();
    state->filter_rarities.clear();
    state->filter_types.clear();
    state->filter_foil = -1;
    state->filter_no_deck = false;
    // Load persisted settings (notifications, focus retry defaults, etc.)
    load_settings();
    // Initialize AppState with loaded defaults
    state->focus_retry_tries = g_focus_retry_tries_default > 0 ? g_focus_retry_tries_default : 12;
    state->focus_retry_interval_ms = g_focus_retry_interval_ms_default > 0 ? g_focus_retry_interval_ms_default : 100;

    // Bottone per aggiungere una nuova carta
    GtkWidget *add_card_button = gtk_button_new_with_label("Nuova Carta");
    gtk_widget_add_css_class(add_card_button, "accent-button");
    g_signal_connect(add_card_button, "clicked", G_CALLBACK(on_add_card_clicked), window);
    // Hide the legacy "Nuova Carta" button by default so it's not shown on the
    // main Database page (we use inline add controls there). It will be shown
    // when a deck is selected via on_select_deck_id.
    gtk_widget_set_visible(add_card_button, FALSE);

    // Bottone per refresh delle carte
    GtkWidget *refresh_button = gtk_button_new_with_label("Refresh");
    gtk_widget_add_css_class(refresh_button, "ghost-button");
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), window);
    state->refresh_button = refresh_button;

    // Campo di ricerca
    GtkWidget *search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Cerca per nome...");
    gtk_widget_set_size_request(search_entry, 200, -1);
    gtk_widget_add_css_class(search_entry, "search-field");
    g_signal_connect(search_entry, "changed", G_CALLBACK(+[](GtkEditable*, gpointer user_data) {
        GtkWindow* window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        if (!state) return;
        // Debounce rapid typing: cancel previous timeout and schedule a new one
        if (state->search_debounce_id != 0) {
            g_source_remove(state->search_debounce_id);
            state->search_debounce_id = 0;
        }
    // 100 ms debounce interval (more responsive)
    state->search_debounce_id = g_timeout_add(100, search_debounce_cb, state);
    }), window);

    state->search_entry = search_entry;

    state->add_card_button = add_card_button;
    state->file_button = file_button;
    state->view_button = view_button;
    // Attach deck_menu (created above) to state so we can populate it dynamically
    state->deck_menu = deck_menu;
    // Keep file and view menu objects so we can rebuild them on language change
    state->file_menu = file_menu;
    state->view_menu = view_menu;
    // Add extra translations map entries (kept separate for clarity)
    __add_extra_translations();
    __add_more_translations();
    __add_tendina_translations();
    __add_settings_translations();
    // Ensure menus reflect current language
    rebuild_menus_for_language(state);
    // Populate initial deck menu (if DB already open later we'll re-populate)
    populate_deck_menu(state);

    // Deck indicator / clear-filter button (hidden when not filtering)
    GtkWidget *deck_button = gtk_button_new_with_label("");
    gtk_widget_add_css_class(deck_button, "ghost-button");
    GtkWidget *deck_label = gtk_label_new("");
    gtk_widget_add_css_class(deck_label, "stat-label");
    gtk_widget_set_visible(deck_label, FALSE);
    // Clicking it clears the deck filter
    g_signal_connect(deck_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        if (!state) return;
        state->selected_deck_id = -1;
        if (state->deck_button) gtk_widget_set_visible(state->deck_button, FALSE);
        refresh_card_list(state);
    }), window);
    state->deck_button = deck_button;
    state->deck_label = deck_label;
    // Button to delete the currently selected deck (hidden when no deck selected)
    GtkWidget *deck_delete_button = gtk_button_new_with_label("Elimina Deck");
    gtk_widget_set_visible(deck_delete_button, FALSE);
    gtk_widget_set_tooltip_text(deck_delete_button, "Elimina il deck e sposta le carte nella collezione principale");
    gtk_widget_add_css_class(deck_delete_button, "danger-button");
    g_signal_connect(deck_delete_button, "clicked", G_CALLBACK(on_deck_delete_clicked), window);
    state->deck_delete_button = deck_delete_button;

    // Button to return to main Database view (visible when a deck is selected)
    GtkWidget *db_button = gtk_button_new();
    // Use a symbolic 'home' icon so the button visually indicates "back to database"
    GtkWidget *db_icon = gtk_image_new_from_icon_name("go-home-symbolic");
    gtk_button_set_child(GTK_BUTTON(db_button), db_icon);
    gtk_widget_set_visible(db_button, FALSE);
    gtk_widget_set_tooltip_text(db_button, translate("Database").c_str());
    gtk_widget_add_css_class(db_button, "ghost-button");
    g_signal_connect(db_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        if (!state) return;
        state->selected_deck_id = -1;
        if (state->deck_button) gtk_widget_set_visible(state->deck_button, FALSE);
        if (state->deck_delete_button) gtk_widget_set_visible(state->deck_delete_button, FALSE);
        if (state->db_button) gtk_widget_set_visible(state->db_button, FALSE);
        refresh_card_list(state);
    }), window);
    state->db_button = db_button;

    // Bottone per chiudere l'app
    GtkWidget *close_button = gtk_button_new();
    GtkWidget *close_icon = gtk_image_new_from_icon_name("window-close-symbolic");
    gtk_button_set_child(GTK_BUTTON(close_button), close_icon);
    gtk_widget_set_tooltip_text(close_button, "Chiudi");
    gtk_widget_add_css_class(close_button, "ghost-button");
    g_signal_connect(close_button, "clicked", G_CALLBACK(+[](GtkButton *, gpointer user_data) {
        GtkApplication *app = GTK_APPLICATION(user_data);
        g_application_quit(G_APPLICATION(app));
    }), app);

    // Box per i bottoni
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(button_box, "toolbar");
    gtk_box_append(GTK_BOX(button_box), file_button);
    gtk_box_append(GTK_BOX(button_box), view_button);
    gtk_box_append(GTK_BOX(button_box), add_card_button);
    gtk_box_append(GTK_BOX(button_box), refresh_button);
    gtk_box_append(GTK_BOX(button_box), search_entry);
    // Inline add controls (shown in main DB view). These replace the old
    // modal add dialog for quick additions: entry + quantity spin + foil.
    GtkWidget *inline_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(inline_entry), "Aggiungi carta...");
    gtk_widget_set_size_request(inline_entry, 180, -1);
    gtk_widget_set_margin_start(inline_entry, 6);
    gtk_widget_add_css_class(inline_entry, "inline-field");

    GtkWidget *inline_spin = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(inline_spin), 1);
    gtk_widget_set_size_request(inline_spin, 56, -1);
    gtk_widget_set_margin_start(inline_spin, 8);
    gtk_widget_set_margin_end(inline_spin, 8);
    gtk_widget_add_css_class(inline_spin, "inline-field");

    GtkWidget *inline_foil = gtk_check_button_new_with_label("Foil");
    gtk_widget_set_margin_start(inline_foil, 6);
    gtk_widget_add_css_class(inline_foil, "inline-toggle");

    // Create AddCardContext for inline widgets and connect entry activate
    AddCardContext* inline_ctx = new AddCardContext{inline_entry, inline_spin, state, GTK_WINDOW(window), inline_foil};
    g_signal_connect(inline_entry, "activate", G_CALLBACK(on_add_card_ok_clicked), inline_ctx);

    // Attach a key controller to the spin so Enter triggers the add action
    {
        GtkEventController *spin_key = gtk_event_controller_key_new();
        g_signal_connect(spin_key, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType mods, gpointer user_data) -> gboolean {
            (void)ctrl; (void)keycode; (void)mods;
            AddCardContext* c = (AddCardContext*)user_data;
            if (!c) return FALSE;
            if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
                on_add_card_ok_clicked(NULL, c);
                return TRUE;
            }
            return FALSE;
        }), inline_ctx);
        gtk_widget_add_controller(inline_spin, spin_key);
    }

    // Connect to the internal editable of the spin after it's realized so that
    // the internal entry's "activate" is handled (some themes create the
    // child lazily).
    g_signal_connect(inline_spin, "realize", G_CALLBACK(+[](GtkWidget* spin, gpointer user_data){
        AddCardContext* c = (AddCardContext*)user_data;
        if (!c) return;
        GtkWidget* edit = find_editable_descendant(spin);
        if (edit) {
            g_signal_connect(edit, "activate", G_CALLBACK(on_add_card_ok_clicked), c);
        }
    }), inline_ctx);

    // Key controller on the button_box: if focus is inside the inline widgets
    // and Enter is pressed, trigger add.
    {
        GtkEventController *bb_key = gtk_event_controller_key_new();
        g_signal_connect(bb_key, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType mods, gpointer user_data) -> gboolean {
            (void)ctrl; (void)keycode; (void)mods;
            AddCardContext* c = (AddCardContext*)user_data;
            if (!c) return FALSE;
            if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
                GtkWindow *win = c->parent;
                if (!GTK_IS_WINDOW(win)) return FALSE;
                GtkWidget *focused = gtk_window_get_focus(win);
                if (!focused) return FALSE;
                if (focused == c->entry || focused == c->spin || gtk_widget_is_ancestor(GTK_WIDGET(c->spin), focused)) {
                    on_add_card_ok_clicked(NULL, c);
                    return TRUE;
                }
            }
            return FALSE;
        }), inline_ctx);
        gtk_widget_add_controller(button_box, bb_key);
    }

    // Append inline widgets to the button box (hidden when viewing a deck)
    gtk_box_append(GTK_BOX(button_box), inline_entry);
    gtk_box_append(GTK_BOX(button_box), inline_spin);
    gtk_box_append(GTK_BOX(button_box), inline_foil);

    state->inline_add_entry = inline_entry;
    state->inline_add_spin = inline_spin;
    state->inline_add_foil = inline_foil;
    if (deck_delete_button) gtk_box_append(GTK_BOX(button_box), deck_delete_button);
    if (db_button) gtk_box_append(GTK_BOX(button_box), db_button);

    // Tabella carte (GtkColumnView)
    state->card_store = g_list_store_new(card_row_get_type());
    GtkColumnView *column_view = GTK_COLUMN_VIEW(gtk_column_view_new(NULL));
    gtk_column_view_set_model(column_view, GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(state->card_store))));

    // Colonna Colore (quadratino)
    GtkListItemFactory *color_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(color_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(box, "color-box");
    // Use CSS-based sizing (1em) so the color square matches the font height and stays square.
    // Center vertically relative to the row text and prevent the box from expanding to fill the cell.
        /* make this cell respond to double-click/right-click like the name column */
        GtkGesture *dbl = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dbl), GDK_BUTTON_PRIMARY);
        g_signal_connect(dbl, "pressed", G_CALLBACK(on_row_double_click), item);
        gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(dbl));
        gtk_list_item_set_child(item, box);
    }), NULL);
    g_signal_connect(color_factory, "bind", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        GtkWidget *box = gtk_list_item_get_child(item);
        if (!row) {
            gtk_widget_set_visible(box, FALSE);
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_SEPARATOR_TITLE || row->id == ROW_ID_HEADER) {
            // Visual-only rows: hide the color box and make item non-selectable
            gtk_widget_set_visible(box, FALSE);
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        gtk_widget_set_visible(box, TRUE);
        // Calcola colore basato su row->colors
        GdkRGBA color = get_color_for_mana(row->colors ? row->colors : "");
        char css_buf[512];
        // width/height in em units makes the box side equal to current font size (approx. font height)
        // force min/max to keep it square. Add a small border-radius for nicer look.
       /* GTK CSS doesn't support the 'width', 'height', 'max-width', 'max-height' or 'display'
        * properties in the same way as browser CSS. Use min-width/min-height and border-radius
        * only to avoid theme parser warnings. */
       snprintf(css_buf, sizeof(css_buf), ".color-box { background-color: rgba(%d,%d,%d,1.0); border: 1px solid rgba(0,0,0,0.6); min-width: 1em; min-height: 1em; border-radius: 2px; }",
           (int)(color.red * 255), (int)(color.green * 255), (int)(color.blue * 255));
        GtkCssProvider *prov = gtk_css_provider_new();
        gtk_css_provider_load_from_string(prov, css_buf);
        gtk_style_context_add_provider(gtk_widget_get_style_context(box), GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_USER);
        g_object_unref(prov);
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(gesture));
        // Aggiungi controller per hover immagine
        GtkEventController *motion = gtk_event_controller_motion_new();
        g_signal_connect(motion, "enter", G_CALLBACK(on_row_enter), item);
        gtk_widget_add_controller(box, motion);
    /* visibility for this cell depends only on deck membership. The small
     * color-square column should remain visible when the tendina is open; we
     * only hide it when the card is in a deck. */
    char* _in_deck = (char*)g_object_get_data(G_OBJECT(item), "in_deck_names");
    if (_in_deck && strlen(_in_deck) > 0) gtk_widget_set_visible(box, FALSE); else gtk_widget_set_visible(box, TRUE);
    }), NULL);
    GtkColumnViewColumn *color_col = gtk_column_view_column_new(NULL, color_factory);
    gtk_column_view_append_column(column_view, color_col);

    // Colonna Nome
    GtkListItemFactory *name_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(name_factory, "setup", G_CALLBACK(name_factory_setup_cb), NULL);
    g_signal_connect(name_factory, "bind", G_CALLBACK(name_factory_bind_cb), NULL);
    GtkColumnViewColumn *name_col = gtk_column_view_column_new("Nome", name_factory);
    gtk_column_view_column_set_expand(name_col, TRUE);
    gtk_column_view_column_set_resizable(name_col, FALSE);
    gtk_column_view_append_column(column_view, name_col);
    state->name_col = name_col;
    // gtk_column_view_column_set_sorter(name_col, GTK_SORTER(gtk_string_sorter_new(gtk_property_expression_new(G_TYPE_STRING, card_row_get_type(), "name"))));

    // Colonna Tipo
    GtkListItemFactory *type_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(type_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.5);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_list_item_set_child(item, label);
    /* Right-click is handled per-cell; double-click is handled on the whole row (outer_vbox)
     * to avoid conflicting/overlapping gesture controllers. */
        GtkGesture *rclk = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclk), GDK_BUTTON_SECONDARY);
        g_signal_connect(rclk, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(rclk));
    }), NULL);
    g_signal_connect(type_factory, "bind", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        GtkWidget *label = gtk_list_item_get_child(item);
        if (!row) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_SEPARATOR_TITLE) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_HEADER) {
            std::string hdr = translate("Tipo");
            std::string markup = "<span weight='bold'>" + hdr + "</span>";
            gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.5);
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
    gtk_label_set_text(GTK_LABEL(label), row->type ? row->type : "");
    if (row->foil) gtk_widget_add_css_class(label, "foil"); else gtk_widget_remove_css_class(label, "foil");
    /* Hide this column cell when the card is in a deck OR when the tendina is open */
    char* _in_deck = (char*)g_object_get_data(G_OBJECT(item), "in_deck_names");
    gpointer _t = NULL;
    if (row) _t = g_object_get_data(G_OBJECT(row), "tendina_open");
    if (!_t) _t = g_object_get_data(G_OBJECT(item), "tendina_open");
    gboolean is_open = _t ? (GPOINTER_TO_INT(_t) != 0) : FALSE;
    if ((_in_deck && strlen(_in_deck) > 0) || is_open) gtk_widget_set_visible(label, FALSE); else gtk_widget_set_visible(label, TRUE);
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *type_col = gtk_column_view_column_new("Tipo", type_factory);
    gtk_column_view_column_set_expand(type_col, TRUE);
    gtk_column_view_column_set_resizable(type_col, FALSE);
    // NOTE: hide the separate 'Tipo' column — the type is now displayed in the Name column's meta label.
    // Do not append type_col to the column_view to keep the UI compact.
    state->type_col = type_col;
    // gtk_column_view_column_set_sorter(type_col, GTK_SORTER(gtk_string_sorter_new(gtk_property_expression_new(G_TYPE_STRING, card_row_get_type(), "type"))));

    // Colonna Colori
    GtkListItemFactory *colors_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(colors_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.5);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_list_item_set_child(item, label);
    /* Make this cell also respond to double-click so clicking anywhere on the row toggles the tendina. */
        GtkGesture *dbl = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dbl), GDK_BUTTON_PRIMARY);
        g_signal_connect(dbl, "pressed", G_CALLBACK(on_row_double_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(dbl));
        GtkGesture *rclk = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclk), GDK_BUTTON_SECONDARY);
        g_signal_connect(rclk, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(rclk));
    }), NULL);
    g_signal_connect(colors_factory, "bind", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        GtkWidget *label = gtk_list_item_get_child(item);
        if (!row) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_SEPARATOR_TITLE) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_HEADER) {
            std::string hdr = translate("Colori");
            std::string markup = "<span weight='bold'>" + hdr + "</span>";
            gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.5);
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
    gtk_label_set_text(GTK_LABEL(label), row->translated_colors ? row->translated_colors : "");
    if (row->foil) gtk_widget_add_css_class(label, "foil"); else gtk_widget_remove_css_class(label, "foil");
    char* _in_deck = (char*)g_object_get_data(G_OBJECT(item), "in_deck_names");
    gpointer _t = NULL;
    if (row) _t = g_object_get_data(G_OBJECT(row), "tendina_open");
    if (!_t) _t = g_object_get_data(G_OBJECT(item), "tendina_open");
    gboolean is_open = _t ? (GPOINTER_TO_INT(_t) != 0) : FALSE;
    if ((_in_deck && strlen(_in_deck) > 0) || is_open) gtk_widget_set_visible(label, FALSE); else gtk_widget_set_visible(label, TRUE);
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *colors_col = gtk_column_view_column_new("Colori", colors_factory);
    gtk_column_view_column_set_expand(colors_col, TRUE);
    gtk_column_view_column_set_resizable(colors_col, FALSE);
    gtk_column_view_append_column(column_view, colors_col);
    state->colors_col = colors_col;
    // gtk_column_view_column_set_sorter(colors_col, GTK_SORTER(gtk_string_sorter_new(gtk_property_expression_new(G_TYPE_STRING, card_row_get_type(), "translated-colors"))));

    // Colonna Costo Mana
    GtkListItemFactory *mana_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(mana_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.5);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_list_item_set_child(item, label);
    /* Make this cell also respond to double-click so clicking anywhere on the row toggles the tendina. */
        GtkGesture *dbl = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dbl), GDK_BUTTON_PRIMARY);
        g_signal_connect(dbl, "pressed", G_CALLBACK(on_row_double_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(dbl));
        GtkGesture *rclk = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclk), GDK_BUTTON_SECONDARY);
        g_signal_connect(rclk, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(rclk));
    }), NULL);
    g_signal_connect(mana_factory, "bind", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        GtkWidget *label = gtk_list_item_get_child(item);
        char cost_str[16];
        if (!row) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_SEPARATOR_TITLE) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_HEADER) {
            std::string hdr = translate("Costo Mana");
            std::string markup = "<span weight='bold'>" + hdr + "</span>";
            gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.5);
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->total_mana_cost > 0) {
            snprintf(cost_str, sizeof(cost_str), "%d", row->total_mana_cost);
        } else {
            snprintf(cost_str, sizeof(cost_str), "0");
        }
    gtk_label_set_text(GTK_LABEL(label), cost_str);
    if (row->foil) gtk_widget_add_css_class(label, "foil"); else gtk_widget_remove_css_class(label, "foil");
    char* _in_deck = (char*)g_object_get_data(G_OBJECT(item), "in_deck_names");
    gpointer _t = NULL;
    if (row) _t = g_object_get_data(G_OBJECT(row), "tendina_open");
    if (!_t) _t = g_object_get_data(G_OBJECT(item), "tendina_open");
    gboolean is_open = _t ? (GPOINTER_TO_INT(_t) != 0) : FALSE;
    if ((_in_deck && strlen(_in_deck) > 0) || is_open) gtk_widget_set_visible(label, FALSE); else gtk_widget_set_visible(label, TRUE);
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *mana_col = gtk_column_view_column_new("Costo Mana", mana_factory);
    gtk_column_view_column_set_fixed_width(mana_col, 100);
    gtk_column_view_column_set_resizable(mana_col, FALSE);
    gtk_column_view_append_column(column_view, mana_col);
    state->mana_col = mana_col;
    // gtk_column_view_column_set_sorter(mana_col, GTK_SORTER(gtk_numeric_sorter_new(gtk_property_expression_new(G_TYPE_INT, card_row_get_type(), "total-mana-cost"))));

    // Colonna Rarità
    GtkListItemFactory *rarity_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(rarity_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.5);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_list_item_set_child(item, label);
    /* Make this cell also respond to double-click so clicking anywhere on the row toggles the tendina. */
        GtkGesture *dbl = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dbl), GDK_BUTTON_PRIMARY);
        g_signal_connect(dbl, "pressed", G_CALLBACK(on_row_double_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(dbl));
        GtkGesture *rclk = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclk), GDK_BUTTON_SECONDARY);
        g_signal_connect(rclk, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(rclk));
    }), NULL);
    g_signal_connect(rarity_factory, "bind", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        GtkWidget *label = gtk_list_item_get_child(item);
        if (!row) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_SEPARATOR_TITLE) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_HEADER) {
            std::string hdr = translate("Rarità");
            std::string markup = "<span weight='bold'>" + hdr + "</span>";
            gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.5);
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
    // Show localized, nicely-capitalized rarity (e.g. "Comune" / "Common")
    const char* raw_rarity = row->rarity ? row->rarity : "";
    std::string localized_rarity = translate_rarity(raw_rarity);
    gtk_label_set_text(GTK_LABEL(label), localized_rarity.c_str());
        if (row->foil) gtk_widget_add_css_class(label, "foil"); else gtk_widget_remove_css_class(label, "foil");
        char* _in_deck = (char*)g_object_get_data(G_OBJECT(item), "in_deck_names");
        if (_in_deck && strlen(_in_deck) > 0) gtk_widget_set_visible(label, FALSE); else gtk_widget_set_visible(label, TRUE);
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *rarity_col = gtk_column_view_column_new("Rarità", rarity_factory);
    gtk_column_view_column_set_fixed_width(rarity_col, 100);
    gtk_column_view_column_set_resizable(rarity_col, FALSE);
    gtk_column_view_append_column(column_view, rarity_col);
    state->rarity_col = rarity_col;
    // gtk_column_view_column_set_sorter(rarity_col, GTK_SORTER(gtk_string_sorter_new(gtk_property_expression_new(G_TYPE_STRING, card_row_get_type(), "rarity"))));

    // Colonna Data di aggiunta
    GtkListItemFactory *date_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(date_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.5);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_list_item_set_child(item, label);
    /* Make this cell also respond to double-click so clicking anywhere on the row toggles the tendina. */
        GtkGesture *dbl = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dbl), GDK_BUTTON_PRIMARY);
        g_signal_connect(dbl, "pressed", G_CALLBACK(on_row_double_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(dbl));
        GtkGesture *rclk = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclk), GDK_BUTTON_SECONDARY);
        g_signal_connect(rclk, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(rclk));
    }), NULL);
    g_signal_connect(date_factory, "bind", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        GtkWidget *label = gtk_list_item_get_child(item);
        if (!row) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_SEPARATOR_TITLE) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_HEADER) {
            std::string hdr = translate("Data di aggiunta");
            std::string markup = "<span weight='bold'>" + hdr + "</span>";
            gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.5);
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
    std::string formatted = format_datetime(row->added_date ? row->added_date : "");
    gtk_label_set_text(GTK_LABEL(label), formatted.c_str());
    if (row->foil) gtk_widget_add_css_class(label, "foil"); else gtk_widget_remove_css_class(label, "foil");
    char* _in_deck = (char*)g_object_get_data(G_OBJECT(item), "in_deck_names");
    gpointer _t = NULL;
    if (row) _t = g_object_get_data(G_OBJECT(row), "tendina_open");
    if (!_t) _t = g_object_get_data(G_OBJECT(item), "tendina_open");
    gboolean is_open = _t ? (GPOINTER_TO_INT(_t) != 0) : FALSE;
    if ((_in_deck && strlen(_in_deck) > 0) || is_open) gtk_widget_set_visible(label, FALSE); else gtk_widget_set_visible(label, TRUE);
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *date_col = gtk_column_view_column_new("Data di aggiunta", date_factory);
    gtk_column_view_column_set_fixed_width(date_col, 150);
    gtk_column_view_column_set_resizable(date_col, FALSE);
    gtk_column_view_append_column(column_view, date_col);
    state->date_col = date_col;

    // Colonna Quantità
    GtkListItemFactory *qty_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(qty_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.5);
        gtk_list_item_set_child(item, label);
    /* Make this cell also respond to double-click so clicking anywhere on the row toggles the tendina. */
        GtkGesture *dbl = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dbl), GDK_BUTTON_PRIMARY);
        g_signal_connect(dbl, "pressed", G_CALLBACK(on_row_double_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(dbl));
        GtkGesture *rclk = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclk), GDK_BUTTON_SECONDARY);
        g_signal_connect(rclk, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(rclk));
    }), NULL);
    g_signal_connect(qty_factory, "bind", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        GtkWidget *label = gtk_list_item_get_child(item);
        if (!row) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_SEPARATOR_TITLE) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_HEADER) {
            std::string hdr = translate("Quantità");
            std::string markup = "<span weight='bold'>" + hdr + "</span>";
            gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        // Prefer a precomputed display string when available (shows "no-deck / total"),
        // otherwise fall back to the integer quantity.
        if (row->quantity_display && row->quantity_display[0]) {
            gtk_label_set_text(GTK_LABEL(label), row->quantity_display);
        } else {
            char qty[16];
            snprintf(qty, sizeof(qty), "%d", row->quantity);
            gtk_label_set_text(GTK_LABEL(label), qty);
        }
        if (row->foil) gtk_widget_add_css_class(label, "foil"); else gtk_widget_remove_css_class(label, "foil");
        char* _in_deck = (char*)g_object_get_data(G_OBJECT(item), "in_deck_names");
        if (_in_deck && strlen(_in_deck) > 0) gtk_widget_set_visible(label, FALSE); else gtk_widget_set_visible(label, TRUE);
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *qty_col = gtk_column_view_column_new("Quantità", qty_factory);
    gtk_column_view_column_set_fixed_width(qty_col, 80);
    gtk_column_view_column_set_resizable(qty_col, FALSE);
    gtk_column_view_append_column(column_view, qty_col);
    state->qty_col = qty_col;
    // Menu per ordinamento
    GMenu *name_menu = g_menu_new();
    g_menu_append(name_menu, "Ordina Crescente", "app.sort.name.asc");
    g_menu_append(name_menu, "Ordina Decrescente", "app.sort.name.desc");
    gtk_column_view_column_set_header_menu(name_col, G_MENU_MODEL(name_menu));
    g_object_unref(name_menu);

    GMenu *type_menu = g_menu_new();
    g_menu_append(type_menu, "Ordina Crescente", "app.sort.type.asc");
    g_menu_append(type_menu, "Ordina Decrescente", "app.sort.type.desc");
    gtk_column_view_column_set_header_menu(type_col, G_MENU_MODEL(type_menu));
    g_object_unref(type_menu);

    GMenu *colors_menu = g_menu_new();
    g_menu_append(colors_menu, "Ordina Crescente", "app.sort.colors.asc");
    g_menu_append(colors_menu, "Ordina Decrescente", "app.sort.colors.desc");
    gtk_column_view_column_set_header_menu(colors_col, G_MENU_MODEL(colors_menu));
    g_object_unref(colors_menu);

    GMenu *mana_menu = g_menu_new();
    g_menu_append(mana_menu, "Ordina Crescente", "app.sort.mana.asc");
    g_menu_append(mana_menu, "Ordina Decrescente", "app.sort.mana.desc");
    gtk_column_view_column_set_header_menu(mana_col, G_MENU_MODEL(mana_menu));
    g_object_unref(mana_menu);

    GMenu *rarity_menu = g_menu_new();
    g_menu_append(rarity_menu, "Ordina Crescente", "app.sort.rarity.asc");
    g_menu_append(rarity_menu, "Ordina Decrescente", "app.sort.rarity.desc");
    gtk_column_view_column_set_header_menu(rarity_col, G_MENU_MODEL(rarity_menu));
    g_object_unref(rarity_menu);

    GMenu *date_menu = g_menu_new();
    g_menu_append(date_menu, "Ordina Crescente", "app.sort.date.asc");
    g_menu_append(date_menu, "Ordina Decrescente", "app.sort.date.desc");
    gtk_column_view_column_set_header_menu(date_col, G_MENU_MODEL(date_menu));
    g_object_unref(date_menu);

    GMenu *qty_menu = g_menu_new();
    g_menu_append(qty_menu, "Ordina Crescente", "app.sort.qty.asc");
    g_menu_append(qty_menu, "Ordina Decrescente", "app.sort.qty.desc");
    gtk_column_view_column_set_header_menu(qty_col, G_MENU_MODEL(qty_menu));
    g_object_unref(qty_menu);
    // gtk_column_view_column_set_sorter(qty_col, GTK_SORTER(gtk_numeric_sorter_new(gtk_property_expression_new(G_TYPE_INT, card_row_get_type(), "quantity"))));

    state->selection = GTK_SELECTION_MODEL(gtk_column_view_get_model(column_view));
    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scrolled_window, TRUE);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), GTK_WIDGET(column_view));
    gtk_widget_add_css_class(scrolled_window, "card-scroller");
    state->column_view = column_view;

    // Header bar per un look più moderno
    GtkWidget *header_bar = gtk_header_bar_new();
    gtk_widget_add_css_class(header_bar, "topbar");
    gtk_widget_set_hexpand(button_box, TRUE);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header_bar), button_box);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), close_button);
    gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

    // Layout
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *cards_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(cards_page, TRUE);
    gtk_widget_set_vexpand(cards_page, TRUE);
    gtk_box_append(GTK_BOX(cards_page), scrolled_window);

    // Box inferiore: nome database e totale carte
    GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(bottom_box), db_name_box);
    if (state->deck_label) gtk_box_append(GTK_BOX(bottom_box), state->deck_label);
    gtk_box_append(GTK_BOX(bottom_box), state->total_cards_label);
    gtk_box_append(GTK_BOX(bottom_box), state->filter_label);
    // Pagination controls (Prev / Page X/Y / Next) and page size selector
    // Initialize pagination state defaults (0 = view all)
    state->page_size = g_page_size_default; // 0 means 'All rows'
    state->current_page = 0;
    state->total_rows = 0;
    // Prev button
    GtkWidget *prev_btn = gtk_button_new_with_label("◀");
    gtk_widget_set_tooltip_text(prev_btn, "Previous page");
    gtk_box_append(GTK_BOX(bottom_box), prev_btn);
    // Page label
    GtkWidget *page_lbl = gtk_label_new("Page 1/1");
    gtk_box_append(GTK_BOX(bottom_box), page_lbl);
    // Next button
    GtkWidget *next_btn = gtk_button_new_with_label("▶");
    gtk_widget_set_tooltip_text(next_btn, "Next page");
    gtk_box_append(GTK_BOX(bottom_box), next_btn);
    // Page size combo
    GtkWidget *ps_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ps_combo), "10");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ps_combo), "25");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ps_combo), "50");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ps_combo), "100");
    // Set active index based on loaded page size (if not 'All')
    int active_idx = 2; // default -> 50
    if (state->page_size == 10) active_idx = 0;
    else if (state->page_size == 25) active_idx = 1;
    else if (state->page_size == 50) active_idx = 2;
    else if (state->page_size == 100) active_idx = 3;
    gtk_combo_box_set_active(GTK_COMBO_BOX(ps_combo), active_idx);
    gtk_widget_set_tooltip_text(ps_combo, "Rows per page");
    gtk_box_append(GTK_BOX(bottom_box), ps_combo);
    // 'View All' toggle: when active, page_size == 0 and pagination is disabled
    GtkWidget *all_toggle = gtk_toggle_button_new_with_label("All");
    gtk_widget_set_tooltip_text(all_toggle, "Toggle view all rows (no pagination)");
    if (state->page_size == 0) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(all_toggle), TRUE);
    gtk_box_append(GTK_BOX(bottom_box), all_toggle);
    // Save widget refs in state
    state->prev_page_button = prev_btn;
    state->next_page_button = next_btn;
    state->page_label = page_lbl;
    state->page_size_combo = ps_combo;
    state->view_all_toggle = all_toggle;

    // Connect signals for pagination controls
    g_signal_connect(prev_btn, "clicked", G_CALLBACK(on_prev_page_clicked), window);
    g_signal_connect(next_btn, "clicked", G_CALLBACK(on_next_page_clicked), window);
    g_signal_connect(ps_combo, "changed", G_CALLBACK(on_page_size_changed), window);
    g_signal_connect(all_toggle, "toggled", G_CALLBACK(on_view_all_toggled), window);
    gtk_box_append(GTK_BOX(cards_page), bottom_box);

    GtkWidget *stats_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(stats_container, 16);
    gtk_widget_set_margin_bottom(stats_container, 32);
    gtk_widget_set_margin_start(stats_container, 24);
    gtk_widget_set_margin_end(stats_container, 24);
    gtk_widget_set_hexpand(stats_container, TRUE);
    gtk_widget_set_vexpand(stats_container, TRUE);
    gtk_widget_add_css_class(stats_container, "stats-root");

    GtkWidget *stats_scroller = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(stats_scroller, TRUE);
    gtk_widget_set_vexpand(stats_scroller, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(stats_scroller), stats_container);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(stats_scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *stack = gtk_stack_new();
    gtk_widget_set_hexpand(stack, TRUE);
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 250);
    gtk_stack_add_named(GTK_STACK(stack), cards_page, "cards");
    gtk_stack_add_named(GTK_STACK(stack), stats_scroller, "stats");
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "cards");

    gtk_box_append(GTK_BOX(vbox), stack);
    gtk_window_set_child(GTK_WINDOW(window), vbox);

    state->main_stack = stack;
    state->cards_page = cards_page;
    state->stats_page = stats_scroller;
    state->stats_container = stats_container;
    stats_clear(state, true);
    // Salva lo stato globale nella finestra principale
    g_object_set_data(G_OBJECT(window), "app_state", state);

    // Global key controller for shortcuts (GTK4 way). Use a GtkEventControllerKey so we don't rely on GdkEventKey.
    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), app);
    gtk_widget_add_controller(window, key_controller);


    // Azione per nuovo database
    GSimpleAction *newdb_action = g_simple_action_new("newdb", NULL);
    g_signal_connect(newdb_action, "activate", G_CALLBACK(on_new_db_clicked), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(newdb_action));

    // Azione per apri database (GTK4: GtkFileDialog)
    GSimpleAction *opendb_action = g_simple_action_new("opendb", NULL);
    g_signal_connect(opendb_action, "activate", G_CALLBACK(+[](GSimpleAction *action, GVariant *parameter, gpointer user_data) {
        GtkWindow *parent = GTK_WINDOW(user_data);
        GtkFileDialog *dialog = gtk_file_dialog_new();
        // Crea filtro e lista filtri
        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_add_pattern(filter, "*.db");
        gtk_file_filter_set_name(filter, "Database SQLite (*.db)");
        GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
        g_list_store_append(filters, filter);
        gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
        gtk_file_dialog_open(dialog, parent, NULL, [](GObject *source_object, GAsyncResult *res, gpointer user_data) {
            GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
            GtkWindow *parent = GTK_WINDOW(user_data);
            AppState* state = (AppState*)g_object_get_data(G_OBJECT(parent), "app_state");
            GFile *file = gtk_file_dialog_open_finish(dialog, res, NULL);
            if (file) {
                char *filename = g_file_get_path(file);
                if (filename) {
                    if (state->db) delete state->db;
                    state->db = new Database(filename);
                    state->db_path = filename;
                    update_cards_schema_flags(state);
                    gtk_label_set_text(GTK_LABEL(state->db_name_label), filename);
                    // Salva percorso su file
                    std::ofstream lastdb("lastdb.txt");
                    lastdb << filename << std::endl;
                    // Ricrea la GListStore e aggiorna la ColumnView
                    if (state->card_store) g_object_unref(state->card_store);
                    state->card_store = g_list_store_new(card_row_get_type());
                    GtkSelectionModel *selection = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(state->card_store)));
                    gtk_column_view_set_model(state->column_view, selection);
                        refresh_card_list(state);
                        // Populate deck menu now that a DB is open
                        populate_deck_menu(state);
                    g_free(filename);
                }
                g_object_unref(file);
            }
        }, parent);
        g_object_unref(dialog);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(opendb_action));

    // Mana curve action (deck-aware). Parameter: optional int (deck_id)
    GSimpleAction *mana_curve_action = g_simple_action_new("mana_curve", G_VARIANT_TYPE_INT32);
    g_signal_connect(mana_curve_action, "activate", G_CALLBACK(on_mana_curve_action), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(mana_curve_action));

    // Delete deck action (accepts int deck_id)
    GSimpleAction *delete_deck_action = g_simple_action_new("delete_deck", G_VARIANT_TYPE_INT32);
    g_signal_connect(delete_deck_action, "activate", G_CALLBACK(+[](GSimpleAction *action, GVariant *parameter, gpointer user_data){
        // action wrapper: set selected_deck_id in state and trigger existing delete dialog
        GtkWindow* parent = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(parent), "app_state");
        if (!state) return;
        if (parameter && g_variant_is_of_type(parameter, G_VARIANT_TYPE_INT32)) {
            int did = g_variant_get_int32(parameter);
            state->selected_deck_id = did;
        }
        // call existing on_deck_delete_clicked to show confirmation dialog
        on_deck_delete_clicked(NULL, parent);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(delete_deck_action));

    // Azione per creare un deck (menu File)
    GSimpleAction *create_deck_action = g_simple_action_new("create_deck", NULL);
    g_signal_connect(create_deck_action, "activate", G_CALLBACK(on_create_deck_action), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(create_deck_action));

    // Action to select a deck by id (used by File->Seleziona Deck submenu)
    GSimpleAction *select_deck_id_action = g_simple_action_new("select_deck_id", G_VARIANT_TYPE_INT32);
    g_signal_connect(select_deck_id_action, "activate", G_CALLBACK(on_select_deck_id), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(select_deck_id_action));

    // Azione per selezionare un deck (menu File)
    GSimpleAction *select_deck_action = g_simple_action_new("select_deck", NULL);
    g_signal_connect(select_deck_action, "activate", G_CALLBACK(on_select_deck_action), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(select_deck_action));

    // Azione per aggiungere carta ad un mazzo (invocata dal context menu)
    GSimpleAction *add_to_deck_action = g_simple_action_new("add_to_deck", G_VARIANT_TYPE_INT32);
    g_signal_connect(add_to_deck_action, "activate", G_CALLBACK(on_add_to_deck), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(add_to_deck_action));

    // Azione per cancellare il filtro mazzo (torna al DB principale) - no parameter
    GSimpleAction *clear_deck_action = g_simple_action_new("clear_deck", NULL);
    g_signal_connect(clear_deck_action, "activate", G_CALLBACK(on_clear_deck), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(clear_deck_action));


    // Azione per eliminare carta
    GSimpleAction *delete_action = g_simple_action_new("delete_card", G_VARIANT_TYPE_INT32);
    g_signal_connect(delete_action, "activate", G_CALLBACK(on_delete_card), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(delete_action));

    // Export actions
    GSimpleAction *export_db_action = g_simple_action_new("export_db", NULL);
    g_signal_connect(export_db_action, "activate", G_CALLBACK(on_export_database_action), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(export_db_action));

    GSimpleAction *export_deck_action = g_simple_action_new("export_deck", NULL);
    g_signal_connect(export_deck_action, "activate", G_CALLBACK(on_export_deck_action), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(export_deck_action));

    // Filters action (View->Filtri)
    GSimpleAction *filters_action = g_simple_action_new("filters", NULL);
    g_signal_connect(filters_action, "activate", G_CALLBACK(on_filters_action), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(filters_action));

    // Stateful desktop notifications action (boolean state) and Preferences
    GSimpleAction *notifications_action = g_simple_action_new_stateful("notifications", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(g_notifications_enabled));
    g_signal_connect(notifications_action, "activate", G_CALLBACK(+[](GSimpleAction *action, GVariant *parameter, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        // Toggle the boolean state
        GVariant *cur = g_action_get_state(G_ACTION(action));
        gboolean val = FALSE;
        if (cur) { val = g_variant_get_boolean(cur); g_variant_unref(cur); }
        gboolean next = !val;
        g_simple_action_set_state(action, g_variant_new_boolean(next));
        g_notifications_enabled = next;
        save_settings();
        // Rebuild view menu to ensure labels/localization updated
        rebuild_menus_for_language(state);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(notifications_action));

    // Preferences action to show a small dialog for notification and focus retry settings
    GSimpleAction *preferences_action = g_simple_action_new("preferences", NULL);
    g_signal_connect(preferences_action, "activate", G_CALLBACK(on_preferences_action), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(preferences_action));

    // Rebuild menus now that actions (including stateful notifications) are registered
    // This ensures menu items referencing app.notifications and app.preferences
    // are active/clickable from the first presentation of the UI.
    rebuild_menus_for_language(state);

    // Azioni per ordinamento
    GSimpleAction *sort_name_asc = g_simple_action_new("sort.name.asc", NULL);
    g_signal_connect(sort_name_asc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "name", true);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_name_asc));

    GSimpleAction *sort_name_desc = g_simple_action_new("sort.name.desc", NULL);
    g_signal_connect(sort_name_desc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "name", false);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_name_desc));

    GSimpleAction *sort_type_asc = g_simple_action_new("sort.type.asc", NULL);
    g_signal_connect(sort_type_asc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "type", true);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_type_asc));

    GSimpleAction *sort_type_desc = g_simple_action_new("sort.type.desc", NULL);
    g_signal_connect(sort_type_desc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "type", false);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_type_desc));

    GSimpleAction *sort_colors_asc = g_simple_action_new("sort.colors.asc", NULL);
    g_signal_connect(sort_colors_asc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "translated-colors", true);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_colors_asc));

    GSimpleAction *sort_colors_desc = g_simple_action_new("sort.colors.desc", NULL);
    g_signal_connect(sort_colors_desc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "translated-colors", false);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_colors_desc));

    GSimpleAction *sort_mana_asc = g_simple_action_new("sort.mana.asc", NULL);
    g_signal_connect(sort_mana_asc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "total-mana-cost", true);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_mana_asc));

    GSimpleAction *sort_mana_desc = g_simple_action_new("sort.mana.desc", NULL);
    g_signal_connect(sort_mana_desc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "total-mana-cost", false);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_mana_desc));

    GSimpleAction *sort_rarity_asc = g_simple_action_new("sort.rarity.asc", NULL);
    g_signal_connect(sort_rarity_asc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "rarity", true);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_rarity_asc));

    GSimpleAction *sort_rarity_desc = g_simple_action_new("sort.rarity.desc", NULL);
    g_signal_connect(sort_rarity_desc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "rarity", false);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_rarity_desc));

    GSimpleAction *sort_date_asc = g_simple_action_new("sort.date.asc", NULL);
    g_signal_connect(sort_date_asc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "added_date", true);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_date_asc));

    GSimpleAction *sort_date_desc = g_simple_action_new("sort.date.desc", NULL);
    g_signal_connect(sort_date_desc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "added_date", false);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_date_desc));

    GSimpleAction *sort_qty_asc = g_simple_action_new("sort.qty.asc", NULL);
    g_signal_connect(sort_qty_asc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "quantity", true);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_qty_asc));

    GSimpleAction *sort_qty_desc = g_simple_action_new("sort.qty.desc", NULL);
    g_signal_connect(sort_qty_desc, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        sort_card_list(state, "quantity", false);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sort_qty_desc));
    // Azione per aggiungere carta
    GSimpleAction *add_card_action = g_simple_action_new("add_card", NULL);
    g_signal_connect(add_card_action, "activate", G_CALLBACK(on_add_card_action), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(add_card_action));

    // Azione per mettere a fuoco il campo di ricerca (scorciatoia Ctrl+F)
    GSimpleAction *search_action = g_simple_action_new("search_card", NULL);
    g_signal_connect(search_action, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        if (state && state->search_entry) {
            // Use the robust focus helper to retry if necessary
            schedule_focus_retries(state->search_entry, state);
        }
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(search_action));

    // Azione per eliminare riga selezionata
    GSimpleAction *delete_selected_action = g_simple_action_new("delete_selected", NULL);
    g_signal_connect(delete_selected_action, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        if (!state || !state->db) return;
        GtkSelectionModel *selection = state->selection;
        guint selected_pos = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(selection));
        if (selected_pos == GTK_INVALID_LIST_POSITION) return;
        GListModel *model = gtk_single_selection_get_model(GTK_SINGLE_SELECTION(selection));
        CardRow *row = (CardRow*)g_list_model_get_item(model, selected_pos);
        if (!row) return;
        GVariant *param = g_variant_new_int32(row->id);
        on_delete_card(NULL, param, window);
        g_variant_unref(param);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(delete_selected_action));

    // Language actions
    GSimpleAction *lang_it = g_simple_action_new("lang.it", NULL);
    g_signal_connect(lang_it, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        current_language = "it";
        update_ui_texts(window, state);
        refresh_card_list(state);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(lang_it));

    GSimpleAction *lang_en = g_simple_action_new("lang.en", NULL);
    g_signal_connect(lang_en, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer user_data) {
        GtkWindow *window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        current_language = "en";
        update_ui_texts(window, state);
        refresh_card_list(state);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(lang_en));

    // Imposta acceleratori
    gtk_application_set_accels_for_action(app, "app.add_card", (const char*[]){"<Control>n", NULL});
    gtk_application_set_accels_for_action(app, "app.delete_selected", (const char*[]){"Delete", NULL});
    // Keyboard accelerators for search and preferences
    gtk_application_set_accels_for_action(app, "app.search_card", (const char*[]){"<Control>f", NULL});
    gtk_application_set_accels_for_action(app, "app.preferences", (const char*[]){"<Control>comma", NULL});

    // Carica ultimo database usato
    std::ifstream lastdb("lastdb.txt");
    if (lastdb) {
        std::string path;
        std::getline(lastdb, path);
        if (!path.empty() && std::filesystem::exists(path)) {
            if (state->db) delete state->db;
            state->db = new Database(path);
            state->db_path = path;
            update_cards_schema_flags(state);
            gtk_label_set_text(GTK_LABEL(state->db_name_label), path.c_str());
            refresh_card_list(state);
            // Ensure the deck submenu is up-to-date when loading the last used DB at startup
            populate_deck_menu(state);
        }
    }

    gtk_window_present(GTK_WINDOW(window));
}

static GMenu* create_context_menu(CardRow *row, AppState *state) {
    GMenu *menu = g_menu_new();
    GMenuItem *add_item = g_menu_item_new("Aggiungi a Deck", NULL);
    g_menu_item_set_action_and_target_value(add_item, "app.add_to_deck", g_variant_new_int32(row->id));
    g_menu_append_item(menu, add_item);
    g_object_unref(add_item);
    GMenuItem *delete_item = g_menu_item_new("Elimina", NULL);
    g_menu_item_set_action_and_target_value(delete_item, "app.delete_card", g_variant_new_int32(row->id));
    g_menu_append_item(menu, delete_item);
    g_object_unref(delete_item);
    return menu;
}

int main(int argc, char *argv[]) {
    // Check for --log flag. If not present, silence stdout/stderr so logs
    // (g_print / std::cout) do not appear. If present, leave streams as-is.
    bool log_enabled = false;
    int dst = 1;
    for (int src = 1; src < argc; ++src) {
        if (strcmp(argv[src], "--log") == 0) {
            log_enabled = true;
            continue; // drop from argv
        }
        argv[dst++] = argv[src];
    }
    argc = dst;

    if (!log_enabled) {
        // Redirect stdout/stderr to /dev/null to suppress logs by default
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
    }

    GtkApplication *app = gtk_application_new("org.magicdb.collection", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

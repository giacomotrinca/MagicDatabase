
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
    gchar *translated_colors;
    int total_mana_cost;
    gchar *image_url;
    gchar *added_date; // ISO format: YYYY-MM-DDTHH:MM:SS
    gchar *price_usd; // Prezzo in USD come stringa
    int foil; // 0 = non-foil, 1 = foil
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
    self->translated_colors = NULL;
    self->total_mana_cost = 0;
    self->image_url = NULL;
    self->added_date = NULL;
    self->price_usd = NULL;
}

// Helper constants for special row ids (separator/header rows shown in lists)
#define ROW_ID_SEPARATOR_TITLE    -1001
#define ROW_ID_HEADER             -1002

// Calculate a simple total mana cost from a mana cost string.
// This is a best-effort parser: it treats numeric symbols as their value
// and each color symbol or generic symbol as +1. Returns 0 for empty.
static int calculate_total_mana_cost(const std::string &mana) {
    if (mana.empty()) return 0;
    int total = 0;
    std::string num;
    for (size_t i = 0; i < mana.size(); ++i) {
        char c = mana[i];
        if (std::isdigit((unsigned char)c)) {
            // accumulate contiguous digits
            num.push_back(c);
            // if next is non-digit, flush
            if (i + 1 >= mana.size() || !std::isdigit((unsigned char)mana[i+1])) {
                try { total += std::stoi(num); } catch(...) { }
                num.clear();
            }
        } else if (std::isalpha((unsigned char)c)) {
            // letters like R G W count as 1
            total += 1;
        }
        // ignore braces, slashes, punctuation
    }
    return total;
}

// Forward-declare accessor for current language so formatters located earlier can use it
static const std::string& get_current_language();

// Very small datetime formatter — returns the input or a simplified human form.
static std::string format_datetime(const char* iso) {
    if (!iso) return std::string();
    std::string s = iso;
    if (s.empty()) return s;
    // Parse ISO-like timestamp: accept YYYY-MM-DDTHH:MM:SS or YYYY-MM-DD HH:MM:SS
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
            // YYYY-MM-DDTHH:MM (no seconds)
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
        // fallback: return original string
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
    int wday = lt->tm_wday; // 0=Sun
    int monidx = lt->tm_mon; // 0-based
    if (get_current_language() == "it") {
        snprintf(out, sizeof(out), "%s %02d %s %04d %02d:%02d:%02d",
                 days_it[wday], lt->tm_mday, months_it[monidx], lt->tm_year + 1900, lt->tm_hour, lt->tm_min, lt->tm_sec);
    } else {
        snprintf(out, sizeof(out), "%s %02d %s %04d %02d:%02d:%02d",
                 days_en[wday], lt->tm_mday, months_en[monidx], lt->tm_year + 1900, lt->tm_hour, lt->tm_min, lt->tm_sec);
    }
    return std::string(out);
}

// Forward-declare translate_colors (defined later) so helpers above can call it
static std::string translate_colors(const char* colors);

// Factory helper to create and initialize a CardRow instance
static CardRow* card_row_new(int id, const char* name, const char* type, const char* colors, const char* set_code, const char* mana_cost, const char* rarity, int quantity, const char* image_url, const char* added_date, const char* price_usd, int foil) {
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
    // translated_colors uses the helper defined later; store an English/Italian translation
    r->translated_colors = g_strdup(translate_colors(r->colors).c_str());
    return r;
}
// CSS provider used for separator styling (initialized on demand)
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

// Translation table: key -> { lang -> translation }
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

static std::string current_language = "it";

// Provide accessor for earlier functions
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
};

// Try to grab focus multiple times (short retries) to overcome races where the
// window manager or other widgets steal focus when dialogs close.
static gboolean grab_focus_to_entry(gpointer data) {
    FocusTarget* ft = (FocusTarget*)data;
    if (!ft) return G_SOURCE_REMOVE;
    GtkWidget* entry = ft->entry;
    if (entry && GTK_IS_WIDGET(entry)) {
        // Ensure parent window is presented (helps when WM focus policy is odd)
        GtkWindow* win = GTK_WINDOW(gtk_widget_get_ancestor(entry, GTK_TYPE_WINDOW));
        if (win && GTK_IS_WINDOW(win)) {
            gtk_window_present(win);
        }
        std::cout << "DEBUG: grab_focus_to_entry attempt=" << ft->tries << " focusing entry=" << entry << " type=" << G_OBJECT_TYPE_NAME(entry) << "\n";
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
    } else {
        std::cout << "DEBUG: grab_focus_to_entry called with invalid entry pointer=" << entry << "\n";
        delete ft;
        return G_SOURCE_REMOVE;
    }

    ft->tries -= 1;
    if (ft->tries <= 0) {
        delete ft;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE; // keep the timeout running
}

// Send a desktop notification using notify-send. Uses g_shell_quote to safely
// quote title/body and g_spawn_command_line_async to invoke the command.
static void send_notification(const std::string& title, const std::string& body) {
    GError* error = NULL;
    gchar* qtitle = g_shell_quote(title.c_str());
    gchar* qbody = g_shell_quote(body.c_str());
    gchar* cmd = g_strdup_printf("notify-send %s %s", qtitle, qbody);
    gboolean ok = g_spawn_command_line_async(cmd, &error);
    if (!ok) {
        std::cout << "DEBUG: notify-send failed: " << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
    }
    g_free(cmd);
    g_free(qtitle);
    g_free(qbody);
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
    GtkColumnViewColumn *name_col, *type_col, *colors_col, *mana_col, *rarity_col, *date_col, *qty_col;
    GtkWidget* add_card_button;
    GtkWidget* file_button;
    GtkWidget* view_button;
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
    int filter_foil; // -1 = any, 0 = non-foil only, 1 = foil only
    bool filter_no_deck; // true = only show cards not in any deck
};

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

// Export helper: queries cards (optionally filtered by deck) and writes a TXT file.
// If 'deck' is true, deck_id must be provided and file is named <deckname>_data.txt
// Otherwise file is data/tot_database.txt
// Forward-declare load_cards_from_db with the new only_no_deck parameter so
// this export helper (which appears earlier in the file) can call it.
static std::vector<std::map<std::string, std::string>> load_cards_from_db(Database* db, const std::string& filter, int deck_filter, bool only_no_deck);

static bool export_cards_to_txt(AppState* state, bool deck, int deck_id, const std::string& lang) {
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

    std::ofstream out(filename);
    if (!out) return false;
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
        if (ch && GTK_IS_TOGGLE_BUTTON(ch)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch), want);
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
    g_menu_append(new_view, translate("Filtri...").c_str(), "app.filters");
    if (state->view_button) gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(state->view_button), G_MENU_MODEL(new_view));
    if (state->view_menu) g_object_unref(state->view_menu);
    state->view_menu = new_view;
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
    // background
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    // margins
    const int margin = 40;
    int w = width - margin*2;
    int h = height - margin*2;
    int buckets = cap_bucket + 1;
    int bar_w = buckets > 0 ? (w / buckets) : w;
    // find max count
    int maxc = 1;
    for (auto &p : counts) if (p.second > maxc) maxc = p.second;
    // draw axes
    cairo_set_source_rgb(cr, 0,0,0);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, margin, margin);
    cairo_line_to(cr, margin, margin + h);
    cairo_line_to(cr, margin + w, margin + h);
    cairo_stroke(cr);
    // draw bars
    for (int b=0;b<buckets;++b) {
        int cx = margin + b*bar_w;
        int cnt = 0;
        auto it = counts.find(b);
        if (it != counts.end()) cnt = it->second;
        double height_frac = maxc > 0 ? (double)cnt / (double)maxc : 0.0;
        int bh = (int)(height_frac * (h - 20));
        // color gradient
        double t = buckets>1 ? (double)b / (double)(buckets-1) : 0.0;
        cairo_set_source_rgb(cr, 0.2 + 0.6 * t, 0.4, 0.2 + 0.4*(1.0-t));
        cairo_rectangle(cr, cx + 4, margin + h - bh, bar_w - 8, bh);
        cairo_fill(cr);
        // label
        char lbl[32];
        if (b < cap_bucket) snprintf(lbl, sizeof(lbl), "%d", b);
        else snprintf(lbl, sizeof(lbl), ">=%d", cap_bucket);
        cairo_set_source_rgb(cr, 0,0,0);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12.0);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, lbl, &ext);
        double lx = cx + (bar_w - ext.width) / 2.0 - ext.x_bearing;
        double ly = margin + h + 16;
        cairo_move_to(cr, lx, ly);
        cairo_show_text(cr, lbl);
    }
    // title
    cairo_set_source_rgb(cr, 0,0,0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 16.0);
    cairo_move_to(cr, margin, 18);
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

// Action handler: show mana curve for deck (parameter int deck id) or current selected deck
static void on_mana_curve_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    GtkWindow* parent = GTK_WINDOW(user_data);
    AppState* state = (AppState*)g_object_get_data(G_OBJECT(parent), "app_state");
    if (!state || !state->db) return;
    int deck_id = -1;
    if (parameter && g_variant_is_of_type(parameter, G_VARIANT_TYPE_INT32)) deck_id = g_variant_get_int32(parameter);
    if (deck_id == -1) deck_id = state->selected_deck_id;
    if (deck_id == -1) {
        GtkAlertDialog *a = gtk_alert_dialog_new("%s", "Seleziona un deck prima di visualizzare la curva mana");
        gtk_alert_dialog_show(a, parent);
        g_object_unref(a);
        return;
    }
    // Query DB for mana_cost, name, quantity and colors in this deck
    std::map<int,int> counts;
    int total_cards = 0;
    // detailed info per bucket: cost -> list of (name, qty)
    std::map<int, std::vector<std::pair<std::string,int>>> bucket_details;
    std::map<std::string,int> color_counts;
    state->db->query("SELECT english_name, localized_name, name, mana_cost, quantity, colors FROM cards WHERE deck_id = ?", [&](const std::map<std::string,std::string>& row){
        std::string mana = row.count("mana_cost") ? row.at("mana_cost") : "";
        int qty = 1;
        try { if (row.count("quantity") && !row.at("quantity").empty()) qty = std::stoi(row.at("quantity")); } catch(...) { qty = 1; }
        int cost = calculate_total_mana_cost(mana);
        if (cost < 0) cost = 0;
        counts[cost] += qty;
        total_cards += qty;
        // name preference: localized, english, raw name
        std::string cname = "";
        if (row.count("localized_name") && !row.at("localized_name").empty()) cname = row.at("localized_name");
        else if (row.count("english_name") && !row.at("english_name").empty()) cname = row.at("english_name");
        else if (row.count("name")) cname = row.at("name");
        bucket_details[cost].push_back({cname, qty});
        // colors parsing: either JSON array or compact codes
        std::string cols = row.count("colors") ? row.at("colors") : "";
        if (!cols.empty()) {
            try {
                auto j = nlohmann::json::parse(cols);
                if (j.is_array()) {
                    for (auto &c : j) {
                        std::string cc = c.get<std::string>();
                        color_counts[cc] += qty;
                    }
                }
            } catch(...) {
                // fallback: treat as sequence of letters (e.g., WUBRG or "WU")
                for (char ch : cols) {
                    if (std::isalpha((unsigned char)ch)) {
                        std::string s(1, ch);
                        color_counts[s] += qty;
                    }
                }
            }
        }
    }, std::vector<std::string>{std::to_string(deck_id)});
    // cap bucket at 10
    int cap = 10;
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
    // create surface
    int w = 800, h = 400;
    cairo_surface_t* surf = create_mana_surface(buckets, total_cards, cap, w, h);
    // create pixbuf from surface and show in dialog with export buttons and stats
    GdkPixbuf* pb = gdk_pixbuf_get_from_surface(surf, 0, 0, w, h);
    GtkWidget* dialog = create_styled_dialog(parent, w+360, h+120);
    gtk_window_set_title(GTK_WINDOW(dialog), "Curva Mana");
    GtkWidget* hmain = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_window_set_child(GTK_WINDOW(dialog), hmain);
    // Left: image preview
    GtkWidget* img = gtk_image_new_from_pixbuf(pb);
    gtk_box_append(GTK_BOX(hmain), img);
    // Right: stats and controls
    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(hmain), right);
    // Stats computation
    double avg = 0.0;
    int max_cost = 0;
    int mode_bucket = 0; int mode_count = 0;
    for (auto &p : buckets) {
        int cost = p.first; int cnt = p.second;
        avg += cost * (double)cnt;
        if (cost > max_cost) max_cost = cost;
        if (cnt > mode_count) { mode_count = cnt; mode_bucket = cost; }
    }
    if (total_cards > 0) avg /= (double)total_cards;
    // median calculation
    int median = 0;
    if (total_cards > 0) {
        int half = (total_cards + 1) / 2;
        int acc = 0;
        for (auto &p : buckets) {
            acc += p.second;
            if (acc >= half) { median = p.first; break; }
        }
    }
    // Summary labels
    char buf[256];
    snprintf(buf, sizeof(buf), "Totale carte: %d", total_cards);
    GtkWidget* lbl_total = gtk_label_new(buf);
    snprintf(buf, sizeof(buf), "Costo medio: %.2f", avg);
    GtkWidget* lbl_avg = gtk_label_new(buf);
    snprintf(buf, sizeof(buf), "Mediana: %d", median);
    GtkWidget* lbl_median = gtk_label_new(buf);
    snprintf(buf, sizeof(buf), "Bucket più comune: %d (%d carte)", mode_bucket, mode_count);
    GtkWidget* lbl_mode = gtk_label_new(buf);
    snprintf(buf, sizeof(buf), "Massimo costo bucket: %d", max_cost);
    GtkWidget* lbl_max = gtk_label_new(buf);
    gtk_box_append(GTK_BOX(right), lbl_total);
    gtk_box_append(GTK_BOX(right), lbl_avg);
    gtk_box_append(GTK_BOX(right), lbl_median);
    gtk_box_append(GTK_BOX(right), lbl_mode);
    gtk_box_append(GTK_BOX(right), lbl_max);
    // Distribution list (compact)
    GtkWidget* dist_label = gtk_label_new(NULL);
    std::string dists = "Distribuzione: ";
    for (auto &p : buckets) {
        double pct = total_cards>0 ? (100.0 * p.second / total_cards) : 0.0;
        char t[64]; snprintf(t, sizeof(t), "%d:%d(%.0f%%) ", p.first, p.second, pct);
        dists += t;
    }
    gtk_label_set_text(GTK_LABEL(dist_label), dists.c_str());
    gtk_label_set_xalign(GTK_LABEL(dist_label), 0.0);
    gtk_label_set_xalign(GTK_LABEL(lbl_total), 0.0);
    gtk_label_set_xalign(GTK_LABEL(lbl_avg), 0.0);
    gtk_label_set_xalign(GTK_LABEL(lbl_median), 0.0);
    gtk_label_set_xalign(GTK_LABEL(lbl_mode), 0.0);
    gtk_label_set_xalign(GTK_LABEL(lbl_max), 0.0);
    gtk_box_append(GTK_BOX(right), dist_label);
    // Buttons: export png/pdf and export stats
    GtkWidget* btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* export_png = gtk_button_new_with_label("Export PNG");
    GtkWidget* export_pdf = gtk_button_new_with_label("Export PDF");
    GtkWidget* export_stats = gtk_button_new_with_label("Export stats");
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_box_append(GTK_BOX(btns), export_png);
    gtk_box_append(GTK_BOX(btns), export_pdf);
    gtk_box_append(GTK_BOX(btns), export_stats);
    gtk_box_append(GTK_BOX(btns), close);
    gtk_box_append(GTK_BOX(right), btns);

    // Handlers (avoid inline lambdas in macros)
    struct ExportCtx { cairo_surface_t* surf; GtkWindow* parent; int w,h; std::map<int,int> buckets; int total; int cap; std::string deck_name; std::map<int, std::vector<std::pair<std::string,int>>> bucket_details; std::map<std::string,int> color_counts; double avg; int median; int mode_bucket; int mode_count; int max_cost; };
    ExportCtx* ectx = new ExportCtx{surf, GTK_WINDOW(dialog), w, h, buckets, total_cards, cap, deck_name, buckets_details_capped, color_counts, avg, median, mode_bucket, mode_count, max_cost};
    g_signal_connect(export_png, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data){
    ExportCtx* c = (ExportCtx*)user_data;
    if (!c || !c->surf) return;
    ensure_data_dir_exists("data");
    std::string base = c->deck_name.empty() ? (std::string("mana_curve_") + std::to_string(time(nullptr))) : (std::string("mana_curve_") + sanitize_filename(c->deck_name));
    std::string fname = std::string("data/") + base + ".png";
    cairo_surface_write_to_png(c->surf, fname.c_str());
    std::cout << "Exported PNG to " << fname << std::endl;
    }), ectx);
    g_signal_connect(export_pdf, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data){
    ExportCtx* c = (ExportCtx*)user_data;
    if (!c) return;
    ensure_data_dir_exists("data");
    std::string base = c->deck_name.empty() ? (std::string("mana_curve_") + std::to_string(time(nullptr))) : (std::string("mana_curve_") + sanitize_filename(c->deck_name));
    std::string fname = std::string("data/") + base + ".pdf";
    cairo_surface_t* pdf = cairo_pdf_surface_create(fname.c_str(), c->w, c->h);
    cairo_t* cr = cairo_create(pdf);
    // Redraw vectorially on the PDF surface for better quality
    draw_mana_on_cairo(cr, c->w, c->h, c->buckets, c->total, c->cap);
    cairo_destroy(cr);
    cairo_surface_flush(pdf);
    cairo_surface_destroy(pdf);
    std::cout << "Exported PDF to " << fname << std::endl;
    }), ectx);
    g_signal_connect(export_stats, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data){
    ExportCtx* c = (ExportCtx*)user_data;
    if (!c) return;
    ensure_data_dir_exists("data");
    std::string base = c->deck_name.empty() ? (std::string("mana_stats_") + std::to_string(time(nullptr))) : (std::string("mana_stats_") + sanitize_filename(c->deck_name));
    std::string fname = std::string("data/") + base + ".txt";
    std::ofstream out(fname);
    if (!out) return;
    out << "Deck: " << c->deck_name << "\n";
    out << "Total cards: " << c->total << "\n";
    out << "Average mana cost: " << c->avg << "\n";
    out << "Median cost: " << c->median << "\n";
    out << "Mode bucket: " << c->mode_bucket << " (" << c->mode_count << ")\n";
    out << "Max bucket: " << c->max_cost << "\n\n";
    out << "Distribution:\n";
    for (auto &p : c->buckets) out << "  " << p.first << ": " << p.second << "\n";
    out << "\nPer-bucket card lists:\n";
    for (auto &p : c->bucket_details) {
        out << "Bucket " << p.first << ":\n";
        for (auto &it : p.second) {
            out << "    " << it.first << " x" << it.second << "\n";
        }
    }
    out << "\nColor breakdown:\n";
    for (auto &p : c->color_counts) out << "  " << p.first << ": " << p.second << "\n";
    out.close();
    std::cout << "Exported stats to " << fname << std::endl;
    }), ectx);
    g_signal_connect(close, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer user_data){
        GtkWidget* dlg = GTK_WIDGET(user_data);
        if (dlg) gtk_window_destroy(GTK_WINDOW(dlg));
    }), dialog);

    gtk_widget_show(dialog);
    // cleanup when dialog is destroyed
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget* w, gpointer user_data){
        ExportCtx* c = (ExportCtx*)user_data;
        if (c) {
            if (c->surf) cairo_surface_destroy(c->surf);
            delete c;
        }
    }), ectx);
    if (pb) g_object_unref(pb);
}

// Dialog per ricerca carta Scryfall
struct AddCardContext {
    GtkWidget* entry;
    GtkWidget* spin;
    AppState* state;
    GtkWindow* parent;
    GtkWidget* foil_checkbox;
};

// Funzione per caricare le carte dal database
static std::vector<std::map<std::string, std::string>> load_cards_from_db(Database* db, const std::string& filter = "", int deck_filter = -1, bool only_no_deck = false) {
    std::vector<std::map<std::string, std::string>> cards;
    if (!db) return cards;
    // Build SQL and optional params for deck filtering
    std::string sql = "SELECT id, english_name, localized_name, name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, image_url, added_date, price_usd, foil, sideboard, deck_id FROM cards";
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
            if (it == agg.end()) {
                AggItem ai;
                ai.rep = r;
                ai.total_qty = qty;
                ai.latest_date = added;
                agg.emplace(key, std::move(ai));
            } else {
                it->second.total_qty += qty;
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
    auto cards = load_cards_from_db(state->db, filter, state->selected_deck_id, state->filter_no_deck);
    std::cout << "Loading " << cards.size() << " cards from db" << std::endl;
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
            if (state->filter_foil != -1) {
                if (state->filter_foil != foil) continue;
            }
            if (state->filter_no_deck) {
                if (row.count("deck_id") && !row.at("deck_id").empty()) continue;
            }
            CardRow* crow = card_row_new(std::stoi(row.at("id")),
                                         display_name.c_str(),
                                         display_type.c_str(),
                                         row.at("colors").c_str(),
                                         row.at("set_code").c_str(),
                                         row.at("mana_cost").c_str(),
                                         row.at("rarity").c_str(),
                                         std::stoi(row.at("quantity")),
                                         row.at("image_url").c_str(),
                                         row.count("added_date") ? row.at("added_date").c_str() : "",
                                         row.count("price_usd") ? row.at("price_usd").c_str() : "",
                                         foil);
            g_list_store_append(state->card_store, crow);
            g_object_unref(crow);
            // Accumulate totals
            try {
                int qty = std::stoi(row.at("quantity"));
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
    if (!state->db->query("SELECT english_name, localized_name, type, localized_type, colors, set_code, mana_cost, rarity, image_url, price_usd, foil FROM cards WHERE id = ?", [&](const std::map<std::string,std::string>& r){ crow = r; }, std::vector<std::string>{std::to_string(card_id)})) {
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
    int foil = 0;
    if (crow.count("foil")) {
        try { foil = std::stoi(crow["foil"]); } catch(...) { foil = 0; }
    }

    if (to_remove >= current_qty) {
        // Move entire row: insert into main (deck_id = -1) merged, then delete original
    bool ok = state->db->insert_card(en, ln, ty, lty, cols, setc, mana, rar, current_qty, img, price, -1, foil);
        if (!ok) return false;
        return state->db->delete_card(card_id);
    }
    // Partial remove: decrease original and insert into main deckless rows
    if (!state->db->update_quantity(card_id, current_qty - to_remove)) return false;
    return state->db->insert_card(en, ln, ty, lty, cols, setc, mana, rar, to_remove, img, price, -1, foil);
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
    if (!state->db->query("SELECT english_name, localized_name, type, localized_type, colors, set_code, mana_cost, rarity, image_url, price_usd, foil FROM cards WHERE id = ?", [&](const std::map<std::string,std::string>& r){ crow = r; }, std::vector<std::string>{std::to_string(card_id)})) {
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
    int foil = 0;
    if (crow.count("foil")) {
        try { foil = std::stoi(crow["foil"]); } catch(...) { foil = 0; }
    }
    // Use Database::insert_card which will merge into existing identical card in the target deck
    return state->db->insert_card(en, ln, ty, lty, cols, setc, mana, rar, to_move, img, price, target_deck_id, foil, sideboard);
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
            {
                FocusTarget* ft = new FocusTarget();
                ft->entry = entry;
                ft->tries = 12; // increase attempts
                g_timeout_add(100, grab_focus_to_entry, ft);
            }
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
            bool success = state->db->insert_card(card.english_name, card.localized_name, card.type, card.localized_type, card.colors, card.set_name, card.mana_cost, card.rarity, quantity, card.image_url, card.price_usd, -1, foil);
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
            send_notification("Carta aggiunta", body);
            // Reset inputs so user can add another card quickly
            gtk_editable_set_text(GTK_EDITABLE(entry), "");
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1);
            // Try to grab focus now and also schedule the reliable timeout
            gtk_widget_grab_focus(entry);
            FocusTarget* ft = new FocusTarget();
            ft->entry = entry;
            ft->tries = 12; // increase attempts
            g_timeout_add(100, grab_focus_to_entry, ft);
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
                FocusTarget* ft = new FocusTarget();
                ft->entry = (GtkWidget*)user_data;
                ft->tries = 6;
                g_timeout_add(50, grab_focus_to_entry, ft);
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
                bool success = sctx->state->db->insert_card(card.english_name, card.localized_name, card.type, card.localized_type, card.colors, card.set_name, card.mana_cost, card.rarity, sctx->quantity, card.image_url, card.price_usd, -1, foil);
                std::cout << "Inserted card: " << card.english_name << " in set " << card.set_name << " qty " << sctx->quantity << " success: " << success << std::endl;
                if (success) {
                    refresh_card_list(sctx->state);
                    g_main_context_iteration(NULL, FALSE);
                    // Send brief desktop notification instead of showing details dialog
                    std::filesystem::path p(sctx->state->db_path);
                    std::string dbname = p.filename().string();
                    std::string body = card.name + " aggiunta a " + dbname;
                    send_notification("Carta aggiunta", body);
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
                        FocusTarget* ft = new FocusTarget();
                        ft->entry = (GtkWidget*)user_data;
                        ft->tries = 12;
                        g_timeout_add(100, grab_focus_to_entry, ft);
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
                        FocusTarget* ft = new FocusTarget();
                        ft->entry = (GtkWidget*)user_data;
                        ft->tries = 12;
                        g_timeout_add(100, grab_focus_to_entry, ft);
                    }), sctx->original_ctx->entry);
                }
                gtk_window_destroy(GTK_WINDOW(select_dialog));
            }

            if (sctx->original_ctx) {
                // Reset inputs immediately (so the dialog below shows cleared fields)
                gtk_editable_set_text(GTK_EDITABLE(sctx->original_ctx->entry), "");
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(sctx->original_ctx->spin), 1);
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
            gtk_label_set_text(GTK_LABEL(state->db_name_label), db_path.c_str());
                    Database db(db_path);
                    db.execute("CREATE TABLE IF NOT EXISTS cards (id INTEGER PRIMARY KEY, name TEXT, type TEXT, colors TEXT, set_code TEXT, mana_cost TEXT, rarity TEXT, quantity INTEGER, image_url TEXT, added_date TEXT, price_usd TEXT, foil INTEGER DEFAULT 0)");
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
            gtk_label_set_text(GTK_LABEL(state->db_name_label), db_path.c_str());
            // Salva percorso su file
            std::ofstream lastdb("lastdb.txt");
            lastdb << db_path << std::endl;
            // Populate deck menu for the new database
            populate_deck_menu(state);
        }
    Database db(db_path);
    db.execute("CREATE TABLE IF NOT EXISTS cards (id INTEGER PRIMARY KEY, name TEXT, type TEXT, colors TEXT, set_code TEXT, mana_cost TEXT, rarity TEXT, quantity INTEGER, image_url TEXT, added_date TEXT, price_usd TEXT, foil INTEGER DEFAULT 0)");
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
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(box), button_box);
    GtkWidget *ok_button = gtk_button_new_with_label("Esporta");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    struct ExportCtx { AppState* state; GtkWidget* combo; GtkWidget* dialog; };
    ExportCtx* ctx = new ExportCtx{state, combo, dialog};

    g_signal_connect(ok_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        ExportCtx* c = (ExportCtx*)user_data;
        int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(c->combo));
        std::string lang = (idx == 1) ? "en" : "it";
        bool ok = export_cards_to_txt(c->state, false, -1, lang);
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
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(box), button_box);
    GtkWidget *ok_button = gtk_button_new_with_label("Esporta");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    struct ExportDeckCtx { AppState* state; GtkWidget* combo; GtkWidget* dialog; int deck_id; };
    ExportDeckCtx* ctx = new ExportDeckCtx{state, combo, dialog, state->selected_deck_id};

    g_signal_connect(ok_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        ExportDeckCtx* c = (ExportDeckCtx*)user_data;
        int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(c->combo));
        std::string lang = (idx == 1) ? "en" : "it";
        bool ok = export_cards_to_txt(c->state, true, c->deck_id, lang);
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
        GtkWidget* foil_combo;
        GtkWidget* chk_not_in_deck;
    };
    FiltersCtx* ctx = new FiltersCtx{state, dialog, chk_w, chk_u, chk_b, chk_r, chk_g, chk_common, chk_uncommon, chk_rare, chk_mythic, foil_combo, chk_not_in_deck};
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
    // Foil
    int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(c->foil_combo));
    if (idx == 0) st->filter_foil = -1;
    else if (idx == 1) st->filter_foil = 1;
    else st->filter_foil = 0;
        // Deckless filter
        gboolean no_deck_active = gtk_check_button_get_active(GTK_CHECK_BUTTON(c->chk_not_in_deck));
        st->filter_no_deck = no_deck_active ? true : false;
        refresh_card_list(st);
        if (c->dialog) gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), ctx);

    g_signal_connect(clear_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        FiltersCtx* c = (FiltersCtx*)user_data;
        AppState* st = c->state;
        st->filter_colors.clear();
        st->filter_rarities.clear();
        st->filter_foil = -1;
        st->filter_no_deck = false;
        refresh_card_list(st);
        if (c->dialog) gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), ctx);

    g_signal_connect(cancel_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        FiltersCtx* c = (FiltersCtx*)user_data;
        if (c->dialog) gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
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
    on_add_card_clicked(NULL, user_data);
}

static std::string translate_type(const char* type) {
    if (!type) return "";
    std::string t = type;
    if (current_language == "en") return t;
    static std::map<std::string, std::string> translations = {
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
    auto it = translations.find(t);
    if (it != translations.end()) return it->second;
    return t; // Se non trovato, restituisci originale
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    // Carica CSS personalizzato
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "/* Dark, modern palette: deep background, subtle surfaces and a warm accent */\n"
        ".small-popover { padding: 0px; margin: 0px; border: none; background-color: #222426; color: #e6edf3; }\n"
        "window {\n"
        "    background-color: #121316;\n"
        "    color: #e6edf3;\n"
        "    font-family: 'Inter', 'Segoe UI', 'Ubuntu', sans-serif;\n"
        "    font-size: 13px;\n"
        "}\n"
        "/* Top bar */\n"
        ".topbar {\n"
        "    padding: 2px 4px;\n"
        "    min-height: 28px;\n"
        "}\n"
        "/* Buttons */\n"
        "button, menubutton {\n"
        "    border-radius: 8px;\n"
        "    padding: 4px 8px;\n"
        "    background-color: transparent;\n"
        "    color: #e6edf3;\n"
        "    border: 1px solid rgba(230,237,243,0.06);\n"
        "    transition: all 0.14s ease;\n"
        "}\n"
        "button:hover, menubutton:hover {\n"
        "    background-color: rgba(230,237,243,0.03);\n"
        "    transform: translateY(-1px);\n"
        "}\n"
        "/* Entry */\n"
        "entry {\n"
        "    border-radius: 8px;\n"
        "    padding: 4px 8px;\n"
        "    background-color: transparent;\n"
        "    color: #e6edf3;\n"
        "    border: 1px solid rgba(230,237,243,0.06);\n"
        "    transition: all 0.14s ease;\n"
        "}\n"
        "entry:hover {\n"
        "    background-color: rgba(230,237,243,0.03);\n"
        "    transform: translateY(-1px);\n"
        "}\n"
        "/* Column view / table */\n"
        "columnview {\n"
        "    background-color: #17171a;\n"
        "    border: 1px solid rgba(255,255,255,0.04);\n"
        "    border-radius: 8px;\n"
        "    color: #e6edf3;\n"
        "}\n"
        "columnview row {\n"
        "    padding: 8px 12px;\n"
        "}\n"
        "columnview row:nth-child(even) {\n"
        "    background-color: #151518;\n"
        "}\n"
        "columnview row:hover {\n"
        "    background-color: #1f2023;\n"
        "}\n"
    "label { color: #e6edf3; }\n"
    "/* Foil row styling */\n"
    ".foil { color: #D4AF37; }\n"
        "scrolledwindow { box-shadow: 0 6px 18px rgba(0,0,0,0.6); border-radius: 8px; }\n"
        "/* Accent colors: violet + amber for highlights */\n"
        ".accent { color: #b388ff; }\n"
        ".accent-bg { background-color: #3a2a4a; border-radius:6px; padding:2px 6px; }\n"
        "/* Smaller menubutton */\n"
        "menubutton { padding: 6px 10px; border-radius: 6px; }\n"
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
    gtk_box_append(GTK_BOX(db_name_box), db_name_label);

    // Stato globale dell'applicazione
    AppState* state = new AppState;
    state->db_path = "";
    state->db = nullptr;
    state->db_name_label = db_name_label;
    state->total_cards_label = gtk_label_new("Totale carte: 0");
    state->filter_label = gtk_label_new("");
    state->selected_deck_id = -1;
    state->deck_button = NULL;
    state->deck_label = NULL;
    state->deck_delete_button = NULL;
    state->db_button = NULL;
    state->filter_colors.clear();
    state->filter_rarities.clear();
    state->filter_foil = -1;
    state->filter_no_deck = false;

    // Bottone per aggiungere una nuova carta
    GtkWidget *add_card_button = gtk_button_new_with_label("Nuova Carta");
    g_signal_connect(add_card_button, "clicked", G_CALLBACK(on_add_card_clicked), window);

    // Bottone per refresh delle carte
    GtkWidget *refresh_button = gtk_button_new_with_label("Refresh");
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        GtkWindow* window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        if (!state || !state->db) return;
    // Load all cards (respecting current deck filter)
    auto cards = load_cards_from_db(state->db, std::string(""), state->selected_deck_id, state->filter_no_deck);
        int updated = 0;
        for (const auto& row : cards) {
            std::string search_name = row.at("english_name");
            if (search_name.empty()) {
                search_name = row.at("name");  // Fallback to original name if english_name is empty
            }
            if (search_name.empty()) continue;
            auto results = search_cards_from_scryfall(search_name);
            // Find the one with matching set_code
            ScryfallCard* found = nullptr;
            for (auto& card : results) {
                if (card.set_name == row.at("set_code")) {
                    found = &card;
                    break;
                }
            }
            if (found) {
                bool success = state->db->update_card_info(std::stoi(row.at("id")), found->english_name, found->localized_name, found->type, found->localized_type, found->colors, found->mana_cost, found->rarity, found->image_url, found->price_usd);
                if (success) updated++;
                std::cout << "Updated card " << search_name << " success: " << success << std::endl;
            } else {
                std::cout << "Card " << search_name << " not found in Scryfall" << std::endl;
            }
        }
        std::cout << "Refreshed " << updated << " cards" << std::endl;
        refresh_card_list(state);
    }), window);

    // Campo di ricerca
    GtkWidget *search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Cerca per nome...");
    gtk_widget_set_size_request(search_entry, 200, -1);
    g_signal_connect(search_entry, "changed", G_CALLBACK(+[](GtkEditable*, gpointer user_data) {
        GtkWindow* window = GTK_WINDOW(user_data);
        AppState* state = (AppState*)g_object_get_data(G_OBJECT(window), "app_state");
        if (!state) return;
        refresh_card_list(state);
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
    // Ensure menus reflect current language
    rebuild_menus_for_language(state);
    // Populate initial deck menu (if DB already open later we'll re-populate)
    populate_deck_menu(state);

    // Deck indicator / clear-filter button (hidden when not filtering)
    GtkWidget *deck_button = gtk_button_new_with_label("");
    GtkWidget *deck_label = gtk_label_new("");
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
    g_signal_connect(deck_delete_button, "clicked", G_CALLBACK(on_deck_delete_clicked), window);
    state->deck_delete_button = deck_delete_button;

    // Button to return to main Database view (visible when a deck is selected)
    GtkWidget *db_button = gtk_button_new();
    // Use a symbolic 'home' icon so the button visually indicates "back to database"
    GtkWidget *db_icon = gtk_image_new_from_icon_name("go-home-symbolic");
    gtk_button_set_child(GTK_BUTTON(db_button), db_icon);
    gtk_widget_set_visible(db_button, FALSE);
    gtk_widget_set_tooltip_text(db_button, translate("Database").c_str());
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
    g_signal_connect(close_button, "clicked", G_CALLBACK(+[](GtkButton *, gpointer user_data) {
        GtkApplication *app = GTK_APPLICATION(user_data);
        g_application_quit(G_APPLICATION(app));
    }), app);

    // Box per i bottoni
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(button_box), file_button);
    gtk_box_append(GTK_BOX(button_box), view_button);
    gtk_box_append(GTK_BOX(button_box), add_card_button);
    gtk_box_append(GTK_BOX(button_box), refresh_button);
    gtk_box_append(GTK_BOX(button_box), search_entry);
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
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box, GTK_ALIGN_START);
    gtk_widget_set_hexpand(box, FALSE);
    gtk_widget_set_vexpand(box, FALSE);
    gtk_widget_set_margin_end(box, 6);
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
        snprintf(css_buf, sizeof(css_buf), ".color-box { background-color: rgba(%d,%d,%d,1.0); border: 1px solid rgba(0,0,0,0.6); width: 1em; height: 1em; min-width: 1em; min-height: 1em; max-width: 1em; max-height: 1em; border-radius: 2px; display: inline-block; }",
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
    }), NULL);
    GtkColumnViewColumn *color_col = gtk_column_view_column_new(NULL, color_factory);
    gtk_column_view_append_column(column_view, color_col);

    // Colonna Nome
    GtkListItemFactory *name_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(name_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_list_item_set_child(item, label);
    }), NULL);
    g_signal_connect(name_factory, "bind", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        GtkWidget *label = gtk_list_item_get_child(item);
        if (!row) {
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_list_item_set_selectable(item, FALSE);
            return;
        }
        if (row->id == ROW_ID_SEPARATOR_TITLE) {
            // Separator title: centered bold text, non-selectable and styled
            std::string sep = translate("Sideboard");
            std::string markup = "<span weight='bold'>" + sep + "</span>";
            gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.5);
            gtk_widget_set_hexpand(label, TRUE);
            gtk_list_item_set_selectable(item, FALSE);
            // Apply separator CSS class/style to the whole list item so it spans all columns
            gtk_widget_add_css_class(GTK_WIDGET(item), "separator-row");
            ensure_separator_css_provider();
            if (separator_css_provider) {
                gtk_style_context_add_provider(gtk_widget_get_style_context(GTK_WIDGET(item)), GTK_STYLE_PROVIDER(separator_css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
            }
            gtk_widget_remove_css_class(label, "foil");
            return;
        }
        if (row->id == ROW_ID_HEADER) {
            std::string hdr = translate("Nome");
            std::string markup = "<span weight='bold'>" + hdr + "</span>";
            gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_list_item_set_selectable(item, FALSE);
            /* Apply header-row class so the repeated header visually matches the main header */
            gtk_widget_add_css_class(GTK_WIDGET(item), "header-row");
            ensure_separator_css_provider();
            if (separator_css_provider) {
                gtk_style_context_add_provider(gtk_widget_get_style_context(GTK_WIDGET(item)), GTK_STYLE_PROVIDER(separator_css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
            }
            return;
        }
        // Normal row
        gtk_label_set_text(GTK_LABEL(label), row->name ? row->name : "");
        /* Remove any visual-only classes that may have been applied when this ListItem was reused */
        gtk_widget_remove_css_class(GTK_WIDGET(item), "separator-row");
        gtk_widget_remove_css_class(GTK_WIDGET(item), "header-row");
        // Apply foil styling
        if (row->foil) gtk_widget_add_css_class(label, "foil"); else gtk_widget_remove_css_class(label, "foil");
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
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
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *type_col = gtk_column_view_column_new("Tipo", type_factory);
    gtk_column_view_column_set_expand(type_col, TRUE);
    gtk_column_view_column_set_resizable(type_col, FALSE);
    gtk_column_view_append_column(column_view, type_col);
    state->type_col = type_col;
    // gtk_column_view_column_set_sorter(type_col, GTK_SORTER(gtk_string_sorter_new(gtk_property_expression_new(G_TYPE_STRING, card_row_get_type(), "type"))));

    // Colonna Colori
    GtkListItemFactory *colors_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(colors_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.5);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_list_item_set_child(item, label);
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
        char qty[16];
        snprintf(qty, sizeof(qty), "%d", row->quantity);
        gtk_label_set_text(GTK_LABEL(label), qty);
        if (row->foil) gtk_widget_add_css_class(label, "foil"); else gtk_widget_remove_css_class(label, "foil");
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
    state->column_view = column_view;

    // Layout
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // Box superiore: bottoni a sinistra, chiudi a destra (slim top bar)
    GtkWidget *top_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(top_box, "topbar");
    gtk_box_append(GTK_BOX(top_box), button_box);
    gtk_widget_set_hexpand(button_box, TRUE);
    gtk_box_append(GTK_BOX(top_box), close_button);
    gtk_box_append(GTK_BOX(vbox), top_box);

    // Scrolled window con tabella
    gtk_box_append(GTK_BOX(vbox), scrolled_window);

    // Box inferiore: nome database e totale carte
    GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(bottom_box), db_name_box);
    if (state->deck_label) gtk_box_append(GTK_BOX(bottom_box), state->deck_label);
    gtk_box_append(GTK_BOX(bottom_box), state->total_cards_label);
    gtk_box_append(GTK_BOX(bottom_box), state->filter_label);
    gtk_box_append(GTK_BOX(vbox), bottom_box);
    gtk_window_set_child(GTK_WINDOW(window), vbox);
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

    // Carica ultimo database usato
    std::ifstream lastdb("lastdb.txt");
    if (lastdb) {
        std::string path;
        std::getline(lastdb, path);
        if (!path.empty() && std::filesystem::exists(path)) {
            if (state->db) delete state->db;
            state->db = new Database(path);
            state->db_path = path;
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
    GtkApplication *app = gtk_application_new("org.magicdb.collection", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

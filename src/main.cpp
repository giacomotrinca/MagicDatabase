
#include <fstream>
#include <iostream>
#include <iostream>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
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
    self->foil = 0;
}

static std::string translate_colors(const std::string& colors_str) {
    std::string display_colors;
    if (!colors_str.empty()) {
        try {
            auto colors = nlohmann::json::parse(colors_str);
            std::vector<std::string> translated;
            for (const auto& c : colors) {
                std::string code = c;
                if (code == "W") translated.push_back("Bianco");
                else if (code == "U") translated.push_back("Blu");
                else if (code == "B") translated.push_back("Nero");
                else if (code == "R") translated.push_back("Rosso");
                else if (code == "G") translated.push_back("Verde");
                else translated.push_back(code);
            }
            if (translated.empty()) {
                display_colors = "Incolore";
            } else {
                for (size_t i = 0; i < translated.size(); ++i) {
                    if (i > 0) display_colors += "-";
                    display_colors += translated[i];
                }
            }
        } catch (...) {
            display_colors = colors_str;
        }
    } else {
        display_colors = "Incolore";
    }
    return display_colors;
}

static std::string current_language = "it";

static std::string translate_rarity(const std::string& rarity) {
    if (current_language == "en") return rarity;
    static std::map<std::string, std::string> translations = {
        {"common", "Comune"},
        {"uncommon", "Non Comune"},
        {"rare", "Rara"},
        {"mythic", "Mitica"}
    };
    auto it = translations.find(rarity);
    if (it != translations.end()) return it->second;
    return rarity; // fallback
}

static int calculate_total_mana_cost(const std::string& mana_cost) {
    int total_cost = 0;
    if (!mana_cost.empty()) {
        size_t pos = 0;
        while ((pos = mana_cost.find('{', pos)) != std::string::npos) {
            size_t end = mana_cost.find('}', pos);
            if (end != std::string::npos) {
                std::string symbol = mana_cost.substr(pos + 1, end - pos - 1);
                try {
                    int num = std::stoi(symbol);
                    total_cost += num;
                } catch (...) {
                    total_cost += 1;
                }
                pos = end + 1;
            } else {
                break;
            }
        }
    }
    return total_cost;
}

static std::string format_datetime(const std::string& iso) {
    if (iso.empty()) return "";
    struct tm tm{};
    // Parse ISO YYYY-MM-DDTHH:MM:SS
    if (strptime(iso.c_str(), "%Y-%m-%dT%H:%M:%S", &tm) == nullptr) {
        return iso;
    }
    // Normalize to time_t to get weekday
    time_t tt = mktime(&tm);
    if (tt == (time_t)-1) return iso;
    struct tm local_tm{};
#if defined(_MSC_VER)
    localtime_s(&local_tm, &tt);
#else
    localtime_r(&tt, &local_tm);
#endif
    char buf[256];
    if (current_language == "en") {
        const char* weekdays_en[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        const char* months_en[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
        snprintf(buf, sizeof(buf), "%s %02d %s %d %02d:%02d:%02d",
                 weekdays_en[local_tm.tm_wday], local_tm.tm_mday, months_en[local_tm.tm_mon], 1900 + local_tm.tm_year,
                 local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
    } else {
        const char* weekdays_it[] = {"dom","lun","mar","mer","gio","ven","sab"};
        const char* months_it[] = {"gennaio","febbraio","marzo","aprile","maggio","giugno","luglio","agosto","settembre","ottobre","novembre","dicembre"};
        snprintf(buf, sizeof(buf), "%s %02d %s %d %02d:%02d:%02d",
                 weekdays_it[local_tm.tm_wday], local_tm.tm_mday, months_it[local_tm.tm_mon], 1900 + local_tm.tm_year,
                 local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
    }
    return std::string(buf);
}

CardRow* card_row_new(int id, const char* name, const char* type, const char* colors, const char* set_code, const char* mana_cost, const char* rarity, int quantity, const char* image_url, const char* added_date, const char* price_usd, int foil) {
    CardRow* row = (CardRow*)g_object_new(card_row_get_type(), NULL);
    row->id = id;
    row->name = g_strdup(name);
    row->type = g_strdup(type);
    row->colors = g_strdup(colors);
    row->set_code = g_strdup(set_code);
    row->mana_cost = g_strdup(mana_cost);
    std::string trans_rarity = translate_rarity(rarity ? rarity : "");
    row->rarity = g_strdup(trans_rarity.c_str());
    row->quantity = quantity;
    std::string trans = translate_colors(colors ? colors : "");
    row->translated_colors = g_strdup(trans.c_str());
    row->total_mana_cost = calculate_total_mana_cost(mana_cost ? mana_cost : "");
    row->image_url = g_strdup(image_url);
    row->added_date = g_strdup(added_date ? added_date : "");
    row->price_usd = g_strdup(price_usd ? price_usd : "");
    row->foil = foil;
    return row;
}

static std::map<std::string, std::map<std::string, std::string>> translations = {
    {"Nome", {{"it", "Nome"}, {"en", "Name"}}},
    {"Tipo", {{"it", "Tipo"}, {"en", "Type"}}},
    {"Colori", {{"it", "Colori"}, {"en", "Colors"}}},
    {"Costo Mana", {{"it", "Costo Mana"}, {"en", "Mana Cost"}}},
    {"Rarità", {{"it", "Rarità"}, {"en", "Rarity"}}},
    {"Data di aggiunta", {{"it", "Data di aggiunta"}, {"en", "Added Date"}}},
    {"Quantità", {{"it", "Quantità"}, {"en", "Quantity"}}},
    {"Nuova Carta", {{"it", "Nuova Carta"}, {"en", "New Card"}}},
    {"Cerca per nome...", {{"it", "Cerca per nome..."}, {"en", "Search by name..."}}},
    {"File", {{"it", "File"}, {"en", "File"}}},
    {"Visualizza", {{"it", "Visualizza"}, {"en", "View"}}},
    {"Lingua", {{"it", "Lingua"}, {"en", "Language"}}},
    {"Nuovo Database", {{"it", "Nuovo Database"}, {"en", "New Database"}}},
    {"Apri Database", {{"it", "Apri Database"}, {"en", "Open Database"}}},
    {"Crea Deck", {{"it", "Crea Deck"}, {"en", "Create Deck"}}},
    {"Esporta Database", {{"it", "Esporta Database"}, {"en", "Export Database"}}},
    {"Esporta Deck", {{"it", "Esporta Deck"}, {"en", "Export Deck"}}},
    {"Seleziona Deck", {{"it", "Seleziona Deck"}, {"en", "Select Deck"}}},
    {"Filtri...", {{"it", "Filtri..."}, {"en", "Filters..."}}},
    {"Elimina Deck", {{"it", "Elimina Deck"}, {"en", "Delete Deck"}}},
    {"Torna al Database Principale", {{"it", "Torna al Database Principale"}, {"en", "Back to Main Database"}}},
    {"Ordina Crescente", {{"it", "Ordina Crescente"}, {"en", "Sort Ascending"}}},
    {"Ordina Decrescente", {{"it", "Ordina Decrescente"}, {"en", "Sort Descending"}}},
    {"Elimina", {{"it", "Elimina"}, {"en", "Delete"}}},
    {"Totale carte", {{"it", "Totale carte"}, {"en", "Total cards"}}},
    {"Valore totale", {{"it", "Valore totale"}, {"en", "Total Value"}}}
};

std::string translate(const std::string& key) {
    auto it = translations.find(key);
    if (it != translations.end()) {
        auto lang_it = it->second.find(current_language);
        if (lang_it != it->second.end()) return lang_it->second;
    }
    return key;
}

static std::string translate_colors(const char* colors) {
    if (!colors) return "";
    std::string c = colors;
    std::map<std::string, std::string> color_trans;
    if (current_language == "it") {
        color_trans = {
            {"W", "Bianco"},
            {"U", "Blu"},
            {"B", "Nero"},
            {"R", "Rosso"},
            {"G", "Verde"}
        };
    } else if (current_language == "en") {
        color_trans = {
            {"W", "White"},
            {"U", "Blue"},
            {"B", "Black"},
            {"R", "Red"},
            {"G", "Green"}
        };
    } else {
        // Default to original
        std::string result;
        for (char ch : c) {
            result += ch;
            result += " ";
        }
        if (!result.empty()) result.pop_back();
        return result;
    }
    std::string result;
    for (char ch : c) {
        std::string s(1, ch);
        auto it = color_trans.find(s);
        if (it != color_trans.end()) result += it->second + " ";
        else result += s + " ";
    }
    if (!result.empty()) result.pop_back();
    return result;
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
    GMenu *deck_menu;
    GMenu *file_menu;
    GMenu *view_menu;
    // Filters
    std::set<std::string> filter_colors; // set of color codes, e.g. "W", "U"
    std::set<std::string> filter_rarities; // set of rarities: "common","uncommon","rare","mythic"
    int filter_foil; // -1 = any, 0 = non-foil only, 1 = foil only
};

// Forward declaration for refresh used by deck helpers
void refresh_card_list(AppState* state);
// forward-declare populate_deck_menu so helpers can call it
static void populate_deck_menu(AppState *state);
// Forward declaration for card-to-deck helper
static bool add_card_to_deck(AppState* state, int card_id, int to_move, int target_deck_id);
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
        filename = std::string("data/tot_database.txt");
    }

    std::ofstream out(filename);
    if (!out) return false;

    // Header
    out << "id\tname\ttype\tcolors\tset_code\tmana_cost\trarity\tquantity\tadded_date\tprice_usd\tfoil\n";

    // Build SQL
    std::string sql = "SELECT id, english_name, localized_name, name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, added_date, price_usd, foil FROM cards";
    std::vector<std::string> params;
    if (deck) {
        sql += " WHERE deck_id = ?";
        params.push_back(std::to_string(deck_id));
    }
    state->db->query(sql, [&](const std::map<std::string,std::string>& row) {
        std::string name = row.count("english_name") && !row.at("english_name").empty() ? row.at("english_name") : row.at("name");
        std::string lname = row.count("localized_name") && !row.at("localized_name").empty() ? row.at("localized_name") : row.at("name");
        std::string type_en = row.count("type") ? row.at("type") : "";
        std::string type_local = row.count("localized_type") && !row.at("localized_type").empty() ? row.at("localized_type") : type_en;
        std::string out_name = (lang == "en") ? name : lname;
        std::string out_type = (lang == "en") ? type_en : type_local;
        out << row.at("id") << '\t'
            << out_name << '\t'
            << out_type << '\t'
            << (row.count("colors") ? row.at("colors") : "") << '\t'
            << (row.count("set_code") ? row.at("set_code") : "") << '\t'
            << (row.count("mana_cost") ? row.at("mana_cost") : "") << '\t'
            << (row.count("rarity") ? row.at("rarity") : "") << '\t'
            << (row.count("quantity") ? row.at("quantity") : "0") << '\t'
            << (row.count("added_date") ? row.at("added_date") : "") << '\t'
            << (row.count("price_usd") ? row.at("price_usd") : "") << '\t'
            << (row.count("foil") ? row.at("foil") : "0") << '\n';
    }, params);

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
    // Iterate rows in the list_box and for each checked item perform the add/split via helper
    GtkWidget* row = gtk_widget_get_first_child(GTK_WIDGET(list_box));
    while (row) {
        int card_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "card_id"));
        GtkWidget* hbox = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
        if (!hbox) { row = gtk_widget_get_next_sibling(row); continue; }
        gboolean checked = FALSE;
        int to_move = 0;
        // First try to obtain check and spin pointers stored on the row (more reliable)
        GtkWidget* check = (GtkWidget*)g_object_get_data(G_OBJECT(row), "check");
        GtkWidget* spin = (GtkWidget*)g_object_get_data(G_OBJECT(row), "spin");
        // Prefer an explicit persisted 'selected' flag if present (set on press/activate)
        gpointer selptr = g_object_get_data(G_OBJECT(row), "selected");
        if (selptr) {
            checked = GPOINTER_TO_INT(selptr) != 0;
            std::cout << "DEBUG: using persisted selected flag=" << (checked ? "true" : "false") << " for row=" << row << std::endl;
        } else if (check) {
            std::cout << "DEBUG: found stored check ptr=" << check << " for row=" << row;
            if (G_IS_OBJECT(check)) std::cout << " type=" << g_type_name_from_instance((GTypeInstance*)check);
            std::cout << std::endl;
            // Use GObject property access for 'active' to be robust across widget types
            gboolean act = FALSE;
            g_object_get(G_OBJECT(check), "active", &act, NULL);
            checked = act;
        }
        if (spin) {
            std::cout << "DEBUG: found stored spin ptr=" << spin << " for row=" << row << std::endl;
            if (GTK_IS_SPIN_BUTTON(spin)) {
                to_move = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
            }
        }
        // Fallback: iterate children if no stored widgets found
        if (!check || !spin) {
            GtkWidget* child = gtk_widget_get_first_child(hbox);
            while (child) {
                std::cout << "DEBUG: child ptr=" << child;
                if (G_IS_OBJECT(child)) std::cout << " type=" << g_type_name_from_instance((GTypeInstance*)child);
                std::cout << std::endl;
                // Check for an 'active' property on the child to detect toggle buttons
                if (!check) {
                    GParamSpec* ps = g_object_class_find_property(G_OBJECT_GET_CLASS(child), "active");
                    if (ps) {
                        gboolean act = FALSE;
                        g_object_get(G_OBJECT(child), "active", &act, NULL);
                        checked = act;
                    }
                }
                if (!spin && GTK_IS_SPIN_BUTTON(child)) {
                    to_move = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(child));
                }
                child = gtk_widget_get_next_sibling(child);
            }
        }
    // Always print row details for debugging; this helps determine if user actually
    // checked any rows or if the dialog had zero selectable rows.
    std::cout << "DEBUG: row details -> card_id=" << card_id << ", checked=" << (checked ? "true" : "false") << ", to_move=" << to_move << std::endl;
        if (checked && to_move > 0) {
            std::cout << "DEBUG: attempting to add card_id=" << card_id << " to deck=" << state->selected_deck_id << " qty=" << to_move << std::endl;
            bool ok = add_card_to_deck(state, card_id, to_move, state->selected_deck_id);
            std::cout << "DEBUG: add_card_to_deck returned " << ok << " for card_id=" << card_id << std::endl;
            // After processing, clear persisted selection so dialog state is fresh next time
            g_object_set_data(G_OBJECT(row), "selected", NULL);
        }
        // Advance to next row in the list box
        row = gtk_widget_get_next_sibling(row);
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
        // use action app.select_deck_id with int param
        GMenuItem *item = g_menu_item_new(name.c_str(), NULL);
        g_menu_item_set_action_and_target_value(item, "app.select_deck_id", g_variant_new_int32(std::stoi(id)));
        g_menu_append_item(state->deck_menu, item);
        g_object_unref(item);
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
    refresh_card_list(state);
}


static void update_ui_texts(GtkWindow *window, AppState* state) {
    gtk_column_view_column_set_title(state->name_col, translate("Nome").c_str());
    gtk_column_view_column_set_title(state->type_col, translate("Tipo").c_str());
    gtk_column_view_column_set_title(state->colors_col, translate("Colori").c_str());
    gtk_column_view_column_set_title(state->mana_col, translate("Costo Mana").c_str());
    gtk_column_view_column_set_title(state->rarity_col, translate("Rarità").c_str());
    gtk_column_view_column_set_title(state->date_col, translate("Data di aggiunta").c_str());
    gtk_column_view_column_set_title(state->qty_col, translate("Quantità").c_str());
    gtk_button_set_label(GTK_BUTTON(state->add_card_button), translate("Nuova Carta").c_str());
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

// Dialog per ricerca carta Scryfall
struct AddCardContext {
    GtkWidget* entry;
    GtkWidget* spin;
    AppState* state;
    GtkWindow* parent;
    GtkWidget* foil_checkbox;
};

// Funzione per caricare le carte dal database
static std::vector<std::map<std::string, std::string>> load_cards_from_db(Database* db, const std::string& filter = "", int deck_filter = -1) {
    std::vector<std::map<std::string, std::string>> cards;
    if (!db) return cards;
    // Build SQL and optional params for deck filtering
    std::string sql = "SELECT id, english_name, localized_name, name, type, localized_type, colors, set_code, mana_cost, rarity, quantity, image_url, added_date, price_usd, foil FROM cards";
    std::vector<std::string> params;
    if (deck_filter != -1) {
        sql += " WHERE deck_id = ?";
        params.push_back(std::to_string(deck_filter));
    }
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
    auto cards = load_cards_from_db(state->db, filter, state->selected_deck_id);
    std::cout << "Loading " << cards.size() << " cards from db" << std::endl;
    int total_quantity = 0;
    double total_value = 0.0;
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
            // Require exact match of the selected colors set: a card is shown only if its
            // color set equals the selected set. This means selecting "W" will show only
            // mono-white cards; a card ["W","R"] will be shown only when both W and R
            // are selected.
            if (card_colors != state->filter_colors) continue;
        }
        // Rarity (compare lower-case tokens)
        if (!state->filter_rarities.empty()) {
            std::string card_rarity = row.count("rarity") ? row.at("rarity") : "";
            std::transform(card_rarity.begin(), card_rarity.end(), card_rarity.begin(), ::tolower);
            if (state->filter_rarities.count(card_rarity) == 0) continue;
        }
        // Foil
        if (state->filter_foil != -1) {
            if (state->filter_foil != foil) continue;
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
        } catch (...) {
            // ignore parse errors
        }
    }
    // Update total cards label (only total quantity)
    char buf[128];
    snprintf(buf, sizeof(buf), "Totale carte: %d", total_quantity);
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
static bool add_card_to_deck(AppState* state, int card_id, int to_move, int target_deck_id) {
    if (!state || !state->db) return false;
    int current_qty = 0;
    if (!state->db->get_card_quantity(card_id, current_qty)) return false;
    if (to_move <= 0) return false;
    if (to_move >= current_qty) {
        return state->db->set_card_deck(card_id, target_deck_id);
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
    return state->db->insert_card(en, ln, ty, lty, cols, setc, mana, rar, to_move, img, price, target_deck_id, foil);
}

static void on_add_card_ok_clicked(GtkButton *button, gpointer user_data) {
    AddCardContext* ctx = (AddCardContext*)user_data;
    GtkWidget *entry = ctx->entry;
    GtkWidget *spin = ctx->spin;
    AppState* state = ctx->state;
    const char *card_name = gtk_editable_get_text(GTK_EDITABLE(entry));
    int quantity = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
    
    auto cards = search_cards_from_scryfall(card_name);
    
    if (cards.empty()) {
    GtkWindow* parent = ctx->parent;
    GtkWidget *anc = gtk_widget_get_ancestor(ctx->entry, GTK_TYPE_WINDOW);
    if (anc && GTK_IS_WIDGET(anc)) gtk_window_destroy(GTK_WINDOW(anc));
    delete ctx;
        std::string msg = "Nessuna carta trovata per: " + std::string(card_name);
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", msg.c_str());
        gtk_alert_dialog_show(alert, parent);
        g_object_unref(alert);
    } else if (cards.size() == 1 && cards[0].is_exact_match) {
        // Carta singola trovata esattamente
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
        
    GtkWindow* parent = ctx->parent;
    GtkWidget *anc = gtk_widget_get_ancestor(ctx->entry, GTK_TYPE_WINDOW);
    if (anc && GTK_IS_WIDGET(anc)) gtk_window_destroy(GTK_WINDOW(anc));
    delete ctx;
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", msg.c_str());
        gtk_alert_dialog_show(alert, parent);
        g_object_unref(alert);
    } else {
    // Multiple corrispondenze - mostra finestra di dialogo per scegliere
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

        // Aggiungi ogni carta alla lista
        for (size_t i = 0; i < cards.size(); ++i) {
            const auto& card = cards[i];
            std::string display_text = card.name + " (" + card.set_name + ") - " + card.type;
            GtkWidget *row = gtk_list_box_row_new();
            GtkWidget *label = gtk_label_new(display_text.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
            gtk_list_box_append(GTK_LIST_BOX(list_box), row);
            
            // Salva l'indice della carta nei dati del row
            g_object_set_data(G_OBJECT(row), "card_index", (gpointer)i);
        }

        GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_append(GTK_BOX(box), button_box);

        GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
        gtk_box_append(GTK_BOX(button_box), cancel_button);

        // Crea un contesto per il dialogo di selezione
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
            SelectCardContext* ctx = (SelectCardContext*)user_data;
            // Chiudi la finestra di selezione
            GtkWidget* select_dialog = gtk_widget_get_ancestor(GTK_WIDGET(ctx->list_box), GTK_TYPE_WINDOW);
            if (select_dialog && GTK_IS_WIDGET(select_dialog)) {
                gtk_window_destroy(GTK_WINDOW(select_dialog));
            }
            // Riapri la finestra di ricerca originale (non chiuderla)
            delete ctx->cards;
            delete ctx;
        }), select_ctx);

        // Gestisci doppio click sulla riga
        g_signal_connect(list_box, "row-activated", G_CALLBACK(+[](GtkWidget* list_box, GtkListBoxRow* row, gpointer user_data) {
            SelectCardContext* ctx = (SelectCardContext*)user_data;
            size_t index = (size_t)g_object_get_data(G_OBJECT(row), "card_index");
            
            if (index < ctx->cards->size()) {
                auto& card = (*ctx->cards)[index];
                
                std::string msg = "Nome: " + card.name + "\n";
                msg += "Tipo: " + card.type + "\n";
                msg += "Colori: " + card.colors + "\n";
                msg += "Set: " + card.set_name + "\n";
                msg += "Costo Mana: " + card.mana_cost + "\n";
                msg += "Rarità: " + card.rarity + "\n";
                msg += "Testo: " + card.oracle_text;
                
                if (ctx->state && ctx->state->db) {
                    int foil = 0;
                    if (ctx->original_ctx && ctx->original_ctx->foil_checkbox) {
                        gboolean f = FALSE;
                        g_object_get(G_OBJECT(ctx->original_ctx->foil_checkbox), "active", &f, NULL);
                        foil = f ? 1 : 0;
                    }
                    bool success = ctx->state->db->insert_card(card.english_name, card.localized_name, card.type, card.localized_type, card.colors, card.set_name, card.mana_cost, card.rarity, ctx->quantity, card.image_url, card.price_usd, -1, foil);
                    std::cout << "Inserted card: " << card.english_name << " in set " << card.set_name << " qty " << ctx->quantity << " success: " << success << std::endl;
                    if (success) {
                        refresh_card_list(ctx->state);
                        g_main_context_iteration(NULL, FALSE);
                        msg += "\n\n[Salvata nel database]";
                    } else {
                        msg += "\n\n[Errore nel salvare]";
                    }
                } else {
                    msg += "\n\n[ERRORE: Nessun database aperto]";
                }
                
                GtkAlertDialog *alert = gtk_alert_dialog_new("%s", msg.c_str());
                gtk_alert_dialog_show(alert, ctx->parent);
                g_object_unref(alert);
                
                // Chiudi entrambe le finestre
                GtkWidget* search_dialog = gtk_widget_get_ancestor(ctx->original_ctx->entry, GTK_TYPE_WINDOW);
                GtkWidget* select_dialog = gtk_widget_get_ancestor(ctx->list_box, GTK_TYPE_WINDOW);
                if (search_dialog && GTK_IS_WIDGET(search_dialog)) {
                    gtk_window_destroy(GTK_WINDOW(search_dialog));
                }
                if (select_dialog && GTK_IS_WIDGET(select_dialog)) {
                    gtk_window_destroy(GTK_WINDOW(select_dialog));
                }
                delete ctx->original_ctx;
            }
            
            delete ctx->cards;
            delete ctx;
        }), select_ctx);

        gtk_window_present(GTK_WINDOW(dialog));
    }
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
            GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(roww), hbox);

            GtkWidget *check = gtk_check_button_new();
            gtk_box_append(GTK_BOX(hbox), check);
            g_signal_connect(check, "toggled", G_CALLBACK(on_deck_row_check_toggled), roww);
            GtkWidget *label = gtk_label_new(display.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_box_append(GTK_BOX(hbox), label);
            GtkWidget *spin = gtk_spin_button_new_with_range(1, qty, 1);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1);
            gtk_box_append(GTK_BOX(hbox), spin);

            // Store the card id on the row so we can read it later from the list box
            g_object_set_data(G_OBJECT(roww), "card_id", GINT_TO_POINTER(cid));
            // Save checkbox and spin on the row so handlers can toggle/inspect them
            g_object_set_data(G_OBJECT(roww), "check", check);
            g_object_set_data(G_OBJECT(roww), "spin", spin);
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
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Nome carta (italiano o inglese, ricerca parziale supportata)");
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
    struct AddToDeckCtx { AppState* state; GtkWidget* list_box; GtkWidget* dialog; int card_id; };
    AddToDeckCtx* ctx = new AddToDeckCtx{state, list_box, dialog, card_id};
    g_signal_connect(ok_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        AddToDeckCtx* c = (AddToDeckCtx*)user_data;
        GtkListBoxRow* sel = gtk_list_box_get_selected_row(GTK_LIST_BOX(c->list_box));
        if (sel) {
            char* idstr = (char*)g_object_get_data(G_OBJECT(sel), "deck_id");
            if (idstr) {
                int deck_id = atoi(idstr);
                // Use helper to perform move (handles splitting if needed)
                int current_qty = 0;
                if (c->state->db->get_card_quantity(c->card_id, current_qty)) {
                    // move entire quantity
                    if (add_card_to_deck(c->state, c->card_id, current_qty, deck_id)) {
                        std::cout << "Moved card " << c->card_id << " to deck " << deck_id << std::endl;
                    }
                }
            }
        }
        refresh_card_list(c->state);
        gtk_window_destroy(GTK_WINDOW(c->dialog));
        delete c;
    }), ctx);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        AddToDeckCtx* c = (AddToDeckCtx*)user_data;
        gtk_window_destroy(GTK_WINDOW(c->dialog));
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
    };

    FiltersCtx* ctx = new FiltersCtx{state, dialog, chk_w, chk_u, chk_b, chk_r, chk_g, chk_common, chk_uncommon, chk_rare, chk_mythic, foil_combo};
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
    state->filter_colors.clear();
    state->filter_rarities.clear();
    state->filter_foil = -1;

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
    auto cards = load_cards_from_db(state->db, std::string(""), state->selected_deck_id);
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
        gtk_label_set_text(GTK_LABEL(label), row->name ? row->name : "");
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
        gtk_label_set_text(GTK_LABEL(label), row->rarity ? row->rarity : "");
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

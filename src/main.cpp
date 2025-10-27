
#include <fstream>
#include <iostream>
#include <iostream>
#include <gtk/gtk.h>
#include <string>
#include <map>
#include <vector>
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

static std::string translate_rarity(const std::string& rarity) {
    if (rarity == "common") return "Comune";
    if (rarity == "uncommon") return "Non Comune";
    if (rarity == "rare") return "Rara";
    if (rarity == "mythic") return "Mitica";
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

CardRow* card_row_new(int id, const char* name, const char* type, const char* colors, const char* set_code, const char* mana_cost, const char* rarity, int quantity, const char* image_url) {
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
    return row;
}
struct AppState {
    std::string db_path;
    Database* db;
    GtkWidget* db_name_label;
    GListStore* card_store;
    GtkColumnView* column_view;
    GtkSelectionModel* selection;
};

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
};

// Funzione per caricare le carte dal database
static std::vector<std::map<std::string, std::string>> load_cards_from_db(Database* db) {
    std::vector<std::map<std::string, std::string>> cards;
    if (!db) return cards;
    db->query("SELECT id, name, type, colors, set_code, mana_cost, rarity, quantity, image_url FROM cards", [&](const std::map<std::string, std::string>& row) {
        cards.push_back(row);
    });
    return cards;
}

// Funzione globale per aggiornare la lista carte (GListStore)
void refresh_card_list(AppState* state) {
    if (!state || !state->card_store) return;
    g_list_store_remove_all(state->card_store);
    if (!state->db) return;
    auto cards = load_cards_from_db(state->db);
    std::cout << "Loading " << cards.size() << " cards from db" << std::endl;
    for (const auto& row : cards) {
        std::cout << "Card: " << row.at("name") << ", " << row.at("set_code") << ", " << row.at("quantity") << std::endl;
        CardRow* crow = card_row_new(std::stoi(row.at("id")),
                                     row.at("name").c_str(),
                                     row.at("type").c_str(),
                                     row.at("colors").c_str(),
                                     row.at("set_code").c_str(),
                                     row.at("mana_cost").c_str(),
                                     row.at("rarity").c_str(),
                                     std::stoi(row.at("quantity")),
                                     row.at("image_url").c_str());
        g_list_store_append(state->card_store, crow);
        g_object_unref(crow);
    }
    // Update the columnview model
    // GtkSelectionModel *selection = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(state->card_store)));
    // gtk_column_view_set_model(state->column_view, selection);
    // g_object_unref(selection);
    // Force redraw
    gtk_widget_queue_draw(GTK_WIDGET(state->column_view));
    // gtk_widget_queue_draw(state->listview);
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
        gtk_window_destroy(GTK_WINDOW(gtk_widget_get_ancestor(ctx->entry, GTK_TYPE_WINDOW)));
        delete ctx;
        std::string msg = "Nessuna carta trovata per: " + std::string(card_name);
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", msg.c_str());
        gtk_alert_dialog_show(alert, parent);
        g_object_unref(alert);
    } else if (cards.size() == 1 && cards[0].is_exact_match) {
        // Carta singola trovata esattamente
        auto& card = cards[0];
        std::string msg = "Nome: " + card.name + "\n";
        msg += "Tipo: " + card.type + "\n";
        msg += "Colori: " + card.colors + "\n";
        msg += "Set: " + card.set_name + "\n";
        msg += "Costo Mana: " + card.mana_cost + "\n";
        msg += "Rarità: " + card.rarity + "\n";
        msg += "Testo: " + card.oracle_text;
        
        if (state && state->db) {
            bool success = state->db->insert_card(card.name, card.type, card.colors, card.set_name, card.mana_cost, card.rarity, quantity, card.image_url);
            std::cout << "Inserted card: " << card.name << " in set " << card.set_name << " qty " << quantity << " success: " << success << std::endl;
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
        gtk_window_destroy(GTK_WINDOW(gtk_widget_get_ancestor(ctx->entry, GTK_TYPE_WINDOW)));
        delete ctx;
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", msg.c_str());
        gtk_alert_dialog_show(alert, parent);
        g_object_unref(alert);
    } else {
        // Multiple corrispondenze - mostra finestra di dialogo per scegliere
        GtkWidget *dialog = gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(dialog), "Seleziona Carta");
        gtk_window_set_modal(GTK_WINDOW(dialog), true);
        gtk_window_set_transient_for(GTK_WINDOW(dialog), ctx->parent);
        gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);

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
            if (select_dialog) {
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
                    bool success = ctx->state->db->insert_card(card.name, card.type, card.colors, card.set_name, card.mana_cost, card.rarity, ctx->quantity, card.image_url);
                    std::cout << "Inserted card: " << card.name << " in set " << card.set_name << " qty " << ctx->quantity << " success: " << success << std::endl;
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
                if (search_dialog) {
                    gtk_window_destroy(GTK_WINDOW(search_dialog));
                }
                if (select_dialog) {
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
            std::string db_path = "data/collection.db";
            if (state->db) delete state->db;
            state->db = new Database(db_path);
            state->db_path = db_path;
            gtk_label_set_text(GTK_LABEL(state->db_name_label), db_path.c_str());
            Database db(db_path);
            db.execute("CREATE TABLE IF NOT EXISTS cards (id INTEGER PRIMARY KEY, name TEXT, type TEXT, colors TEXT, set_code TEXT, mana_cost TEXT, rarity TEXT, quantity INTEGER, image_url TEXT)");
            std::ofstream lastdb("lastdb.txt");
            lastdb << db_path << std::endl;
            refresh_card_list(state);
        } else {
            // Mostra dialogo per scegliere database
            GtkWidget *dialog = gtk_window_new();
            gtk_window_set_title(GTK_WINDOW(dialog), "Scegli Database");
            gtk_window_set_modal(GTK_WINDOW(dialog), true);
            gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
            gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 200);
            
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
            return;  // Non continuare se stiamo mostrando il dialogo di selezione
        }
    }
    
    // Ora che abbiamo un database aperto, mostra il dialogo di ricerca
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Cerca carta su Scryfall");
    gtk_window_set_modal(GTK_WINDOW(dialog), true);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 350, 100);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Nome carta (italiano o inglese, ricerca parziale supportata)");
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget *spin = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1);
    gtk_box_append(GTK_BOX(box), spin);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), button_box);
    GtkWidget *ok_button = gtk_button_new_with_label("Cerca");
    GtkWidget *cancel_button = gtk_button_new_with_label("Annulla");
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    // Imposta pulsante OK come default
    gtk_window_set_default_widget(GTK_WINDOW(dialog), ok_button);

    // Recupera lo stato globale
    AddCardContext* ctx = new AddCardContext{entry, spin, state, parent};
    g_signal_connect(ok_button, "clicked", G_CALLBACK(on_add_card_ok_clicked), ctx);
    g_signal_connect_swapped(cancel_button, "clicked", G_CALLBACK(gtk_window_destroy), dialog);

    // Collega activate dell'entry al click su OK
    g_signal_connect(entry, "activate", G_CALLBACK(on_add_card_ok_clicked), ctx);

    gtk_window_present(GTK_WINDOW(dialog));
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
        }
        Database db(db_path);
        db.execute("CREATE TABLE IF NOT EXISTS cards (id INTEGER PRIMARY KEY, name TEXT, type TEXT, colors TEXT, set_code TEXT, mana_cost TEXT, rarity TEXT, quantity INTEGER, image_url TEXT)");
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
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Nuovo Database");
    gtk_window_set_modal(GTK_WINDOW(dialog), true);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 100, 100);

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
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_modal(GTK_WINDOW(window), FALSE);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(gtk_widget_get_ancestor(label, GTK_TYPE_WINDOW)));
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
    AppState *state = (AppState*)g_object_get_data(G_OBJECT(gtk_widget_get_root(column_view)), "app_state");
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
    if (qty == 1) {
        if (state->db->delete_card(id)) {
            std::cout << "Deleted card with id " << id << std::endl;
            refresh_card_list(state);
        } else {
            std::cout << "Error deleting card" << std::endl;
        }
    } else {
        // Mostra dialogo per scegliere quante eliminare
        GtkWidget *dialog = gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(dialog), "Elimina Carte");
        gtk_window_set_modal(GTK_WINDOW(dialog), true);
        gtk_window_set_transient_for(GTK_WINDOW(dialog), window);
        gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 100);

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
            if (to_delete >= ctx->qty) {
                // Elimina completamente
                if (ctx->state->db->delete_card(ctx->id)) {
                    std::cout << "Deleted card with id " << ctx->id << std::endl;
                } else {
                    std::cout << "Error deleting card" << std::endl;
                }
            } else {
                // Aggiorna quantità
                int new_qty = ctx->qty - to_delete;
                if (ctx->state->db->update_quantity(ctx->id, new_qty)) {
                    std::cout << "Updated card id " << ctx->id << " quantity to " << new_qty << std::endl;
                } else {
                    std::cout << "Error updating quantity" << std::endl;
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

static std::string translate_type(const char* type) {
    if (!type) return "";
    std::string t = type;
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

    // Menu model per il GtkMenuButton

    GMenu *file_menu = g_menu_new();
    g_menu_append(file_menu, "Nuovo Database", "app.newdb");
    g_menu_append(file_menu, "Apri Database", "app.opendb");

    GtkWidget *file_button = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(file_button), "File");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(file_button), G_MENU_MODEL(file_menu));


    // Box per il nome del database attualmente aperto
    GtkWidget *db_name_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *db_name_label = gtk_label_new("Nessun database aperto");
    gtk_box_append(GTK_BOX(db_name_box), db_name_label);

    // Stato globale dell'applicazione
    AppState* state = new AppState;
    state->db_path = "";
    state->db = nullptr;
    state->db_name_label = db_name_label;

    // Bottone per aggiungere una nuova carta
    GtkWidget *add_card_button = gtk_button_new_with_label("Nuova Carta");
    g_signal_connect(add_card_button, "clicked", G_CALLBACK(on_add_card_clicked), window);

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
    gtk_box_append(GTK_BOX(button_box), add_card_button);

    // Tabella carte (GtkColumnView)
    state->card_store = g_list_store_new(card_row_get_type());
    GtkColumnView *column_view = GTK_COLUMN_VIEW(gtk_column_view_new(NULL));
    gtk_column_view_set_model(column_view, GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(state->card_store))));

    // Colonna Colore (quadratino)
    GtkListItemFactory *color_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(color_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(box, "color-box");
        gtk_widget_set_size_request(box, 14, 14);
        gtk_list_item_set_child(item, box);
    }), NULL);
    g_signal_connect(color_factory, "bind", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        GtkWidget *box = gtk_list_item_get_child(item);
        // Calcola colore basato su row->colors
        GdkRGBA color = get_color_for_mana(row->colors ? row->colors : "");
        char css_buf[256];
        snprintf(css_buf, sizeof(css_buf), ".color-box { background-color: rgba(%d,%d,%d,1.0); border: 1px solid black; }",
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
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *name_col = gtk_column_view_column_new("Nome", name_factory);
    gtk_column_view_column_set_expand(name_col, TRUE);
    gtk_column_view_append_column(column_view, name_col);
    // gtk_column_view_column_set_sorter(name_col, GTK_SORTER(gtk_string_sorter_new(gtk_property_expression_new(G_TYPE_STRING, card_row_get_type(), "name"))));

    // Colonna Tipo
    GtkListItemFactory *type_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(type_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_list_item_set_child(item, label);
    }), NULL);
    g_signal_connect(type_factory, "bind", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        GtkWidget *label = gtk_list_item_get_child(item);
        std::string translated = translate_type(row->type);
        gtk_label_set_text(GTK_LABEL(label), translated.c_str());
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *type_col = gtk_column_view_column_new("Tipo", type_factory);
    gtk_column_view_column_set_expand(type_col, TRUE);
    gtk_column_view_append_column(column_view, type_col);
    // gtk_column_view_column_set_sorter(type_col, GTK_SORTER(gtk_string_sorter_new(gtk_property_expression_new(G_TYPE_STRING, card_row_get_type(), "type"))));

    // Colonna Colori
    GtkListItemFactory *colors_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(colors_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_list_item_set_child(item, label);
    }), NULL);
    g_signal_connect(colors_factory, "bind", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        CardRow *row = (CardRow*)gtk_list_item_get_item(item);
        GtkWidget *label = gtk_list_item_get_child(item);
        gtk_label_set_text(GTK_LABEL(label), row->translated_colors ? row->translated_colors : "");
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *colors_col = gtk_column_view_column_new("Colori", colors_factory);
    gtk_column_view_column_set_expand(colors_col, TRUE);
    gtk_column_view_append_column(column_view, colors_col);
    // gtk_column_view_column_set_sorter(colors_col, GTK_SORTER(gtk_string_sorter_new(gtk_property_expression_new(G_TYPE_STRING, card_row_get_type(), "translated-colors"))));

    // Colonna Costo Mana
    GtkListItemFactory *mana_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(mana_factory, "setup", G_CALLBACK(+[](GtkListItemFactory *, GtkListItem *item, gpointer) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
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
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *mana_col = gtk_column_view_column_new("Costo Mana", mana_factory);
    gtk_column_view_column_set_expand(mana_col, TRUE);
    gtk_column_view_append_column(column_view, mana_col);
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
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *rarity_col = gtk_column_view_column_new("Rarità", rarity_factory);
    gtk_column_view_column_set_expand(rarity_col, TRUE);
    gtk_column_view_append_column(column_view, rarity_col);
    // gtk_column_view_column_set_sorter(rarity_col, GTK_SORTER(gtk_string_sorter_new(gtk_property_expression_new(G_TYPE_STRING, card_row_get_type(), "rarity"))));

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
        // Aggiungi gesture per click destro
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
        g_signal_connect(gesture, "pressed", G_CALLBACK(on_row_right_click), item);
        gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(gesture));
    }), NULL);
    GtkColumnViewColumn *qty_col = gtk_column_view_column_new("Quantità", qty_factory);
    gtk_column_view_append_column(column_view, qty_col);

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

    // Box inferiore: nome database
    GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(bottom_box), db_name_box);
    gtk_box_append(GTK_BOX(vbox), bottom_box);
    gtk_window_set_child(GTK_WINDOW(window), vbox);
    // Salva lo stato globale nella finestra principale
    g_object_set_data(G_OBJECT(window), "app_state", state);


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
                    g_free(filename);
                }
                g_object_unref(file);
            }
        }, parent);
        g_object_unref(dialog);
    }), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(opendb_action));

    // Azione per eliminare carta
    GSimpleAction *delete_action = g_simple_action_new("delete_card", G_VARIANT_TYPE_INT32);
    g_signal_connect(delete_action, "activate", G_CALLBACK(on_delete_card), window);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(delete_action));

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
        }
    }

    gtk_window_present(GTK_WINDOW(window));
}

static GMenu* create_context_menu(CardRow *row, AppState *state) {
    GMenu *menu = g_menu_new();
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

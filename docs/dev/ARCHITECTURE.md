# MagicDatabase — Architettura

> Nota di sviluppo per orientarsi nel codebase. Aggiornata al 2026-08-22.

## Panoramica

App desktop GTK4 in C++17 per gestire una collezione di Magic: The Gathering.
Sincronizza i dati da [Scryfall](https://scryfall.com), storage locale SQLite,
analisi mana curve con grafici Cairo, export TXT/CSV/TSV/PNG/PDF.

**Stack**: GTK4 · SQLite3 (WAL) · libcurl · nlohmann::json · Cairo · fontconfig

## Struttura del repo

```
Makefile              build (g++, pkg-config gtk4/fontconfig, -lsqlite3 -lcurl)
magicdb               binario
src/
  main.cpp            ~11.300 righe: TUTTA la UI + logica applicativa (monolite)
  database.cpp/.h     layer SQLite (~1.400 righe)
  scryfall.cpp/.h     client HTTP Scryfall (~825 righe)
  utils.cpp/.h        ensure_data_dir_exists()
  style.css           tema attivo (caricato da on_activate)
  theme.css           MORTO — mai caricato da nessuno
data/                 runtime: db, img cache, export, settings.ini, migrations/
img/                  icone app + font mana (Andrew Gioia)
lastdb.txt            percorso ultimo db aperto
.github/workflows/    ci.yml + build.yml (build ubuntu, artifact tar.gz)
```

## Schema database

### Tabella `cards` (CREATE in main.cpp:2284–2307, colonne via migration in Database::Database)
```
id INTEGER PK, name TEXT, type TEXT, colors TEXT, set_code TEXT,
mana_cost TEXT, rarity TEXT, quantity INTEGER, image_url TEXT,
added_date TEXT, price_usd TEXT, foil INTEGER DEFAULT 0, oracle_text TEXT,
english_name TEXT, localized_name TEXT, localized_type TEXT,
scryfall_id TEXT, oracle_id TEXT, collector_number TEXT,
deck_id INTEGER REFERENCES decks(id), sideboard INTEGER DEFAULT 0
```
NOTA: `canonical_name` è scritto dagli insert ma NON è nel CREATE — esiste solo nei DB pre-esistenti o via migration .sql.

### Altre tabelle
- `decks(id, name UNIQUE)`
- `deck_snapshots`, `deck_snapshot_rows` — snapshot mazzo (database.cpp:375–405)
- `card_tags`, `deck_tags`, `card_notes`, `deck_notes` — feature future
- `migrations(name PK, applied_at)` — registro file `data/migrations/*.sql`
- `cards_fts` — FTS5 contentless (`content=''`), trigger `trg_cards_ai/au/ad`

### PRAGMA / indici
`journal_mode=WAL`, `synchronous=NORMAL`, `temp_store=MEMORY`.
Indici su deck_id, (deck_id,sideboard), set_code, added_date, foil, name,
english_name, localized_name, oracle_id, scryfall_id, collector_number + ANALYZE.

## Layer Database (database.cpp)

API pubblica (database.h): `insert_card` (merge-or-insert con dedupe), `delete_card`,
`update_quantity`, `get_card_quantity`, `update_card_info`, `create_deck`,
`query_decks`, `set_card_deck`, `query(sql, cb, params)`, snapshot CRUD,
tag/note CRUD, `search_fulltext` (FTS5 MATCH con fallback LIKE).

Particolarità FTS5: i trigger contentless falliscono sulle DELETE, quindi ogni
write path fa drop triggers → scrive → recreate (database.cpp:674–932). Finestra
di incoerenza durante le scritture; razzioso con più thread (vedi BUGS.md).

## Layer Scryfall (scryfall.cpp)

| Funzione | Endpoint | Note |
|---|---|---|
| `search_cards_from_scryfall` | `/cards/search?q=name:` | italiano prima, fallback EN arricchito |
| `fetch_card_named_exact` | `/cards/named?exact=` | |
| `fetch_price_from_named` | `/cards/named?exact=` | fallback prezzo |
| `fetch_card_by_id` / `fetch_card_by_set_number` / `fetch_print` | `/cards/...` | scoring set+4/collector+8 |
| `download_image_data` | bytes raw | salvate dai call-site di main.cpp |

Cache: 3 mappe globali (`g_cache`, `g_single_card_cache`, `g_price_cache`) con
mutex e TTL 10 min. Eviction solo lazy alla lookup → crescita non limitata (no LRU).
Le risposte VUOTE vengono cachate (un errore transitorio avvelena 10 min).

HTTP: max 3 tentativi, timeout 10s connect / 25s total, backoff 200ms×attempt,
retry su 429/5xx/timeout/DNS. RAII per curl handle e headers. Nessun
`curl_global_init/cleanup` esplicito (delega a curl_easy_init).

## Threading model

Nessun thread pool né std::async. Tutti thread detached `std::thread`:
- bulk refresh worker (main.cpp:3105) — progress/ETA via dialog callback
- prefetcher thumbnails (main.cpp:7289–7320) — throttle 50ms
- refresh singola carta (main.cpp:8314) — chiama `db->update_card_info`
- download immagini detail pane (main.cpp:9711–9727) — marshalling a GTK main loop
  con `g_main_context_invoke` (on_picture_update_invoke, 9732)

Il Database NON ha lock interno: accessi concorrenti da thread detached.
I cache Scryfall sono mutex-guardati.

## Theming / CSS

- Solo `src/style.css` viene caricato (std::ifstream in on_activate, main.cpp:9759–9776),
  provider a GTK_STYLE_PROVIDER_PRIORITY_USER. Se manca: warning stderr e tema default.
- CSS inline per separatori lista (ensure_separator_css_provider, 1249–1258).
- SVG mana icons: ricolorazione fill in memoria (apply_svg_fill_override, 774) +
  texture cache globale senza evizione (g_svg_texture_cache, 793–826).
- Font icona mana registrato via fontconfig (register_mana_icon_font, 708–743).
- Font Inter in data/fonts/.

## I18n

Tabella traduzioni IT/EN + `translate()` (main.cpp:1323–1513). Toggle dal menu View.
Però molte stringhe dei dialoghi sono hardcoded inline fuori dal sistema;
stub vuoti `__add_more_translations` ecc. (1301–1353).

## Mappa sezioni main.cpp (range righe)

| Righe | Sezione |
|---|---|
| 1–122 | include, forward decl, DialogWidgets, CardRow GObject |
| 124–205 | card_row_* type impl |
| 207–540 | helper stringhe/costo mana/markup Pango/colori |
| 541–1086 | rendering icone mana (SVG, font, texture cache) |
| 1087–1250 | formattazione date/prezzi |
| 1251–1322 | separator CSS provider |
| 1323–1513 | traduzioni + translate() |
| 1515–1630 | focus-retry idle helpers |
| 1631–1883 | settings save/load, notifiche, AppState, welcome overlay |
| 1884–2656 | schema check, switch db, debounce, sanitize_filename, dialog styled |
| 2657–3651 | risoluzione carte Scryfall + bulk refresh worker + refresh dialog |
| 3652–4060 | export TXT/CSV/TSV collezione |
| 4061–4494 | deck picker, deck menu, update_ui_texts |
| 4496–6597 | disegno Cairo chart + on_mana_curve_action (finestra analytics completa) |
| 6285–7231 | load_cards_from_db/page, sort comparators, paginazione, refresh_card_list |
| 7232–7696 | sort, remove_from_deck, thumbnail prefetcher, add_to_deck |
| 7697–8170 | add-card dialog, new-db dialog, hover preview popup, context menu |
| 8171–8824 | delete card, refetch singola, quantity dialogs, deck actions |
| 8672–9091 | export database/deck, filtri |
| 9092–9201 | deck select, translate_type |
| 9202–9755 | name column factory (detail revealer, picture loader) |
| 9757–11301 | on_activate: CSS, finestra, toolbar, ColumnView+factory, GActions, accels |
| 11303–11345 | context menu, main() |

## Persistenza runtime

- `data/settings.ini`: lingua, notifiche, parametri focus-retry, ultimo deck
- `lastdb.txt`: path ultimo database (CWD-relative!)
- `data/img/`: cache immagini (nome = filename URL SENZA strip query — vedi BUGS.md B1)
- `data/migrations/*.sql`: migrazioni versionate

## Build & CI

`make` → g++ -std=c++17 -Wall (con -Wno-unused-function/-deprecated-declarations),
pkg-config gtk4+fontconfig, link sqlite3/curl/stdc++fs. Regola %.o con output
colorato e timing per-file. CI ubuntu-latest: build + artifact tar.gz + sha256.

# MagicDatabase — Bug noti e problemi

> Registro vivo: aggiorna quando trovi/risolvi. Formato: ID, gravità, stato, riferimento codice.
> Stati: APERTO / IN CORSO / RISOLTO / WONTFIX

## Critici

### B1 · Filename cache immagini con `?` — RISOLTO
- **Dove**: main.cpp (prefetcher, picture loader, hover preview) → tutti ora usano `image_cache_path()` che strip la query string
- **Causa**: `std::filesystem::path(url).filename()` NON strip la query string dell'URL Scryfall (`...abc.jpg?1680086803`) → file salvati come `abc.jpg?1680086803`
- **Effetti**:
  - `?` illegale su Windows/NTFS → `git clone` fallisce il checkout se i file finiscono nel repo (è successo: 4 file rimosso dall'indice git il 2026-08-22)
  - Scryfall ruota il timestamp sulla stessa immagine → stessa carta cachata più volte con nomi diversi → cache check "file exists?" inefficace
- **Fix**: sanitizzare il nome file (strip tutto dopo `?`), migrare/cancellare vecchi file con `?`, e NON tracciare data/img in git

### B2 · Database senza thread-safety — RISOLTO
- **Fix applicato**: `std::recursive_mutex` su tutte le API pubbliche di Database (database.h/cpp)

### B3 · restore_snapshot perde dati — RISOLTO
- **Fix applicato**: deck_snapshot_rows esteso (migration automatica) con localized_type, image_url, price_usd, scryfall_id, oracle_id, collector_number; create_snapshot/restore_snapshot/get_snapshot_rows aggiornati

### B4 · Migrazioni .sql senza transazione — RISOLTO
- **Fix applicato**: BEGIN/COMMIT per file + check del record di applicazione

## Alti

### B5 · Chiamate di rete bloccanti sul thread UI
- **Dove**: add-card OK handler fa chiamata Scryfall bloccante (main.cpp ~7325–7696); probabilmente altri handler
- **Effetto**: UI freezata durante la ricerca
- **Fix**: spostare su worker thread + marshalling come già fatto per picture loader

### B6 · Thread detached sopravvivono ai widget/Database
- **Dove**: picture thread cattura raw `GtkWidget* pic` (main.cpp:9711) senza ref; worker che usano `db` possono girare dopo la distruzione
- **Fix**: GRefPtr / g_object_ref sui widget toccati dai callback, join o flag di shutdown per i worker

### B7 · Cache Scryfall senza limiti
- **Dove**: scryfall.cpp:66–76, eviction solo lazy (206–208, 222–224, 276–278)
- **Effetto**: crescita illimitata in sessioni lunghe; README dice "LRU" ma non lo è
- **Fix**: LRU con cap (es. 256 entry) o sweep periodico

### B8 · Risposte vuote cachate 10 min
- **Dove**: scryfall.cpp:507
- **Effetto**: un failure transitorio (rete/quota) "avvelena" la ricerca per TTL
- **Fix**: non cachare esito=0 carte, o TTL corto per risposte vuote

## Medi

### B9 · FTS5 MATCH con input utente grezzo
- **Dove**: database.cpp:1367 (`search_fulltext`), LIKE path senza escape `%`/`_` (1386)
- **Effetto**: caratteri sintassi FTS (`"`, `*`, `-`, `(`) causano errori prepare o semantica strana
- **Fix**: escape/quote della query prima del MATCH

### B10 · Migrazioni colonne copia-incolla (~300 righe)
- **Dove**: Database::Database, blocchi PRAGMA table_info + ALTER TABLE ×12 (database.cpp:91–405)
- **Fix**: helper unico `ensure_column(table, column, decl)`

### B11 · canonical_name mai creato dal CREATE TABLE
- **Dove**: CREATE cards in main.cpp:2284–2307 vs INSERT in database.cpp:691
- **Effetto**: db nuovo di zecca fallirebbe l'insert (funziona solo perché le migration .sql o db pre-esistenti la creano)

### B12 · query() maschera gli errori
- **Dove**: database.cpp:998 — qualsiasi step != ROW (incluso BUSY/ERROR) = fine righe, return true
- **Fix**: controllare rc == SQLITE_DONE, propagare errore

### B13 · execute()/query() SQL arbitrario da call-site
- **Dove**: main.cpp ~6410–6477 concatena frammenti SQL (valori bound ok, ma fragile)
- **Fix**: incapsulare le query usate come API del Database

## Bassi / igiene

- Debug `std::cout << "DEBUG:"` in produzione (main.cpp:7411, 7462, 7470; database.cpp:969–984) — soppresso reindirizzando stdout in main() (11336) invece di toglierli
- Stub traduzioni vuoti marcati unused: `__add_more_translations` ecc. (1301–1353)
- Stringhe IT/EN hardcoded fuori da translate() nei dialoghi → lingua switch parziale
- `theme.css` morto (mai caricato)
- `populate_deck_menu` duplicato (2259, 2452); dichiarazioni duplicate on_delete_deck_confirmed/cancel (2458–2459 vs 8455/8495)
- Texture cache SVG senza evizione (g_svg_texture_cache, main.cpp:793–826)
- Percorsi CWD-relative ovunque (data/, lastdb.txt) → dipende da dove lanci il binario; usare g_get_user_data_dir() o percorso exe-relative
- data/collection.db tracciato in git (db personale dentro il repo) — valutare rimozione
- Magic numbers sparsi: throttle 50ms (7314), CMC cap 10 (4895), focus retry 12×100ms
- curl_easy_escape(nullptr,...) presuppone global init fatto altrove
- sqlite3_step ignorati in punti: registrazione migration applicata (553), delete nota (1298)

## Storico interventi

- 2026-08-22: repo riparato dopo clone fallito su NTFS — rimossi dall'indice i 4 file `data/img/*.jpg?<ts>`; checkout completato con `git restore`. Le cancellazioni sono in staging: spariranno da GitHub al prossimo push.

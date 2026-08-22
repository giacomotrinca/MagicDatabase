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

### B5 · Chiamate di rete bloccanti sul thread UI — RISOLTO
- **Fix applicato**:
  - add-card (`on_add_card_ok_clicked`): ricerca Scryfall spostata su thread con risultato marshalled via `g_main_context_invoke`; input disabilitati durante la ricerca, guard anti-doppio-submit (`ctx->busy`)
  - hover preview (`on_row_enter`): download in background, popup mostrato subito, texture applicata al termine

### B6 · Thread detached sopravvivono ai widget/Database — PARZIALMENTE RISOLTO
- **Fix applicato**: `GWeakRef` invece di raw pointer per picture loader detail pane (`PicCtx`) e hover preview (`HoverImageCtx`); owner del dialog add-card tracciato con weak ref (`AddCardContext::lifetime_owner`)
- **Rimane**: worker bulk refresh e refetch singola usano ancora puntatori raw a AppState/db

### B7 · Cache Scryfall senza limiti
- **Dove**: scryfall.cpp:66–76, eviction solo lazy (206–208, 222–224, 276–278)
- **Effetto**: crescita illimitata in sessioni lunghe; README dice "LRU" ma non lo è
- **Fix**: LRU con cap (es. 256 entry) o sweep periodico

### B8 · Risposte vuote cachate 10 min — RISOLTO
- **Fix applicato**: `perform_search` cachera solo result set non vuoti; prezzi vuoti non più cachati su failure; single-card già sicuro

## Medi

### B9 · FTS5 MATCH con input utente grezzo — RISOLTO
- **Fix applicato**: query quotata come frase FTS5 (doppie quote interne raddoppiate); LIKE con escape `\` di `%`, `_`, `\` + `ESCAPE '\'`

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

### B14 · lastdb.txt con percorso assoluto di altra macchina → db non riaperto
- **Evidenza**: al primo lancio "Nessun database aperto"; lastdb.txt conteneva `/mnt/01D9269698DA8D30/...` (path di un altro mount)
- **Fix roadmap**: F3.6 (percorsi exe-relative/XDG). Workaround temporaneo: lastdb.txt relativo

### B15 · Contrasto illeggibile su temi chiare di sistema — RISOLTO
- **Causa**: style.css è progettato dark ma non copriva i background base; su tema Adwaita light le righe restavano bianche con testo quasi bianco (`label{color:@text_primary}`)
- **Diagnosi**: test empirico `GTK_THEME=Adwaita:dark` → lista perfettamente leggibile
- **Fix applicato**: `GTK_THEME=Adwaita:dark` via `g_setenv` in main() se l'utente non l'ha impostata (le GtkSettings prima di gtk_init vengono ignorate)

### B16 · Colonne tabella fuori schermo su finestre strette — RISOLTO
- **Sintomo**: a ~950px Rarità/Data/Quantità/Prezzo tagliate; contenuto allocato a ~1280px
- **Diagnosi** (probe runtime con `COLUMN_DEBUG_PROBE`, catena measure/alloc):
  - `GtkApplicationWindow min=1283` mentre il contenuto richiedeva solo 704 → colpevole: **header bar** (GtkCenterBox = somma minimi delle 3 sezioni)
  - dentro la toolbar: `.search-field{min-width:220px}` CSS + `set_size_request(180/200)` sulle entry + spinbutton min intrinseco ~190px
  - GtkOverlay misurava anche il welcome overlay (~1280px) → fix `gtk_overlay_set_measure_overlay(FALSE)`
  -GtkStack homogeneous ereditava la pagina stats più larga → `set_hhomogeneous(FALSE)`
- **Fix applicati**:
  - entry: `width_chars`/`max_width_chars` adattivi invece di size_request fissi; `.search-field` min 76px
  - pannello dettagli avvolto in ScrolledWindow(AUTOMATIC,NEVER) così il revealer chiuso non propaga min-width alla colonna Nome
  - una sola colonna espandibile (Nome); Colori fissa 70; Mana/Rarità 85; Data 110; Quantità 70; Prezzo 75
  - collasso responsivo: sotto 1050px le label File/Visualizza diventano icon-only
- **Nota**: lo spinbutton mantiene min ~190px (intrinseco GTK4, da indagare)

## Storico interventi

- 2026-08-22: repo riparato dopo clone fallito su NTFS — rimossi dall'indice i 4 file `data/img/*.jpg?<ts>`; checkout completato con `git restore`. Le cancellazioni sono in staging: spariranno da GitHub al prossimo push.

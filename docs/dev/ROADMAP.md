# MagicDatabase — Roadmap di miglioramento

> Priorità in sequenza concordata: prima stabilità (backend + frontend critici),
> poi refactoring architetturale, poi feature. Spunta gli elementi completati.

## Fase 1 — Fix critici (stabilità)

- [x] **F1.1** Sanitize filename immagini cache: strip query string URL prima del salvataggio
      (helper `image_cache_path()` in main.cpp, usato da prefetcher/picture loader/hover preview)
      + rimossi da git i 4 file con `?` — vedi BUGS.md B1
- [x] **F1.2** Mutex su tutte le API di Database (`std::recursive_mutex`) — B2
- [x] **F1.3** restore_snapshot: ripristina ora scryfall_id, oracle_id, collector_number,
      localized_type, price_usd, image_url (schema deck_snapshot_rows esteso con auto-migration) — B3
- [x] **F1.4** Migrazioni .sql in transazione per-file con log errori — B4
- [x] **F1.5** Rimossi binari e .o dal repo git (.gitignore li copriva già ma erano tracciati)

## Fase 2 — Robustezza frontend

- [ ] **F2.0** Cleanup vecchi file immagine con `?` già presenti in data/img/ sulle macchine Linux

- [ ] **F2.1** Eliminare chiamate di rete bloccanti sul thread UI (add-card handler e simili) — B5
- [ ] **F2.2** Ref-safety widget nei thread (GRefPtr/g_object_ref), shutdown ordinato dei worker detached — B6
- [ ] **F2.3** Non cachare risposte Scryfall vuote (o TTL breve) — B8
- [ ] **F2.4** Escape input per FTS5 MATCH e LIKE — B9

## Fase 3 — Refactoring architetturale

- [ ] **F3.1** Spezzare main.cpp (~11.300 righe) in moduli:
      `ui/` (finestra principale, dialoghi, column view), `charts/` (mana curve Cairo),
      `workers/` (refresh, prefetcher), `i18n/`, `app_state`
      Partire dalle sezioni a confini netti (vedi mappa in ARCHITECTURE.md):
      1. charts 4496–6597 · 2. export 3652–4060 · 3. filtri 8825–9091 · 4. icone 541–1086
- [ ] **F3.2** Cache Scryfall LRU con cap — B7
- [ ] **F3.3** Helper unico per migrazioni colonne (`ensure_column`) — B10
- [ ] **F3.4** Completare i18n: tutte le stringhe via translate(), eliminare stub vuoti
- [ ] **F3.5** Igiene: togliere debug cout, dead code (theme.css, duplicati populate_deck_menu),
      controllare sqlite3_step ignorati, query() che propaga errori — B12
- [ ] **F3.6** Percorsi assoluti robusti (exe-relative o XDG) invece di CWD-relative

## Fase 4 — Feature / qualità (proposte)

- [ ] Test unitari sul layer Database (sqlite in-memory) e sui parser Scryfall (fixture JSON)
- [ ] Rimuovere data/collection.db dal repo (dati personali); template db vuoto opzionale
- [ ] Logging framework semplice (livelli, file) al posto di stdout/stderr sparso
- [ ] Aggiornare README: sezione "LRU cache" non corretta; documentare data/migrations/
- [ ] Windows build (già il bug B1 lo rendeva impossibile): valutare CI matrix
- [ ] Backup automatico del db prima delle bulk refresh

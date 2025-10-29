MagicDatabase
===============

A compact desktop collection manager for Magic: The Gathering.

This GTK4 application is written in modern C++ and stores card data locally in SQLite. It integrates with Scryfall for card metadata and images, supports decks and sideboards, provides tagging and snapshots (deck versioning), and includes a per-deck mana-curve analyzer with PNG/PDF export and a textual report.

Table of contents
- Features
- Quick start
- Dependencies
- How to use (short)
- Mana curve export
- Export filenames & data layout
- Troubleshooting
- Development notes
- Contributing
- License & contact

Features
--------

- Local SQLite storage for your collection
- Scryfall integration for accurate card data and images
- Decks with sideboard support and per-deck snapshots (create/list/restore)
- Tags and notes for cards and decks, searchable
- Filters by color, rarity and foil; text search
- Mana-curve analysis per deck (chart + statistics) and export (PNG, PDF, text)

Quick start
-----------

Clone, build and run (fish shell example):

```fish
git clone https://github.com/giacomotrinca/MagicDatabase.git
cd MagicDatabase
make
./magicdb
```

If the binary does not start, install the system development packages listed below and re-run `make`.

Dependencies
------------

Required (development packages / headers):

- C++17 toolchain (g++ or clang)
- GTK4 development headers
- sqlite3 development headers
- Cairo development headers (for PNG/PDF export)
- libcurl development headers
- nlohmann/json (header-only) or distro package
- make, pkg-config

Example for Ubuntu/Debian:

```fish
sudo apt update
sudo apt install -y build-essential pkg-config git \
  libgtk-4-dev libsqlite3-dev libcairo2-dev libcurl4-openssl-dev nlohmann-json3-dev
```

How to use (short)
------------------

- Launch: `./magicdb`
- Create/open a database (File → New / Open). Databases and exported files are placed under `data/`.
- Add cards (File → New Card or Ctrl+N). Mark cards as foil when appropriate.
- Create/select decks (File → Select Deck) and add cards; mark sideboard entries.
- Use View → Filters to filter by color/rarity/foil or to show "not in any deck".

Mana curve export
-----------------

The Mana Curve view is deck-scoped and includes:

- Histogram (CMC buckets 0..10+)
- Statistics: total cards, average CMC, median, mode, per-bucket distribution
- Exports:
  - PNG: raster snapshot
  - PDF: vector redraw (high quality)
  - Text report: per-bucket card lists and color breakdown

Export filenames & data layout
-----------------------------

- All exports are written to `data/`.
- Naming conventions:
  - `data/mana_curve_<sanitized-deck-name>.png`
  - `data/mana_curve_<sanitized-deck-name>.pdf`
  - `data/mana_stats_<sanitized-deck-name>.txt`
- Card `colors` are stored as a JSON array (e.g. `["W","U"]`). `foil` and `sideboard` are integer flags (0/1).

Troubleshooting
---------------

- PDF export crashes or corrupted PDF: ensure Cairo development headers are installed (e.g. `libcairo2-dev` on Debian/Ubuntu).
- GTK/Cairo header errors while building: verify `pkg-config --cflags --libs gtk4` returns usable flags.
- Duplicate rows: check whether `foil` or `sideboard` differ — those fields are part of uniqueness rules.

Development notes
-----------------

- The database wrapper includes helper methods for snapshots, tags, and notes. Consider adding unit tests using SQLite in-memory to validate insert/merge/split behavior.
- Filters are implemented client-side for responsiveness; for very large databases, prefer translating filters to SQL `WHERE` clauses.

Contributing
------------

Contributions are welcome. Suggested tasks:

- Add or improve translations (EN/IT)
- Add unit tests for the database layer
- Improve mana-curve visuals or export metadata

Workflow:

```bash
git checkout -b feature/your-thing
implement and test
git push origin feature/your-thing
open a PR
```

License & contact
-----------------

MIT — see the `LICENSE` file.

Author: Giacomo Trinca — https://github.com/giacomotrinca
Issues: https://github.com/giacomotrinca/MagicDatabase/issues

---

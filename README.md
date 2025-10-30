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

What's new (Oct 2025)
----------------------

- Improved Mana Curve preview:
  - The "Curva Mana" dialog now computes the curve on-the-fly from the current view/filters (works for both the main DB view and per-deck views).
  - Three curve modes are available: "Totale" (overall), "Tipo" (by card type), and "Colore" (by mana color). Use the buttons above the chart to switch between them.
  - The chart is enclosed in a styled frame with margins for a cleaner preview.
- Visual and data behavior:
  - Type plots now exclude Lands (they are folded into "Other").
  - Color plots use mana-accurate colors: White shown as grey dashed, Blue/Black/Red/Green use the expected hues, and Colorless (incolore) is magenta.
  - Empty type/color series are not plotted and the corresponding mode buttons are disabled when no data is available.
  - Y-axis tick labels have been added to the multi-series plots for easier reading.
- Export and dialog improvements:
  - Export options remain available (PNG/PDF/TXT) and are accessible from the preview dialog.
  - Export and Close buttons are displayed in a compact column on the right of the preview; top control buttons are compact as well.
  - The preview dialog attempts to fit its contents (no oversized fixed window), and the preview surface is pre-rendered for snappy switching between modes.
- Other UX & performance tweaks:
  - The per-page size and "View All" behavior are persisted between runs.
  - Thumbnails are prefetched asynchronously for faster list navigation.
  - The textual statistics panel has been simplified (the raw distribution line removed) and provides per-bucket lists in the exported text report.


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

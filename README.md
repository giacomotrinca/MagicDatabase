# 🎴 MagicDatabase

[![Build Status](https://github.com/giacomotrinca/MagicDatabase/actions/workflows/build.yml/badge.svg)](https://github.com/giacomotrinca/MagicDatabase/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![GTK4](https://img.shields.io/badge/GTK-4.0-blue)](https://gtk.org/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-orange)](https://isocpp.org/)

A sleek, fast, and slightly obsessive GTK4 desktop app to manage your Magic: The Gathering collection — built in C++17, powered by Scryfall, and tuned for people who want their collection neat, searchable, and styled with gold foil rows.

This repo is one of many projects I'm using to build my presence in the community — expect iterative polish, pragmatic features, and a little personality. :P

---

## ✨ Highlights

- Scryfall integration for accurate card data and images
- Bilingual UI (Italian / English) with instant switching
- Foil-aware database (foil is a separate row; foil rows render in gold)
- Powerful filtering (colors, rarities, foil) and sorting
- Local SQLite storage with simple TXT export

---

## 🚀 Quickstart — Build & Run

Clone, build and run in three commands:

```bash
git clone https://github.com/giacomotrinca/MagicDatabase.git
cd MagicDatabase
make
./magicdb
```

If `./magicdb` starts, you're ready. If not, read the platform-specific install steps below to ensure dependencies are present.

---

## 🛠️ Dependencies

- C++17 toolchain (g++/clang)
- GTK4 development headers (gtk4)
- sqlite3 development headers
- libcurl development headers
- nlohmann/json (header-only; distro packages available)
- make

Below are concrete install commands for common platforms.

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y build-essential pkg-config git \
  libgtk-4-dev libsqlite3-dev libcurl4-openssl-dev nlohmann-json3-dev

make
./magicdb
```

Notes: On older Ubuntu releases GTK4 packages may be missing or old. Consider upgrading to a newer release or using flatpak/MSYS if needed.

### Fedora

```bash
sudo dnf install -y @development-tools pkgconfig git \
  gtk4-devel sqlite-devel libcurl-devel nlohmann-json-devel

make
./magicdb
```

### Arch Linux

```bash
sudo pacman -Syu --needed base-devel git gtk4 sqlite curl nlohmann-json

make
./magicdb
```

### macOS (Homebrew)

```bash
brew update
brew install gtk4 sqlite curl nlohmann-json pkg-config

make
./magicdb
```

Homebrew may install GTK under `/usr/local` or `/opt/homebrew`; pkg-config usually finds it.

### Windows (MSYS2) — recommended for native builds

1. Install MSYS2: https://www.msys2.org/
2. Open the "MSYS2 MinGW 64-bit" shell and run:

```bash
pacman -Syu
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk4 \
  mingw-w64-x86_64-sqlite mingw-w64-x86_64-curl mingw-w64-x86_64-nlohmann-json git

cd /c/path/to/MagicDatabase
make
./magicdb.exe
```

Alternative: use WSL (Ubuntu) and follow the Ubuntu instructions if you prefer a Linux environment.

---

## 🔧 Build Tips & Troubleshooting

- If pkg-config can't find GTK4, check `PKG_CONFIG_PATH` to include Homebrew/MSYS2 pkgconfig paths.
- If you get missing `nlohmann/json`, either install the distro package or add include path via `-I` in the Makefile.
- Run `./magicdb` from a terminal to see console logs — they help diagnose runtime issues.

If you prefer, I can add a containerized/flatpak build to make distribution easier — tell me which you prefer.

---

## 📖 How to Use (Short)

1. Start the app (`./magicdb`).
2. File → New Database to create a collection (saved under `data/`).
3. Click "Nuova Carta" / "New Card" and search by name; pick the correct result and optionally mark it as foil.
4. Use Visualizza → Lingua to switch UI language; dates and labels update instantly.
5. Use the search box, sorting headers and View → Filters to refine what you see.

Foil cards are stored as distinct rows in the DB and appear with gold-colored text in the UI.

---

## 🧭 Developer Notes

- Card colors are stored as a JSON array in the `colors` column.
- The `foil` column is an integer (0/1) and is included in uniqueness checks so foil/non-foil rows remain separate.
- UI filtering is currently client-side; for very large DBs we can convert filters to SQL queries.

---

## 🤝 Contributing & Getting Noticed

If you're reading this and want to help me build a presence in the community, contributions are massively appreciated.

1. Fork the repo
2. Create a branch: `git checkout -b feature/your-awesome-thing`
3. Implement, test, and push
4. Open a PR and describe the value

Good first tasks: translations, unit tests for the Database wrapper, SQL-filtering performance improvements.

---

## 📄 License

MIT — see the `LICENSE` file.

---

## 👋 Contact

- Author: Giacomo Trinca — https://github.com/giacomotrinca
- Issues: https://github.com/giacomotrinca/MagicDatabase/issues

---

May your draws be legendary. Keep shipping and building your name. ✨
# 🎴 MagicDatabase

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](https://github.com/giacomotrinca/MagicDatabase)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![GTK4](https://img.shields.io/badge/GTK-4.0-blue)](https://gtk.org/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-orange)](https://isocpp.org/)

A sleek, modern GTK4-based desktop application for managing your Magic: The Gathering card collection. Seamlessly search, add, and organize cards with real-time data from Scryfall, featuring dynamic localization in English and Italian.

![MagicDatabase Screenshot](https://via.placeholder.com/800x600?text=MagicDatabase+Screenshot) <!-- Replace with actual screenshot -->

## ✨ Features

- **🔍 Scryfall Integration**: Search and fetch card data directly from Scryfall's comprehensive API.
- **🌍 Dynamic Localization**: Switch between English and Italian instantly – names, types, colors, and rarity update on the fly!
- **🔄 Smart Refresh**: Update existing cards' information without duplicates, with fallback for cards missing English names.
- **📊 SQLite Database**: Robust local storage with localized fields for offline access.
- **🎨 Modern UI**: Dark-themed GTK4 interface with smooth animations and responsive design.
- **📋 Advanced Sorting & Filtering**: Sort by name, type, colors, mana cost, rarity, or date. Filter by name.
- **🖼️ Image Hover**: Hover over cards to preview high-quality images.
- **📈 Collection Stats**: View total cards and estimated value.
- **🗂️ Database Management**: Create, open, and manage multiple collections.

## 🚀 Installation

### Prerequisites

- **GTK4**: Install GTK4 development libraries.
  - Ubuntu/Debian: `sudo apt install libgtk-4-dev`
  - Fedora: `sudo dnf install gtk4-devel`
  - macOS: `brew install gtk4`
  - Windows: Use MSYS2 or vcpkg.

- **SQLite3**: `sudo apt install libsqlite3-dev` (or equivalent).
- **libcurl**: `sudo apt install libcurl4-openssl-dev`.
- **nlohmann/json**: Header-only library, included via `-I/usr/include/nlohmann`.
- **C++17 Compiler**: GCC 7+ or Clang 5+.

### Build Instructions

1. **Clone the Repository**:
   ```bash
   git clone https://github.com/giacomotrinca/MagicDatabase.git
   cd MagicDatabase
   ```

2. **Compile**:
   ```bash
   make
   ```

3. **Run**:
   ```bash
   ./magicdb
   ```

## 📖 Usage

1. **Launch the App**: Run `./magicdb` to start the application.
2. **Create/Open Database**: Use "File" > "New Database" or "Open Database" to manage your collection.
3. **Add Cards**: Click "Nuova Carta" (New Card), search by name, and select from results.
4. **Switch Language**: Go to "Visualizza" > "Lingua" to toggle between Italian and English.
5. **Refresh Data**: Hit the "Refresh" button to update all cards from Scryfall.
6. **Sort & Filter**: Use column headers for sorting, and the search bar for filtering.

### Keyboard Shortcuts

- `Ctrl+N`: Add new card
- `Delete`: Delete selected card

## 🏗️ Architecture

- **Frontend**: GTK4 for UI components (buttons, lists, menus).
- **Backend**: C++17 with SQLite for data persistence.
- **API**: libcurl for Scryfall HTTP requests, nlohmann/json for parsing.
- **Localization**: In-memory translation maps with dynamic UI updates.

## 🤝 Contributing

Contributions are welcome! Please:

1. Fork the repository.
2. Create a feature branch: `git checkout -b feature/amazing-feature`.
3. Commit changes: `git commit -m 'Add amazing feature'`.
4. Push to branch: `git push origin feature/amazing-feature`.
5. Open a Pull Request.

### Development Setup

- Ensure all dependencies are installed.
- Use `make` for building.
- Test localization by switching languages and verifying translations.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- [Scryfall](https://scryfall.com/) for the amazing card database API.
- [GTK](https://gtk.org/) for the powerful UI toolkit.
- [nlohmann/json](https://github.com/nlohmann/json) for JSON parsing.

## 📞 Contact

- **Author**: Giacomo Trinca
- **GitHub**: [@giacomotrinca](https://github.com/giacomotrinca)
- **Issues**: [Report Bugs](https://github.com/giacomotrinca/MagicDatabase/issues)

---

*May your draws be legendary! 🧙‍♂️*</content>
<parameter name="filePath">/mnt/01D9269698DA8D30/gitrepos/MagicDatabase/README.md
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
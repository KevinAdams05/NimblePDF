> [!NOTE]
> An LLM was used to aid in development of this code.

# NimblePDF

A native [Haiku](https://www.haiku-os.org/) PDF viewer, derived from
[BePDF](https://github.com/HaikuArchives/BePDF), with the xpdf rendering
backend being replaced by [poppler](https://poppler.freedesktop.org/).

A Linux port is a possible future goal once the Haiku version is stable.

## Status

🚧 **Active development.** NimblePDF was forked from BePDF and its xpdf
rendering backend has been replaced by [poppler](https://poppler.freedesktop.org/).
The migration compiles and links; runtime verification on Haiku (rendering,
navigation, search, and annotation save) is in progress.

## Repository layout

```
NimblePDF/
├── assets/             # Logos, icon source artwork
├── dist/               # Resource files shipped with the app
│   ├── docs/           # User-facing docs (help, license texts)
│   ├── encodings/      # CMap and encoding tables
│   ├── fonts/          # Bundled fonts
│   └── license/        # Third-party license texts
├── docs/               # Developer documentation
│   └── STYLE_GUIDE.md  # Coding style (read this first)
├── source/             # NimblePDF application source
│   ├── graphics/       # Vector graphics primitives
│   ├── haiku/          # Haiku-specific UI and integration
│   │   └── utils/      # Small helpers
│   ├── locales/        # Translation catalogs
│   └── Makefile        # Haiku Generic Makefile
├── scripts/            # Dev/lint helpers
├── build.sh            # Top-level build entry point
├── LICENSE             # GPL v2+ (NimblePDF) + third-party license notes
└── README.md           # This file
```

## Building

NimblePDF builds natively on Haiku using the standard
[Generic Makefile](https://www.haiku-os.org/development/learning-the-api/).
From the repository root:

```sh
./build.sh
```

Build prerequisites (Haiku), installed via `pkgman`:

- `poppler25.12_devel` and `poppler_data` (the PDF rendering engine)
- `freetype2-devel`

Cross-compiling from Linux is documented in
[docs/CROSS_BUILD.md](docs/CROSS_BUILD.md) (coming soon).

## Contributing

Before opening a PR, read [docs/STYLE_GUIDE.md](docs/STYLE_GUIDE.md) — it
covers the formatting and naming conventions used throughout the project
(based on Haiku's coding guidelines).

Run the formatters and linter:

```sh
clang-format -i source/haiku/**/*.{cpp,h}     # uses .clang-format at repo root
python3 scripts/checkstyle-nimblepdf.py source/haiku
```

## License

NimblePDF is licensed under the **GNU General Public License, version 2 or
later** (GPL v2+), as a derivative of BePDF (also GPL v2+).

- Project license: [LICENSE](LICENSE) (NimblePDF summary)
- Full GPL v2 text: [COPYING](COPYING)
- Full GPL v3 text: [COPYING3](COPYING3)

Third-party components (each retains its own license):
- **BePDF** sources — GPL v2+, Benoit Triquet, Hubert Figuiere,
  Michael Pfeiffer, waddlesplash
- **poppler** — GPL v2 / v2+ (the rendering engine, linked as a system
  library)

See individual source-file headers for per-file copyright details.

## Credits

- **BePDF** — the foundation NimblePDF builds on. Thanks to Michael
  Pfeiffer and the BePDF/HaikuArchives contributors.
- **xpdf** — the original rendering engine (Glyph & Cog), inherited via
  BePDF and since replaced by poppler.
- **poppler** — the rendering engine NimblePDF now uses.

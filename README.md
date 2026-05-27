> [!NOTE]
> An LLM was used to aid in development of this code.

# NimblePDF

A native [Haiku](https://www.haiku-os.org/) PDF viewer, derived from
[BePDF](https://github.com/HaikuArchives/BePDF), with the xpdf rendering
backend being replaced by [poppler](https://poppler.freedesktop.org/).

A Linux port is a possible future goal once the Haiku version is stable.

## Status

🚧 **Early development.** NimblePDF has just been forked from BePDF. The
source tree currently still uses xpdf; the poppler migration is the next
major piece of work after the style audit. The application is not yet in a
working state under the NimblePDF name.

## Repository layout

```
NimblePDF/
├── assets/             # Logos, icon source artwork
├── dist/               # Resource files shipped with the app
│   ├── docs/           # User-facing docs (help, license texts)
│   ├── encodings/      # CMap and encoding tables (from xpdf)
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
├── xpdf/               # Vendored xpdf 4.x (to be replaced by poppler)
├── scripts/            # Dev/lint helpers
├── build.sh            # Top-level build entry point
├── LICENSE             # MIT (NimblePDF) + notes on third-party licenses
└── README.md           # This file
```

## Building

NimblePDF builds natively on Haiku using the standard
[Generic Makefile](https://www.haiku-os.org/development/learning-the-api/).
From the repository root:

```sh
./build.sh
```

Build prerequisites (Haiku):

- `freetype2-devel`
- xpdf dependencies (autoconf-generated; see `xpdf/INSTALL`)
- Once the poppler migration begins: `poppler-devel`, `cairo-devel`,
  `glib2-devel`

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
- **xpdf** sources — GPL v3, Glyph & Cog, LLC (slated for removal)
- **poppler** (planned) — GPL v2 / v2+

See individual source-file headers for per-file copyright details.

## Credits

- **BePDF** — the foundation NimblePDF builds on. Thanks to Michael
  Pfeiffer and the BePDF/HaikuArchives contributors.
- **xpdf** — the original rendering engine (Glyph & Cog).
- **poppler** — the rendering engine NimblePDF is migrating to.

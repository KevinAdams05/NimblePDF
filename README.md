<img src="assets/logo_lightmode.png" alt="Alt Text">

# NimblePDF

A native [Haiku](https://www.haiku-os.org/) PDF viewer, derived from
[BePDF](https://github.com/HaikuArchives/BePDF), with the xpdf rendering
backend being replaced by [poppler](https://poppler.freedesktop.org/).

A Linux port is a possible future goal once the Haiku version is stable.

## Status

🚧 **Active development.** NimblePDF was forked from BePDF and its xpdf
rendering backend has been replaced by [poppler](https://poppler.freedesktop.org/).


## Logging Bugs / How to Help

Bugs are welcome! To log a bug, [please log it here in github as an issue](https://github.com/KevinAdams05/NimblePDF/issues), and include as much detail as possible. Please attach the PDF that is causing the error, if possible. Also attach your syslog and state which version/hrev of Haiku you are running.

PRs are welcome! However, please test all code changes on Beta5 and the latest nightly before opening the PR!


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

- **BePDF** — the foundation NimblePDF builds on. Thanks to Benoit Triquet, Hubert Figuiere,
  Michael Pfeiffer, waddlesplash and the BePDF/HaikuArchives contributors.
- **xpdf** — the original rendering engine (Glyph & Cog), inherited via
  BePDF and since replaced by poppler.
- **poppler** — the rendering engine NimblePDF now uses.

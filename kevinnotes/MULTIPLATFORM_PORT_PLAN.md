> [!NOTE]
> An LLM was used to aid in development of this plan.

# NimblePDF — multi-platform port plan (Linux · Windows · KevRexOS)

**Status:** scoping / strategy. No port work started. This is the umbrella plan
covering all three targets and the shared core extraction that gates them.

**Companion docs:**
- `KEVREXOS_PORT_ANALYSIS.md` — the deep dive for the `os::` target (API mapping,
  per-file coupling, KevRexOS-specific risks). This plan does **not** duplicate
  it; it places that work inside the larger picture.
- `diagrams/port-architecture.svg` — the layered picture this plan describes.
- Style guide §9 (`docs/STYLE_GUIDE.md`) — portability rules. This plan **is** the
  "one deliberate cross-platform pass" §9.2 said to defer until it was needed.

---

## 1. The thesis

A PDF viewer is ~80% "render PDF correctly" and ~20% "wrap it in a UI." The hard
80% is **poppler**, which is already cross-platform — the entire xpdf→poppler
migration was, in hindsight, the port-enablement work. So the question is not
"can we port the engine" (it's done) but "how cheaply can we share everything
that isn't the toolkit."

**Strategy: extract a toolkit-free `libnimblepdf-core`, define a handful of thin
platform seams, and write one UI per toolkit.** Crucially we do **not** write a
separate hand-rolled UI per OS. The split is:

| Frontend | Toolkit | Covers | Status |
|---|---|---|---|
| `haiku/` | BeAPI (`B*`) | Haiku | ✔ shipping (v2.2.0) |
| `qt/` | **Qt 6** | **Linux + Windows** (+ macOS for free) | planned |
| `kevrexos/` | `os::` | KevRexOS | planned (see companion doc) |

Two toolkits, three+ platforms. Linux and Windows share one Qt codebase.

---

## 2. Architecture (three layers)

See `diagrams/port-architecture.svg`. Bottom to top:

1. **poppler 25.12** — external dependency, already portable.
2. **`libnimblepdf-core`** — portable C++17/20, *no UI toolkit, no OS calls*.
   Document model, rendering to an RGBA buffer, search, annotations + save,
   history, outline, settings *model*. ~45% of today's LOC is already here in
   spirit; the one real refactor is prying the non-UI half out of `PDFView`.
3. **Platform seams** — small pure-virtual interfaces the core needs from the
   host: bitmap/render-target, settings store, file chooser, file watcher,
   printer, clipboard, string catalog. One implementation per frontend.
4. **Frontends** — own only the windows/widgets and the seam implementations.

The discipline that makes this work (already mandated by style guide §9.3):
**no poppler type escapes the core**, and string conversion stays at the
`TextConversion` seam.

---

## 3. The core / UI split (per-file)

Derived from a dependency scan of `source/haiku/` (UI-token vs poppler-token vs
OS-glue-token counts) and reconciled with `KEVREXOS_PORT_ANALYSIS.md` §8.

### Tier 1 — Core, portable as-is → `core/` (recompile only)

| File | LOC | Role |
|---|---|---|
| `Annotation.cpp` | 1788 | annotation model (poppler, zero UI) |
| `AnnotWriter.cpp` | 848 | save / XRef rewrite (poppler) |
| `AnnotAppearance.cpp` | 560 | appearance-stream generation |
| `BeFontEncoding.cpp` | 765 | encoding tables (pure) |
| `PDFSearch.cpp` | 260 | text search over `TextPage` |
| `TreeParser.cpp` | 116 | outline tree from catalog |
| `PageLabels.cpp` | 195 | page-label logic |
| `EmbeddedFileSpec.cpp` | 175 | attachment specs |
| `DisplayCIDFonts.cpp` | 190 | CID font config |
| `TextConversion.cpp` | 165 | the `GooString`↔string seam |
| `History.cpp` | 157 | nav history (pure) |
| `PageCache.cpp` / `CachedPage.cpp` | 23 / 167 | page caching |

### Tier 2 — Render/IO bridge → `core/` + one thin seam each

| File | LOC | Seam it needs |
|---|---|---|
| `BeSplashOutputDev.cpp` → **`SplashRenderer`** | 392 | **IRenderTarget** — render poppler page into an RGBA buffer; frontend wraps the buffer (BBitmap / QImage / os::Bitmap). *The single highest-leverage file.* |
| `PageRenderer.cpp` | 382 | background thread + "page ready" notify |
| `AnnotationRenderer.cpp` | 734 | 2D draw primitives (lines/rects/paths/text) — render into the same RGBA buffer, or a tiny `ICanvas` |
| `BitmapPool.cpp` | 144 | re-evaluate; a buffer pool, not a BBitmap pool |
| `PDFPrint.cpp` | 414 | **IPrinter** |
| `utils/Thread.cpp` | 67 | threading (already a wrapper) |
| `EntryChangedMonitor.cpp` | 87 | **IFileWatcher** (reload-on-change) |

### Tier 3 — UI + app shell → per-frontend (`haiku/`, `qt/`, `kevrexos/`)

| File | LOC | Notes |
|---|---|---|
| **`PDFView.cpp`** | **2705** | **SPLIT** — extract `DocumentController` (page/zoom/rotate/nav/coord-transforms/hit-test/render-orchestration) into core; leave a thin platform view that draws the cached buffer and forwards input. *Critical path.* |
| `PDFWindow.cpp` | 2020 | main window — menus/toolbar/panels/command wiring. Pure translation per toolkit. |
| `NimblePDFApplication.cpp` | 880 | **SPLIT** — core init (`globalParams`, doc lifecycle) vs platform app shell (BApplication / QApplication / os::Application). |
| `Settings.cpp` | 299 | **SPLIT** — settings *model* (core) vs persistence (**ISettingsStore**). |
| dialog zoo: `OutlinesWindow` `FindTextWindow` `PreferencesWindow` `FileInfoWindow` `PasswordWindow` `PrintSettingsWindow` `PrintingProgressWindow` `StatusWindow` `TraceWindow` `AnnotationWindow` `AttachmentView` | ~3500 | reimplement per toolkit; logic is known, work is mechanical |
| `LayoutUtils.cpp`, `utils/ResourceLoader.cpp`, `utils/EntryMenuItem.cpp` | ~415 | layout/resource/menu helpers — per toolkit |
| `NimblePDF.cpp` | 41 | `main()` entry — thin, per platform |

**Rough budget:** Tier 1 ≈ 5.5k LOC (free), Tier 2 ≈ 2.2k (seam rewrites),
Tier 3 ≈ 10k+ (per-toolkit UI — the bulk, but mechanical, and shared between
Linux+Windows via Qt).

---

## 4. Platform seams (the abstraction interfaces)

Keep these *small*. One header of pure-virtual interfaces in `core/`, one impl
folder per frontend.

| Interface | Purpose | Haiku | Qt (Linux+Win) | KevRexOS |
|---|---|---|---|---|
| `IRenderTarget` / bitmap | hold + blit an RGBA buffer | `BBitmap` | `QImage`/`QPixmap` | `os::Bitmap` |
| `ISettingsStore` | app prefs + per-doc state | BFS node attrs + settings file | `QSettings` (registry/ini) + sidecar | `os::Attribute` + file |
| `IFileChooser` | open/save dialogs | `BFilePanel` | `QFileDialog` | `os::FileRequester` |
| `IFileWatcher` | reload on external change | `watch_node` | `QFileSystemWatcher` | `os::NodeMonitor` |
| `IPrinter` | print rendered pages | `BPrintJob` | `QPrinter` | (defer) |
| `IClipboard` | copy selected text | `BClipboard` | `QClipboard` | `os::Clipboard` |
| `IStringCatalog` | i18n lookup | catkeys / `BCatalog` | Qt Linguist (`.ts`/`tr()`) | `os::Catalog` |

**Per-doc state is the subtle one.** Haiku stores view state + user bookmarks in
BFS extended attributes (`nimblepdf:*`). Linux/Windows filesystems can't be
relied on for xattrs, so `ISettingsStore` must support a **central store keyed by
file path/hash** (sidecar or a per-user DB) as the default, with xattrs as a
Haiku-only optimization. This is a real behavioral design point, not just an API
swap.

---

## 5. Toolkit decision for Linux + Windows: **Qt 6** (recommended)

| | Qt 6 | wxWidgets |
|---|---|---|
| One codebase Win+Linux(+mac) | ✔ | ✔ |
| Custom page view (scroll/zoom/overlay) | `QAbstractScrollArea` / `QGraphicsView` — excellent | workable, more manual |
| Annotation overlay + input | strong (event model, painters) | adequate |
| i18n | Qt Linguist maps ~1:1 onto our catkeys workflow | gettext-ish, looser |
| Printing | `QPrinter` (clean) | `wxPrinter` |
| poppler integration | **official `poppler-qt6` bindings exist** | none (use raw Splash) |
| Dependency weight | heavier (ship Qt libs) | lighter |
| License vs GPLv2+ | LGPL — compatible | wxWindows licence — compatible |

**Pick Qt 6.** The custom page view, annotation overlays, i18n, and printing all
map cleanly, and it's the most productive for the dialog-heavy UI. wxWidgets is a
fine fallback if Qt's footprint is unwanted.

**poppler-qt6 caveat — decision: do NOT use it.** It's tempting (less code), but
it would fork the render path away from the Haiku/KevRexOS `SplashRenderer`. Keep
**one** renderer: the core's `SplashRenderer` produces an RGBA buffer on *every*
platform; the Qt frontend just wraps that buffer in a `QImage`. Maximum sharing,
one place for render bugs. (Revisit only if Splash perf on Windows disappoints.)

---

## 6. Phased roadmap

### Phase 0 — Core extraction (shared; the gate for everything)
Done once, on Haiku, where it stays continuously testable.
1. Create `core/` and move Tier 1 files in; build a `libnimblepdf-core` static lib
   the Haiku app links. App still passes all current tests → proves no behavior
   change.
2. Define the seam interfaces (§4); make Haiku implement them (mechanical — wraps
   existing `B*` calls). Route `Settings`/attributes through `ISettingsStore`,
   rendering through `IRenderTarget`.
3. **Split `PDFView`**: extract `DocumentController` (non-UI) into core; Haiku
   `PDFView` becomes a thin `BView` over it. **This is the critical-path refactor**
   — everything downstream rides on it. Validate on Haiku before any new frontend.
4. Split `NimblePDFApplication` into core-init + app-shell.
**Exit:** Haiku app unchanged in behavior, but built as `core + haiku-frontend`.

### Phase 1 — Linux / Qt (do first; cheapest mature target)
1. CMake build of `core` + a `qt/` frontend on Linux; poppler from distro
   (`libpoppler-dev`).
2. Minimal viewer: `QMainWindow` + a scroll-area view drawing the `SplashRenderer`
   buffer (`QImage`). No chrome.
3. Navigation (scroll/zoom/page/rotate) via `DocumentController`.
4. Implement the seams: `QSettings` + sidecar store, `QFileDialog`,
   `QFileSystemWatcher`, `QPrinter`, `QClipboard`.
5. Port the dialog zoo + toolbar/menus to Qt widgets.
6. i18n: convert catkeys → Qt `.ts`; `tr()` wraps.
7. Package: AppImage + `.deb`/Flatpak.
**Exit:** feature-parity Linux build. This validates the core split on a second
toolkit and de-risks Windows.

### Phase 2 — Windows / Qt (mostly free once Phase 1 works)
1. Same `qt/` code; build with MSVC (or MinGW) + CMake.
2. poppler on Windows via **vcpkg** (`poppler[core]`) or an MSYS2/prebuilt lib —
   the only real new build chore.
3. Replace POSIX-isms surfaced in the core (paths, `pread`, etc.) — should be
   minimal if the core stayed clean; audit `BeFontEncoding`/`SplashRenderer`/IO.
4. Windows specifics: file association + `.ico` (already have it in `assets/icon/`),
   high-DPI, installer (Inno Setup / WiX), bundle Qt + poppler DLLs.
**Exit:** Windows installer. Delta vs Linux is build + packaging, not code.

### Phase 3 — KevRexOS / os:: (gated)
Per `KEVREXOS_PORT_ANALYSIS.md`. Gate on (a) poppler-on-KevRexOS finished and
(b) an `os::Window` "hello" proving the appserver works on screen. Then:
recompile core → port `SplashRenderer`→`os::Bitmap` (headless page→PNG) →
minimal viewer → shell → fill widget gaps (toolbar/outline/split/tab/card are the
known `os::` gaps). Localization optional/deferred; ship pre-rendered icons (no
HVIF on KevRexOS).

---

## 7. The critical refactor: splitting `PDFView` (2705 lines)

This is the make-or-break item; the rest is mechanical once it's clean.

**Moves to core (`DocumentController`):** current page / zoom / rotation state,
page geometry, device↔user coordinate transforms, hit-testing (links,
annotations, text selection geometry), navigation (`MoveToPage`, history wiring,
named destinations), and orchestration of `PageRenderer` (request page → receive
RGBA buffer).

**Stays in the platform view (thin):** the actual widget, `Draw`/`paintEvent`
blitting the cached buffer, scrollbar plumbing, raw mouse/keyboard events
forwarded to the controller, cursor changes.

Do it **on Haiku first**, keeping the Haiku app green throughout — it's a
refactor, not a rewrite, and the Haiku build is the regression oracle.

---

## 8. Risks

| Risk | Platform | Mitigation |
|---|---|---|
| `PDFView` split leaks UI into core / behavior drift | all | do it on Haiku with the live app as regression test |
| Per-doc state with no xattrs | Linux/Win | `ISettingsStore` central/sidecar store from day one |
| poppler build on Windows | Win | vcpkg `poppler`; budget time for it |
| Splash render perf at high DPI | Win/Linux | tile/cache; consider poppler-qt6 only if needed |
| Annotation editing (custom overlay interaction) | all | most intricate UI; port after read-only viewer works |
| Appserver maturity / widget gaps | KevRexOS | gate on GUI smoke test (companion doc §7) |
| GPL compliance when bundling | Win/Linux | Qt LGPL (dynamic-link or comply), poppler GPL — fine for a GPL app; ship source offer |

---

## 9. Sequencing discipline (what NOT to do)
- Don't scatter `#ifdef __HAIKU__` / `_WIN32` through the existing files. The split
  is **folders** (`core/` vs `<frontend>/`) + **interfaces**, not preprocessor soup.
- Don't start a frontend before Phase 0's `PDFView` split lands — you'd port a
  moving target.
- Don't fork the renderer (no poppler-qt6) — one `SplashRenderer` for all.
- Keep the poppler-interop files in upstream style (memory
  `feedback-nimblepdf-poppler-style`); they port by swapping the buffer target.

---

## 10. Effort sense (relative, not calendar)
- **Phase 0** — moderate, and the riskiest *intellectually* (the `PDFView` split).
  Highest leverage: pays off for all three targets.
- **Phase 1 (Linux/Qt)** — the largest *volume* (the whole Qt UI), but mechanical.
- **Phase 2 (Windows)** — small once Phase 1 is done: build + packaging.
- **Phase 3 (KevRexOS)** — moderate, mechanical `B*`→`os::`, gated on OS readiness.

Recommended order: **Phase 0 → Linux → Windows → KevRexOS.** Linux first because
Qt+poppler are trivially available there, so it shakes out the core extraction
fastest; Windows then rides the same Qt code; KevRexOS slots in when its GUI is
ready.

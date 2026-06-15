> [!NOTE]
> An LLM was used to aid in development of this analysis.

# NimblePDF → KevRexOS port analysis

**Status:** pre-planning / scoping. Created 2026-06-11 for review. No port work
has started; this is a feasibility + preparation map.

**Target:** KevRexOS — a young 64-bit OS derived from Syllable (itself AtheOS
lineage). GUI API is the `os::` namespace toolkit (`/home/kevin/Code/Syllable/
syllable-org/system/sys/include/{gui,util,storage}`). KevRexOS rebrands
Syllable's `atheos/` headers under `kevrexos/` (confirmed: `kevrexos/types.h`).

See `diagrams/kevrexos-port-surface.svg` for the layered picture.

---

## 1. Verdict

**Feasible, moderate effort. The risky part (the PDF engine) is essentially
solved; the work is a large-but-mechanical GUI/app-shell rewrite from `B*` to
`os::`.**

Two things make this much easier than the Linux port the style guide §9.2
contemplates:

1. **Shared BeOS lineage.** Haiku and Syllable/`os::` are both BeOS-descended,
   so the object model is conceptually 1:1: Application/Looper/Handler/Messenger,
   Window/View, Message, Rect/Point, Bitmap, File/Node/Attribute, NodeMonitor,
   Locker/AutoLocker all have direct counterparts. This is a *translation*, not
   a *re-architecture*.
2. **poppler 25.12.0 — NimblePDF's exact target — is being ported to KevRexOS
   now.** The poppler port is **in progress and will be complete before the
   NimblePDF port begins** (per Kevin). The core is already proven headless:
   `build/poppler-port/popplersmoke.cpp` opens a PDF via the public API
   (`PDFDocFactory`), reports page count + media box, and extracts page-1 text
   through `TextOutputDev` (exercising FreeType) — all PASS, linked against the
   staged `libpoppler.so.155`. So by port time the scariest dependency is a
   given, not a risk.

The cost lives in the **Haiku-bound shell (~55% of LOC)** — every `BWindow`/
`BView` subclass, the application object, settings, and the dialog zoo — plus a
small but pivotal **render-glue rewrite** (`BeSplashOutputDev` → `os::Bitmap`).

Rough proportions (from the coupling inventory in §6):

| Layer | Share | Effort |
|---|---|---|
| Portable core (poppler + std C++) | ~35% | recompile |
| Interop / render glue | ~10% | rewrite bitmap/thread/message target |
| Haiku-bound GUI/app shell | ~55% | port `B*` → `os::` |

---

## 2. What's already in place on the KevRexOS side

- **poppler port in progress** (`build/poppler-port/poppler-25.12.0/`),
  `libpoppler.so.155` staged, `kros-cmake-defs.h` toolchain shim. Will be
  finished first.
- **Headless core validated** (`popplersmoke`): PDFDoc open, page geometry,
  TextOutputDev + FreeType text extraction.
- **Observed OS-youth caveats** (from the smoke-test source, worth tracking):
  - `sys_pread` is `_sys_nosys`; libc emulates via `lseek`+`read` (works, but
    poppler's `FileStream`/`GooFile` read exclusively via `pread` — confirm
    throughput/correctness on real files).
  - `init` wires child stdio to `/dev/null`; the test logged via a raw
    `__NR_debug_write` (65) syscall to the serial console.
  - 64-bit, custom syscall table.
- **Open unknown — the GUI/appserver.** Syllable ships a full `os::` appserver
  and apps (e.g. `syllable-org/system/apps/.../aedit`, `charmap`). Whether
  KevRexOS's port of the appserver is *functional on screen* yet is unverified —
  the poppler proof was headless. **First port milestone must be a trivial
  `os::Window` "hello" on KevRexOS**, before any NimblePDF GUI work.

---

## 3. API mapping (Haiku `B*` → Syllable `os::`) — verified against headers

Verdict legend: **drop-in** (rename only) · **rework** (different shape) ·
**gap** (missing / build it).

| Area | Haiku | `os::` | Verdict | Notes |
|---|---|---|---|---|
| App / loop | BApplication, BLooper, BHandler, BMessenger | os::Application, os::Looper, os::Handler, os::Messenger | **drop-in** | `MessageReceived()` → `HandleMessage(Message*)`. |
| Window/View | BWindow, BView (`Draw`→`Paint`, `MouseDown`, `Invalidate`) | os::Window, os::View | **drop-in** | Same float coord system, hook methods, AddChild/FindView. |
| Geometry | BRect, BPoint | os::Rect, os::Point | **drop-in** | Same public `left/top/right/bottom`, `x/y`; also IRect/IPoint. |
| Region | BRegion | os::Region | **rework** | Lower-level clip-rect list; likely ignorable for a PDF viewer. |
| Message | BMessage (`AddInt32`/`FindInt32`/`what`) | os::Message (`Add*`/`Find*`/`GetCode`/`SetCode`) | **drop-in** | `Flatten/Unflatten` take a pointer, not a stream. |
| String | BString, `operator<<`, `SetToFormat` | os::String, `Format(fmt,…)` | **rework** | **No `operator<<`** — sweep `<<` chains to `Format()`. |
| **Bitmap** | BBitmap(BRect, color_space), `Bits()`, `BytesPerRow()`, B_RGB32 | os::Bitmap(w,h,color_space), `LockRaster()`→`uint8*`, `GetBytesPerRow()`, CS_RGB32 | **near drop-in** | Same direct-raster model + same colour spaces. **This is why the render swap is cheap.** |
| Menus | BMenuBar/BMenu/BMenuItem | os::Menu/os::MenuItem (no separate MenuBar) | **rework** | Menu is a View; compose the bar manually. |
| Controls | BButton, BCheckBox, BScrollBar | os::Button, os::CheckBox, os::ScrollBar | **drop-in** | Present, Control-derived. |
| Controls | BTextControl, BListView/BOutlineListView, BScrollView, BToolBar, BTabView, BSplitView, BCardView, BLayout | (partial / absent) | **gap** | Biggest widget gaps. StringView + ListView exist; the richer Haiku layout/toolbar/outline/split/tab/card views likely need custom builds. PDFWindow leans on several. |
| File panel | BFilePanel | os::FileRequester (`LOAD_REQ`/`SAVE_REQ`) | **drop-in** | Same messenger-callback workflow. |
| Storage | BFile, BPath, BNode, BDirectory | os::File, os::Path, os::FSNode, os::Directory | **drop-in/rework** | `BNode`→`FSNode`; `entry_ref`→`FileReference`. |
| **Attributes** | BNode `ReadAttr`/`WriteAttr` | os::Attribute (`Read`/`Write`/`ReadPos`/`WritePos`/`GetType`, `fsattr_type`) | **rework, but present** | Extended attributes exist — per-doc state survives. |
| Node monitor | `watch_node` | os::NodeMonitor (`NWATCH_*`) | **drop-in** | Confirmed present. |
| Threads | `spawn_thread`/`find_thread` (C) | os::Thread (subclass, `Run()`) | **rework** | Function → class. `utils/Thread` already wraps this. |
| Locking | BLocker, BAutolock | os::Locker, os::AutoLocker | **drop-in** | |
| Localization | catkeys + B_TRANSLATE | os::Catalog (ID-based) | **rework / optional** | Different model; could defer or drop initially. |
| Resources/icons | rdef + BResources + **HVIF vector icons** (BIconUtils) | os::Resource + `SetIcon(os::Bitmap*)` | **gap (icons)** | **No vector-icon equivalent** — ship pre-rendered bitmaps. |
| Base types | status_t, int32/uint32, off_t | `kevrexos/types.h` equivalents | **drop-in** | `status_t` etc. map. **`B_OK`/`B_*` error codes likely differ — verify.** |

---

## 4. The one swap that unblocks everything: the render pipeline

Current path (Haiku):

```
PDFView::DrawPage → PageRenderer (bg thread)
  → BeSplashOutputDev over poppler SplashOutputDev
  → PDFDoc::displayPage(...) renders into SplashBitmap
  → BeSplashOutputDev::redraw(): blit SplashBitmap scanlines → BBitmap raster
  → async BMessage to PDFView looper → BView::Draw → DrawBitmap(cachedBBitmap)
```

On KevRexOS only the OS-facing tail changes:

- `SplashBitmap → os::Bitmap`: `BeSplashOutputDev::redraw()` already writes raw
  scanlines into the target raster using `Bits()` + `BytesPerRow()`. `os::Bitmap`
  exposes the **same** direct-pointer model (`LockRaster()` + `GetBytesPerRow()`)
  with the **same** `CS_RGB32` colour space → near 1:1.
- `BView::DrawBitmap → os::View::DrawBitmap`.
- async `BMessage`→looper → `os::Message`→looper (drop-in).
- `BitmapPool` (a Haiku resource-limit workaround) → re-evaluate; KevRexOS may
  not need the same pooling.

**`BeSplashOutputDev` is the single highest-leverage file.** Get it rendering
into an `os::Bitmap` (prove it by writing a page to a PNG headless), and the
rest of the port is "just" UI.

---

## 5. What to do NOW to prepare (without starting the port)

**Framing:** the style guide §9.2 is explicit — *do not* pre-emptively scatter
`#ifdef __HAIKU__` or build a compat layer piecemeal; that's a single future
refactor done all at once. So "prepare" means **keep the eventual port cheap**,
not start it. All of the below are no-regret and help the Haiku version too.

1. **Hold the poppler boundary (§9.3).** The portable core is the payoff of the
   whole poppler migration — protect it. No poppler calls in Window/View
   classes; keep poppler types wrapped at the lowest layer. `TextConversion` is
   the legitimate `BString`↔`GooString` seam — keep conversion there, don't let
   `std::string`/`GooString` creep up into UI code.
2. **Keep the existing single-file seams single.** These are already chokepoints
   — *keep* them that way and route all new code through them:
   - **Logging** → `Trace.h` (one header; swap `syslog` for the KevRexOS
     equivalent later).
   - **Resources/icons** → `utils/ResourceLoader` (one place that touches
     `BResources`/HVIF).
   - **Threading** → `utils/Thread` (use it for new threads; don't sprinkle raw
     `spawn_thread`).
   - **Rendering** → `BeSplashOutputDev::redraw()` is the *only* place that
     should touch the OS bitmap type. Don't blit anywhere else.
   - **Settings / attributes** → `Settings.cpp` +
     `NimblePDFApplication::UpdateFileAttributes()`. These scatter `ReadAttr`/
     `WriteAttr` slightly; consolidating attribute access here later is the one
     refactor worth doing *before* a port (it becomes the os::Attribute seam).
3. **Don't deepen coupling in new work.** Async via the existing
   PageRenderer looper/handler pattern, not ad-hoc `BMessage` sites; keep the
   (mostly portable) `Annotation` model free of `BView` calls — rendering stays
   in `AnnotationRenderer`.
4. **Stay disciplined on Haiku type aliases** (`status_t`, `int32`, `off_t`,
   §9.1). A future typedef layer maps these to `kevrexos/types.h` for free;
   raw `int`/`unsigned` would fight that.
5. **Keep vector/bitmap masters for every icon.** You're mid icon work
   (`nimblepdf_icon.ico`, light/dark logos). Since KevRexOS has **no HVIF**,
   keep an SVG/bitmap master per icon so a KevRexOS icon set is an export, not a
   redraw. (You already keep SVG sources — good.)
6. **First KevRexOS milestone is an `os::Window` smoke test** (separate from
   NimblePDF) to confirm the appserver works on screen. Worth doing opportun-
   istically whenever KevRexOS GUI is claimed ready.

**Explicitly NOT now:** a `B*`→`os::` abstraction layer, `#ifdef` platform
blocks, or any renaming. Per §9.2 that's one deliberate future pass.

---

## 6. Suggested port sequence (when poppler-on-KevRexOS is done)

1. **Prove the GUI exists** — trivial `os::Window` hello on KevRexOS.
2. **Recompile the portable core** (§ band ①) against staged `libpoppler`.
3. **Port `BeSplashOutputDev` → `os::Bitmap`**; headless page→bitmap→PNG.
4. **Minimal viewer** — one `os::Window` + `os::View` showing a rendered page,
   no chrome.
5. **Navigation** (scroll/zoom/page), then the **dialog/window shell**, then
   **fill the widget gaps** (toolbar, outline list, tabs/split as needed).
6. **Settings via `os::Attribute`**; decide on localization (port `os::Catalog`
   or ship English-only first).

---

## 7. Biggest risks / unknowns (ranked)

1. **KevRexOS appserver/GUI maturity** — the poppler proof was headless; on-screen
   `os::Window` is unverified by this project. Gate the port on a GUI smoke test.
2. **Widget gaps** — BToolBar, BOutlineListView, BTabView, BSplitView, BCardView,
   BTextControl, BScrollView. PDFWindow/OutlinesWindow/PreferencesWindow depend
   on these; expect custom `os::View` builds.
3. **HVIF icons** — none on KevRexOS; pre-render bitmaps.
4. **`pread` maturity** — poppler reads via `pread`; KevRexOS emulates it. Verify
   on large/real PDFs.
5. **Error constants** — `status_t` maps but `B_OK`/`B_*` likely differ; audit.
6. **Localization model mismatch** — catkeys → ID-based `os::Catalog`.
7. **String formatting** — `BString operator<<` → `os::String::Format()` (broad
   but mechanical).

---

## 8. Appendix — per-file coupling ranking (source of the §1 proportions)

**PORTABLE** (poppler/std; recompile): AnnotWriter, AnnotAppearance,
BeFontEncoding, EmbeddedFileSpec, TextConversion (BString at the boundary only),
History, TreeParser, PageLabels, Annotation (model logic; rendering excluded).

**MOSTLY-PORTABLE** (poppler + thin Haiku glue): BeSplashOutputDev (render
swap), PageRenderer (thread + async msg), BitmapPool, CachedPage, PageCache,
PDFSearch, DisplayCIDFonts, PDFPrint.

**HAIKU-BOUND** (GUI/app/storage shell; port to `os::`): PDFWindow, PDFView,
NimblePDFApplication, Settings, AnnotationRenderer, EntryChangedMonitor,
LayoutUtils, and the dialog set — OutlinesWindow, FindTextWindow,
PreferencesWindow, FileInfoWindow, PasswordWindow, PrintSettingsWindow,
PrintingProgressWindow, StatusWindow, TraceWindow, AnnotationWindow,
AttachmentView — plus utils/{Thread, ResourceLoader, EntryMenuItem}.

> Note: `BeSplashOutputDev`/`BeFontEncoding` etc. are the poppler-interop layer
> and are deliberately left in upstream style for diff-comparability against
> poppler upgrades (see memory `feedback-nimblepdf-poppler-style`). That does
> **not** change for the port — they port by swapping the OS bitmap target, not
> by restyling.

> [!NOTE]
> An LLM was used to aid in development of this review.

<!--
Generated 2026-06-15 by a multi-agent review workflow: 8 per-subsystem
reviewers -> per-finding adversarial verification (skeptical) -> synthesis.
62 confirmed defects from 112 candidates across ~22 Haiku app files; the
poppler-interop layer (BeSplashOutputDev, AnnotWriter, Annotation, etc.) was
excluded. Severities are the verifiers' adjusted values; the synthesizer
re-grouped a few items by real-world impact.
-->

# NimblePDF Pre-Release Code Review

## Executive Summary

NimblePDF is in solid structural shape for a fork mid-migration (xpdf→poppler) and rebrand, but the review surfaced two genuinely release-blocking defects plus a cluster of memory-safety and lifetime hazards that cluster around the app's background-threading model. The single most damaging bug is an inverted dirty-flag comparison in the settings macros that silently discards nearly every persisted preference; alongside it sits a guaranteed NULL dereference on every successful document open. The dominant *category* of risk is unsynchronized cross-thread access to poppler `PDFDoc`/`XRef` and to UI-owned objects passed as raw pointers into detached worker threads (save, find, attachment, render), which manifest as intermittent use-after-free crashes today. A secondary theme is heavy reliance on Haiku-only idioms — BFS extended attributes, raw-pointer BMessage passing, `system()` launches, BLocker recursion, and 32-bit BGRA pixel punning — that will not survive the planned Qt port unaffected and are collected separately below.

---

## Critical

- **Settings.h:35-40 — Setter dirty-flag logic inverted (`==` instead of `!=`).** `DEFINE_SETTER` sets `fChanged = fChanged || (_##name == name)` before assigning, marking the object dirty only when the value is *unchanged*. Because `GlobalSettings::Save()` (Settings.cpp:145) writes only when `HasChanged()` is true, nearly every preference changed through these generated setters (zoom, rotation, DPI, window positions/sizes, panel flags) silently fails to persist. The hand-written `SetPanelDirectory` (Settings.cpp:58) correctly uses `!=`, confirming the regression. **Fix:** change the comparison to `(_##name != name)` (and the string setter at line 157 to `(_##name != string)`).

---

## High

- **Settings.h:154-159 — String setter dirty-flag logic also inverted.** `DEFINE_STRING_SETTER` repeats the same `==` bug, so the Author string (the only `STRING_SETTINGS` entry) is never persisted on change unless another setting happened to dirty the object. **Fix:** `fChanged = fChanged || (_##name != string);`

- **PreferencesWindow.cpp:442-452 — WORKSPACE_CHANGED reads a non-existent `index` field; workspace selection silently ignored.** Menu items are built with a bare `new BMessage(WORKSPACE_CHANGED)` (lines 65/72) carrying no `index`, but the handler gates its entire body on `msg->FindInt32("index", &index) == B_OK`. Haiku does not auto-inject a positional `index` into radio-mode menu invoke messages, so `FindInt32` always fails and selecting a workspace does nothing. **Fix:** add `msg->AddInt32("index", i)` when building each item (and adjust the index→workspace arithmetic), or read the marked item from the menu in the handler.

- **PrintSettingsWindow.cpp:151 — Menu marking dereferences `ItemAt()` without NULL check; DPI index map has unhandled cases.** The DPI switch maps stored DPI 600→i=4 and 720→i=5, but the resolution menu only contains indices 0–3 (Max, separator, 72, 300). A persisted DPI of 600/720 yields `ItemAt(4) == NULL` and an immediate NULL dereference in the constructor every time the window opens. **Fix:** add the 600/720 menu items to match the index map, or clamp/validate the stored DPI and NULL-check every `ItemAt()` before `SetMarked`.

- **NimblePDFApplication.cpp:634-646 — NULL dereference of `fWindow` after a failed/encrypted open.** When the first open fails (`!ok`) and `fWindow` was NULL, the freshly-created `win` is deleted and `fWindow` stays NULL; execution falls through to the page-jump block, which unconditionally calls `fWindow->MessageReceived(&goToPageMsg)` whenever `pageNum != 0`. Since `ArgvReceived` defaults `pg=1`, the common launch path crashes. The encrypted path (creates a PasswordWindow, leaves `fWindow` NULL) hits the same crash. **Fix:** gate the page jump on success and non-NULL: `if (ok && fWindow != NULL && pageNum != 0)`, placed inside the success branch.

- **PDFView.cpp:724-731 — `SetViewCursor` (and `LoadFile`) called from the constructor before the view is attached; NULL `Window()`.** `PDFView` is constructed in `CreateMainView` before its parent is added to the window, so `Window()` returns NULL for the whole constructor body. `LoadFile` unconditionally calls `Window()->IsLocked()` (line 342) and `SetViewCursor` calls `Window()->Lock()` (line 726) — a guaranteed NULL deref on every document open. **Fix:** NULL-check `Window()` before locking in `SetViewCursor`/`InitViewCursor`/`SetSelection`, defer the initial cursor set to `AttachedToWindow`, and document the attachment invariant.

- **PDFView.cpp:2660-2705 — Background save thread captures a raw `FileAttachmentAnnot*` the window thread can free.** `SaveFileAttachment` passes the annotation as a raw pointer in a BMessage to a detached `SaveFileAttachmentThread`, which later calls `fileAttachment->Save(GetXRef(), ...)`. Meanwhile the window thread can delete the annotation, reload the document (`delete fDoc` rebuilds the XRef), or quit — with no lock or join. The author's own `// TODO validate pointer` flags it. **Fix:** resolve/serialize the attachment bytes on the window thread before spawning, or hold a strong reference and guarantee the `PDFDoc`/`XRef` outlives the worker (ref-count the doc, or join the thread before `LoadFile`/destruction).

- **PDFSearch.cpp:244 — Mismatched `delete` on array allocated with `new[]` (heap corruption).** The Unicode buffer from `Utf8ToUnicode()` is allocated with `new Unicode[*length]` (TextConversion.cpp:130) but freed at the `done:` label with scalar `delete u`. This is undefined behavior on every Find operation. **Fix:** `delete[] u;`

- **PDFSearch.cpp:224-247 — `Window()` dereferenced without NULL check from the find thread; window may be gone.** `FindThread` runs on a background thread and unconditionally calls `Window()->Lock()` (224) and `Window()->PostMessage(...)` (245). `QuitRequested` waits only for the page renderer, never joins the find thread, so the window can be torn down mid-search → NULL/dangling deref. The `fStopFindThread` bool is also read cross-thread with no synchronization. **Fix:** capture a `BMessenger` up front and post through it, or block-join the find thread before the window quits; make the stop flag atomic.

- **Settings.cpp:229-247 — `realloc` failure leaks the original buffer (and then NULL-derefs) in the bookmark-read loop.** `buffer = (char*)realloc(buffer, buf_size)` overwrites the pointer; on failure the original allocation leaks. Worse, `attr_size` still equals the prior `buf_size` (≥65536), so the `<= 0` guard does not fire and execution reaches `bookmarks.Unflatten(buffer)` with `buffer == NULL`. **Fix:** `char* tmp = (char*)realloc(buffer, buf_size); if (!tmp) { free(buffer); buffer = NULL; break; } buffer = tmp;`

- **ResourceLoader.cpp:95-97 — `LoadVectorIcon` dereferences `AppResources()` without a NULL check.** Every other loader in the file guards the `BResources*`; `LoadVectorIcon` calls `resources->LoadResource(...)` directly. `AppResources()` returns NULL with no resource fork / before `be_app` is ready, crashing on the modern vector-icon path. **Fix:** `if (resources == NULL) return NULL;` before use.

---

## Medium

- **PDFView.cpp:341-350 — `LoadFile` unlocks the window during a long load while the looper keeps dispatching to the view.** `OpenFile()` runs `delete fDoc; fDoc = newDoc` with the window unlocked, so queued messages (`MoveToPage`, `OnLink`, `DisplayLink`, `MouseDown`) can dereference a half-replaced `fDoc`/`fPage`/`fNimblePDFAcroForm`. Only `Draw()` honors `fLoading`. **Fix:** guard every `fDoc`/`fPage`/`fNimblePDFAcroForm` access on the window thread with `fLoading`, or load on a dedicated worker and post a completion message so all `fDoc` mutation stays on the window thread.

- **PDFView.cpp:1846-1858 — `PostRedraw` clears `fRendering` without verifying the id matches the current render.** Unlike `RedrawAborted` (which checks `fRendererID == id`), `PostRedraw` checks only `id != -1`. A stale `UPDATE_MSG` from a prior render thread (queued before it exited; `Wait()` does not drain the looper queue) wrongly marks the new render finished and invalidates against a not-ready page. The `// TODO` at 1848 acknowledges it. **Fix:** mirror `RedrawAborted` — act only when `id == fRendererID`.

- **PageRenderer.cpp:62-69,363-374 — `RedrawCallback` posts `UPDATE_MSG` while the bitmap is mid-render and shared with the window thread.** The render thread keeps drawing into `fBitmap` while the looper blits the same `BBitmap`; the window handler does not take `gPdfLock`, so the read races the write. On Haiku this produces torn frames; on the Qt port (non-thread-safe `QImage`/`QPixmap`, no app_server cushion) it becomes a hard crash. **Fix:** acquire `gPdfLock` (or a dedicated bitmap lock) in the window handler before reading the bitmap; on the Qt port snapshot the rendered region into a thread-owned buffer before posting.

- **PageRenderer.cpp:363-373 — `Notify()` dereferences `fLooper` without a NULL check.** Both DEBUG and release branches call `fLooper->PostMessage()`; `fLooper` is NULL until `SetListener()`. Today it is only coincidentally safe (the `init` guard); any reorder that starts rendering before `SetListener`, or a torn-down listener, is a NULL deref / use-after-free. **Fix:** `if (fLooper == NULL) return;` in both branches; prefer a `BMessenger` (validates the target) and abort+wait the renderer before destroying the listener.

- **PageRenderer.cpp:178-190 — `ResizeBitmap` constructs `BBitmap` directly with no validity guard.** A failed large allocation yields an invalid bitmap that is then `Lock()`ed and drawn into in `Render()`, crashing. `BitmapPool` exists precisely to pre-check allocation size but is bypassed here. **Fix:** check `fBitmap->IsValid()` after construction; on failure set an error/empty state, post `ABORT_MSG`, and bail out of `Start()` — or route the allocation through `BitmapPool`.

- **PDFWindow.cpp:1954-1969 — `SaveFileThread` touches `PDFDoc`/`PageRenderer` and walks the XRef off the window thread without `gPdfLock`.** `WriteTo()` mutates the shared XRef/Catalog (`setModifiedObject`, `removeIndirectObject`, `saveAs`) while `PageRenderer::Render()` holds `gPdfLock` over the same structures; the pre-save `SyncAnnotation` is locked but the lock is released before the thread spawns. (Note: `BAlert::Go()` from the worker is *fine* on Haiku — ignore that part of the original write-up.) **Fix:** hold `gPdfLock` for the duration of `WriteTo()`, or snapshot the document under the lock before spawning.

- **PDFView.cpp:1461-1481 — `actionLaunch` runs the link target via `system()` with a shell `&` suffix (command injection).** The raw PDF-supplied filename and params are concatenated and passed to `system()` (which invokes `/bin/sh -c`) with no escaping; a crafted filename (`foo; rm -rf /boot/home`) executes arbitrary commands on user confirmation. **Fix:** replace `system()` with `be_roster->Launch()` passing an explicit argv array, drop the `&` shell idiom, and guard the feature behind a setting. (See also the portability section.)

- **PDFView.cpp:1372-1376 — `IsLinkToPDF` dereferences `getFileName()` without a NULL check.** Poppler's `LinkGoToR::getFileName()` can return NULL (its own `isOk()` proves it), so `getFileName()->c_str()` crashes on a crafted GoToR link with a missing file spec. Same unguarded pattern at line 1684. **Fix:** NULL-check the `GooString*` before `c_str()`; return false when absent.

- **PDFView.cpp:2404-2405 — Drag-to-file `BFile` write omits `B_CREATE_FILE` and ignores `InitCheck`/`Write` results.** `BFile(&d, name, B_WRITE_ONLY)` on a non-existent drop target fails `InitCheck`; `Write()` silently no-ops and the user believes the text was saved. The bitmap-save path (2461-2462) has the same defect. **Fix:** add `B_CREATE_FILE | B_ERASE_FILE`, check `InitCheck() == B_OK`, verify `Write()`/`Translate()` returns, and surface failures.

- **NimblePDFApplication.cpp:642-646 — Window method called directly from the application thread instead of posting.** `RefsReceived` calls `fWindow->MessageReceived(&goToPageMsg)` directly from the BApplication looper, bypassing the window's queue and looper lock and racing its own thread. The success branch above correctly locks around `LoadFile`. **Fix:** `fWindow->PostMessage(&goToPageMsg);`

- **NimblePDFApplication.cpp:730 — Array allocated with `new[]` freed with scalar `delete` (UB).** `argvCopy` is `new char*[argc]` (702) but freed with `delete argvCopy` (730); the early-return at 711 also leaks it. **Fix:** `delete[] argvCopy;`, or drop the shallow-copy array and read `argv` directly.

- **EntryChangedMonitor.cpp:61-87 — Node-monitor handler never calls `NotifyListener()`; the on-disk-change reload feature is dead.** `MessageReceived` filters down to `B_STAT_CHANGED` then returns; the body is an unfinished TODO. The rest of the feature (listener registration, `EntryChanged` handler) is fully wired but unreachable. **Fix:** record stat (size + mtime) at `StartWatching`; in `MessageReceived` re-stat and only notify when size/mtime actually changed, ignoring attribute-only changes.

- **PDFPrint.cpp:291-296 — Integer division computing the fallback print scale.** `fScale` is `double` but `xdpi/72`, `ydpi/72`, `300/72` use int operands, so `300/72` stores `4.0` not `4.166…`; output renders at a coarser DPI. Line 114 already does it correctly. **Fix:** use `/ 72.0` on all three lines.

- **PDFPrint.cpp:110 — `const char* title` cast to `char* fTitle` with no ownership/lifetime guarantee.** `fTitle` aliases `PDFView::fTitle`'s buffer; the detached print thread outlives `Print()`, so reopening a PDF (`delete fTitle`) or closing the view dangles `fTitle` before `BPrintJob(fTitle)` reads it. **Fix:** copy the title into an owned `BString` member and free it in a (new) `PrintView` destructor.

- **History.cpp:110-116 (and UpdateFile 150-157) — `History::fFile` holds no reference count → use-after-free.** `fFile` is a raw pointer into a `HistoryFile` owned entirely by `HistoryPosition` refcounts; when the last referencing position is destroyed (Add's 101-item truncation) the file self-deletes, dangling `fFile`. A subsequent `AddPosition` calls `fFile->IncreaseUseCount()` on freed memory. **Fix:** have `History` hold its own count — `IncreaseUseCount` when assigning `fFile` (in `SetFile` and `UpdateFile`), `DecreaseUseCount` when overwriting, and drop the manual `delete fFile`.

- **PDFSearch.cpp:43-73 — `FindThread` captures raw `PDFDoc*`/`PDFView*`/`FindTextWindow*` with no lifetime ownership.** It calls `doc->displayPage()`/`getNumPages()` on the worker while the window thread can `delete fDoc` via `LoadFile`/destructor with no lock and no join (`StopFind` only sets a flag, never joins). Opening a new file mid-search is a use-after-free. **Fix:** hold the PDF lock around all `PDFDoc`/page access on the worker and ensure the doc/view cannot be freed until the find thread joins.

- **PDFSearch.cpp:229-231 — `CopySelection` reads window-owned state off the window lock.** `FindThread` calls `CopySelection()` (239, and on the `found:` fast path) without a lock; it reads `fRendering`, `fSelected`, `fSelection`, `fBitmap`, `fWidth`, `fHeight` — all owned by the looper thread — racing a concurrent render. (`FindText` and `SetSelection` are *not* races: the page text is stable post-READY and `SetSelection` self-locks.) **Fix:** wrap the `CopySelection` call (or the field reads inside it) in `Window()->Lock()`/`Unlock()`, or marshal the result to the window thread.

- **OutlinesWindow.cpp:529-565 — Unreachable page-ref branch and dead `deleteLink` flag.** `link` is non-NULL only inside `if (item->isDest())`, so the first `if (link != NULL)` consumes every valid link and the `else if (link && link->isPageRef())` at 542 is structurally unreachable; a GoTo dest that is a page reference is posted as a raw `LinkDest*` (DEST_NOTIFY) instead of REF_NOTIFY, and `deleteLink` is never set so the cleanup at 564 is dead. **Fix:** inspect a non-NULL link — `isPageRef()` → REF_NOTIFY else DEST_NOTIFY — and remove or wire up `deleteLink`. (Same root issue noted at 104-109; deduplicated here.)

- **OutlinesWindow.cpp:530-535 — Raw `LinkDest*` posted across threads with a documented but unfixed lifetime race.** DEST_NOTIFY posts a `LinkDest*` owned by the `OutlineListItem`; the in-source XXX comment admits that loading a new document (which `MakeEmpty`s the items and `delete`s the `LinkDest` in the destructor) before the looper handles the message dangles the pointer, then `GotoDest(dest)` use-after-frees. **Fix:** copy the resolved destination (page num / named-dest string / page-ref num+gen) into the BMessage fields, or include a document-generation token the receiver validates. (Portability landmine too — see below.)

- **CachedPage.cpp:159-167,34-41 — `CachedPage::fAnnotations` is a non-owning borrow with murky lifetime.** `PageRenderer::SetDoc` calls `fAnnotations.SetSize(...)` which deletes every `Annotations*`, but `CachedPage::MakeEmpty()`/destructor never clear `fAnnotations`; an unguarded `OverAnnotation()` call (PDFView.cpp:806/1723, when `fEditAnnot` is set) can dereference the freed pointer after a doc switch. **Fix:** clear `fAnnotations` (SetAnnotations(NULL)) in `MakeEmpty` and whenever `SetDoc` rebuilds the list, and NULL-guard `GetAnnotations()` callers.

- **FindTextWindow.cpp:135-139 — `fSearching` set true and left stuck when find text is empty.** `fSearching = true` runs before the empty-text guard returns, with no reset; `QuitRequested` then refuses to close the window (posting a spurious `FIND_ABORT_NOTIFY_MSG`) until another start/stop cycle. **Fix:** move `fSearching = true` after the empty-text guard.

- **FileInfoWindow.cpp:85-106 — `ToDate` overflows its `static char[80]` from attacker-controlled digit runs.** The digit-scan loop (96-99) is unbounded; when `to-from > 12`, `COPYN(to-from-10)` copies that many bytes into the 80-byte buffer with no clamp. A crafted PDF date with ~100+ digits overflows the buffer — reachable by opening a malicious PDF and viewing File Info. (The static buffer is also non-reentrant.) **Fix:** clamp every `COPYN` write to `sizeof(s)-1-j`, reject/truncate absurd digit runs, and replace the static buffer with a caller-supplied buffer or `BString`. (Two near-identical findings at 85-87 and 100-106 deduplicated here.)

- **NimblePDFApplication.cpp:736-822 / Settings.cpp BFS attributes — see Portability section** (per-document state, the largest port landmine, collected below).

- **OutlineListItem (OutlinesWindow.cpp:104-109) — see 529-565 above** (dead `isPageRef` branch; deduplicated). The uninitialized `fLink` union is benign today but should still be zero-initialized in the constructor (`fLink.dest = NULL`) so any future misread is a NULL deref rather than a wild pointer.

- **AnnotationWindow.cpp:159-167 — `FindMarked` default-item fallback is dead code; can return NULL.** When nothing is marked, `menu->ItemAt(0)` is called but its result discarded, so `item` stays NULL and `WriteMessage` silently omits font/size/alignment for new annotations. **Fix:** `if (item == NULL) item = menu->ItemAt(0);`

- **Thread.cpp:46-59 — `Resume()` can `delete this` on a running thread → double-free.** A second `Resume()` finds the thread no longer suspended, then `delete this` races/duplicates the `delete thread` in `DoRun`. Today all callers Resume exactly once, so it is latent, but there is no guard. **Fix:** add a state flag to reject double-Resume; on a genuine resume failure `kill_thread` the suspended thread before deleting, or make `DoRun` the sole owner of deletion and have `Resume()` only report status.

- **AttachmentView.cpp:338-343 — `SaveAttachmentThread` uses `fDoc`/`fXRef` with no lock or lifetime guarantee.** `ActuallySave` calls `fDoc->getCatalog()->embeddedFile(...)` on the worker with no `gPdfLock` (inconsistent with every other poppler call site) and `fDoc` is a raw pointer with no guard against teardown → use-after-free if the user closes/reopens mid-save. (The "leaked thread" sub-claim is wrong — `DoRun` self-deletes.) **Fix:** take `gPdfLock` inside `ActuallySave` and prevent document teardown while a save thread is live.

- **AttachmentView.cpp:233,297-327 — `AttachmentItem*` pointers passed across threads outlive their owner.** Raw `AttachmentItem*` (owned by the `BColumnListView`) are stuffed into the save BMessage and dereferenced on the worker; `Fill()`/`Clear()` on a document switch frees the rows under the worker → use-after-free. **Fix:** copy the primitive data (file index + name string) into the BMessage and operate only on the copies.

- **ResourceLoader.cpp:98 — `LoadVectorIcon` off-by-one allocates a bitmap one pixel too large.** `BRect(0,0,size,size)` yields `(size+1)²` pixels (BRect edges are inclusive); other loaders correctly use `dimension - 1`. With default size=21 every toolbar icon is 22×22. **Fix:** `BRect(0, 0, size - 1, size - 1)`.

---

## Low

- **PDFWindow.cpp:1468-1479 — Missing `break` causes fall-through from `BOOKMARK_ENTERED_NOTIFY` into `QUIT_NOTIFY`.** Adding a user bookmark falls through and runs `delete fAWMessenger; fAWMessenger = NULL;`, orphaning any open annotation window (the next `ShowAnnotationWindow` creates a second one and leaks the first). **Fix:** add `break;` after the `BOOKMARK_ENTERED_NOTIFY` case body. *(Logic bug — only Low because impact is a window leak, not a crash.)*

- **PDFWindow.cpp:253-267 — Leaked `Object` for `PageLabels` in `UpdatePageList`.** `Object* pageLabels = new Object(catDict.dictLookup("PageLabels"))` is passed to `labels.Parse()` and never deleted; leaks one poppler `Object` per document load/reload. **Fix:** make it a stack `Object` and pass `&pageLabels`, or `delete pageLabels` before `Unlock`.

- **PDFWindow.cpp:940-944 — `AnnotationWindow` messenger left stale after `NewDoc` quits the window.** Unlike the sibling FIW/PSW branches, `fAWMessenger` is not reset to NULL after `w->Quit()`, leaking the `BMessenger` object. **Fix:** `delete fAWMessenger; fAWMessenger = NULL;` after `Quit()`.

- **PDFWindow.cpp:242-250,895,1731-1732 — `sprintf` into fixed buffers with translated format strings.** `AddBookmark` and the custom-zoom label format *translated* strings (`"Page %d"`, `"Custom zoom factor (%d%%)"`) into fixed buffers; a translation with an extra/wrong `%`-spec is UB. `SetTotalPageNumber` already uses the safe `snprintf`/computed-length idiom. **Fix:** use `snprintf` with `sizeof(buffer)` and validate translated format strings.

- **PDFWindow.cpp:1107-1132 — `GOTO_PAGE_CMD` reads `page` uninitialized when source is a non-`BTextControl`.** If `FindPointer("source")` succeeds but the `dynamic_cast` fails, `page` (uninitialized at 997) is never assigned yet `MoveToPage(page)` still fires. **Fix:** initialize `int32 page = 0;` and gate `MoveToPage` on a separate `hasPage` flag.

- **NimblePDFApplication.cpp:726 — `get_ref_for_path` return value ignored; invalid CLI path still enqueued.** A non-existent command-line path leaves `fileToOpen` invalid but is still posted, producing a generic "Error opening file" rather than a clear diagnostic. **Fix:** check the return and report/abort before building the message.

- **NimblePDFApplication.cpp:628-635 — Acknowledged stale-`fWindow` logic in the error path.** When an existing window's `LoadFile` fails, the `if (fWindow == NULL)` guard means the open-file-panel fallback never runs (the author's own "fixme" comment). The window gracefully redisplays the prior PDF (PDFView.cpp:352-358), so this is a UX gap, not data loss. **Fix:** track success per branch and decide recovery explicitly rather than gating on `fWindow == NULL`.

- **Settings.cpp:244 — `BMessage::Unflatten` return ignored when loading bookmarks.** A truncated/corrupt `nimblepdf:bookmarks` attribute leaves the message partially populated and treated as valid. ResourceLoader.cpp:84 already checks the return. **Fix:** `if (bookmarks.Unflatten(buffer) != B_OK) bookmarks.MakeEmpty();`

- **AnnotationRenderer.cpp:670-679 — `DoInk` reads `points[0]` without an emptiness check.** An empty Ink path (`n==0`) is parsed as valid and not deleted; `new BPoint[0]` then `MovePenTo(points[0])` is an out-of-bounds read on a crafted/malformed annotation. **Fix:** `if (n <= 0) continue;` before dereferencing.

- **BitmapPool.cpp:112-144 — `getBitmap` does not validate the constructed `BBitmap`.** The `freeMemorySize()` heuristic can be stale, so construction can still fail; the non-validated bitmap is returned, breaking the documented "returns NULL on failure" contract. *(Currently dead code — no callers — hence Low.)* **Fix:** `if (bitmap->InitCheck() != B_OK) { delete bitmap; ...; return NULL; }`

- **PDFPrint.cpp:133-134 — Redundant `startDoc(nullptr)` before `startDoc(doc)`.** The doc is already available in the constructor, so the first call needlessly allocates/frees the font engine. **Fix:** remove the `startDoc(nullptr)` line.

- **FindTextWindow.cpp:180-184 — Scalar `delete` on a `new char[]` buffer.** `delete buffer` should be `delete[] buffer`; UB flagged by sanitizers/MSVC. **Fix:** `delete[] buffer`, or better use `BString::SetToFormat`.

- **PreferencesWindow.cpp:80-83 — `char workspace[3]` too small if workspace count reaches 100+ (dead code).** Latent stack overflow; the variable is never read and Haiku caps workspaces at 32. **Fix:** delete the unused buffer.

- **FileInfoWindow.cpp:274-291 — Unguarded `fState` read outside the per-row lock.** `fState` is read on the PDFWindow thread while written on the FileInfoWindow thread; worst case is one extra row before a stop is noticed (enum read is effectively atomic on x86). **Fix:** read `fState` under the same lock, or document the model.

- **FileInfoWindow.cpp:371-372 — `fFontList`/tab view rebuilt on every `Refresh` without destroying the prior tree.** Loading a second document while the window is open leaks the entire previous view hierarchy and orphans the old buttons. **Fix:** remove/delete the existing tab view before rebuilding, or build once and update contents on subsequent calls; only `Show()` on first construction.

- **AttachmentView.cpp:304-328 — Multi-file save silently drops a file after 49 name collisions.** The postfix loop falls through without saving or reporting if all 50 candidate names exist, while progress still advances. **Fix:** detect the no-slot case and report an error (or use a guaranteed-unique name).

- **StatusWindow.cpp:77 — Division by `fTotal` with no zero/uninitialized guard.** `fTotal` starts at -1; an out-of-order `CURRENT_NOTIFY` or a 0 total yields negative/inf/NaN progress. Current callers send TOTAL before CURRENT (FIFO), so it is latent. **Fix:** `if (fTotal > 0) fStatus->SetTo(p / (float)fTotal);`

- **ResourceLoader.cpp:84-86 — `LoadBitmap` unflattens a resource without using the length.** `Unflatten((char*)bits)` ignores the resource `length`, so a truncated compiled-in resource could read past the buffer (low risk — resources are not user-supplied; the Instantiate cast is actually type-safe here). **Fix:** validate `length` and prefer a sized `BMemoryIO`.

---

## Portability landmines for the Qt port

These are the items that matter most for the upcoming Linux/Windows Qt frontends. Treat as a pre-port checklist:

1. **Per-document state lives in BFS extended attributes (Settings.cpp:181-299 `FileAttributes::Read/Write`; NimblePDFApplication.cpp:771-822 `UpdateFileAttributes`).** Page, zoom, rotation, window frame, and flattened bookmarks are stored via `BNode::ReadAttr/WriteAttr` on `nimblepdf:*`/`META:`/`PDF:` attributes. ext4 xattrs are limited/namespaced, FAT/exFAT have none, NTFS only via ADS — none round-trip. Resume-on-reopen and saved bookmarks silently vanish on the port. **Action:** introduce an `ISettingsStore` seam with a portable backend (path-hash-keyed sidecar or per-user DB) as default, keeping BFS attributes as a Haiku-only optimization behind it. *(Already on the project's port roadmap — implement before the port begins.)*

2. **Link launch via `system()` with a shell `&` (PDFView.cpp:1461-1481).** `/bin/sh -c` does not exist on Windows `cmd`, the `&` background idiom is meaningless there, and the unescaped concatenation is a command-injection vector on every platform. **Action:** replace with a portable argv-based launcher (`QProcess` / `BRoster::Launch`), drop the `&`, keep the confirmation alert, and gate the feature behind a setting.

3. **Raw heap/object pointers passed through asynchronous BMessages across threads** — `LinkDest*` (OutlinesWindow.cpp:530-535), `FileAttachmentAnnot*` (PDFView.cpp:2660-2705), `AttachmentItem*` (AttachmentView.cpp:233). This pattern relies on a single address space and manual lifetime coordination and does not map to Qt's queued-connection/signal model (which copies payloads). **Action:** serialize the needed primitive data (page num / named-dest / num+gen; file index + name; attachment bytes) into the message payload instead of passing owning pointers.

4. **Live render-target `BBitmap` shared between the render thread and the UI thread (PageRenderer.cpp:62-69,363-374).** Haiku's app_server tolerates the torn read; Qt's `QImage`/`QPixmap` are not thread-safe and have no app_server cushion, turning the latent race into a hard crash. **Action:** snapshot/copy the rendered region into a thread-owned buffer before posting it to the UI, rather than sharing the live render target.

5. **32-bit BGRA layout and little-endian `uint32` pixel punning (AnnotationRenderer.cpp:193-227).** `*(uint32*)&rgb_color` comparisons and manual channel swizzles bake in both `B_RGBA32` byte order and host endianness ("Hope this also works on PPC!"). **Action:** read channels by explicit byte index, assert/convert the source to a known format first, and on the Qt port use `QImage::Format_ARGB32` with `QRgb` accessors; remove the type-pun.

6. **`BLocker` recursion assumed in `OutputTracer` (TraceWindow.cpp:269-278).** `WriteData` locks `fLock` then calls `CreateWindow`, which re-locks — fine on recursive `BLocker`, an immediate self-deadlock on a non-recursive `std::mutex`/`QMutex`. The outer `if (fWindow == NULL)` check (line 259) is also unlocked (broken double-checked locking, two threads can construct two windows). **Action:** split into an internal `_CreateWindowLocked()` (lock held by caller), remove the unlocked outer check, and on the port use an explicitly recursive mutex or restructure to lock once.

7. **`new[]`/scalar-`delete` mismatches are UB everywhere and will be flagged by Clang/MSVC + sanitizers** — PDFSearch.cpp:244 (Unicode buffer, *every Find*), NimblePDFApplication.cpp:730 (`argvCopy`), FindTextWindow.cpp:180-184 (page label buffer). **Action:** fix all three to `delete[]`; the Qt/Windows toolchains are far less forgiving than Haiku's allocator.

8. **`static char*` bound to string literals (AnnotationWindow.cpp:129).** Ill-formed in C++11+ and rejected by standards-conformant Clang/GCC/MSVC in a stricter cross build. **Action:** `static const char* const gAlignment[]`. (Also wire up `B_TRANSLATE_MARK` if those alignment labels are meant to be localized — currently they are not collected.)

---

## Recommended pre-release actions

1. **Fix the two settings dirty-flag inversions (Settings.h:38 and :157) immediately** — they silently lose nearly every persisted preference and the Author string, and are one-character changes.
2. **Eliminate the guaranteed/near-certain NULL-deref crashes on common paths:** PDFView.cpp constructor `Window()` use (724-731 + LoadFile 342), NimblePDFApplication.cpp:634-646 failed/encrypted open, PrintSettingsWindow.cpp:151 stored-DPI 600/720, ResourceLoader.cpp:95 vector-icon load, and Settings.cpp:229-247 realloc-then-NULL-unflatten.
3. **Fix the three `new[]`/`delete` heap-corruption mismatches** (PDFSearch.cpp:244, NimblePDFApplication.cpp:730, FindTextWindow.cpp:184) — UB on every Find / launch.
4. **Repair the broken-feature defects** users will notice: workspace selection ignored (PreferencesWindow.cpp:442), bookmark fall-through (PDFWindow.cpp:1468), dead reload watcher (EntryChangedMonitor.cpp), and the print-scale integer division (PDFPrint.cpp:291).
5. **Harden the background-thread lifetime/locking model** as a coordinated pass — save (PDFWindow.cpp:1954, PDFView.cpp:2660), find (PDFSearch.cpp), attachment (AttachmentView.cpp), print (PDFPrint.cpp:110), and render (PageRenderer.cpp) threads all need: `gPdfLock` around poppler access, no raw UI-owned pointers across threads, and a join (or doc/window outlives-worker guarantee) before teardown.
6. **Add the document-content overflow/NULL guards** that a malicious PDF can trigger: `ToDate` buffer overflow (FileInfoWindow.cpp:100), `getFileName()` NULL deref (PDFView.cpp:1372), `DoInk` empty path (AnnotationRenderer.cpp:673), and replace the `system()` launch (PDFView.cpp:1461).
7. **Land the portability seams before starting the Qt port** — at minimum the `ISettingsStore` abstraction over BFS attributes (item 1 above) and the cross-thread message-payload refactor (item 3 above), since both are architectural and expensive to retrofit later.
8. **Mop up the Low-severity leaks and latent bugs** (UpdatePageList Object leak, stale messengers, uninitialized `page`, FileInfoWindow refresh leak, StatusWindow divide guard) in a final cleanup commit.

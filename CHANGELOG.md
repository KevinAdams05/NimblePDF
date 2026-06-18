# Changelog

All notable changes to NimblePDF are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/).

## [2.2.1] - 2026-06-18

A correctness-and-stability release. Text search and text copy — both broken in
2.2.0 — now work, and several crashes are fixed.

### Fixed
- **Preferences were not being saved.** A dirty-flag inversion in the settings
  writer discarded nearly every changed preference. Settings now persist across
  launches.
- **Text search did not work.** The displayed page's text layer was never
  finalized, so Find reported "not found" for text that was clearly present.
  Find now works on the current page and across pages, forward and backward.
- **Copying selected text did not work.** Same root cause as search — selecting
  text and copying it to the clipboard now produces real text.
- **Crash when saving an annotated PDF.** A double-free in the save worker
  crashed the app on every annotation save.
- **Crashes and hangs around Find.** Closing the window or pressing Stop during
  a search could crash or hang the application. The search worker was
  restructured so this is safe, and the main window stays responsive while a
  background search runs.
- **Saving could corrupt the in-memory document**, because the save ran
  concurrently with page rendering; the two are now serialized.
- Additional crash fixes from a pre-release review: opening encrypted or
  unreadable files, print settings, resource/icon loading, and search buffers.
- Corrected in-app help and repository links.

## [2.2.0] - 2026-06-15

The first NimblePDF release, forked from BePDF.

### Changed
- Replaced the xpdf rendering backend with **poppler** (25.12).
- Rebranded from BePDF to **NimblePDF**, with a new application icon and MIME
  type.

### Added
- Native Haiku About window (`BAboutWindow`).
- Localized into 25+ languages.

### Fixed
- Opens to a blank window instead of showing an error when launched without a
  file.

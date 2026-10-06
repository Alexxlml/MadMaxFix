# Changelog

## 1.2.0 - 2026-10-06
First public release.
- **Steam support** (build 2015-10-22) in addition to GOG (build 2015-12-01). On Steam the hooks are installed once the game has decrypted its code in memory; the DRM is not touched.
- Script helpers used by the Scrap Crew fix are verified against the game's own calls before hooking.
- `ScrapCap` is limited to 500, the most the game pays per stronghold.
- Code, log messages, configuration and documentation in English.

## 1.1 (private test build)
- Scrap Crew rewards restored on GOG, based on the real time the game was closed.

## 1.0 (private test build)
- Main-menu Settings fix on GOG (display-mode list limited to 160 unique rows per monitor).

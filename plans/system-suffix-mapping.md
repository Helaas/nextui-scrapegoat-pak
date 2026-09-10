# Implementation Plan — User-Mappable Systems + Suffix Inventory

**Status:** Revised after review; start with baseline capture and inventory verification
**Created / revised:** 2026-09-09
**Base branch:** `feat/universal-device-binary`
**Suggested branch:** `codex/system-suffix-mapping`

---

## 1. Problem and scope

NextUI selects an emulator from a ROM folder's suffix, for example `Game Boy Advance (GBA)`.
It looks for a matching `launch.sh` in an installed emulator pak, with an SD-card pak taking
precedence over a system pak. It has no central suffix allowlist. A suffix identifies an
**emulator association**, which does not necessarily identify one scraping platform.

ScrapeGoat currently uses static suffix tables to determine ScreenScraper and libretro support.
Folders with unknown suffixes scan successfully but disappear from the library browser.

Two deliverables:

1. **User mapping:** select a scraping platform on-device, with both suffix defaults and
   folder-specific overrides. Unknown folders must be visible and actionable.
2. **Inventory and gap closing:** audit NextUI and every Pak Store emulator entry, verify both
   installed suffixes and their platform meaning, and report provider coverage separately from
   unresolved evidence. Keep the audit repeatable with a committed report.

### The GPGX case the design must support

The [GPGX v1.0.0 documentation](https://github.com/Helaas/nextui-gppx-pak/blob/v1.0.0/README.md#roms)
uses all of these folders with the same suffix:

```text
Roms/Mega Drive (GPGX)/
Roms/Master System (GPGX)/
Roms/Game Gear (GPGX)/
Roms/SG-1000 (GPGX)/
Roms/Mega CD (GPGX)/
```

A global `GPGX → megadrive` mapping would misclassify four folders. Ship reviewed candidates for
GPGX without a blanket default. Users can map each folder independently or set a suffix default
for their own library. Hiding one folder must not hide its siblings.

---

## 2. Measured baseline and evidence

These counts describe the reviewed checkout and storefront on 2026-09-09. Capture actual source
revisions in the generated report; do not treat the counts as permanently current.

### 2.1 Current mappings — `src/systems.c`

| Table | Entries | Consumer |
|---|---|---|
| `ss_platforms[]` | 48 | `ss_platform_id()` |
| `libretro_dirs[]` | 33 | `libretro_dir()` |
| `display_names[]` | 51 | `ss_display_name()` |

`ss_display_name()` has no callers in `src/` or `third_party/apostrophe`. Delete it and its table.
The library uses `console_dir.display`, derived from the folder name. New catalog names are used
in the picker and Settings, without replacing library names.

There are **nine calls** to the surviving lookup functions in the current checkout:

```text
src/ui.c:225             ss_platform_id(console->tag)
src/ui.c:226             libretro_dir(console->tag)
src/screenscraper.c:1004  ss_platform_id(console->tag)
src/queue.c:427          libretro_dir(system_tag)
src/queue.c:486          libretro_dir(item.system_tag)
src/queue.c:1049         ss_platform_id(console->tag)
src/queue.c:1094         libretro_dir(console->tag)
src/queue.c:1125         ss_platform_id(console->tag)
src/queue.c:1193         ss_platform_id(console->tag)
```

Migrate these callers: tag-only compatibility shims cannot resolve folder overrides. Re-run `rg`
when implementing; the listed line numbers are navigation hints for this checkout.

### 2.2 NextUI suffixes

Scan **both** layouts in `/Volumes/Storage/GitHub/NextUI`:

```text
skeleton/SYSTEM/*/paks/Emus/*.pak
skeleton/EXTRAS/Emus/*/*.pak
```

The former `skeleton/**/paks/Emus/*.pak` glob finds only base systems: EXTRAS has no `paks` directory.

- Base: 7 distinct suffixes — `FC GB GBA GBC MD PS SFC`.
- EXTRAS: 30 distinct suffixes — `32X A2600 A5200 A7800 C128 C64 COLECO CPC FBN FDS GG LYNX
  MGBA MSX NGP NGPC P8 PCE PET PKM PLUS4 PRBOOM PUAE SEGACD SG1000 SGB SMS SUPA VB VIC`.
- Union: **37 suffixes**. `PET`, `PLUS4`, and `PRBOOM` have no current ScreenScraper mapping.

Record platforms and origins separately when a suffix occurs in multiple directories. Check for
`launch.sh`; report unexpected incomplete pak layouts rather than counting them as working emulators.

### 2.3 Pak Store

Source: [storefront.json](https://raw.githubusercontent.com/LoveRetro/nextui-pak-store/refs/heads/gh-pages/storefront.json).
The reviewed snapshot has 117 `paks` and 4 `experimental_paks`, including **26 EMU entries**.
There is no authoritative suffix field. `release_filename` supplies a candidate, not proof:

- `ScummVM` publishes `SCUMMVM.pak.zip`.
- `A800.zip` and `EASYRPG.zip` lack `.pak` in the asset name.
- `.pakz` bundles may repeat one suffix across devices or contain distinct suffixes. Multiple
  `.pak` directories do not automatically mean multiple distinct suffixes.
- Some repositories assemble all pak directories at release time. GPGX is one example.

The provisional missing-suffix list is:

```text
DICE GPGX GW J2ME MKXPZ NEOCD O2 PORTS SGX SMSU SWAN WSC ZQUEST
```

Verify suffix identity **and platform meaning** independently. Two target corrections are known:

- `SWAN` is SwanStation for **PlayStation**, according to the storefront description and its
  linked [pak repository](https://github.com/discus7l/nextui-swanstation-pak). It is not WonderSwan.
- `SMSU` is Genesis Plus GX with **Mega Drive/Genesis MSU-MD** support; see the
  [pak documentation](https://github.com/SkyZ0ro/Trimui-Brick-GMD). It is not a Master System default.

Bind this evidence to the storefront version inspected during implementation. Keep `SUPERGRAFX`
as a legacy alias and add the verified `SGX` association. Do not delete aliases merely because
their names are absent from current observed pak names.

### 2.4 Runtime and build facts

- `extract_tag()` and `scan_console_dirs()` in `device.c` already accept arbitrary suffixes.
  The library filters them in `show_library_screen()` in `ui.c`.
- Settings live under `{SDCARD}/.userdata/shared/ScrapeGoat/`. Reuse SD-card path resolution
  and directory-creation helpers. cJSON is already vendored and used for settings.
- `launch.sh` enters the pak directory, but resource lookup should use the executable path.
  `daemon.c` already resolves it via `_NSGetExecutablePath` on Mac and `/proc/self/exe` on Linux.
  The daemon forks and then re-execs the binary with `--daemon`.
- `Makefile` discovers C sources automatically. `do-package` must include the catalog alongside
  the existing `resources/bin` contents, including in the universal package.
- `ap_list` supports a secondary action; text filtering requires `ap_keyboard()`.
- Artwork/manual jobs capture `system_id` at enqueue. Cheats resolve their provider directory
  during processing, and their in-memory cache is keyed by suffix. Both cheat behaviors must
  change for two GPGX folders targeting different systems in the same queue.

---

## 3. Design decisions

| Area | Decision |
|---|---|
| Targets | Full bundled ScreenScraper platform list plus reviewed libretro associations, including cheat-only targets where applicable |
| Delivery | Runtime JSON from the pak; no compiled fallback |
| Precedence | Folder override → user suffix default → bundled suffix default |
| Ambiguous suffixes | Reviewed picker candidates; no guessed default |
| User data | Separate override file in userdata, surviving pak updates |
| Entry points | Unmapped library rows and Settings → System Mappings |
| Picker | This-folder scope by default; optional suffix-default scope; suggestions, A–Z, Y search |
| Visibility | User-hidden is a folder preference independent of platform and provider capability |
| Queue | Edits require no pending work; jobs retain provider targets through handoff |
| Inventory | On-demand script and committed report; no CI or live provider lookup on-device |
| Evidence | Release-specific packaging evidence and reviewed overrides; incomplete inputs cannot pass |

Missing/invalid shipped JSON is an explicit startup error. Provider unavailability, an unmapped
folder, and a user-hidden folder are separate conditions.

---

## 4. Phase 1 — Data model, loader, and queue integration

### 4.1 Shipped catalog: `resources/systems.json`

Illustrative valid subset:

```json
{
  "schema": 1,
  "platforms": [
    {
      "id": "megadrive",
      "name": "Mega Drive / Genesis",
      "aliases": ["Mega Drive", "Genesis", "Sega MSU-MD"],
      "ss_id": 1,
      "libretro_dir": "Sega - Mega Drive - Genesis"
    },
    {
      "id": "mastersystem",
      "name": "Master System",
      "aliases": ["Sega Master System"],
      "ss_id": 2,
      "libretro_dir": "Sega - Master System - Mark III"
    }
  ],
  "tags": {
    "MD": "megadrive",
    "SMS": "mastersystem"
  },
  "tag_candidates": {
    "GPGX": ["megadrive", "mastersystem"]
  }
}
```

The full GPGX candidate list also includes Game Gear, SG-1000, and Mega CD after those catalog
entries are seeded. It intentionally has no bundled `tags` entry.

Rules:

- `id` is a permanent local identifier. Existing IDs survive upstream name changes (§8.1).
- `ss_id` is a positive integer or omitted/null when no association is available. `libretro_dir`
  is an exact reviewed `cht/` directory name or omitted/null. Capabilities are independent.
- A known platform with neither capability may still have a catalog entry. Audit evidence must
  distinguish reviewed provider unavailability from provider coverage not yet verified.
- `name` and optional `aliases` support selection/search, preserving library folder names.
- `tags` holds reviewed unambiguous defaults. `tag_candidates` holds reviewed choices for
  associations spanning systems. No second C alias table or weighted fuzzy scorer.
- Neither map contains `ignored`/`hidden` sentinels. A provider limitation is not a user preference;
  an unresolved association must not be labeled verified unsupported.

### 4.2 User overrides and folder identity

File: `{SDCARD}/.userdata/shared/ScrapeGoat/system_overrides.json`.

```json
{
  "schema": 1,
  "tags": {
    "GPGX": "megadrive"
  },
  "folders": {
    "Master System (GPGX)": { "platform": "mastersystem" },
    "Mega Drive Homebrew (GPGX)": { "platform": "megadrive", "hidden": true }
  }
}
```

Folder keys are paths **relative to `Roms/`**, using `/` and preserving spelling/case. The current
scanner yields top-level folders, so keys are their actual directory names. Derive keys with one
helper from the ROM root and `console_path`, checking path-component boundaries and normalizing
relative Mac roots consistently. Reject absolute/empty keys and `.`/`..` components. Do not key
by display name, absolute mount path, or lowercased suffix; do not resolve symlinks in a way that
merges distinct named folders.

The literal directory name includes `.disabled` when present. Renaming a folder, including toggling
that suffix, changes its key: retain the old override for removal in Settings, and use suffix
defaults for the new name until remapped. Changing SD mount location does not change keys.

- Folder `platform` wins over user `tags`, which wins over shipped `tags`.
- `hidden: true` suppresses only that folder and preserves its platform. Showing it removes
  `hidden` without discarding the platform override.
- Clearing a folder mapping removes only `platform`; clearing a suffix default removes its key
  from user `tags`. Lower defaults resurface. Remove empty folder objects.
- Hidden folders remain editable in Settings. No action silently hides every folder of a suffix.
- Absent overrides are normal. Invalid data produces a recoverable warning; preserve the original
  file before a later rewrite so rejected user data is not silently lost (§4.5).

### 4.3 Module and resource location

Implement in existing `systems.c` / `systems.h`; no separate loader module is needed initially.
Extract the existing executable-path resolver to a small shared helper in `device.c` / `device.h`
for systems and daemon. Check buffer limits and platform API failures.

Resource selection:

1. If `$SCRAPEGOAT_SYSTEMS_JSON` is set, use exactly that file. An invalid explicit path is an
   error, not a reason to silently fall back.
2. Otherwise use `<exe_dir>/resources/systems.json`.
3. On `PLATFORM_MAC` only, if the executable-adjacent file is absent, allow repo-root
   `./resources/systems.json`. Never fall back after a parse/validation failure.

Errors name the selected path, or the attempted paths if none exists.

### 4.4 Public API shape

Exact names may follow project conventions; preserve these semantics:

```c
typedef struct {
    const char *id;
    const char *name;
    int ss_id;                 /* -1 when unavailable */
    const char *libretro_dir;  /* NULL when unavailable */
} sg_platform;

typedef enum {
    MAPPING_NONE = 0,
    MAPPING_BUILTIN,
    MAPPING_USER_TAG,
    MAPPING_USER_FOLDER,
} mapping_source;

typedef struct {
    const sg_platform *platform; /* NULL means no selected target */
    mapping_source source;
    bool hidden;                /* independent of platform/source */
} sg_mapping;

int systems_init(void);
const char *systems_last_error(void);
void systems_shutdown(void);

sg_mapping systems_resolve(const char *console_path, const char *tag);
int systems_platform_count(void);
const sg_platform *systems_platform_at(int index);
const sg_platform *systems_platform_by_id(const char *id);

/* NULL platform_id clears the override at that scope. */
int systems_set_tag(const char *tag, const char *platform_id);
int systems_set_folder_platform(const char *console_path, const char *platform_id);
int systems_set_folder_hidden(const char *console_path, bool hidden);

/* Reviewed candidates + simple display-name/alias matches; at most max results. */
int systems_suggest(const char *tag, const char *display,
                    const sg_platform **out, int max);
```

Provide read-only enumeration of saved overrides for Settings to identify which scope can be
cleared and list absent-folder/suffix overrides; do not expose mutable cJSON state to the UI.

Delete old tag-only lookup APIs after migrating callers. Resolve once with folder context where
possible. Catalog objects/strings remain immutable until shutdown; override edits do not rebuild
the catalog or invalidate platform pointers. Join workers before freeing catalog data. Production
mutations use the UI queue guard in §4.6.

### 4.5 Validation, persistence, and suggestions

Use cJSON and ordinary arrays; linear lookup suffices for these catalog/override sizes.

Validate schema, container/field types, unique nonempty IDs, nonempty names, positive integer
ScreenScraper IDs, lengths, and references in defaults/candidates. Reject duplicate JSON keys
that make precedence ambiguous. Provider directory names must be single safe path components,
not traversal paths. A malformed shipped catalog, including dangling references, is fatal.
Initialize all-or-nothing.

For overrides, absent is normal. Malformed JSON/wrong schema loads shipped defaults and warns;
individually invalid entries in otherwise readable data are skipped and identified. Before
rewriting a file with rejected data, save a recoverable copy. If that copy fails, do not overwrite
it. Surface warnings in the UI as well as diagnostics because handheld users may never read stderr.

Persistence is transactional: build and validate proposed state, write a temporary file in the
same directory, check write/flush/close errors, then `rename()` into place. Publish new in-memory
state only after persistence succeeds. Reuse existing directory creation. Failure retains the
previous file and effective mappings and produces a useful error.

Suggestions: exact case-insensitive display-name/alias matches, reviewed suffix candidates/default,
then nonempty substring matches on names/aliases. Deduplicate, break ties by name, and return at
most five. Never auto-save a suggestion. Search also accepts platform IDs. Keep synonyms and
suffix knowledge in JSON instead of a second hardcoded table.

### 4.6 Queue, cache, and daemon behavior

**Edits apply to newly queued work. Existing jobs keep their captured provider targets.**

For the first version, mapping and hide/show edits require no pending/nonterminal jobs and no
active daemon. Users may inspect mappings while downloads continue. Immediately before mutation,
use a small queue helper that checks for unfinished work, refuses if any remains, joins a finishing
manager via the existing transition helper, and invalidates in-memory cheat state. Do not cancel
work to permit editing. `queue_is_active()` alone is insufficient: idle/pending jobs must also
prevent it. Enqueue and mutations are serialized on the foreground UI thread; the daemon never
edits overrides. A failed save may leave the cache empty but leaves mappings unchanged. No hot
reload or cross-process editing protocol is required.

Migrate the data flow:

1. Artwork/manual enqueue and direct ScreenScraper paths resolve with `console->path` and
   `console->tag`, use the selected `ss_id`, and reject hidden/provider-ineligible folders.
2. Cheat enqueue resolves identically and copies the exact `libretro_dir` into an owned queue
   field, e.g. `cheat_dir[256]`. Validate length before enqueue; never silently truncate. Keep
   existing captured `system_id` for ScreenScraper jobs.
3. Cheat processing uses the captured directory. Key its in-memory list cache by **libretro
   directory**, not suffix, so two GPGX folders use different databases in one queue. Invalidation
   frees memory, not the downloaded git checkout.
4. Serialize/deserialize the new field in `daemon.c`. Re-exec, background handoff and restore must
   preserve targets even if catalog data changes between processes.
5. Legacy cheat jobs lack this field: resolve stored `console_path` and suffix once, before
   workers start, then capture the directory. If no valid eligible target exists, mark that item
   with an explicit mapping error. Preserve existing ScreenScraper IDs in legacy artwork/manual
   items. Invalid nonempty serialized directory values are errors, not legacy data.

Keep NextUI-facing output tags/locations unchanged: a scraping platform is not an emulator tag.
Cheats/manuals currently share output namespaces for equal suffixes. Same-named games in two GPGX
folders therefore retain that existing collision limit; folder mapping does not provide distinct
output paths. Changing that convention requires separate NextUI compatibility work. Use distinct
game names in the basic mapping test and document the limitation.

### 4.7 Startup and packaging

- In `main.c`, initialize systems after `ap_init()` and before `queue_init()`. Failure shows a
  blocking path-specific error, quits the UI, and returns 1.
- In `daemon_main()`, initialize systems before queue restore/start. Failure logs the diagnostic,
  reports readiness failure and cleans up acquired resources.
- Call `systems_shutdown()` after `queue_shutdown()` in both processes.
- `do-package` copies `resources/.` into the existing pak `resources/`, preserving `resources/bin`.
  Assert `resources/systems.json` is in the final universal release zip. Device runtime must not
  depend on generator credentials or network access for loading mappings.

---

## 5. Phase 2 — Library browser

### 5.1 Resolve before counting

Add resolved state to `system_stats`. Resolve using folder path and suffix to derive `has_ss`,
`has_libretro`, and `hidden`. Keep folder display names unchanged. Skip expensive per-ROM asset
checks for hidden/unmapped folders. Provider eligibility is independent for each mode.

### 5.2 Visibility and selection

```text
user-hidden                      → omit from library; retain in Settings
no selected platform             → show "unmapped" in every accessible library mode
selected platform supports mode  → show existing counts and ROM browser
selected platform lacks provider → omit from that mode; explain capability in Settings
```

A platform without ScreenScraper may still appear in Cheats. Unverified provider decisions remain
audit work even when their runtime capability is currently unavailable.

Selecting an unmapped row opens the picker in **This folder** scope. After a successful mutation,
rebuild all mappings, counts, labels and visibility: a suffix-default change may affect siblings.
Preserve selection by folder key and clamp the viewport when rows disappear. On cancellation or
save failure retain the prior list.

Use one clear build/free boundary for `show_library_screen` allocations; avoid duplicating exit
cleanup. Keep no-ROM-folders and no-eligible-systems empty states distinct.

---

## 6. Phase 3 — Mapping picker and Settings

### 6.1 Shared picker

Identify folder/suffix, effective platform, mapping source, and provider capabilities. Include:

1. Scope row: `This folder` by default, or `Suffix default (<tag>)`. Explain that suffix scope
   affects folders sharing the tag while folder overrides still win.
2. Suggestions, then the remaining catalog A–Z without duplicates. Show compact provider metadata
   so a Cheats user can see which choices lack a cheat database.
3. `Hide this folder` / `Show this folder`, always folder-scoped and retaining its platform.
4. `Clear folder override` or `Restore bundled suffix default`, only when an override exists at
   that scope. Describe the actual fallback; a built-in-only mapping has nothing to clear.
   Restoring an ambiguous suffix may leave it unmapped.

Use A select, B back, Y keyboard search over names/aliases/IDs. Empty search resets filtering;
keep scope, visibility and clear actions available while filtering. Do not imply every platform
supports every mode.

Before mutation run §4.6's guard. If busy, show `Wait for downloads to finish or cancel them before
changing system mappings.` Return changed only after save succeeds. Never label an unmapped folder
`not scrapeable`: its platform has not been selected.

### 6.2 Settings → System Mappings

Insert the clickable entry after `Manual download directory`. In the current settings screen,
update array size/item count 6→7, add switch case 4, shift Clear cheat cache to 5 and `show_hidden`
to index 6. Preserve existing settings-save behavior.

List **folders**, not one collapsed row per suffix. Include hidden/disabled/empty folders for
management while retaining existing shortcut exclusions. Include saved overrides for absent
folders, labeled `folder missing`, so they can be cleared after a rename. Distinguish duplicate
display names with actual directory spelling.

Sort unmapped present folders first. Show effective platform, hidden state, and source in the
row/detail view; selection opens the shared picker. Include a `Suffix defaults` entry listing
scanned suffixes and saved user suffix defaults, including absent ones. Reuse platform-selection
logic in suffix scope; offer no visibility action without a selected folder. No raw-ID editor.

---

## 7. Phase 4 — Repeatable inventory and coverage audit

### 7.1 Invocation and bootstrap

Implement `scripts/audit_systems.py` with Python standard library only. Add:

```make
NEXTUI_REPO ?= ../NextUI

.PHONY: audit-systems
audit-systems:
	@python3 scripts/audit_systems.py --nextui-repo "$(NEXTUI_REPO)" --write-report
```

Document overriding `NEXTUI_REPO` and add the target to Makefile help.

Full coverage requires a seeded `resources/systems.json`. Provide `--inventory-only` for discovery
before the catalog exists: it never reads the catalog, labels output `coverage not evaluated`,
and cannot replace the committed coverage report. A missing catalog in full mode is incomplete,
not an empty supported set.

### 7.2 Acquisition and confirmation

1. **NextUI:** scan both §2.2 layouts, recording pak/launch paths, devices, repository commit and
   dirty-tree status. Missing repository/layouts or unreadable files make the run incomplete.
   Continue other discovery without permitting success.
2. **Storefront:** filter EMU entries across both arrays. Retain store ID, version, release asset,
   devices, disabled/experimental flags and repo URL. Include disabled entries with their status.
   Record source URL, fetch time and content hash. Network/parse failures are incomplete runs.
3. **Published version:** resolve the storefront version to its release/tag and source commit.
   Never silently substitute the default branch or newest release. An unresolvable advertised
   version needs manual confirmation.
4. **Installed suffixes:** asset-name stripping gives candidates only. At the resolved commit,
   inspect emulator paths, launch scripts, installation metadata and packaging rules. Count paths
   installed under `Emus`, not tool/test paks. A tree alone proves neither released contents nor
   completeness of a partial result. Check truncated API responses and generated layouts, and
   distinguish device repetitions from distinct suffixes. Do not execute upstream scripts.
5. **Manual evidence:** keep a reviewed `PAK_EVIDENCE` table in the script for generated layouts
   and human interpretation. Each entry records store ID, storefront version, source commit,
   installed tags/device scope, supporting release-specific URLs and reasoning. Apply to partial
   or contradictory results as well as empty trees. Version mismatch requires re-review; stale
   evidence cannot silently close an item.
6. **Target meaning:** independently verify systems associated with each suffix using release
   documentation or launch/core/packaging metadata. Record singular, multi-system or unknown
   scope. Confirm provider IDs/directories separately. Do not infer meaning from `SWAN` or `SMSU`.

Avoid automatic large-asset downloads. Release-specific source/packaging evidence is acceptable
when its relationship and limits are recorded. Otherwise leave the release unresolved for manual
evidence. Use timeouts and optional `$GITHUB_TOKEN` without logging credentials. Rate limits,
404s, unavailable repositories and incomplete pagination remain visible in the report.

### 7.3 Coverage model and `SYSTEMS.md`

Generate a deterministic table sorted by suffix/source, with suffix, source pak/version, devices,
evidence, default/candidate platforms, ScreenScraper/cheat coverage and status. Provider states
are **supported**, **verified unavailable**, or **not verified**. Local user preferences are not
audit inputs.

Report separately:

- Unambiguous defaults with verified target/provider decisions.
- Verified associations requiring folder selection, with reviewed candidate sets or documented
  limits for open-ended runtimes. Do not count these as automatic suffix mappings.
- Known platforms with verified provider limitations, including cheat-only targets.
- Missing default/candidate decisions where suitable targets are known.
- Unresolved suffix identity, platform meaning, provider decisions or release evidence.
- Incomplete source acquisition and stale manual evidence.
- Aliases with no current observed pak, informational only.

Summarize NextUI/store suffix counts, defaults, folder-selection cases, provider coverage, gaps,
unresolved items and source completeness. Include source revisions/hashes and evidence, not only
a timestamp. Recognizing a tag name is not coverage, and user overrides cannot close catalog gaps.

### 7.4 Exit codes

- `0`: full audit with complete sources, no unresolved identity/target/provider decisions and no
  missing reviewed defaults/candidates. Verified provider limitations remain separately counted.
  Inventory-only success certifies discovery only and is unsuitable for the release coverage gate.
- `1`: acquisition complete, but mapping gaps or manual-review decisions remain.
- `2`: missing/invalid inputs, fetch failures, partial/truncated sources or invalid catalog.

An unsuccessful `--write-report` may write a report, but prominently label its summary `INCOMPLETE`
or `GAPS REMAIN`. Unsupported placeholders cannot turn uncertain evidence into a passing audit.

---

## 8. Phase 5 — Seed the catalog and close verified gaps

### 8.1 Generation and regeneration

First capture old tables in a committed regression fixture: **48 ScreenScraper and 33 libretro
associations**. Seed the catalog preserving every resolved value and alias before deleting C
tables. Also preserve absent provider mappings for existing tags unless additions are explicitly
reviewed. These values are a compatibility baseline, not proof that new inferred associations
are correct.

Use `scripts/gen_systems_catalog.py`, standard library only, to import the full ScreenScraper
`systemesListe.php` response and libretro `cht/` inventory. Credentials follow existing environment/
`.env.local` conventions. Never print credential-bearing URLs or commit secrets. Device runtime
uses only the bundled catalog.

Regeneration rules:

- Existing `ss_id → local id` associations are the identity registry. Propose a slug once for
  genuinely new platforms; collisions need review. Name changes may update `name`, never the ID.
- Preserve local IDs for cheat-only targets too. Do not regenerate them from directory spelling.
  Missing upstream records are reported, not automatically deleted or reassigned.
- Preserve reviewed `tags`, `tag_candidates`, aliases and libretro bindings. Seed bindings from
  all 33 existing associations. Reuse exact known associations; otherwise emit candidates for
  human review. Do not automatically bind fuzzy name matches.
- Include reviewed cheat-only platforms even without a ScreenScraper counterpart. Unmatched
  directories and unknown provider coverage stay on the audit work list.
- Write a candidate via `--output` for review; publishing a bundled update is explicit. Validate
  and compare with the regression fixture before replacement. Identical inputs must preserve
  identities and decisions. Record provider snapshot revisions/hashes in report/evidence metadata.

### 8.2 Gap-closing work list

These are review tasks, not permission to guess IDs or declare uncertain platforms unsupported:

| Suffix | Target/action | Qualification |
|---|---|---|
| `PET` | Commodore PET | Verify provider IDs and cheats |
| `PLUS4` | Commodore Plus/4 | Verify provider IDs and cheats |
| `PRBOOM` | Inspect Doom/PrBoom content and coverage | Do not blindly use a generic PC ID |
| `SGX` | PC Engine SuperGrafx | Verify published pak; retain `SUPERGRAFX` alias |
| `GPGX` | Mega Drive, Master System, Game Gear, SG-1000, Mega CD | Candidates/folder overrides; no bundled default |
| `SMSU` | Mega Drive / Genesis MSU-MD | Not Master System; verify modified-game representation |
| `SWAN` | PlayStation / SwanStation | Not WonderSwan; verify published version |
| `WSC` | WonderSwan Color | Confirm release-specific scope |
| `O2` | Magnavox Odyssey² | Verify provider associations |
| `NEOCD` | Neo Geo CD | Verify provider associations |
| `DICE` | Inspect arcade-game scope | Verify database; no arbitrary Arcade ID |
| `J2ME` | Java mobile games | Verify both providers independently |
| `GW` | Game & Watch | Check provider catalogs before availability decision |
| `PORTS` | Inspect PortMaster coverage | Open-ended content does not prove no scrapeable games |
| `ZQUEST` | Inspect Zelda Classic content | Document provider limits or unresolved work |
| `MKXPZ` | Inspect RPG Maker runtime scope | Verify relevant targets and provider limits |

Record evidence for unavailable providers and retain other supported capabilities. For mixed
content, document one-platform-per-folder limits. Per-ROM overrides are outside this feature;
separate folders are required to select different targets. Do not ship global hide defaults.

---

## 9. Failure handling

| Situation | Behavior |
|---|---|
| Shipped catalog missing/unreadable/invalid | Blocking path-specific UI error and exit 1; daemon readiness failure and cleanup |
| Overrides absent | Normal, use bundled defaults |
| Overrides corrupt or partly invalid | Start with valid/default data, visible warning, preserve original before rewrite |
| Save fails | Show error; previous file and in-memory mappings remain effective |
| Edit during unfinished work | Refuse mutation without cancelling or retargeting jobs |
| No selected target | Visible unmapped row and picker |
| Selected target lacks a provider | That mode unavailable; other capabilities unaffected |
| User-hidden folder | Omit only from library; reversible in Settings |
| Legacy cheat job cannot resolve | Explicit item error; no guessed target |
| Audit sources incomplete | Incomplete report and nonzero exit |

---

## 10. Files touched

| File | Change |
|---|---|
| `resources/systems.json` | Catalog, reviewed defaults/candidates/aliases |
| `src/systems.c` / `.h` | Loader, scoped resolution, persistence and suggestions; delete old APIs |
| `src/device.c` / `.h` | Shared executable-path helper; reuse existing path conventions |
| `src/ui.c` | Folder-aware stats, picker, Settings, mutation guard and list rebuild |
| `src/screenscraper.c` | Resolve direct scraping with folder context |
| `src/queue.c` / `.h` | Captured cheat target, cache key, guarded edits and legacy restore |
| `src/main.c` | Systems lifecycle and diagnostics |
| `src/daemon.c` | Shared path helper, systems lifecycle and serialized cheat target |
| `Makefile` | Packaging assertion, audit/test targets, portable NextUI default, help |
| `scripts/audit_systems.py` | Version-bound evidence, inventory, completeness and coverage |
| `scripts/gen_systems_catalog.py` | Stable candidate generation and preservation of reviewed data |
| `tests/` | Baseline fixture and small runnable resolver/audit/queue regression checks |
| `SYSTEMS.md` | Generated coverage report and evidence |
| `README.md` | Update stale support count; document scopes, limits, rename and queue behavior |
| `pak.json` | Changelog/version update consistent with eventual release |

Keep provider download internals in `cheats.c` unchanged unless integration identifies a necessary
adjustment. Do not claim queue or ScreenScraper callers are unaffected.

---

## 11. Verification

### 11.1 Persistent automated checks

Add a small `make test-systems` target using the existing C toolchain/cJSON and Python `unittest`
where useful; no new framework. Commit the baseline fixture and runnable checks.

- All 48 ScreenScraper and 33 cheat associations resolve identically without user overrides.
  Check the actual C resolver as well as generated JSON.
- Two GPGX folders resolve differently; folder overrides beat user suffix defaults, which beat
  shipped defaults. Clearing each scope exposes the next default.
- Hide/show affects one folder and preserves its target. A synthetic cheat-only platform remains
  eligible for Cheats with no ScreenScraper ID.
- Keys survive SD-root changes, reject invalid/traversal inputs and handle rename as documented.
- Invalid catalog/override data and simulated write/rename failures follow §9. Failed persistence
  never changes effective mappings; invalid explicit resource paths never silently fall back.
- Pending work blocks edits. After completion, remapping cannot reuse the previous target's cheat
  list. Two GPGX targets in one queue use their respective provider caches.
- Serialization retains the cheat target across handoff; legacy jobs resolve once before workers
  or receive explicit errors. Use local fakes rather than live downloads.
- Audit fixtures cover both NextUI layouts, device repetitions, generated multi-suffix bundles,
  partial trees, stale evidence, unresolved targets, missing repo/catalog and rate-limited/
  truncated responses. Assert completeness and exit codes.
- Repeated generation preserves IDs, user-reference compatibility, defaults, candidates, aliases
  and reviewed bindings even when upstream display names change.

### 11.2 Mac UI checks

```bash
make mac
make run-mac
```

Use temporary SD/catalog fixtures and the environment hook rather than renaming the real catalog.

1. `Custom Console (TESTSYS)` with a dummy ROM starts unmapped in Artwork/Cheats and, after the
   existing manual-directory setting is configured, Manuals. Map, save, restart and clear the
   folder override; it becomes unmapped again when no suffix default exists.
2. `Mega Drive (GPGX)` and `Master System (GPGX)` with different game names resolve to distinct
   targets, counts and queue destinations. A user suffix default does not replace folder overrides.
3. Hide/show one GPGX folder via Settings without affecting its sibling or losing its target.
4. Existing MGBA, disabled-folder and shortcut fixtures retain scanning behavior. Settings permits
   clearing an absent folder's retained override.
5. Verify search, scope switching, capability labels, clear wording, busy-queue rejection and
   selection after a row disappears or becomes ineligible.
6. Invalid/missing explicit catalog paths and corrupt overrides show correct errors/warnings,
   never a silent zero-support state or lost user data during recovery.

Do not expect a bundled `SWAN → PlayStation` mapping to become unmapped after clearing its user
override. Do not expect PORTS to be hidden by default.

### 11.3 Package, device, and audit

Run `make package` and inspect the final zip for the catalog and existing git resources. On an
available device, deploy and verify startup, two folder mappings, restart persistence and
foreground/background handoff retaining provider targets. Record device checks that could not
be performed; Mac-only testing does not establish device behavior.

Run `make audit-systems`. Review the report and exit code: success requires complete evidence and
decisions under §7.4, not merely entries for every suffix. Commit the reviewed report alongside
the catalog it evaluates.

---

## 12. Sequencing

1. Capture baseline mappings/source revisions. Implement inventory-only discovery using both
   NextUI layouts and version-specific store evidence; this has no catalog dependency.
2. Seed the catalog from existing tables/provider inventories, preserving all 48/33 associations.
   Implement stable candidate generation and its identity-preservation check.
3. Compare full coverage against that seed. Verify defaults/candidates, including SWAN/SMSU and
   GPGX's multi-system scope; document unresolved evidence and provider decisions.
4. Implement resolver/persistence together with call-site migration, queue/cache handling and
   daemon serialization. No tag-only production consumer or mutable-cache assumption remains
   at this integration boundary.
5. Implement library visibility, shared picker and Settings as one usable UI increment, avoiding
   an unmapped action that calls a picker which does not yet exist.
6. Close remaining verified gaps/provider reviews; run persistent, package and available device
   checks, then regenerate the coverage report.
7. Update README and release metadata. Report remaining unverified items explicitly instead of
   claiming completeness by suppressing them.

---

## 13. Remaining research and explicit limits

- Verify every advertised store release and provider association; §8.2 is an initial work list,
  not a completed inventory. Commit evidence for hand-confirmed generated packages.
- Resolve provider scope for PRBOOM, DICE, J2ME, GW, PORTS, ZQUEST and MKXPZ without assuming a
  generic target or global hidden state.
- Selection is per folder with optional suffix defaults, not per ROM. Shared NextUI output
  namespaces and folder-rename behavior are documented limits.
- No automatic catalog updates, live mapping edits during queued work, fuzzy provider binding
  or new packaging format is required for this feature.

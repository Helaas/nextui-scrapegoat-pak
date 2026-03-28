# ScrapeGoat

Scrape artwork from ScreenScraper.fr and download cheats from Libretro for your ROM library on `tg5040`, `tg5050`, and `my355` devices.

## Supported Platforms

| Platform | Device | Screen | Build |
|----------|--------|--------|-------|
| `tg5040` (TG5040) | TrimUI Smart Pro | 1280×720 | Docker (ARM64) |
| `tg5040` (TG3040) | TrimUI Brick | 1024×768 | Docker (ARM64) |
| `tg5050` | TrimUI Smart Pro S | 1280×720 | Docker (ARM64) |
| `my355` | Miyoo Flip / RK3566 class | Device-dependent | Docker (ARM64) |

> The Brick and Smart Pro share the same `tg5040` filesystem layout (tools, roms, settings paths are identical). The pak auto-detects the Brick via the `DEVICE` environment variable (`"brick"` vs `"smartpro"`), which NextUI's `launch.sh` exports at startup.

## What It Does

- Scrapes cover art and media from ScreenScraper.fr for all your ROMs
- Automatically detects your ROM library by console and game count
- Choose to scrape **missing only** (skip games with existing artwork) or **all ROMs** (overwrite existing)
- Downloads `.cht` cheat files from the Libretro cheat database for supported systems
- Live progress tracking with thread count, ETA, and API quota display
- Configurable artwork priority (20 media types available including covers, wheels, fan art, and more)
- Configurable region priority (USA, Europe, Japan, France, Germany, Spain, Italy, Portugal, World, and more)
- Supports 47 systems via ScreenScraper (33 bundled + 14 community paks, plus `P8` alias support for PICO-8 folders)
- Supports 33 systems for cheat downloading via Libretro
- Shows detailed scraping summary (Total / Found / Not Found / Errors)
- Fully supports multi-disc games, CUE/BIN disc images, and zip-archived ROMs
- Handles MD5 hashing with automatic fallback to filename-only matching
- Interrupt at any time with a button press and install progress immediately

## Usage

1. Launch **ScrapeGoat** from the NextUI Tools menu
2. Pick one of:
   - **Scrape Artwork** — Download cover art and media for your ROM library
   - **Download Cheats** — Get `.cht` cheat files for your games
   - **Settings** — Configure credentials, artwork priority, and region priority
3. Follow the on-screen prompts

## Menu Options

### Scrape Artwork

1. **Launch the scrape** — The app uses embedded developer credentials. For optimal speeds, configure your ScreenScraper.fr user credentials in Settings (see [Credentials](#credentials) below).
2. **Pick a console** — Choose which system to scrape (only systems with ScreenScraper support are shown)
3. **Choose scrape mode:**
   - **Scrape missing only** — Skip games that already have artwork, faster for reruns
   - **Scrape all** — Overwrite existing artwork, useful for switching to a different art style
4. **Watch the live scrape** — Real-time progress shows:
   - **ROMs left** — Remaining games in the current batch
   - **Threads** — How many parallel requests the API allows for your account
   - **ETA** — Estimated time to completion (exponential moving average)
   - **Quota** — Your daily API request usage (e.g. `90/20000`)
5. **Stop or wait** — Press **Y** to stop early and install what's scraped so far, or wait for completion
6. **Review summary** — See Total / Found / Not Found / Errors

Supported systems for artwork (46 total):
- **Nintendo:** Famicom/NES, Game Boy, Game Boy Color, Game Boy Advance (and mGBA), SNES/SFC, Virtual Boy, Famicom Disk System, Nintendo 64, Nintendo DS
- **Sega:** Master System, Genesis/MD, Game Gear, Mega-CD, 32X, Dreamcast, Saturn
- **Sony:** PlayStation, PlayStation Portable
- **Atari:** 2600, 5200, 7800, Lynx, Jaguar, Atari 800
- **NEC:** PC Engine/TurboGrafx, PC Engine SuperGrafx
- **Other:** Neo Geo Pocket, Neo Geo Pocket Color, Commodore 64, Commodore 128, Amstrad CPC, MSX, Amiga, ColecoVision, SG-1000, Arcade (FBNeo), Pokémon Mini, VIC-20, Intellivision, 3DO, PICO-8 (`PICO` and legacy `P8` tags), TIC-80, RPG Maker 2000/2003, ScummVM

### Download Cheats

1. **Pick a console** — Choose which system to download cheats for (only systems with Libretro cheat support are shown)
2. **Watch the download progress** — Shows current ROM being matched
3. **Review summary** — See Total / Downloaded / Not Found / Errors

The pak downloads cheats from the official [Libretro cheat database](https://github.com/libretro/libretro-database/tree/master/cht) and matches them to your ROM files by normalized name. When multiple cheat files exist for the same game (e.g. different regions), ScrapeGoat picks the one that best matches your ROM's region and your configured region priority. See [How Matching Works](#how-matching-works) for details.

Supported systems for cheats (33 total, based on current Libretro `cht/` directories):
Famicom/NES, Game Boy, Game Boy Color, Game Boy Advance, Game Boy Advance (mGBA), SNES/SFC, Super Famicom (Supafaust), Super Game Boy, PlayStation, Master System, Genesis/MD, Mega-CD, Game Gear, Sega 32X, Sega Saturn, PC Engine, Nintendo FDS, Arcade (FBNeo), Atari 2600, Atari 5200, Atari 7800, Lynx, ColecoVision, MSX, Nintendo 64, Nintendo DS, PlayStation Portable, Dreamcast, Atari Jaguar, PC Engine SuperGrafx, Intellivision, Atari 800, TIC-80

### Settings

Configure your ScreenScraper credentials, customize artwork and region priority, and control visibility of hidden/disabled ROMs.

#### ScreenScraper Credentials

**Developer Credentials** (embedded at build time)
- Your ScrapeGoat binary includes embedded ScreenScraper.fr developer credentials
- These enable scraping at **limited rate**: ~1 request per minute, single-threaded
- Required; the app will not function without them

**User Credentials** (configured in Settings) — *Optional*
- Your personal ScreenScraper.fr username and password
- Greatly increases rate limit: 100+ requests per minute
- Enables multi-threaded scraping (threads based on your API tier)
- Saved locally in `~/.userdata/shared/ScrapeGoat/settings.json` (never committed to git)
- Leave blank if you prefer to scrape at the basic rate with developer credentials only

**In summary:**
- Without user credentials: Scraping works but is slow (~1 req/min, single-threaded)
- With user credentials: Much faster (100+ req/min, multiple threads possible depending on your user level)
- User credentials are **always optional** — the app works either way

#### Artwork Priority

Reorderable list of media types. The order determines which artwork is downloaded when multiple options are available:

| Type | Description |
|------|-------------|
| Box art (2D) | Traditional 2D game cover (most common) |
| Box art (3D) | 3D rendered box art |
| Mix Recalbox V1 | Custom composite artwork (Recalbox style v1) |
| Mix Recalbox V2 | Custom composite artwork (Recalbox style v2) |
| Screenshot (title) | Title screen or fade-in |
| Screenshot (in-game) | Gameplay screenshot |
| Fan art | Community-created artwork |
| Wheel | Wheel logo / game title graphic |
| Wheel (carbon) | Wheel logo with carbon fiber effect |
| Wheel (steel) | Wheel logo with steel effect |

Default order: Box art 2D → Box art 3D → Mix Recalbox V1 → Screenshots

Use **X** to reorder, **START** to save.

#### Region Priority

Reorderable list of regions for matching the best artwork:

| Region | Code |
|--------|------|
| USA | us |
| Europe | eu |
| Japan | jp |
| World | wor |
| France | fr |
| Germany | de |
| Spain | es |
| Italy | it |
| Portugal | pt |
| ScreenScraper | ss |

Default order: USA → Europe → Japan → World  
By default, **Custom/Homebrew** (`cus`) is always appended as a last resort if no regional match is found.

Use **X** to reorder, **START** to save.

#### Show hidden/disabled ROMs

When **Off** (default), the ROM and console pickers hide:
- Folders and files that start with `.` (dot-prefixed)
- Folders and files that end in `.disabled`

Turn this **On** to include those entries in your scrape. Useful if you want to include your `.disabled` consoles or hidden games in the scrape run.

| Option | Default |
|--------|---------|
| Show hidden/disabled | **Off** |

## How Matching Works

### Artwork (ScreenScraper)

When scraping artwork, each ROM is identified through a two-step lookup against the [ScreenScraper.fr](https://www.screenscraper.fr) API:

1. **MD5 hash match** — ScrapeGoat computes the MD5 checksum and file size of the ROM content and sends them to the API. This is the most accurate method because it identifies the exact ROM dump regardless of filename.
   - For **zip archives**, the largest file inside the zip is hashed (the actual ROM, not the archive itself).
   - For **multi-disc folders** (containing an `.m3u` file) and **CUE/BIN folders** (containing a `.cue` file), the primary disc image is hashed (first track found by extension priority: `.chd` → `.iso` → `.bin` → `.img`).
2. **Filename fallback** — If the hash lookup returns no results (e.g. a patched ROM or homebrew), ScrapeGoat retries using the ROM filename only.

If neither method finds a match, the ROM is counted as "Not Found" in the summary.

Once a game is identified, ScrapeGoat picks the best artwork using your **Artwork Priority** list (e.g. Box art 2D → Box art 3D → Mix → Screenshots) and **Region Priority** list (e.g. USA → Europe → Japan → World). It tries each artwork type in order, and for each type tries your preferred regions first.

### Cheats (Libretro)

Cheat matching uses **normalized name comparison** between your ROM filenames and the [Libretro cheat database](https://github.com/libretro/libretro-database/tree/master/cht) filenames:

1. Both the ROM display name (filename without extension) and the cheat filename are normalized by:
   - Stripping all parenthetical groups — `(USA)`, `(Europe)`, `(Rev 1)`, `(Code Breaker)`, etc.
   - Converting to lowercase
   - Removing all punctuation
   - Collapsing whitespace
2. If the normalized names match exactly, the cheat file is a candidate.

**Region-aware selection:** The Libretro cheat database often contains multiple cheat files for the same game with different region tags (e.g. `Castlevania (USA) (Code Breaker).cht` and `Castlevania (Europe) (Code Breaker).cht`). When multiple candidates match the same ROM, ScrapeGoat picks the best one using this priority:

1. **Direct region overlap** — A cheat file whose region tags match the ROM's region tags is preferred. For example, a `(USA)` ROM will prefer a `(USA)` or `(USA, Europe)` cheat over a `(Japan)` one.
2. **World** — A cheat tagged `(World)` is treated as universal and preferred over a region mismatch.
3. **User's region priority** — If no direct match exists, your configured **Region Priority** (from Settings) is used as a tiebreaker.
4. **No region tag** — Cheats without any region marker are used as a neutral fallback.

Region keywords are recognized from parenthetical groups in filenames: `USA`, `Europe`, `Japan`, `World`, `France`, `Germany`, `Spain`, `Italy`, `Portugal`, `Australia`, `Korea`, `China`, `Taiwan`, and their common abbreviations (`US`, `EU`, `JP`, `FR`, `DE`, etc.). Non-region tags like `(Code Breaker)`, `(SGB Enhanced)`, and `(Rev 1)` are ignored.

For example, if you have `Pokemon - Emerald Version (USA, Europe).gba` and the Libretro database has both `Pokemon - Emerald Version (USA, Europe) (Code Breaker).cht` and `Pokemon - Feuerrote Edition (G).cht`, only the first matches (both normalize to `pokemon emerald version`). If there were a `(Japan)` variant too, the `(USA, Europe)` one would be selected because it directly overlaps with the ROM's regions.

Existing cheat files on your device are never overwritten — if a `.cht` already exists for a ROM, it is skipped.

## Artwork Storage

Scraped artwork is saved as PNG files in your ROM directories:

```
/mnt/SDCARD/Roms/Game Boy Advance (GBA)/
  .media/
    Pokemon Emerald.png
    Pokemon FireRed.png
    ...
```

The pak stores one `.png` per game in a `.media/` subfolder. Your ROM files are untouched.

## Cheat Storage

Downloaded cheats are organized by system:

```
/mnt/SDCARD/Cheats/
  GBA/
    Pokemon Emerald.cht
    Pokemon FireRed.cht
    ...
  GBC/
    ...
```

Cheat files are matched to your ROMs by normalized game name. If a cheat file already exists on your device, it is skipped (not re-downloaded).

## Credentials

### How Credentials Work

ScrapeGoat uses two types of ScreenScraper.fr credentials:

**1. Developer Credentials** (embedded in the binary)
- Provided by ScreenScraper.fr for application developers
- Embedded at **build time** via the Makefile (`-ldflags`)
- Required for any scraping to work
- Provides basic API access (~1 request/minute, single-threaded)
- Used by maintainers building the ScrapeGoat binary; end users don't need to obtain these separately

**2. User Credentials** (configured via Settings) — *Optional for faster scraping*
- Your personal ScreenScraper.fr username and password
- Stored locally on your device in `~/.userdata/shared/ScrapeGoat/settings.json`
- When configured, provides much higher rate limits (100+ requests/minute, multiple threads)
- Configure via **Settings → Username/Password** in the app
- Leave empty if you prefer basic speed with developer credentials only

### Getting User Credentials (Optional)

If you want faster scraping speeds:

1. Visit [screenscraper.fr](https://www.screenscraper.fr/)
2. Create an account
3. In the app, go to **Settings** and enter your ScreenScraper.fr username and password
4. Now your scrapes will use your personal account's higher rate limits

Without user credentials, scraping still works but at ~1 request per minute (single-threaded).

## Logging

Logs are written to:

```
/mnt/SDCARD/.userdata/<platform>/logs/scrapegoat.log
```

The platform is read from the `PLATFORM` environment variable. If not set, it defaults to `tg5040`. Logs include:
- API requests and responses
- Artwork/cheat downloads (success and errors)
- Settings load/save operations
- Error details and stack traces

## Building

### Prerequisites

**macOS (development):**
```bash
brew install go sdl2 sdl2_ttf sdl2_image sdl2_gfx
```

**Embedded (tg5040/tg5050):**
- Docker with ARM64 support
- GNU Make

### First-Time Setup

> **Note:** Only developers/maintainers building the ScrapeGoat binary need to do this. If you're installing a pre-built `.pak` or `.pakz`, skip to [Installing on a Handheld](#installing-on-a-handheld).

If you're building from source, you need ScreenScraper.fr **developer credentials** to embed in the binary:

1. Visit [screenscraper.fr](https://www.screenscraper.fr/) and request developer API access
2. You'll receive developer ID and password
3. Create `.env.local` with your developer credentials:

```bash
cp .env.example .env.local
```

4. Edit `.env.local` and add:
```bash
SCREENSCRAPER_DEV_ID=your_dev_id
SCREENSCRAPER_DEV_PASSWORD=your_dev_password
```

These are embedded in the binary at build time via the Makefile. The `.env.local` file is in `.gitignore` and will never be committed.

Embedded builds vendor their own curl/OpenSSL runtime bits and package `lib/cacert.pem` alongside the pak.

### Build Commands

```bash
# Build the mac development binary
make mac

# Build for specific device platforms
make tg5040
make tg5050
make my355

# Build all device platforms
make

# Package per-platform bundles
make package-tg5040
make package-tg5050
make package-my355

# Package all supported platforms into release zips + combined .pakz
make package

# See all targets
make help
```

### Output

| Target | Output |
|--------|--------|
| mac | `build/mac/scrapegoat` |
| tg5040 | `build/tg5040/scrapegoat` |
| tg5050 | `build/tg5050/scrapegoat` |
| my355 | `build/my355/scrapegoat` |
| package-tg5040 | `build/release/tg5040/ScrapeGoat.pak.zip` |
| package-tg5050 | `build/release/tg5050/ScrapeGoat.pak.zip` |
| package-my355 | `build/release/my355/ScrapeGoat.pak.zip` |
| package | `build/release/all/ScrapeGoat.pakz` |

The `.pak.zip` includes:
- Binary (`scrapegoat`)
- Launch script (`launch.sh`)
- Pak metadata (`pak.json`)
- License file (`LICENSE`)
- Bundled static git binary
- Bundled TLS trust store at `lib/cacert.pem` used by both libcurl and git

## Installing on a Handheld

1. **Build and package:** `make package` for all platforms, or `make package-<platform>` for one target.

2. **For a per-platform zip:**
   - Extract `ScrapeGoat.pak.zip` to your SD card as `Tools/<platform>/ScrapeGoat.pak/`
   - Replace `<platform>` with `tg5040`, `tg5050`, or `my355`

3. **For the combined `.pakz`:**
   - Place `build/release/all/ScrapeGoat.pakz` at the root of your SD card
   - NextUI will auto-install it into the matching `Tools/<platform>/ScrapeGoat.pak/` directory

4. **Launch** from the NextUI Tools menu

## Acknowledgements

Built with [Apostrophe](https://github.com/Helaas/Apostrophe). The original Go version of this pak was built with [Gabagool](https://github.com/BrandonKowalski/gabagool) by [@BrandonKowalski](https://github.com/BrandonKowalski).

Uses the [Libretro cheat database](https://github.com/libretro/libretro-database) and [ScreenScraper.fr](https://www.screenscraper.fr/) API.

## License

MIT

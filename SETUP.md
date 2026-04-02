# ScrapeGoat Pak — Setup Guide

## Credentials

ScrapeGoat requires ScreenScraper.fr **developer credentials** to function. These are embedded into the binary at build time via the Makefile (not loaded at runtime).

### Required: Developer Credentials

| Variable | Description |
|----------|-------------|
| `SCREENSCRAPER_DEV_ID` | Developer ID from screenscraper.fr |
| `SCREENSCRAPER_DEV_PASSWORD` | Developer password from screenscraper.fr |

Without these, the build will fail (`make check-credentials` runs automatically). With developer credentials only, scraping works at a limited rate (~1 request/min, single-threaded).

### Optional: User Credentials

End users can configure their personal ScreenScraper.fr username and password via the in-app **Settings** screen. This unlocks higher rate limits (100+ requests/min, multi-threaded), depending on the user's API tier. User credentials are stored locally on the device at `~/.userdata/shared/ScrapeGoat/settings.json` and are never committed to git.

### Optional: Debug Credentials

| Variable | Description |
|----------|-------------|
| `SCREENSCRAPER_DEBUG_PASSWORD` | Debug password from screenscraper.fr (limited to 100 uses/day) |
| `SCREENSCRAPER_FORCE_LEVEL` | Override user level for thread count testing (e.g. `30`, `50`) |
| `SCREENSCRAPER_FORCE_UPDATE` | Set to `1` to force API cache refresh during testing |

## Local Development Setup

### 1. Prerequisites

**macOS:**
```bash
brew install sdl2 sdl2_ttf sdl2_image libcurl pkg-config
```

**Embedded device builds** (tg5040/tg5050/my355):
- Docker with ARM64 support
- GNU Make

### 2. Set Up Developer Credentials

```bash
cp .env.example .env.local
```

Edit `.env.local` and replace the placeholders with real credentials:

```bash
SCREENSCRAPER_DEV_ID=your_actual_dev_id
SCREENSCRAPER_DEV_PASSWORD=your_actual_dev_password
```

**How this works:** The Makefile includes `.env.local` directly (`-include .env.local`) and passes the values as C preprocessor defines (`-DSCREENSCRAPER_DEV_ID=\"...\"`) at compile time. No runtime environment variables are needed.

### 3. Obtain Developer Credentials

1. Visit [screenscraper.fr](https://www.screenscraper.fr/) and create an account
2. Go to the forums and request developer API access
3. Your account will be approved and you'll receive your `dev_id` and `dev_password` via the developer dashboard
4. Add them to `.env.local`

### 4. Build and Run

```bash
make run-mac        # Build + run macOS development binary
```

If credentials are missing or still contain placeholders, the build will fail with:

```
ERROR: SCREENSCRAPER_DEV_ID and SCREENSCRAPER_DEV_PASSWORD must be set in .env.local
```

See the [README](README.md#building) for all build targets (`make tg5040`, `make package`, `make deploy`, etc.).

## Deployment

### Embedded Devices (TG5040/TG5050/MY355)

Credentials are baked into the binary at build time. The deployed `.pak` contains no `.env` files or plaintext secrets — only the compiled binary with embedded credentials.

```bash
# Build, package, and deploy via ADB (auto-detects device platform)
make deploy

# Or package for manual SD card installation
make package
```

## Runtime Notes

### Background mode handoff

If you quit ScrapeGoat with pending work and choose **Background**, the app writes the full queue to `~/.userdata/shared/ScrapeGoat/daemon/`, starts a headless daemon, and exits. When ScrapeGoat is launched again, the foreground app requests a handoff, the daemon persists a final queue snapshot and exits, and the queue resumes locally in the normal Progress screen with completed and failed entries preserved.

### Background daemon files

The daemon stores its runtime files under:

```
~/.userdata/shared/ScrapeGoat/daemon/
```

Important files:
- `daemon.log` — background-mode log output
- `queue.json` — persisted queue snapshot used for recovery and relaunch handoff
- `daemon.pid`, `daemon.lock`, `daemon.control`, `settings.json` — internal IPC/runtime files


## Security Best Practices

1. **Never commit `.env.local` to git** — `.gitignore` protects you, but be careful
2. **`.env.example` contains only placeholders** — review it before updating `.env.local`
3. **Credentials are case-sensitive** — `SCREENSCRAPER_DEV_ID` ≠ `screenscraper_dev_id`
4. **Log sanitization** — the app strips `sspassword` and `devpassword` values from logged URLs
5. **User credentials** are stored as plaintext JSON at `~/.userdata/shared/ScrapeGoat/settings.json` on the device (not tracked by git)

## Troubleshooting

### Build fails with "credentials not found"
- Ensure `.env.local` exists and contains real values (not placeholders)
- Run `make check-credentials` to verify

### "API rate limit exceeded" or slow scraping
- Developer-only mode is limited to ~1 request/min
- Configure your personal ScreenScraper.fr credentials in **Settings** for higher limits

### "Invalid username/password" (user credentials)
- User credentials are configured via the in-app **Settings** screen, not `.env.local`
- Go to **Settings** and enter your screenscraper.fr username and password

### Background handoff or resume issues
- Check the normal app log at `~/.userdata/<platform>/logs/scrapegoat.log`
- Check the daemon log at `~/.userdata/shared/ScrapeGoat/daemon/daemon.log`
- If the daemon directory still exists after a failed resume, `queue.json` is the recovery snapshot ScrapeGoat will try to import on the next launch

## File Overview

| File | Purpose | In Git? |
|------|---------|---------|
| `.env.example` | Shows required variables with placeholders | Yes |
| `.env.local` | Your actual developer credentials | No (`.gitignore`) |
| `Makefile` | Reads `.env.local` and embeds credentials via `-D` flags | Yes |
| `src/screenscraper.c` | Uses embedded credential defines at compile time | Yes |
| `~/.userdata/shared/ScrapeGoat/settings.json` | User credentials & app settings (on device) | No |
| `~/.userdata/shared/ScrapeGoat/daemon/` | Background-mode queue state, IPC files, and daemon log (on device) | No |

## See Also

- [.env.example](.env.example) — Variable reference with descriptions
- [README.md](README.md#building) — Build commands, targets, and output
- [src/screenscraper.c](src/screenscraper.c) — Credential loading via compile-time defines

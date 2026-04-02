#ifndef SCREENSCRAPER_H
#define SCREENSCRAPER_H

#include "device.h"
#include <stdbool.h>
#include <stdatomic.h>

/* ── Constants ────────────────────────────────────────────── */

#define SS_API_BASE   "https://api.screenscraper.fr/api2"
#define SS_SOFT_NAME  "ScrapeGoat-v2.0.0"
#define SS_USER_AGENT "ScrapeGoat/2.0.0"

/* ── Build-time credentials ───────────────────────────────── */

#ifndef SCREENSCRAPER_DEV_ID
#define SCREENSCRAPER_DEV_ID ""
#endif
#ifndef SCREENSCRAPER_DEV_PASSWORD
#define SCREENSCRAPER_DEV_PASSWORD ""
#endif
#ifndef SCREENSCRAPER_DEBUG_PASSWORD
#define SCREENSCRAPER_DEBUG_PASSWORD ""
#endif
#ifndef SCREENSCRAPER_FORCE_LEVEL
#define SCREENSCRAPER_FORCE_LEVEL ""
#endif
#ifndef SCREENSCRAPER_FORCE_UPDATE
#define SCREENSCRAPER_FORCE_UPDATE ""
#endif

bool ss_is_debug(void);
int  ss_check_dev_credentials(void);

/* ── Client ───────────────────────────────────────────────── */

typedef struct {
    char username[256];
    char password[256];
    atomic_int *interrupt_signal;
} ss_client;

/* Result of a successful ROM lookup */
typedef struct {
    char game_name[512];
    char media_url[1024];
    char media_format[16]; /* "png" or "jpg" */
    int  requests_today;
    int  max_requests;
    int  max_threads;
} ss_result;

/* Search for a ROM on ScreenScraper.fr.
 * First tries MD5 match, falls back to filename.
 * artwork_types and region_prio are priority-ordered arrays.
 * Returns 0 on success (result filled), 1 if not found,
 * -1 on error, and -2 when cancelled. */
int ss_search_rom(const ss_client *client, const rom_file *rom, int system_id,
                  char **artwork_types, int artwork_count,
                  char **region_prio, int region_count,
                  ss_result *result);

/* Download media from URL and save as PNG at dest_path.
 * JPEG images are converted to PNG. Uses temp file + rename for safety.
 * Returns 0 on success, -1 on error, -2 when cancelled. */
int ss_download_media(const ss_client *client, const char *media_url,
                      const char *dest_path);

/* ── Console scraping (multithreaded) ─────────────────────── */

/* Progress/message callbacks */
typedef void (*progress_fn)(float progress);
typedef void (*message_fn)(const char *msg);

/* Scrape all ROMs in a console directory using concurrent workers.
 * interrupt_signal: atomic flag — set to 1 to stop scraping.
 * Returns the run status, summary, and any user-facing error. */
run_result scrape_console(const console_dir *console, bool missing_only,
                          const app_settings *settings,
                          atomic_int *interrupt_signal,
                          progress_fn set_progress,
                          message_fn set_message);

/* ── Headless mode (daemon) ───────────────────────────────── */

/* Enable headless mode for JPEG-to-PNG conversion (uses stb_image + miniz
 * instead of SDL_image). Must be called before any downloads. */
void ss_set_headless(bool headless);

/* ── Error access ───────────────────────────────────────���─── */

/* Returns the last error message for the calling thread, or NULL. */
const char *ss_get_last_error(void);

/* ── Utilities ────────────────────────────────────────────── */

/* Format a time duration into a short ETA string (e.g. "2m30s"). */
void format_eta(double seconds, char *buf, size_t buflen);

#endif /* SCREENSCRAPER_H */

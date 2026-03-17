#ifndef CHEATS_H
#define CHEATS_H

#include "device.h"
#include <stdbool.h>

/* ── Constants ────────────────────────────────────────────── */

#define CHEAT_REPO_URL    "https://github.com/libretro/libretro-database.git"
#define CHEAT_REPO_BRANCH "master"

/* ── Git availability ─────────────────────────────────────── */

/* Returns the path to the git binary.
 * On macOS uses system git, on device uses the bundled binary. */
const char *get_git_bin(void);

/* Verify git is available and executable. Returns 0 on success. */
int check_git_available(void);

/* ── Cheat download ───────────────────────────────────────── */

/* Progress/message callbacks (same as screenscraper.h) */
typedef void (*cheat_progress_fn)(float progress);
typedef void (*cheat_message_fn)(const char *msg);

/* Download cheats for all ROMs in a console directory.
 * Progress is scaled: [0-0.3] git clone/pull, [0.3-0.5] sparse checkout,
 * [0.5-1.0] matching and copying.
 * Returns the result summary. */
scrape_summary download_cheats_for_console(const console_dir *console,
                                           char **region_prio, int region_count,
                                           int *interrupt_signal,
                                           cheat_progress_fn set_progress,
                                           cheat_message_fn set_message);

#endif /* CHEATS_H */

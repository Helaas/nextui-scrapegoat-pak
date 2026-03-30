#ifndef CHEATS_H
#define CHEATS_H

#include "device.h"
#include <limits.h>
#include <stdbool.h>
#include <stdatomic.h>

/* ── Constants ────────────────────────────────────────────── */

#define CHEAT_REPO_URL    "https://github.com/libretro/libretro-database.git"
#define CHEAT_REPO_BRANCH "master"

enum {
    CHEAT_OP_OK = 0,
    CHEAT_OP_ERROR = -1,
    CHEAT_OP_CANCELLED = -2,
    CHEAT_OP_BUSY = -3,
};

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
 * Returns the run status, summary, and any user-facing error. */
run_result download_cheats_for_console(const console_dir *console,
                                       char **region_prio, int region_count,
                                       atomic_int *interrupt_signal,
                                       cheat_progress_fn set_progress,
                                       cheat_message_fn set_message);

/* ── Cheat existence check ───────────────────────────────── */

/* Check if a cheat file exists for a ROM. */
bool cheat_exists(const char *system_tag, const char *rom_display);

/* ── Git repo management (used by queue worker) ──────────── */

int is_repo_initialized(void);

int init_cheat_repo(atomic_int *interrupt_signal,
                    cheat_message_fn set_message,
                    cheat_progress_fn set_progress,
                    float progress_scale,
                    float progress_offset);

int update_cheat_repo(atomic_int *interrupt_signal,
                      cheat_message_fn set_message,
                      cheat_progress_fn set_progress,
                      float progress_scale,
                      float progress_offset);

int ensure_system_checked_out(const char *libretro_dir_name,
                               atomic_int *interrupt_signal,
                               cheat_message_fn set_message,
                               cheat_progress_fn set_progress,
                               float progress_scale,
                               float progress_offset);

/* ── Cheat list types and functions ──────────────────────── */

typedef struct {
    char path[PATH_MAX];
    const char *regions[16];
    int region_count;
} cheat_candidate;

typedef struct {
    char normalized[256];
    cheat_candidate *candidates;
    int candidate_count;
    int candidate_cap;
} cheat_entry;

typedef struct {
    cheat_entry *entries;
    int count;
    int cap;
} cheat_list;

int build_cheat_list(const char *libretro_dir_name, cheat_list *list);
void cheat_list_free(cheat_list *list);

/* Match a ROM display name to the best cheat file path.
 * Returns NULL if no match found. */
const char *match_cheat(const char *rom_display, cheat_list *list,
                         char **region_prio, int region_count);

/* Copy a cheat file from src to dst with temp+rename safety. */
int copy_cheat_file(const char *src, const char *dst);

/* ── Cheat description parsing ───────────────────────────── */

typedef struct {
    char **descriptions;
    int    count;
} cheat_desc_list;

/* Parse cheat descriptions from a .cht file.
 * Returns 0 on success, -1 on error. Caller must free with cheat_desc_list_free. */
int parse_cheat_descriptions(const char *cht_path, cheat_desc_list *out);
void cheat_desc_list_free(cheat_desc_list *out);

#endif /* CHEATS_H */

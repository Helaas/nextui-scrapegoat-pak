#ifndef QUEUE_H
#define QUEUE_H

#include "device.h"
#include <limits.h>
#include <stdbool.h>
#include <stdatomic.h>

/* ── Constants ────────────────────────────────────────────── */

#define QUEUE_MAX_ITEMS 2048

/* ── Types ────────────────────────────────────────────────── */

typedef enum {
    QUEUE_TYPE_ARTWORK,
    QUEUE_TYPE_CHEAT,
} queue_item_type;

typedef enum {
    QUEUE_NONE = -1,
    QUEUE_IDLE = 0,
    QUEUE_SEARCHING,      /* artwork: API lookup */
    QUEUE_DOWNLOADING,    /* artwork: downloading media */
    QUEUE_CLONING,        /* cheat: git clone/checkout in progress */
    QUEUE_MATCHING,       /* cheat: matching ROM to cheat file */
    QUEUE_DONE,
    QUEUE_NOT_FOUND,
    QUEUE_ERROR,
    QUEUE_SKIPPED,
} queue_item_status;

typedef struct {
    queue_item_type           type;
    char                      rom_display[256];
    char                      rom_path[PATH_MAX];
    char                      system_tag[64];
    char                      system_display[256];
    char                      console_path[PATH_MAX];
    int                       system_id;   /* ScreenScraper ID, -1 if no SS mapping */
    queue_item_status         status;
    char                      error_msg[256];
} queue_item;

typedef struct {
    int total;
    int done;
    int failed;    /* NOT_FOUND + ERROR */
    int pending;   /* IDLE + active statuses */
} queue_stats;

/* ── Lifecycle ────────────────────────────────────────────── */

void queue_init(void);
void queue_shutdown(void);

/* ── Adding items ─────────────────────────────────────────── */

/* Add a single ROM for artwork scraping. Returns true if added. */
bool queue_add_artwork(const rom_file *rom, const console_dir *console);

/* Add a single ROM for cheat downloading. Returns true if added. */
bool queue_add_cheat(const rom_file *rom, const console_dir *console);

/* Add all missing artwork for a console. Returns count added. */
int queue_add_all_artwork(const console_dir *console, bool show_hidden);

/* Add all missing cheats for a console. Returns count added. */
int queue_add_all_cheats(const console_dir *console, bool show_hidden);

/* ── Querying ─────────────────────────────────────────────── */

bool queue_is_queued(const char *rom_path, queue_item_type type);
queue_item_status queue_get_rom_status(const char *rom_path, queue_item_type type);
queue_stats queue_get_stats(void);
bool queue_is_active(void);

/* Returns true if queue state changed since last call (for UI refresh). */
bool queue_check_dirty(void);

/* ── Management ───────────────────────────────────────────── */

/* Remove all completed/failed items from the queue. */
void queue_clear_done(void);

/* Copy current queue items into caller-provided array. Returns count. */
int queue_snapshot(queue_item *out, int max_items);

/* Thread-safe settings update for the worker. */
void queue_set_settings(const app_settings *settings);

#endif /* QUEUE_H */

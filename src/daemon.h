#ifndef DAEMON_H
#define DAEMON_H

#include "queue.h"
#include "device.h"
#include <stdbool.h>

/* ── Detection ───────────────────────────────────────────── */

/* Returns true if a background daemon is currently running. */
bool daemon_is_running(void);

/* ── State I/O ───────────────────────────────────────────── */

/* Read the daemon's current queue state from disk.
 * Returns item count (0 on error). Fills stats_out if non-NULL. */
int daemon_read_queue(queue_item *out, int max_items, queue_stats *stats_out);

/* ── Lifecycle ───────────────────────────────────────────── */

/* Serialize pending items + settings to disk and fork a background daemon.
 * Parent returns 0 on success, -1 on error. Child never returns. */
int daemon_launch(const queue_item *items, int count,
                  const app_settings *settings);

/* Write a stop-sentinel so the running daemon shuts down gracefully. */
void daemon_request_stop(void);

/* Remove all daemon IPC files except queue.json (for result import). */
void daemon_cleanup(void);

/* Remove all daemon IPC files including queue.json. */
void daemon_cleanup_all(void);

/* Entry point for the daemon child process (called via --daemon). */
int daemon_main(void);

#endif /* DAEMON_H */

#include "queue.h"
#include "cheats.h"
#include "screenscraper.h"
#include "systems.h"

#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Suppress GCC warnings about snprintf truncation when combining
   PATH_MAX-sized strings — truncation is safe by design. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

/* ── Queue state ──────────────────────────────────────────── */

static queue_item     *g_items;
static int             g_count = 0;
static uint32_t        g_next_item_id = 1;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool            g_dirty = false;
static bool            g_manager_started = false;
static bool            g_worker_running = false;
static atomic_int      g_api_req_today   = 0;
static atomic_int      g_api_max_req     = 0;
static atomic_int      g_api_max_threads = 0;

/* ── Worker state ─────────────────────────────────────────── */

static pthread_t       g_manager_thread;
static atomic_int      g_interrupt;

/* Thread-safe copy of settings for the worker */
static app_settings    g_settings;
static pthread_mutex_t g_settings_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool            g_settings_valid = false;

/* ── Helpers ──────────────────────────────────────────────── */

static bool is_terminal_status(queue_item_status status) {
    return status == QUEUE_DONE || status == QUEUE_NOT_FOUND ||
           status == QUEUE_ERROR || status == QUEUE_SKIPPED;
}

static void set_dirty_locked(void) {
    g_dirty = true;
}

static bool queue_has_non_terminal_locked(void) {
    for (int i = 0; i < g_count; i++) {
        if (!is_terminal_status(g_items[i].status))
            return true;
    }
    return false;
}

static bool queue_has_idle_items_locked(void) {
    for (int i = 0; i < g_count; i++) {
        if (g_items[i].status == QUEUE_IDLE)
            return true;
    }
    return false;
}

static bool claim_item_locked(int index, queue_item_type type,
                              queue_item_status next_status,
                              queue_item *snapshot) {
    if (index < 0 || index >= g_count)
        return false;
    if (g_items[index].type != type || g_items[index].status != QUEUE_IDLE)
        return false;

    if (snapshot)
        *snapshot = g_items[index];
    g_items[index].status = next_status;
    set_dirty_locked();
    return true;
}

static bool claim_item(int index, queue_item_type type,
                       queue_item_status next_status,
                       queue_item *snapshot) {
    bool claimed;

    pthread_mutex_lock(&g_mutex);
    claimed = claim_item_locked(index, type, next_status, snapshot);
    pthread_mutex_unlock(&g_mutex);
    return claimed;
}

static void set_item_status(int index, queue_item_status status) {
    pthread_mutex_lock(&g_mutex);
    if (index >= 0 && index < g_count) {
        g_items[index].status = status;
        set_dirty_locked();
    }
    pthread_mutex_unlock(&g_mutex);
}

static void set_item_error(int index, queue_item_status status,
                           const char *fmt, ...) {
    char msg[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    pthread_mutex_lock(&g_mutex);
    if (index >= 0 && index < g_count) {
        g_items[index].status = status;
        snprintf(g_items[index].error_msg, sizeof(g_items[index].error_msg),
                 "%s", msg);
        set_dirty_locked();
    }
    pthread_mutex_unlock(&g_mutex);
}

static int find_next_cheat_index(void) {
    int index = -1;

    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_count; i++) {
        if (g_items[i].type == QUEUE_TYPE_CHEAT &&
            g_items[i].status == QUEUE_IDLE) {
            index = i;
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return index;
}

static bool manager_should_run(void) {
    bool running;

    pthread_mutex_lock(&g_mutex);
    running = g_worker_running;
    pthread_mutex_unlock(&g_mutex);
    return running;
}

static app_settings copy_settings(void) {
    app_settings copy = {0};

    pthread_mutex_lock(&g_settings_mutex);
    if (!g_settings_valid) {
        pthread_mutex_unlock(&g_settings_mutex);
        return default_settings();
    }

    copy = g_settings;
    for (int i = 0; i < copy.artwork_prio_count; i++) {
        copy.artwork_prio[i] = g_settings.artwork_prio[i]
            ? strdup(g_settings.artwork_prio[i])
            : NULL;
    }
    for (int i = 0; i < copy.region_prio_count; i++) {
        copy.region_prio[i] = g_settings.region_prio[i]
            ? strdup(g_settings.region_prio[i])
            : NULL;
    }
    pthread_mutex_unlock(&g_settings_mutex);
    return copy;
}

static void free_settings_copy(app_settings *settings) {
    free_settings(settings);
}

static void build_rom_from_item(const queue_item *item, rom_file *rom) {
    memset(rom, 0, sizeof(*rom));
    snprintf(rom->path, sizeof(rom->path), "%s", item->rom_path);
    snprintf(rom->display, sizeof(rom->display), "%s", item->rom_display);

    const char *slash = strrchr(item->rom_path, '/');
    snprintf(rom->name, sizeof(rom->name), "%s",
             slash ? slash + 1 : item->rom_path);

    struct stat st;
    if (stat(item->rom_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        char probe_path[PATH_MAX];

        snprintf(probe_path, sizeof(probe_path), "%s/%s.m3u",
                 item->rom_path, item->rom_display);
        if (stat(probe_path, &st) == 0) {
            rom->is_multi_disc = true;
            return;
        }

        snprintf(probe_path, sizeof(probe_path), "%s/%s.cue",
                 item->rom_path, item->rom_display);
        if (stat(probe_path, &st) == 0)
            rom->is_cue_folder = true;
    }
}

/* ── Artwork worker (multi-threaded) ─────────────────────── */

typedef struct {
    ss_client      client;
    char         **artwork_types;
    int            artwork_count;
    char         **region_prio;
    int            region_count;
    atomic_int     next_index;
    int            artwork_item_count;
    int           *artwork_indices;
} artwork_work;

static artwork_work g_artwork_work;

static void *artwork_worker_fn(void *arg) {
    (void)arg;

    for (;;) {
        if (atomic_load(&g_interrupt))
            break;
        if (!manager_should_run())
            break;

        int batch_index = atomic_fetch_add(&g_artwork_work.next_index, 1);
        if (batch_index >= g_artwork_work.artwork_item_count)
            break;

        int queue_index = g_artwork_work.artwork_indices[batch_index];
        queue_item item;
        if (!claim_item(queue_index, QUEUE_TYPE_ARTWORK, QUEUE_SEARCHING, &item))
            continue;

        rom_file rom;
        build_rom_from_item(&item, &rom);

        ss_result result;
        int search_ret = ss_search_rom(&g_artwork_work.client, &rom, item.system_id,
                                       g_artwork_work.artwork_types,
                                       g_artwork_work.artwork_count,
                                       g_artwork_work.region_prio,
                                       g_artwork_work.region_count,
                                       &result);
        if (search_ret == -2)
            break;
        if (search_ret >= 0) {
            atomic_store(&g_api_req_today, result.requests_today);
            atomic_store(&g_api_max_req, result.max_requests);
            if (result.max_threads > 0)
                atomic_store(&g_api_max_threads, result.max_threads);
        }
        if (search_ret == 1) {
            set_item_status(queue_index, QUEUE_NOT_FOUND);
            continue;
        }
        if (search_ret != 0) {
            const char *err = ss_get_last_error();
            set_item_error(queue_index, QUEUE_ERROR,
                           "%s", err ? err : "Search failed");
            continue;
        }

        set_item_status(queue_index, QUEUE_DOWNLOADING);

        char dest[PATH_MAX];
        artwork_src_path(item.rom_path, item.rom_display, dest, sizeof(dest));

        int download_ret = ss_download_media(&g_artwork_work.client,
                                             result.media_url, dest);
        if (download_ret == -2)
            break;
        if (download_ret == 0)
            set_item_status(queue_index, QUEUE_DONE);
        else {
            const char *err = ss_get_last_error();
            set_item_error(queue_index, QUEUE_ERROR,
                           "%s", err ? err : "Download failed");
        }
    }

    return NULL;
}

/* ── Manual worker (multi-threaded) ──────────────────────── */

typedef struct {
    ss_client      client;
    char         **region_prio;
    int            region_count;
    char           manual_download_dir[PATH_MAX];
    atomic_int     next_index;
    int            manual_item_count;
    int           *manual_indices;
} manual_work;

static manual_work g_manual_work;

static void *manual_worker_fn(void *arg) {
    (void)arg;
    char *manual_type = "manuel";
    char *manual_types[] = {manual_type};

    for (;;) {
        if (atomic_load(&g_interrupt))
            break;
        if (!manager_should_run())
            break;

        int batch_index = atomic_fetch_add(&g_manual_work.next_index, 1);
        if (batch_index >= g_manual_work.manual_item_count)
            break;

        int queue_index = g_manual_work.manual_indices[batch_index];
        queue_item item;
        if (!claim_item(queue_index, QUEUE_TYPE_MANUAL, QUEUE_SEARCHING, &item))
            continue;

        rom_file rom;
        build_rom_from_item(&item, &rom);

        ss_result result;
        int search_ret = ss_search_rom(&g_manual_work.client, &rom, item.system_id,
                                        manual_types, 1,
                                        g_manual_work.region_prio,
                                        g_manual_work.region_count,
                                        &result);
        if (search_ret == -2)
            break;
        if (search_ret >= 0) {
            atomic_store(&g_api_req_today, result.requests_today);
            atomic_store(&g_api_max_req, result.max_requests);
            if (result.max_threads > 0)
                atomic_store(&g_api_max_threads, result.max_threads);
        }
        if (search_ret == 1) {
            set_item_status(queue_index, QUEUE_NOT_FOUND);
            continue;
        }
        if (search_ret != 0) {
            const char *err = ss_get_last_error();
            set_item_error(queue_index, QUEUE_ERROR,
                           "%s", err ? err : "Search failed");
            continue;
        }

        set_item_status(queue_index, QUEUE_DOWNLOADING);

        char dest[PATH_MAX];
        manual_dest_path(g_manual_work.manual_download_dir,
                         item.system_tag, item.rom_display,
                         dest, sizeof(dest));

        int download_ret = ss_download_media(&g_manual_work.client,
                                             result.media_url, dest);
        if (download_ret == -2)
            break;
        if (download_ret == 0)
            set_item_status(queue_index, QUEUE_DONE);
        else {
            const char *err = ss_get_last_error();
            set_item_error(queue_index, QUEUE_ERROR,
                           "%s", err ? err : "Download failed");
        }
    }

    return NULL;
}

/* ── Cheat processing (single-threaded) ──────────────────── */

typedef struct {
    char tag[32];
    cheat_list list;
    bool valid;
} cheat_cache_entry;

#define MAX_CHEAT_CACHE 64
static pthread_mutex_t g_cheat_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static cheat_cache_entry g_cheat_cache[MAX_CHEAT_CACHE];
static int               g_cheat_cache_count = 0;
static bool              g_cheat_repo_ready = false;

static void clear_cheat_repo_state_locked(void) {
    for (int i = 0; i < g_cheat_cache_count; i++) {
        if (g_cheat_cache[i].valid)
            cheat_list_free(&g_cheat_cache[i].list);
    }

    memset(g_cheat_cache, 0, sizeof(g_cheat_cache));
    g_cheat_cache_count = 0;
    g_cheat_repo_ready = false;
}

static bool cheat_repo_is_ready(void) {
    bool ready;

    pthread_mutex_lock(&g_cheat_state_mutex);
    ready = g_cheat_repo_ready;
    pthread_mutex_unlock(&g_cheat_state_mutex);
    return ready;
}

static cheat_list *get_cached_cheat_list_locked(const char *system_tag) {
    for (int i = 0; i < g_cheat_cache_count; i++) {
        if (strcmp(g_cheat_cache[i].tag, system_tag) == 0 &&
            g_cheat_cache[i].valid) {
            return &g_cheat_cache[i].list;
        }
    }
    return NULL;
}

static cheat_list *ensure_cheat_list(const char *system_tag,
                                     atomic_int *interrupt_signal) {
    pthread_mutex_lock(&g_cheat_state_mutex);

    cheat_list *cached = get_cached_cheat_list_locked(system_tag);
    if (cached)
    {
        pthread_mutex_unlock(&g_cheat_state_mutex);
        return cached;
    }

    const char *libretro_dir_name = libretro_dir(system_tag);
    if (!libretro_dir_name) {
        pthread_mutex_unlock(&g_cheat_state_mutex);
        return NULL;
    }

    if (!g_cheat_repo_ready) {
        if (!is_repo_initialized()) {
            int ret = init_cheat_repo(interrupt_signal, NULL, NULL, 0.0f, 0.0f);
            if (ret != CHEAT_OP_OK) {
                pthread_mutex_unlock(&g_cheat_state_mutex);
                return NULL;
            }
        } else {
            int ret = update_cheat_repo(interrupt_signal, NULL, NULL, 0.0f, 0.0f);
            if (ret == CHEAT_OP_BUSY) {
                pthread_mutex_unlock(&g_cheat_state_mutex);
                return NULL;
            }
        }
        g_cheat_repo_ready = true;
    }

    if (ensure_system_checked_out(libretro_dir_name, interrupt_signal,
                                  NULL, NULL, 0.0f, 0.0f) != CHEAT_OP_OK) {
        pthread_mutex_unlock(&g_cheat_state_mutex);
        return NULL;
    }

    if (g_cheat_cache_count >= MAX_CHEAT_CACHE) {
        pthread_mutex_unlock(&g_cheat_state_mutex);
        return NULL;
    }

    cheat_cache_entry *entry = &g_cheat_cache[g_cheat_cache_count];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->tag, sizeof(entry->tag), "%s", system_tag);
    if (build_cheat_list(libretro_dir_name, &entry->list) != 0) {
        memset(entry, 0, sizeof(*entry));
        pthread_mutex_unlock(&g_cheat_state_mutex);
        return NULL;
    }

    entry->valid = true;
    g_cheat_cache_count++;
    cheat_list *list = &entry->list;
    pthread_mutex_unlock(&g_cheat_state_mutex);
    return list;
}

static void process_cheat_item(int queue_index, app_settings *settings) {
    queue_item item;
    queue_item_status initial_status = cheat_repo_is_ready()
        ? QUEUE_MATCHING
        : QUEUE_CLONING;

    if (!claim_item(queue_index, QUEUE_TYPE_CHEAT, initial_status, &item))
        return;

    if (!libretro_dir(item.system_tag)) {
        set_item_error(queue_index, QUEUE_ERROR,
                       "No libretro mapping for system '%s'", item.system_tag);
        return;
    }

    if (!item.force && cheat_exists(item.system_tag, item.rom_display)) {
        set_item_status(queue_index, QUEUE_SKIPPED);
        return;
    }

    cheat_list *list = ensure_cheat_list(item.system_tag, &g_interrupt);
    if (!list || atomic_load(&g_interrupt)) {
        if (!atomic_load(&g_interrupt))
            set_item_error(queue_index, QUEUE_ERROR,
                           "Failed to build cheat list for '%s'",
                           item.system_tag);
        return;
    }

    set_item_status(queue_index, QUEUE_MATCHING);

    int region_count = 0;
    char **region_prio = build_region_types(settings, &region_count);
    if (!region_prio) {
        set_item_error(queue_index, QUEUE_ERROR,
                       "Failed to build region priority list");
        return;
    }

    const char *src_path = match_cheat(item.rom_display, list,
                                       region_prio, region_count);
    if (!src_path) {
        for (int i = 0; i < region_count; i++)
            free(region_prio[i]);
        free(region_prio);
        set_item_status(queue_index, QUEUE_NOT_FOUND);
        return;
    }

    char cheats_base[PATH_MAX];
    char dest_path[PATH_MAX];
    get_cheats_path(cheats_base, sizeof(cheats_base));
    snprintf(dest_path, sizeof(dest_path), "%s/%s/%s.cht",
             cheats_base, item.system_tag, item.rom_display);

    if (copy_cheat_file(src_path, dest_path) == 0)
        set_item_status(queue_index, QUEUE_DONE);
    else
        set_item_error(queue_index, QUEUE_ERROR,
                       "Failed to copy cheat file");

    for (int i = 0; i < region_count; i++)
        free(region_prio[i]);
    free(region_prio);
}

/* ── Manager thread ───────────────────────────────────────── */

static void process_artwork_batch(app_settings *settings) {
    int *indices = NULL;
    int art_count = 0;

    pthread_mutex_lock(&g_mutex);
    if (g_count > 0)
        indices = malloc(sizeof(int) * (size_t)g_count);
    if (indices) {
        for (int i = 0; i < g_count; i++) {
            if (g_items[i].type == QUEUE_TYPE_ARTWORK &&
                g_items[i].status == QUEUE_IDLE) {
                indices[art_count++] = i;
            }
        }
    }
    pthread_mutex_unlock(&g_mutex);

    if (!indices || art_count == 0) {
        free(indices);
        return;
    }

    memset(&g_artwork_work, 0, sizeof(g_artwork_work));
    snprintf(g_artwork_work.client.username,
             sizeof(g_artwork_work.client.username),
             "%s", settings->ss_username);
    snprintf(g_artwork_work.client.password,
             sizeof(g_artwork_work.client.password),
             "%s", settings->ss_password);
    g_artwork_work.client.interrupt_signal = &g_interrupt;
    g_artwork_work.artwork_types =
        build_artwork_types(settings, &g_artwork_work.artwork_count);
    g_artwork_work.region_prio =
        build_region_types(settings, &g_artwork_work.region_count);
    g_artwork_work.artwork_indices = indices;
    g_artwork_work.artwork_item_count = art_count;
    atomic_init(&g_artwork_work.next_index, 0);

    if (!g_artwork_work.artwork_types || !g_artwork_work.region_prio) {
        for (int i = 0; i < art_count; i++)
            set_item_error(indices[i], QUEUE_ERROR,
                           "Failed to build artwork/region settings");

        for (int i = 0; i < g_artwork_work.artwork_count; i++)
            free(g_artwork_work.artwork_types[i]);
        free(g_artwork_work.artwork_types);
        for (int i = 0; i < g_artwork_work.region_count; i++)
            free(g_artwork_work.region_prio[i]);
        free(g_artwork_work.region_prio);
        free(indices);
        g_artwork_work.artwork_types = NULL;
        g_artwork_work.region_prio = NULL;
        g_artwork_work.artwork_indices = NULL;
        g_artwork_work.artwork_item_count = 0;
        return;
    }

    int max_threads = 1;
    if (art_count > 0) {
        int first_index = indices[0];
        queue_item item;
        if (claim_item(first_index, QUEUE_TYPE_ARTWORK, QUEUE_SEARCHING, &item)) {
            rom_file rom;
            build_rom_from_item(&item, &rom);

            ss_result result;
            int ret = ss_search_rom(&g_artwork_work.client, &rom, item.system_id,
                                    g_artwork_work.artwork_types,
                                    g_artwork_work.artwork_count,
                                    g_artwork_work.region_prio,
                                    g_artwork_work.region_count,
                                    &result);
            if (ret >= 0) {
                atomic_store(&g_api_req_today, result.requests_today);
                atomic_store(&g_api_max_req, result.max_requests);
                if (result.max_threads > 0)
                    atomic_store(&g_api_max_threads, result.max_threads);
            }
            if (ret == 0) {
                if (result.max_threads > 1)
                    max_threads = result.max_threads;

                set_item_status(first_index, QUEUE_DOWNLOADING);

                char dest[PATH_MAX];
                artwork_src_path(item.rom_path, item.rom_display,
                                 dest, sizeof(dest));
                if (ss_download_media(&g_artwork_work.client,
                                      result.media_url, dest) == 0) {
                    set_item_status(first_index, QUEUE_DONE);
                } else {
                    const char *err = ss_get_last_error();
                    set_item_error(first_index, QUEUE_ERROR,
                                   "%s", err ? err : "Download failed");
                }
            } else if (ret == 1) {
                set_item_status(first_index, QUEUE_NOT_FOUND);
            } else if (ret != -2) {
                const char *err = ss_get_last_error();
                set_item_error(first_index, QUEUE_ERROR,
                               "%s", err ? err : "Search failed");
            }
        }
        atomic_store(&g_artwork_work.next_index, 1);
    }

    if (art_count > 1 && !atomic_load(&g_interrupt)) {
        int worker_count = max_threads;
        if (worker_count > art_count - 1)
            worker_count = art_count - 1;
        if (worker_count < 1)
            worker_count = 1;

        pthread_t *workers = calloc((size_t)worker_count, sizeof(pthread_t));
        if (workers) {
            int started = 0;
            for (int i = 0; i < worker_count; i++) {
                if (pthread_create(&workers[i], NULL, artwork_worker_fn, NULL) == 0)
                    started++;
                else
                    break;
            }
            for (int i = 0; i < started; i++)
                pthread_join(workers[i], NULL);
            free(workers);
        }
    }

    for (int i = 0; i < g_artwork_work.artwork_count; i++)
        free(g_artwork_work.artwork_types[i]);
    free(g_artwork_work.artwork_types);
    for (int i = 0; i < g_artwork_work.region_count; i++)
        free(g_artwork_work.region_prio[i]);
    free(g_artwork_work.region_prio);
    free(indices);

    g_artwork_work.artwork_types = NULL;
    g_artwork_work.region_prio = NULL;
    g_artwork_work.artwork_indices = NULL;
    g_artwork_work.artwork_item_count = 0;
}

/* Cheat worker thread — runs in parallel with artwork batch */
static void *cheat_thread_fn(void *arg) {
    app_settings *settings = (app_settings *)arg;
    while (!atomic_load(&g_interrupt)) {
        int idx = find_next_cheat_index();
        if (idx < 0) break;
        process_cheat_item(idx, settings);
    }
    return NULL;
}

/* ── Manual batch processing ─────────────────────────────── */

static void process_manual_batch(app_settings *settings) {
    int *indices = NULL;
    int man_count = 0;

    pthread_mutex_lock(&g_mutex);
    if (g_count > 0)
        indices = malloc(sizeof(int) * (size_t)g_count);
    if (indices) {
        for (int i = 0; i < g_count; i++) {
            if (g_items[i].type == QUEUE_TYPE_MANUAL &&
                g_items[i].status == QUEUE_IDLE) {
                indices[man_count++] = i;
            }
        }
    }
    pthread_mutex_unlock(&g_mutex);

    if (!indices || man_count == 0) {
        free(indices);
        return;
    }

    memset(&g_manual_work, 0, sizeof(g_manual_work));
    snprintf(g_manual_work.client.username,
             sizeof(g_manual_work.client.username),
             "%s", settings->ss_username);
    snprintf(g_manual_work.client.password,
             sizeof(g_manual_work.client.password),
             "%s", settings->ss_password);
    g_manual_work.client.interrupt_signal = &g_interrupt;
    g_manual_work.region_prio =
        build_region_types(settings, &g_manual_work.region_count);
    snprintf(g_manual_work.manual_download_dir,
             sizeof(g_manual_work.manual_download_dir),
             "%s", settings->manual_download_dir);
    g_manual_work.manual_indices = indices;
    g_manual_work.manual_item_count = man_count;
    atomic_init(&g_manual_work.next_index, 0);

    if (!g_manual_work.region_prio) {
        for (int i = 0; i < man_count; i++)
            set_item_error(indices[i], QUEUE_ERROR,
                           "Failed to build region settings");

        free(indices);
        g_manual_work.manual_indices = NULL;
        g_manual_work.manual_item_count = 0;
        return;
    }

    int max_threads = 1;
    if (man_count > 0) {
        int first_index = indices[0];
        queue_item item;
        if (claim_item(first_index, QUEUE_TYPE_MANUAL, QUEUE_SEARCHING, &item)) {
            rom_file rom;
            build_rom_from_item(&item, &rom);

            char *manual_type = "manuel";
            char *manual_types[] = {manual_type};
            ss_result result;
            int ret = ss_search_rom(&g_manual_work.client, &rom, item.system_id,
                                    manual_types, 1,
                                    g_manual_work.region_prio,
                                    g_manual_work.region_count,
                                    &result);
            if (ret >= 0) {
                atomic_store(&g_api_req_today, result.requests_today);
                atomic_store(&g_api_max_req, result.max_requests);
                if (result.max_threads > 0)
                    atomic_store(&g_api_max_threads, result.max_threads);
            }
            if (ret == 0) {
                if (result.max_threads > 1)
                    max_threads = result.max_threads;

                set_item_status(first_index, QUEUE_DOWNLOADING);

                char dest[PATH_MAX];
                manual_dest_path(g_manual_work.manual_download_dir,
                                 item.system_tag, item.rom_display,
                                 dest, sizeof(dest));
                if (ss_download_media(&g_manual_work.client,
                                      result.media_url, dest) == 0) {
                    set_item_status(first_index, QUEUE_DONE);
                } else {
                    const char *err = ss_get_last_error();
                    set_item_error(first_index, QUEUE_ERROR,
                                   "%s", err ? err : "Download failed");
                }
            } else if (ret == 1) {
                set_item_status(first_index, QUEUE_NOT_FOUND);
            } else if (ret != -2) {
                const char *err = ss_get_last_error();
                set_item_error(first_index, QUEUE_ERROR,
                               "%s", err ? err : "Search failed");
            }
        }
        atomic_store(&g_manual_work.next_index, 1);
    }

    if (man_count > 1 && !atomic_load(&g_interrupt)) {
        int worker_count = max_threads;
        if (worker_count > man_count - 1)
            worker_count = man_count - 1;
        if (worker_count < 1)
            worker_count = 1;

        pthread_t *workers = calloc((size_t)worker_count, sizeof(pthread_t));
        if (workers) {
            int started = 0;
            for (int i = 0; i < worker_count; i++) {
                if (pthread_create(&workers[i], NULL, manual_worker_fn, NULL) == 0)
                    started++;
                else
                    break;
            }
            for (int i = 0; i < started; i++)
                pthread_join(workers[i], NULL);
            free(workers);
        }
    }

    for (int i = 0; i < g_manual_work.region_count; i++)
        free(g_manual_work.region_prio[i]);
    free(g_manual_work.region_prio);
    free(indices);

    g_manual_work.region_prio = NULL;
    g_manual_work.manual_indices = NULL;
    g_manual_work.manual_item_count = 0;
}

static void *manager_thread_fn(void *arg) {
    (void)arg;

    while (manager_should_run()) {
        if (atomic_load(&g_interrupt))
            break;

        pthread_mutex_lock(&g_mutex);
        bool has_idle_items = queue_has_idle_items_locked();
        pthread_mutex_unlock(&g_mutex);

        if (!has_idle_items) {
            usleep(200000);
            continue;
        }

        app_settings settings = copy_settings();

        /* Check what types of work are pending */
        bool has_idle_artwork = false;
        bool has_idle_cheats = false;
        bool has_idle_manuals = false;
        pthread_mutex_lock(&g_mutex);
        for (int i = 0; i < g_count; i++) {
            if (g_items[i].status == QUEUE_IDLE) {
                if (g_items[i].type == QUEUE_TYPE_ARTWORK)
                    has_idle_artwork = true;
                if (g_items[i].type == QUEUE_TYPE_CHEAT)
                    has_idle_cheats = true;
                if (g_items[i].type == QUEUE_TYPE_MANUAL)
                    has_idle_manuals = true;
            }
        }
        pthread_mutex_unlock(&g_mutex);

        /* Start cheat processing in parallel with artwork/manuals */
        pthread_t cheat_thread = 0;
        bool cheat_started = false;
        if (has_idle_cheats && !atomic_load(&g_interrupt)) {
            if (pthread_create(&cheat_thread, NULL, cheat_thread_fn, &settings) == 0)
                cheat_started = true;
        }

        /* Process artwork batch (blocks until done) */
        if (has_idle_artwork && !atomic_load(&g_interrupt))
            process_artwork_batch(&settings);

        /* Process manual batch after artwork (same API, sequential) */
        if (has_idle_manuals && !atomic_load(&g_interrupt))
            process_manual_batch(&settings);

        /* Wait for cheat thread to finish */
        if (cheat_started)
            pthread_join(cheat_thread, NULL);

        free_settings_copy(&settings);
    }

    pthread_mutex_lock(&g_mutex);
    g_worker_running = false;
    pthread_mutex_unlock(&g_mutex);
    return NULL;
}

static void ensure_manager_started(void) {
    bool start_thread = false;

    pthread_mutex_lock(&g_mutex);
    if (!g_manager_started) {
        g_manager_started = true;
        g_worker_running = true;
        start_thread = true;
        atomic_store(&g_interrupt, 0);
    }
    pthread_mutex_unlock(&g_mutex);

    if (start_thread && pthread_create(&g_manager_thread, NULL,
                                       manager_thread_fn, NULL) != 0) {
        pthread_mutex_lock(&g_mutex);
        g_manager_started = false;
        g_worker_running = false;
        pthread_mutex_unlock(&g_mutex);
    }
}

/* ── Public API ───────────────────────────────────────────── */

void queue_init(void) {
    pthread_mutex_lock(&g_mutex);
    if (!g_items)
        g_items = calloc(QUEUE_MAX_ITEMS, sizeof(queue_item));
    g_count = 0;
    g_next_item_id = 1;
    g_dirty = false;
    g_manager_started = false;
    g_worker_running = false;
    pthread_mutex_unlock(&g_mutex);

    pthread_mutex_lock(&g_cheat_state_mutex);
    clear_cheat_repo_state_locked();
    pthread_mutex_unlock(&g_cheat_state_mutex);

    g_settings_valid = false;
    atomic_init(&g_interrupt, 0);
}

void queue_shutdown(void) {
    bool should_join;

    pthread_mutex_lock(&g_mutex);
    should_join = g_manager_started;
    g_worker_running = false;
    pthread_mutex_unlock(&g_mutex);

    if (should_join) {
        atomic_store(&g_interrupt, 1);
        pthread_join(g_manager_thread, NULL);

        pthread_mutex_lock(&g_mutex);
        g_manager_started = false;
        pthread_mutex_unlock(&g_mutex);
    }

    pthread_mutex_lock(&g_cheat_state_mutex);
    clear_cheat_repo_state_locked();
    pthread_mutex_unlock(&g_cheat_state_mutex);

    pthread_mutex_lock(&g_settings_mutex);
    if (g_settings_valid) {
        free_settings(&g_settings);
        g_settings_valid = false;
    }
    pthread_mutex_unlock(&g_settings_mutex);

    pthread_mutex_lock(&g_mutex);
    g_count = 0;
    g_dirty = false;
    pthread_mutex_unlock(&g_mutex);

    free(g_items);
    g_items = NULL;
}

void queue_set_settings(const app_settings *settings) {
    pthread_mutex_lock(&g_settings_mutex);
    if (g_settings_valid)
        free_settings(&g_settings);

    g_settings = *settings;
    for (int i = 0; i < settings->artwork_prio_count; i++) {
        g_settings.artwork_prio[i] = settings->artwork_prio[i]
            ? strdup(settings->artwork_prio[i])
            : NULL;
    }
    for (int i = 0; i < settings->region_prio_count; i++) {
        g_settings.region_prio[i] = settings->region_prio[i]
            ? strdup(settings->region_prio[i])
            : NULL;
    }
    g_settings_valid = true;
    pthread_mutex_unlock(&g_settings_mutex);
}

/* Forward declarations for internal helpers defined below */
static bool queue_add_artwork_internal(const rom_file *rom, const console_dir *console, bool force);
static bool queue_add_cheat_internal(const rom_file *rom, const console_dir *console, bool force);

bool queue_add_artwork(const rom_file *rom, const console_dir *console) {
    return queue_add_artwork_internal(rom, console, false);
}

bool queue_add_cheat(const rom_file *rom, const console_dir *console) {
    return queue_add_cheat_internal(rom, console, false);
}

int queue_add_all_artwork(const console_dir *console, bool show_hidden) {
    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, show_hidden, &roms);
    if (rom_count <= 0) {
        free(roms);
        return 0;
    }

    int added = 0;
    for (int i = 0; i < rom_count; i++) {
        if (queue_add_artwork(&roms[i], console))
            added++;
    }

    free(roms);
    return added;
}

int queue_add_all_cheats(const console_dir *console, bool show_hidden) {
    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, show_hidden, &roms);
    if (rom_count <= 0) {
        free(roms);
        return 0;
    }

    int added = 0;
    for (int i = 0; i < rom_count; i++) {
        if (queue_add_cheat(&roms[i], console))
            added++;
    }

    free(roms);
    return added;
}

/* ── Forced queue functions (re-download existing) ───────── */

static bool queue_add_artwork_internal(const rom_file *rom,
                                        const console_dir *console,
                                        bool force) {
    int system_id = ss_platform_id(console->tag);
    if (system_id < 0)
        return false;
    if (!force && artwork_exists(rom->path, rom->display))
        return false;

    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_count; i++) {
        if (g_items[i].type == QUEUE_TYPE_ARTWORK &&
            strcmp(g_items[i].rom_path, rom->path) == 0 &&
            !is_terminal_status(g_items[i].status)) {
            pthread_mutex_unlock(&g_mutex);
            return false;
        }
    }

    if (g_count >= QUEUE_MAX_ITEMS) {
        pthread_mutex_unlock(&g_mutex);
        return false;
    }

    queue_item *item = &g_items[g_count++];
    memset(item, 0, sizeof(*item));
    item->id = g_next_item_id++;
    if (g_next_item_id == 0)
        g_next_item_id = 1;
    item->type = QUEUE_TYPE_ARTWORK;
    snprintf(item->rom_display, sizeof(item->rom_display), "%s", rom->display);
    snprintf(item->rom_path, sizeof(item->rom_path), "%s", rom->path);
    snprintf(item->system_tag, sizeof(item->system_tag), "%s", console->tag);
    snprintf(item->system_display, sizeof(item->system_display), "%s", console->display);
    snprintf(item->console_path, sizeof(item->console_path), "%s", console->path);
    item->system_id = system_id;
    item->status    = QUEUE_IDLE;
    item->force     = force;
    set_dirty_locked();
    pthread_mutex_unlock(&g_mutex);

    ensure_manager_started();
    return true;
}

static bool queue_add_cheat_internal(const rom_file *rom,
                                      const console_dir *console,
                                      bool force) {
    if (!libretro_dir(console->tag))
        return false;
    if (!force && cheat_exists(console->tag, rom->display))
        return false;

    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_count; i++) {
        if (g_items[i].type == QUEUE_TYPE_CHEAT &&
            strcmp(g_items[i].rom_path, rom->path) == 0 &&
            !is_terminal_status(g_items[i].status)) {
            pthread_mutex_unlock(&g_mutex);
            return false;
        }
    }

    if (g_count >= QUEUE_MAX_ITEMS) {
        pthread_mutex_unlock(&g_mutex);
        return false;
    }

    queue_item *item = &g_items[g_count++];
    memset(item, 0, sizeof(*item));
    item->id = g_next_item_id++;
    if (g_next_item_id == 0)
        g_next_item_id = 1;
    item->type = QUEUE_TYPE_CHEAT;
    snprintf(item->rom_display, sizeof(item->rom_display), "%s", rom->display);
    snprintf(item->rom_path, sizeof(item->rom_path), "%s", rom->path);
    snprintf(item->system_tag, sizeof(item->system_tag), "%s", console->tag);
    snprintf(item->system_display, sizeof(item->system_display), "%s", console->display);
    snprintf(item->console_path, sizeof(item->console_path), "%s", console->path);
    item->system_id = ss_platform_id(console->tag);
    item->status    = QUEUE_IDLE;
    item->force     = force;
    set_dirty_locked();
    pthread_mutex_unlock(&g_mutex);

    ensure_manager_started();
    return true;
}

bool queue_add_artwork_forced(const rom_file *rom, const console_dir *console) {
    return queue_add_artwork_internal(rom, console, true);
}

bool queue_add_cheat_forced(const rom_file *rom, const console_dir *console) {
    return queue_add_cheat_internal(rom, console, true);
}

int queue_add_all_artwork_forced(const console_dir *console, bool show_hidden) {
    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, show_hidden, &roms);
    if (rom_count <= 0) {
        free(roms);
        return 0;
    }

    int added = 0;
    for (int i = 0; i < rom_count; i++) {
        if (queue_add_artwork_internal(&roms[i], console, true))
            added++;
    }

    free(roms);
    return added;
}

int queue_add_all_cheats_forced(const console_dir *console, bool show_hidden) {
    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, show_hidden, &roms);
    if (rom_count <= 0) {
        free(roms);
        return 0;
    }

    int added = 0;
    for (int i = 0; i < rom_count; i++) {
        if (queue_add_cheat_internal(&roms[i], console, true))
            added++;
    }

    free(roms);
    return added;
}

/* ── Manual queue functions ───────────────────────────────── */

static void get_manual_download_dir(char *buf, size_t buflen) {
    pthread_mutex_lock(&g_settings_mutex);
    if (g_settings_valid)
        snprintf(buf, buflen, "%s", g_settings.manual_download_dir);
    else
        buf[0] = '\0';
    pthread_mutex_unlock(&g_settings_mutex);
}

static bool queue_add_manual_internal(const rom_file *rom,
                                       const console_dir *console,
                                       bool force) {
    int system_id = ss_platform_id(console->tag);
    if (system_id < 0)
        return false;

    char manual_dir[PATH_MAX];
    get_manual_download_dir(manual_dir, sizeof(manual_dir));
    if (manual_dir[0] == '\0')
        return false;

    if (!force && manual_exists(manual_dir, console->tag, rom->display))
        return false;

    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_count; i++) {
        if (g_items[i].type == QUEUE_TYPE_MANUAL &&
            strcmp(g_items[i].rom_path, rom->path) == 0 &&
            !is_terminal_status(g_items[i].status)) {
            pthread_mutex_unlock(&g_mutex);
            return false;
        }
    }

    if (g_count >= QUEUE_MAX_ITEMS) {
        pthread_mutex_unlock(&g_mutex);
        return false;
    }

    queue_item *item = &g_items[g_count++];
    memset(item, 0, sizeof(*item));
    item->id = g_next_item_id++;
    if (g_next_item_id == 0)
        g_next_item_id = 1;
    item->type = QUEUE_TYPE_MANUAL;
    snprintf(item->rom_display, sizeof(item->rom_display), "%s", rom->display);
    snprintf(item->rom_path, sizeof(item->rom_path), "%s", rom->path);
    snprintf(item->system_tag, sizeof(item->system_tag), "%s", console->tag);
    snprintf(item->system_display, sizeof(item->system_display), "%s", console->display);
    snprintf(item->console_path, sizeof(item->console_path), "%s", console->path);
    item->system_id = system_id;
    item->status    = QUEUE_IDLE;
    item->force     = force;
    set_dirty_locked();
    pthread_mutex_unlock(&g_mutex);

    ensure_manager_started();
    return true;
}

bool queue_add_manual(const rom_file *rom, const console_dir *console) {
    return queue_add_manual_internal(rom, console, false);
}

bool queue_add_manual_forced(const rom_file *rom, const console_dir *console) {
    return queue_add_manual_internal(rom, console, true);
}

int queue_add_all_manuals(const console_dir *console, bool show_hidden) {
    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, show_hidden, &roms);
    if (rom_count <= 0) {
        free(roms);
        return 0;
    }

    int added = 0;
    for (int i = 0; i < rom_count; i++) {
        if (queue_add_manual(&roms[i], console))
            added++;
    }

    free(roms);
    return added;
}

int queue_add_all_manuals_forced(const console_dir *console, bool show_hidden) {
    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, show_hidden, &roms);
    if (rom_count <= 0) {
        free(roms);
        return 0;
    }

    int added = 0;
    for (int i = 0; i < rom_count; i++) {
        if (queue_add_manual_internal(&roms[i], console, true))
            added++;
    }

    free(roms);
    return added;
}

bool queue_is_queued(const char *rom_path, queue_item_type type) {
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_count; i++) {
        if (g_items[i].type == type &&
            strcmp(g_items[i].rom_path, rom_path) == 0 &&
            !is_terminal_status(g_items[i].status)) {
            pthread_mutex_unlock(&g_mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return false;
}

queue_item_status queue_get_rom_status(const char *rom_path, queue_item_type type) {
    pthread_mutex_lock(&g_mutex);
    for (int i = g_count - 1; i >= 0; i--) {
        if (g_items[i].type == type &&
            strcmp(g_items[i].rom_path, rom_path) == 0) {
            queue_item_status status = g_items[i].status;
            pthread_mutex_unlock(&g_mutex);
            return status;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return QUEUE_NONE;
}

queue_stats queue_get_stats(void) {
    queue_stats stats = {0};

    pthread_mutex_lock(&g_mutex);
    stats.total = g_count;
    for (int i = 0; i < g_count; i++) {
        queue_item_status status = g_items[i].status;
        if (status == QUEUE_DONE || status == QUEUE_SKIPPED)
            stats.done++;
        else if (status == QUEUE_NOT_FOUND || status == QUEUE_ERROR)
            stats.failed++;
        else
            stats.pending++;
    }
    pthread_mutex_unlock(&g_mutex);
    return stats;
}

queue_api_stats queue_get_api_stats(void) {
    queue_api_stats s;
    s.requests_today = atomic_load(&g_api_req_today);
    s.max_requests   = atomic_load(&g_api_max_req);
    s.max_threads    = atomic_load(&g_api_max_threads);
    return s;
}

bool queue_is_active(void) {
    bool active;

    pthread_mutex_lock(&g_mutex);
    active = queue_has_non_terminal_locked();
    pthread_mutex_unlock(&g_mutex);
    return active;
}

bool queue_check_dirty(void) {
    bool was_dirty;

    pthread_mutex_lock(&g_mutex);
    was_dirty = g_dirty;
    g_dirty = false;
    pthread_mutex_unlock(&g_mutex);
    return was_dirty;
}

static void stop_manager_for_transition(void) {
    atomic_store(&g_interrupt, 1);

    pthread_mutex_lock(&g_mutex);
    bool should_join = g_manager_started;
    pthread_mutex_unlock(&g_mutex);

    if (should_join)
        pthread_join(g_manager_thread, NULL);

    pthread_mutex_lock(&g_mutex);
    g_manager_started = false;
    g_worker_running = false;
    pthread_mutex_unlock(&g_mutex);

    atomic_store(&g_interrupt, 0);
}

void queue_clear_done(void) {
    pthread_mutex_lock(&g_mutex);
    if (queue_has_non_terminal_locked()) {
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    int write_index = 0;
    for (int i = 0; i < g_count; i++) {
        if (!is_terminal_status(g_items[i].status)) {
            if (write_index != i)
                g_items[write_index] = g_items[i];
            write_index++;
        }
    }
    if (write_index != g_count)
        set_dirty_locked();
    g_count = write_index;
    pthread_mutex_unlock(&g_mutex);
}

void queue_cancel_all(void) {
    stop_manager_for_transition();

    /* Mark remaining non-terminal items as cancelled */
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_count; i++) {
        if (!is_terminal_status(g_items[i].status)) {
            g_items[i].status = QUEUE_SKIPPED;
            snprintf(g_items[i].error_msg, sizeof(g_items[i].error_msg),
                     "Cancelled");
        }
    }
    set_dirty_locked();
    g_manager_started = false;
    g_worker_running = false;
    pthread_mutex_unlock(&g_mutex);
}

bool queue_invalidate_cheat_repo_state(void) {
    bool can_invalidate;

    pthread_mutex_lock(&g_mutex);
    can_invalidate = !queue_has_non_terminal_locked();
    pthread_mutex_unlock(&g_mutex);
    if (!can_invalidate)
        return false;

    pthread_mutex_lock(&g_cheat_state_mutex);
    clear_cheat_repo_state_locked();
    pthread_mutex_unlock(&g_cheat_state_mutex);
    return true;
}

int queue_snapshot(queue_item *out, int max_items) {
    pthread_mutex_lock(&g_mutex);
    int count = g_count < max_items ? g_count : max_items;
    memcpy(out, g_items, sizeof(queue_item) * (size_t)count);
    pthread_mutex_unlock(&g_mutex);
    return count;
}

int queue_handoff_snapshot(queue_item *out, int max_items) {
    stop_manager_for_transition();

    pthread_mutex_lock(&g_mutex);
    bool changed = false;
    for (int i = 0; i < g_count; i++) {
        if (!is_terminal_status(g_items[i].status)) {
            g_items[i].status = QUEUE_IDLE;
            g_items[i].error_msg[0] = '\0';
            changed = true;
        }
    }
    if (changed)
        set_dirty_locked();

    int count = g_count < max_items ? g_count : max_items;
    if (out && count > 0)
        memcpy(out, g_items, sizeof(queue_item) * (size_t)count);
    pthread_mutex_unlock(&g_mutex);
    return count;
}

int queue_load_items(const queue_item *items, int count) {
    if (count > QUEUE_MAX_ITEMS)
        count = QUEUE_MAX_ITEMS;

    pthread_mutex_lock(&g_mutex);
    bool has_pending = false;
    for (int i = 0; i < count; i++) {
        g_items[i] = items[i];
        /* Reset in-progress items to idle so workers pick them up */
        if (!is_terminal_status(g_items[i].status)) {
            g_items[i].status = QUEUE_IDLE;
            g_items[i].error_msg[0] = '\0';
            has_pending = true;
        }
        if (g_items[i].id >= g_next_item_id)
            g_next_item_id = g_items[i].id + 1;
    }
    g_count = count;
    g_dirty = true;
    pthread_mutex_unlock(&g_mutex);

    if (has_pending)
        ensure_manager_started();

    return count;
}

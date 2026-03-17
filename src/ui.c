#include "ui.h"
#include "cheats.h"
#include "device.h"
#include "screenscraper.h"
#include "systems.h"

#include "apostrophe.h"
#include "apostrophe_widgets.h"

#include <dirent.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Helpers ──────────────────────────────────────────────── */

static void show_error(const char *message) {
    ap_footer_item footer[] = {{AP_BTN_B, "Back", false}};
    ap_message_opts opts = {.message = message, .footer = footer, .footer_count = 1};
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
}

static void show_warning(const char *message) {
    ap_footer_item footer[] = {{AP_BTN_A, "Continue", false}};
    ap_message_opts opts = {.message = message, .footer = footer, .footer_count = 1};
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
}

/* ── Console name disambiguation ──────────────────────────── */

/* When two consoles share the same display name, append the tag to disambiguate. */
static void build_console_menu_names(const console_dir *consoles, int count,
                                      char names[][512]) {
    /* Count occurrences of each display name */
    for (int i = 0; i < count; i++) {
        int dupes = 0;
        for (int j = 0; j < count; j++) {
            if (strcmp(consoles[i].display, consoles[j].display) == 0)
                dupes++;
        }
        if (dupes > 1)
            snprintf(names[i], 512, "%s (%s)", consoles[i].display, consoles[i].tag);
        else
            snprintf(names[i], 512, "%s", consoles[i].display);

        if (consoles[i].is_disabled) {
            size_t len = strlen(names[i]);
            snprintf(names[i] + len, 512 - len, " [disabled]");
        }
    }
}

/* ── Main menu ────────────────────────────────────────────── */

typedef enum {
    MAIN_QUIT = 0,
    MAIN_SCRAPE_ARTWORK,
    MAIN_DOWNLOAD_CHEATS,
    MAIN_SETTINGS,
} main_action;

static main_action show_main_menu(void) {
    ap_list_item items[] = {
        {.label = "Scrape Artwork"},
        {.label = "Download Cheats"},
        {.label = "Settings"},
    };
    ap_footer_item footer[] = {
        {AP_BTN_B, "Quit", false},
        {AP_BTN_A, "Select", true},
    };

    ap_list_opts opts = ap_list_default_opts("ScrapeGoat", items, 3);
    opts.footer = footer;
    opts.footer_count = 2;

    ap_list_result result;
    int ret = ap_list(&opts, &result);
    if (ret == AP_CANCELLED || result.selected_index < 0)
        return MAIN_QUIT;

    switch (result.selected_index) {
    case 0: return MAIN_SCRAPE_ARTWORK;
    case 1: return MAIN_DOWNLOAD_CHEATS;
    case 2: return MAIN_SETTINGS;
    default: return MAIN_QUIT;
    }
}

/* ── Scrape artwork flow ──────────────────────────────────── */

static int pick_console_for_scraping(const app_settings *settings,
                                      console_dir *out) {
    console_dir *consoles = NULL;
    int console_count = scan_console_dirs(settings->show_hidden, &consoles);
    if (console_count <= 0) {
        show_error("No ROM folders found.");
        free(consoles);
        return 0;
    }

    /* Filter to systems with a ScreenScraper platform ID */
    console_dir *scrapable = malloc(sizeof(console_dir) * (size_t)console_count);
    int scrapable_count = 0;
    for (int i = 0; i < console_count; i++) {
        if (ss_platform_id(consoles[i].tag) >= 0)
            scrapable[scrapable_count++] = consoles[i];
    }
    free(consoles);

    if (scrapable_count == 0) {
        show_error("No supported systems found.\n\nNo ROM folders with a known system tag\nwere found.");
        free(scrapable);
        return 0;
    }

    /* Build menu names */
    char (*names)[512] = malloc(sizeof(char[512]) * (size_t)scrapable_count);
    build_console_menu_names(scrapable, scrapable_count, names);

    ap_list_item *items = calloc((size_t)scrapable_count, sizeof(ap_list_item));
    for (int i = 0; i < scrapable_count; i++)
        items[i].label = names[i];

    ap_footer_item footer[] = {
        {AP_BTN_B, "Back", false},
        {AP_BTN_A, "Select", true},
    };

    ap_list_opts opts = ap_list_default_opts("Select System", items, scrapable_count);
    opts.footer = footer;
    opts.footer_count = 2;

    ap_list_result result;
    int ret = ap_list(&opts, &result);
    free(items);

    int ok = 0;
    if (ret == AP_OK && result.selected_index >= 0 &&
        result.selected_index < scrapable_count) {
        *out = scrapable[result.selected_index];
        ok = 1;
    }

    free(names);
    free(scrapable);
    return ok;
}

static int pick_scrape_mode(bool *missing_only) {
    ap_list_item items[] = {
        {.label = "Scrape missing only  (skip ROMs with existing artwork)"},
        {.label = "Scrape all  (overwrite existing artwork)"},
    };
    ap_footer_item footer[] = {
        {AP_BTN_B, "Back", false},
        {AP_BTN_A, "Select", true},
    };

    ap_list_opts opts = ap_list_default_opts("Scrape Mode", items, 2);
    opts.footer = footer;
    opts.footer_count = 2;

    ap_list_result result;
    int ret = ap_list(&opts, &result);
    if (ret == AP_CANCELLED || result.selected_index < 0)
        return 0;

    *missing_only = (result.selected_index == 0);
    return 1;
}

static void show_scrape_result(run_result result) {
    char msg[256];
    if (result.status == RUN_CANCELLED) {
        snprintf(msg, sizeof(msg),
            "Scraping stopped.\n\nTotal:     %d\nFound:     %d\nNot found: %d\nErrors:    %d",
            result.summary.total, result.summary.found,
            result.summary.not_found, result.summary.errors);
    } else {
        snprintf(msg, sizeof(msg),
            "Scraping complete!\n\nTotal:     %d\nFound:     %d\nNot found: %d\nErrors:    %d",
            result.summary.total, result.summary.found,
            result.summary.not_found, result.summary.errors);
    }

    ap_footer_item footer[] = {{AP_BTN_A, "OK", true}};
    ap_message_opts opts = {.message = msg, .footer = footer, .footer_count = 1};
    ap_confirm_result confirm;
    ap_confirmation(&opts, &confirm);
}

/* Scrape worker context for ap_process_message */
typedef struct {
    const console_dir *console;
    bool missing_only;
    app_settings settings;
    run_result result;
    float *progress;
    int *interrupt_signal;
    char **dynamic_message;
} scrape_ctx;

/* Global pointer for callbacks to access context.
 * Safe because ap_process_message runs the worker on exactly one thread. */
static scrape_ctx *g_scrape_ctx;

static void scrape_progress_cb(float p) {
    if (g_scrape_ctx && g_scrape_ctx->progress)
        *g_scrape_ctx->progress = p;
}

static void scrape_message_cb(const char *msg) {
    if (!g_scrape_ctx || !g_scrape_ctx->dynamic_message) return;
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s", msg);
    *g_scrape_ctx->dynamic_message = buf;
}

static int scrape_worker_fn(void *userdata) {
    scrape_ctx *ctx = (scrape_ctx *)userdata;
    g_scrape_ctx = ctx;

    ctx->result = scrape_console(ctx->console, ctx->missing_only, &ctx->settings,
        (atomic_int *)ctx->interrupt_signal,
        scrape_progress_cb,
        scrape_message_cb
    );

    g_scrape_ctx = NULL;
    return 0;
}

static void scrape_artwork_flow(void) {
    app_settings settings = load_settings();

    if (settings.ss_username[0] == '\0') {
        show_warning("No ScreenScraper.fr user credentials set.\n\nScraping will proceed at basic rate\n(~1 req/min, single-threaded).\n\nFor much faster speeds, go to Settings\nand add your username and password.");
    }

    /* Pick console */
    console_dir console;
    if (!pick_console_for_scraping(&settings, &console)) {
        free_settings(&settings);
        return;
    }

    /* Pick scrape mode */
    bool missing_only;
    if (!pick_scrape_mode(&missing_only)) {
        free_settings(&settings);
        return;
    }

    /* Run scraping with progress UI */
    float progress = 0.0f;
    int interrupt_signal = 0;
    static char dyn_msg_buf[512];
    char *dyn_msg = dyn_msg_buf;
    snprintf(dyn_msg_buf, sizeof(dyn_msg_buf), "Starting...");

    scrape_ctx ctx = {
        .console = &console,
        .missing_only = missing_only,
        .settings = settings,
        .progress = &progress,
        .interrupt_signal = &interrupt_signal,
        .dynamic_message = &dyn_msg,
    };

    ap_process_opts opts = {
        .message = "Scraping...",
        .show_progress = true,
        .progress = &progress,
        .interrupt_signal = &interrupt_signal,
        .interrupt_button = AP_BTN_Y,
        .dynamic_message = &dyn_msg,
        .message_lines = 3,
    };

    ap_process_message(&opts, scrape_worker_fn, &ctx);
    if (ctx.result.status == RUN_ERROR)
        show_error(ctx.result.error);
    else
        show_scrape_result(ctx.result);
    free_settings(&settings);
}

/* ── Download cheats flow ─────────────────────────────────── */

static int pick_console_for_cheats(const app_settings *settings,
                                    console_dir *out) {
    console_dir *consoles = NULL;
    int console_count = scan_console_dirs(settings->show_hidden, &consoles);
    if (console_count <= 0) {
        show_error("No ROM folders found.");
        free(consoles);
        return 0;
    }

    console_dir *supported = malloc(sizeof(console_dir) * (size_t)console_count);
    int supported_count = 0;
    for (int i = 0; i < console_count; i++) {
        if (libretro_dir(consoles[i].tag) != NULL)
            supported[supported_count++] = consoles[i];
    }
    free(consoles);

    if (supported_count == 0) {
        show_error("No supported systems found.\n\nNo ROM folders with a known system tag\nwere found.");
        free(supported);
        return 0;
    }

    char (*names)[512] = malloc(sizeof(char[512]) * (size_t)supported_count);
    build_console_menu_names(supported, supported_count, names);

    ap_list_item *items = calloc((size_t)supported_count, sizeof(ap_list_item));
    for (int i = 0; i < supported_count; i++)
        items[i].label = names[i];

    ap_footer_item footer[] = {
        {AP_BTN_B, "Back", false},
        {AP_BTN_A, "Select", true},
    };

    ap_list_opts opts = ap_list_default_opts("Select System", items, supported_count);
    opts.footer = footer;
    opts.footer_count = 2;

    ap_list_result result;
    int ret = ap_list(&opts, &result);
    free(items);

    int ok = 0;
    if (ret == AP_OK && result.selected_index >= 0 &&
        result.selected_index < supported_count) {
        *out = supported[result.selected_index];
        ok = 1;
    }

    free(names);
    free(supported);
    return ok;
}

static void show_cheat_result(run_result result) {
    char msg[256];
    if (result.status == RUN_CANCELLED) {
        snprintf(msg, sizeof(msg),
            "Download stopped.\n\nTotal:      %d\nDownloaded: %d\nNot found:  %d\nErrors:     %d",
            result.summary.total, result.summary.found,
            result.summary.not_found, result.summary.errors);
    } else {
        snprintf(msg, sizeof(msg),
            "Download complete!\n\nTotal:      %d\nDownloaded: %d\nNot found:  %d\nErrors:     %d",
            result.summary.total, result.summary.found,
            result.summary.not_found, result.summary.errors);
    }

    ap_footer_item footer[] = {{AP_BTN_A, "OK", true}};
    ap_message_opts opts = {.message = msg, .footer = footer, .footer_count = 1};
    ap_confirm_result confirm;
    ap_confirmation(&opts, &confirm);
}

typedef struct {
    const console_dir *console;
    app_settings settings;
    run_result result;
    float *progress;
    int *interrupt_signal;
    char **dynamic_message;
} cheat_ctx;

static cheat_ctx *g_cheat_ctx;

static void cheat_progress_cb(float p) {
    if (g_cheat_ctx && g_cheat_ctx->progress)
        *g_cheat_ctx->progress = p;
}

static void cheat_message_cb(const char *msg) {
    if (!g_cheat_ctx || !g_cheat_ctx->dynamic_message)
        return;

    static char buf[512];
    snprintf(buf, sizeof(buf), "%s", msg);
    *g_cheat_ctx->dynamic_message = buf;
}

static int cheat_worker_fn(void *userdata) {
    cheat_ctx *ctx = (cheat_ctx *)userdata;
    g_cheat_ctx = ctx;

    int region_count = 0;
    char **region_prio = build_region_types(&ctx->settings, &region_count);
    if (!region_prio) {
        ctx->result.status = RUN_ERROR;
        ctx->result.summary = (scrape_summary){0};
        snprintf(ctx->result.error, sizeof(ctx->result.error),
                 "Out of memory while preparing region priorities.");
        g_cheat_ctx = NULL;
        return 0;
    }

    ctx->result = download_cheats_for_console(ctx->console,
        region_prio, region_count, ctx->interrupt_signal,
        cheat_progress_cb,
        cheat_message_cb
    );

    for (int i = 0; i < region_count; i++) free(region_prio[i]);
    free(region_prio);
    g_cheat_ctx = NULL;
    return 0;
}

static void download_cheats_flow(void) {
    app_settings settings = load_settings();

    console_dir console;
    if (!pick_console_for_cheats(&settings, &console)) {
        free_settings(&settings);
        return;
    }

    float progress = 0.0f;
    int interrupt_signal = 0;
    static char dyn_msg_buf[512];
    char *dyn_msg = dyn_msg_buf;
    snprintf(dyn_msg_buf, sizeof(dyn_msg_buf), "Preparing...");

    char title[256];
    snprintf(title, sizeof(title), "Downloading cheats for %s...", console.display);

    cheat_ctx ctx = {
        .console = &console,
        .settings = settings,
        .progress = &progress,
        .interrupt_signal = &interrupt_signal,
        .dynamic_message = &dyn_msg,
    };

    ap_process_opts opts = {
        .message = title,
        .show_progress = true,
        .progress = &progress,
        .interrupt_signal = &interrupt_signal,
        .interrupt_button = AP_BTN_Y,
        .dynamic_message = &dyn_msg,
        .message_lines = 3,
    };

    ap_process_message(&opts, cheat_worker_fn, &ctx);
    if (ctx.result.status == RUN_ERROR)
        show_error(ctx.result.error);
    else
        show_cheat_result(ctx.result);
    free_settings(&settings);
}

/* ── Settings screen ──────────────────────────────────────── */

static void edit_username(app_settings *settings) {
    ap_keyboard_result result;
    int ret = ap_keyboard(settings->ss_username, "Y: Cancel",
                           AP_KB_GENERAL, &result);
    if (ret != AP_OK) return;

    snprintf(settings->ss_username, sizeof(settings->ss_username), "%s", result.text);
    save_settings(settings);
}

static void edit_password(app_settings *settings) {
    ap_keyboard_result result;
    int ret = ap_keyboard(settings->ss_password, "Y: Cancel",
                           AP_KB_GENERAL, &result);
    if (ret != AP_OK) return;

    snprintf(settings->ss_password, sizeof(settings->ss_password), "%s", result.text);
    save_settings(settings);
}

/* ── Artwork priority editor ──────────────────────────────── */

static void edit_artwork_priority(app_settings *settings) {
    int count = 0;
    char **types = build_artwork_types(settings, &count);

    ap_list_item *items = calloc((size_t)count, sizeof(ap_list_item));
    for (int i = 0; i < count; i++) {
        items[i].label = media_type_display(types[i]);
        items[i].metadata = types[i];
    }

    ap_footer_item footer[] = {
        {AP_BTN_B, "Cancel", false},
        {AP_BTN_X, "Reorder", false},
        {AP_BTN_START, "Save", true},
    };

    ap_list_opts opts = ap_list_default_opts("Artwork Priority", items, count);
    opts.reorder_button = AP_BTN_X;
    opts.action_button = AP_BTN_START;
    opts.footer = footer;
    opts.footer_count = 3;

    ap_list_result result;
    int ret = ap_list(&opts, &result);

    if (ret == AP_OK &&
        (result.action == AP_ACTION_TRIGGERED || result.action == AP_ACTION_CONFIRMED)) {
        /* Save new order */
        for (int i = 0; i < settings->artwork_prio_count; i++)
            free(settings->artwork_prio[i]);
        settings->artwork_prio_count = 0;

        for (int i = 0; i < result.item_count && i < MAX_PRIORITY_ITEMS; i++) {
            if (result.items[i].metadata)
                settings->artwork_prio[settings->artwork_prio_count++] =
                    strdup(result.items[i].metadata);
        }
        save_settings(settings);
    }

    free(items);
    for (int i = 0; i < count; i++) free(types[i]);
    free(types);
}

/* ── Region priority editor ───────────────────────────────── */

static void edit_region_priority(app_settings *settings) {
    int count = 0;
    char **regions = build_region_types(settings, &count);

    /* Exclude "cus" (automatic catch-all) */
    int list_count = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(regions[i], "cus") != 0)
            list_count++;
    }

    ap_list_item *items = calloc((size_t)list_count, sizeof(ap_list_item));
    int idx = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(regions[i], "cus") == 0) continue;
        items[idx].label = region_display(regions[i]);
        items[idx].metadata = regions[i];
        idx++;
    }

    ap_footer_item footer[] = {
        {AP_BTN_B, "Cancel", false},
        {AP_BTN_X, "Reorder", false},
        {AP_BTN_START, "Save", true},
    };

    ap_list_opts opts = ap_list_default_opts("Region Priority", items, list_count);
    opts.reorder_button = AP_BTN_X;
    opts.action_button = AP_BTN_START;
    opts.footer = footer;
    opts.footer_count = 3;

    ap_list_result result;
    int ret = ap_list(&opts, &result);

    if (ret == AP_OK &&
        (result.action == AP_ACTION_TRIGGERED || result.action == AP_ACTION_CONFIRMED)) {
        for (int i = 0; i < settings->region_prio_count; i++)
            free(settings->region_prio[i]);
        settings->region_prio_count = 0;

        for (int i = 0; i < result.item_count && i < MAX_PRIORITY_ITEMS; i++) {
            if (result.items[i].metadata)
                settings->region_prio[settings->region_prio_count++] =
                    strdup(result.items[i].metadata);
        }
        save_settings(settings);
    }

    free(items);
    for (int i = 0; i < count; i++) free(regions[i]);
    free(regions);
}

/* ── Artwork options sub-menu ─────────────────────────────── */

static void edit_artwork_options(app_settings *settings) {
    for (;;) {
        /* Build summaries */
        int art_count = 0;
        char **art_types = build_artwork_types(settings, &art_count);
        char art_summary[256] = "";
        for (int i = 0; i < art_count && i < 3; i++) {
            if (i > 0) strcat(art_summary, " > ");
            strncat(art_summary, media_type_display(art_types[i]),
                    sizeof(art_summary) - strlen(art_summary) - 1);
        }
        if (art_count > 3) strcat(art_summary, " > ...");

        int reg_count = 0;
        char **reg_types = build_region_types(settings, &reg_count);
        char reg_summary[256] = "";
        for (int i = 0; i < reg_count && i < 3; i++) {
            if (strcmp(reg_types[i], "cus") == 0) continue;
            if (reg_summary[0]) strcat(reg_summary, " > ");
            strncat(reg_summary, region_display(reg_types[i]),
                    sizeof(reg_summary) - strlen(reg_summary) - 1);
        }
        if (reg_count > 3) strcat(reg_summary, " > ...");

        ap_option art_opt = {.label = art_summary, .value = "edit"};
        ap_option reg_opt = {.label = reg_summary, .value = "edit"};

        ap_options_item items[2] = {
            {.label = "Artwork priority", .type = AP_OPT_CLICKABLE,
             .options = &art_opt, .option_count = 1, .selected_option = 0},
            {.label = "Region priority", .type = AP_OPT_CLICKABLE,
             .options = &reg_opt, .option_count = 1, .selected_option = 0},
        };

        ap_footer_item footer[] = {
            {AP_BTN_B, "Back", false},
            {AP_BTN_A, "Edit", false},
            {AP_BTN_START, "Done", true},
        };

        ap_options_list_opts opts = {
            .title = "Artwork Options",
            .items = items,
            .item_count = 2,
            .footer = footer,
            .footer_count = 3,
            .confirm_button = AP_BTN_START,
        };

        ap_options_list_result result;
        int ret = ap_options_list(&opts, &result);

        for (int i = 0; i < art_count; i++) free(art_types[i]);
        free(art_types);
        for (int i = 0; i < reg_count; i++) free(reg_types[i]);
        free(reg_types);

        if (ret == AP_CANCELLED) return;

        if (result.action == AP_ACTION_SELECTED) {
            switch (result.focused_index) {
            case 0: edit_artwork_priority(settings); break;
            case 1: edit_region_priority(settings); break;
            }
            continue;
        }

        return; /* START pressed = done */
    }
}

/* ── Clear cheat cache ────────────────────────────────────── */

static int clear_cache_worker(void *userdata) {
    (void)userdata;
    char repo[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));

    /* We do a simple recursive delete */
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", repo);

    /* Use nftw-style manual walk instead of system() for safety */
    /* Just remove the directory tree by walking it */
    DIR *dir = opendir(repo);
    if (!dir) return 0;

    /* Simple recursive delete using a helper */
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", repo, entry->d_name);
        struct stat st;
        if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            /* For simplicity, use system rm for recursive dirs */
            char rm_cmd[PATH_MAX + 10];
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", path);
            (void)system(rm_cmd);
        } else {
            unlink(path);
        }
    }
    closedir(dir);
    rmdir(repo);
    return 0;
}

static void clear_cheat_cache(void) {
    ap_footer_item footer[] = {
        {AP_BTN_B, "Cancel", false},
        {AP_BTN_A, "Clear", true},
    };
    ap_message_opts msg_opts = {
        .message = "Clear the downloaded cheat database?\n\nThis deletes the local git checkout.\nIt will be re-downloaded on next use.",
        .footer = footer,
        .footer_count = 2,
    };
    ap_confirm_result confirm;
    int ret = ap_confirmation(&msg_opts, &confirm);
    if (ret != AP_OK || !confirm.confirmed)
        return;

    float progress = 0.0f;
    ap_process_opts proc_opts = {
        .message = "Clearing cheat cache...",
        .show_progress = true,
        .progress = &progress,
    };
    ap_process_message(&proc_opts, clear_cache_worker, NULL);
}

/* ── Settings screen ──────────────────────────────────────── */

static void show_settings_screen(void) {
    for (;;) {
        app_settings settings = load_settings();

        char user_display[260];
        if (settings.ss_username[0])
            snprintf(user_display, sizeof(user_display), "%s", settings.ss_username);
        else
            snprintf(user_display, sizeof(user_display), "<not set>");

        const char *pass_display = settings.ss_password[0] ? "<set>" : "<not set>";

        ap_option user_opt = {.label = user_display, .value = "edit"};
        ap_option pass_opt = {.label = pass_display, .value = "edit"};
        ap_option art_opt = {.label = "...", .value = "edit"};
        ap_option clear_opt = {.label = "...", .value = "clear"};
        ap_option hidden_opts[2] = {
            {.label = "Off", .value = "0"},
            {.label = "On", .value = "1"},
        };

        ap_options_item items[5] = {
            {.label = "Username", .type = AP_OPT_CLICKABLE,
             .options = &user_opt, .option_count = 1, .selected_option = 0},
            {.label = "Password", .type = AP_OPT_CLICKABLE,
             .options = &pass_opt, .option_count = 1, .selected_option = 0},
            {.label = "Artwork Options", .type = AP_OPT_CLICKABLE,
             .options = &art_opt, .option_count = 1, .selected_option = 0},
            {.label = "Clear cheat cache", .type = AP_OPT_CLICKABLE,
             .options = &clear_opt, .option_count = 1, .selected_option = 0},
            {.label = "Include hidden/disabled/empty ROMs", .type = AP_OPT_STANDARD,
             .options = hidden_opts, .option_count = 2,
             .selected_option = settings.show_hidden ? 1 : 0},
        };

        ap_footer_item footer[] = {
            {AP_BTN_B, "Back", false},
            {AP_BTN_A, "Edit", false},
            {AP_BTN_START, "Save", true},
        };

        ap_options_list_opts opts = {
            .title = "Settings",
            .items = items,
            .item_count = 5,
            .footer = footer,
            .footer_count = 3,
            .confirm_button = AP_BTN_START,
        };

        ap_options_list_result result;
        int ret = ap_options_list(&opts, &result);

        if (ret == AP_CANCELLED) {
            free_settings(&settings);
            return;
        }

        if (result.action == AP_ACTION_SELECTED) {
            switch (result.focused_index) {
            case 0: edit_username(&settings); break;
            case 1: edit_password(&settings); break;
            case 2: edit_artwork_options(&settings); break;
            case 3: clear_cheat_cache(); break;
            }
            free_settings(&settings);
            continue;
        }

        /* START pressed: save show_hidden and exit */
        settings.show_hidden = (result.items[4].selected_option == 1);
        save_settings(&settings);
        free_settings(&settings);
        return;
    }
}

/* ── Main application loop ────────────────────────────────── */

void run_app(void) {
    for (;;) {
        main_action action = show_main_menu();
        switch (action) {
        case MAIN_SCRAPE_ARTWORK:   scrape_artwork_flow(); break;
        case MAIN_DOWNLOAD_CHEATS:  download_cheats_flow(); break;
        case MAIN_SETTINGS:         show_settings_screen(); break;
        case MAIN_QUIT:             return;
        }
    }
}

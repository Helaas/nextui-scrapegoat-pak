#include "ui.h"
#include "cheats.h"
#include "device.h"
#include "queue.h"
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
#include <sys/stat.h>

/* Suppress GCC warnings about snprintf truncation when combining
   PATH_MAX-sized strings — truncation is safe by design.
   Also suppress missing-field-initializers for ap_footer_item which
   has an optional button_text field we don't use. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

/* ── Forward declarations ─────────────────────────────────── */

typedef enum { LIB_MODE_ART, LIB_MODE_CHEAT } library_mode;

static bool show_rom_list_screen(const console_dir *console,
                                  const app_settings *settings,
                                  library_mode mode);

static bool show_rom_detail_screen(const rom_file *rom,
                                    const console_dir *console,
                                    library_mode mode,
                                    const app_settings *settings);

/* ── Helpers ──────────────────────────────────────────────── */

static void show_error(const char *message) {
    ap_footer_item footer[] = {{AP_BTN_B, "BACK", false}};
    ap_message_opts opts = {.message = message, .footer = footer, .footer_count = 1};
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
}

static void show_warning(const char *message) {
    ap_footer_item footer[] = {{AP_BTN_A, "CONTINUE", false}};
    ap_message_opts opts = {.message = message, .footer = footer, .footer_count = 1};
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
}

static void show_brief(const char *message) {
    ap_footer_item footer[] = {{AP_BTN_A, "OK", true}};
    ap_message_opts opts = {.message = message, .footer = footer, .footer_count = 1};
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
}

/* Returns true if user chose "Go to Downloads". */
static bool show_queued_brief(const char *message) {
    ap_footer_item footer[] = {
        {AP_BTN_B, "BACK",            false},
        {AP_BTN_A, "GO TO DOWNLOADS", true},
    };
    ap_message_opts opts = {
        .message      = message,
        .footer       = footer,
        .footer_count = 2,
    };
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
    return result.confirmed;
}

/* ── Console name disambiguation ──────────────────────────── */

static void build_console_menu_names(const console_dir *consoles, int count,
                                      char names[][512]) {
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

/* ── System stats for library browser ─────────────────────── */

typedef struct {
    int rom_count;
    int art_count;
    int cheat_count;
    bool has_ss;      /* has ScreenScraper mapping */
    bool has_libretro; /* has libretro cheat directory */
} system_stats;

static system_stats compute_system_stats(const console_dir *console, bool show_hidden) {
    system_stats stats = {0};
    stats.has_ss = (ss_platform_id(console->tag) >= 0);
    stats.has_libretro = (libretro_dir(console->tag) != NULL);

    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, show_hidden, &roms);
    if (rom_count <= 0) {
        free(roms);
        return stats;
    }

    stats.rom_count = rom_count;
    for (int i = 0; i < rom_count; i++) {
        if (artwork_exists(roms[i].path, roms[i].display))
            stats.art_count++;
        if (stats.has_libretro && cheat_exists(console->tag, roms[i].display))
            stats.cheat_count++;
    }

    free(roms);
    return stats;
}

/* ── Main menu ────────────────────────────────────────────── */

typedef enum {
    MAIN_QUIT = 0,
    MAIN_SCRAPE_ART,
    MAIN_DOWNLOAD_CHEATS,
    MAIN_PROGRESS,
    MAIN_API_USAGE,
    MAIN_SETTINGS,
} main_action;

static main_action show_main_menu(void) {
    queue_stats qstats = queue_get_stats();

    char progress_label[64];
    if (qstats.total > 0) {
        int processed = qstats.done + qstats.failed;
        if (qstats.failed > 0)
            snprintf(progress_label, sizeof(progress_label),
                     "Queued Downloads  (%d/%d, %d failed)",
                     processed, qstats.total, qstats.failed);
        else
            snprintf(progress_label, sizeof(progress_label),
                     "Queued Downloads  (%d/%d)",
                     processed, qstats.total);
    } else
        snprintf(progress_label, sizeof(progress_label), "Queued Downloads");

    ap_list_item items[] = {
        {.label = "Artwork"},
        {.label = "Cheats"},
        {.label = progress_label},
        {.label = "API Usage"},
        {.label = "Settings"},
    };
    ap_footer_item footer[] = {
        {AP_BTN_B, "QUIT", false},
        {AP_BTN_A, "SELECT", true},
    };

    ap_list_opts opts = ap_list_default_opts("ScrapeGoat", items, 5);
    opts.footer = footer;
    opts.footer_count = 2;

    ap_list_result result;
    int ret = ap_list(&opts, &result);
    if (ret == AP_CANCELLED || result.selected_index < 0)
        return MAIN_QUIT;

    switch (result.selected_index) {
    case 0: return MAIN_SCRAPE_ART;
    case 1: return MAIN_DOWNLOAD_CHEATS;
    case 2: return MAIN_PROGRESS;
    case 3: return MAIN_API_USAGE;
    case 4: return MAIN_SETTINGS;
    default: return MAIN_QUIT;
    }
}

/* ── Library: ROM list ────────────────────────────────────── */

typedef enum {
    ROM_FILTER_ALL = 0,
    ROM_FILTER_MISSING,
    ROM_FILTER_INSTALLED,
} rom_filter;

static const char *rom_filter_name(rom_filter f) {
    switch (f) {
    case ROM_FILTER_MISSING:   return "Missing";
    case ROM_FILTER_INSTALLED: return "Installed";
    default:                   return "All";
    }
}

static const char *rom_status_label(const rom_file *rom,
                                     const console_dir *console,
                                     library_mode mode) {
    if (mode == LIB_MODE_ART) {
        queue_item_status qs = queue_get_rom_status(rom->path, QUEUE_TYPE_ARTWORK);
        if (qs >= QUEUE_IDLE && qs <= QUEUE_DOWNLOADING) {
            switch (qs) {
            case QUEUE_IDLE:        return "queued";
            case QUEUE_SEARCHING:   return "searching";
            case QUEUE_DOWNLOADING: return "downloading";
            default:                return "queued";
            }
        }
        return artwork_exists(rom->path, rom->display) ? "art" : NULL;
    } else {
        queue_item_status qs = queue_get_rom_status(rom->path, QUEUE_TYPE_CHEAT);
        if (qs >= QUEUE_IDLE && qs <= QUEUE_MATCHING) {
            switch (qs) {
            case QUEUE_IDLE:        return "queued";
            case QUEUE_CLONING:     return "cloning";
            case QUEUE_MATCHING:    return "matching";
            default:                return "queued";
            }
        }
        return cheat_exists(console->tag, rom->display) ? "cht" : NULL;
    }
}

static bool show_rom_list_screen(const console_dir *console,
                                  const app_settings *settings,
                                  library_mode mode) {
    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, settings->show_hidden, &roms);
    if (rom_count <= 0) {
        show_error("No ROMs found in this system.");
        free(roms);
        return false;
    }

    /* Default to Missing filter, but fall back to All if nothing is missing */
    rom_filter filter = ROM_FILTER_MISSING;
    {
        bool has_missing = false;
        for (int i = 0; i < rom_count; i++) {
            bool inst = (mode == LIB_MODE_ART)
                ? artwork_exists(roms[i].path, roms[i].display)
                : cheat_exists(console->tag, roms[i].display);
            if (!inst) { has_missing = true; break; }
        }
        if (!has_missing) filter = ROM_FILTER_ALL;
    }
    int        initial_idx = 0;
    int        visible_start = 0;

    /* Reusable filter map: filter_map[visible_i] = real_i */
    int *filter_map = malloc(sizeof(int) * (size_t)rom_count);

    for (;;) {
        /* Determine which ROMs are installed (needed for filter & label) */
        bool *installed = malloc(sizeof(bool) * (size_t)rom_count);
        for (int i = 0; i < rom_count; i++) {
            installed[i] = (mode == LIB_MODE_ART)
                ? artwork_exists(roms[i].path, roms[i].display)
                : cheat_exists(console->tag, roms[i].display);
        }

        /* Build filter map */
        int visible_count = 0;
        for (int i = 0; i < rom_count; i++) {
            if (filter == ROM_FILTER_MISSING   &&  installed[i]) continue;
            if (filter == ROM_FILTER_INSTALLED && !installed[i]) continue;
            filter_map[visible_count++] = i;
        }

        /* Build list items */
        char (*labels)[512] = malloc(sizeof(char[512]) * (size_t)(visible_count > 0 ? visible_count : 1));
        ap_list_item *items = calloc((size_t)(visible_count > 0 ? visible_count : 1), sizeof(ap_list_item));

        for (int vi = 0; vi < visible_count; vi++) {
            int i = filter_map[vi];
            const char *status = rom_status_label(&roms[i], console, mode);
            bool is_inst = installed[i];
            if (is_inst)
                snprintf(labels[vi], 512, "\u2713  %s", roms[i].display);
            else if (status)
                snprintf(labels[vi], 512, "   %s   [%s]", roms[i].display, status);
            else
                snprintf(labels[vi], 512, "   %s", roms[i].display);
            items[vi].label = labels[vi];
        }

        free(installed);

        /* Title always shows filter tag */
        char title[256];
        snprintf(title, sizeof(title), "%s  [%s]",
                 console->display, rom_filter_name(filter));

        /* Footer: B position depends on screen width */
        ap_footer_item footer[4];
        if (ap_get_screen_width() >= 1024) {
            footer[0] = (ap_footer_item){AP_BTN_B, "BACK",        false};
            footer[1] = (ap_footer_item){AP_BTN_X, "FILTER",      false};
            footer[2] = (ap_footer_item){AP_BTN_Y, "QUEUE SHOWN", false};
        } else {
            footer[0] = (ap_footer_item){AP_BTN_X, "FILTER",      false};
            footer[1] = (ap_footer_item){AP_BTN_Y, "QUEUE SHOWN", false};
            footer[2] = (ap_footer_item){AP_BTN_B, "BACK",        false};
        }
        footer[3] = (ap_footer_item){AP_BTN_A, "OPEN", true};

        ap_list_opts opts = ap_list_default_opts(title, items,
                                                  visible_count > 0 ? visible_count : 0);
        opts.footer                  = footer;
        opts.footer_count            = 4;
        opts.secondary_action_button = AP_BTN_Y;
        opts.tertiary_action_button  = AP_BTN_X;
        opts.initial_index           = initial_idx;
        opts.visible_start_index     = visible_start;

        ap_list_result result;
        int ret = ap_list(&opts, &result);

        initial_idx  = result.selected_index;
        visible_start = result.visible_start_index;

        free(labels);
        free(items);

        if (ret == AP_CANCELLED) break;

        /* X: cycle filter */
        if (result.action == AP_ACTION_TERTIARY_TRIGGERED) {
            filter = (rom_filter)((filter + 1) % 3);
            initial_idx   = 0;
            visible_start = 0;
            continue;
        }

        int sel = result.selected_index;
        if (sel < 0 || sel >= visible_count) {
            if (visible_count == 0) continue; /* empty filtered list, let user change filter */
            break;
        }
        int real = filter_map[sel];

        if (result.action == AP_ACTION_SELECTED || result.action == AP_ACTION_TRIGGERED) {
            /* A: Open detail screen */
            if (show_rom_detail_screen(&roms[real], console, mode, settings)) {
                free(filter_map);
                free(roms);
                return true;
            }
            continue;
        }

        if (result.action == AP_ACTION_SECONDARY_TRIGGERED) {
            /* Y: Queue all visible (filtered) ROMs */
            queue_set_settings(settings);

            /* Count how many visible ROMs are already installed */
            int installed_count = 0;
            for (int vi = 0; vi < visible_count; vi++) {
                int ri = filter_map[vi];
                bool is_inst = (mode == LIB_MODE_ART)
                    ? artwork_exists(roms[ri].path, roms[ri].display)
                    : cheat_exists(console->tag, roms[ri].display);
                if (is_inst) installed_count++;
            }

            bool force = false;
            if (installed_count > 0) {
                ap_list_item choices[] = {
                    {.label = "Queue missing only"},
                    {.label = "Re-download all (including installed)"},
                };
                ap_footer_item cf[] = {
                    {AP_BTN_B, "CANCEL", false},
                    {AP_BTN_A, "SELECT", true},
                };
                char choice_title[64];
                snprintf(choice_title, sizeof(choice_title),
                         "%d already installed", installed_count);
                ap_list_opts copts = ap_list_default_opts(choice_title, choices, 2);
                copts.footer       = cf;
                copts.footer_count = 2;
                ap_list_result cres;
                int cret = ap_list(&copts, &cres);
                if (cret == AP_CANCELLED || cres.selected_index < 0)
                    continue;
                force = (cres.selected_index == 1);
            }

            int added = 0;
            for (int vi = 0; vi < visible_count; vi++) {
                int ri = filter_map[vi];
                if (force) {
                    bool ok = (mode == LIB_MODE_ART)
                        ? queue_add_artwork_forced(&roms[ri], console)
                        : queue_add_cheat_forced(&roms[ri], console);
                    if (ok) added++;
                } else {
                    bool ok = (mode == LIB_MODE_ART)
                        ? queue_add_artwork(&roms[ri], console)
                        : queue_add_cheat(&roms[ri], console);
                    if (ok) added++;
                }
            }

            char msg[128];
            snprintf(msg, sizeof(msg), "Queued %d ROMs for %s.", added,
                     mode == LIB_MODE_ART ? "artwork" : "cheats");
            if (show_queued_brief(msg)) {
                free(filter_map);
                free(roms);
                return true;
            }
            continue;
        }
    }

    free(filter_map);
    free(roms);
    return false;
}

/* ── ROM detail screen ───────────────────────────────────── */

static bool show_rom_detail_screen(const rom_file *rom,
                                    const console_dir *console,
                                    library_mode mode,
                                    const app_settings *settings) {
    bool is_art = (mode == LIB_MODE_ART);
    bool is_installed = is_art
        ? artwork_exists(rom->path, rom->display)
        : cheat_exists(console->tag, rom->display);

    queue_item_status qs = is_art
        ? queue_get_rom_status(rom->path, QUEUE_TYPE_ARTWORK)
        : queue_get_rom_status(rom->path, QUEUE_TYPE_CHEAT);
    bool is_queued = (qs >= QUEUE_IDLE && qs <= QUEUE_MATCHING);

    const char *status_str;
    if (is_queued)
        status_str = rom_status_label(rom, console, mode);
    else if (is_installed)
        status_str = "Installed";
    else
        status_str = "Missing";
    if (!status_str) status_str = "Missing";

    ap_detail_section sections[4];
    int section_count = 0;

    /* Image section — show artwork if present */
    char art_path[PATH_MAX] = {0};
    if (is_art) {
        artwork_src_path(rom->path, rom->display, art_path, sizeof(art_path));
        struct stat st;
        if (stat(art_path, &st) == 0) {
            sections[section_count] = (ap_detail_section){
                .type = AP_SECTION_IMAGE,
                .title = NULL,
                .image_path = art_path,
                .image_w = ap_scale(200),
                .image_h = ap_scale(200),
            };
            section_count++;
        }
    }

    /* Info section */
    ap_detail_info_pair info_pairs[] = {
        {"Status", status_str},
        {"System", console->display},
        {"Type",   is_art ? "Artwork" : "Cheat"},
    };
    sections[section_count] = (ap_detail_section){
        .type = AP_SECTION_INFO,
        .title = NULL,
        .info_pairs = info_pairs,
        .info_count = 3,
    };
    section_count++;

    /* Cheat list section — if cheat mode and installed */
    char *cheat_text = NULL;
    char cheat_title[64] = {0};
    if (!is_art && is_installed) {
        char cheats_base[PATH_MAX];
        char cht_path[PATH_MAX];
        get_cheats_path(cheats_base, sizeof(cheats_base));
        snprintf(cht_path, sizeof(cht_path), "%s/%s/%s.cht",
                 cheats_base, console->tag, rom->display);

        cheat_desc_list descs;
        if (parse_cheat_descriptions(cht_path, &descs) == 0 && descs.count > 0) {
            size_t total_len = 0;
            for (int i = 0; i < descs.count; i++) {
                total_len += 8;
                if (descs.descriptions[i])
                    total_len += strlen(descs.descriptions[i]);
                else
                    total_len += 12;
            }
            cheat_text = malloc(total_len + 1);
            if (cheat_text) {
                cheat_text[0] = '\0';
                char *pos = cheat_text;
                for (int i = 0; i < descs.count; i++) {
                    if (pos != cheat_text) *pos++ = '\n';
                    const char *desc = descs.descriptions[i];
                    if (desc && desc[0])
                        pos += sprintf(pos, "%d. %s", i + 1, desc);
                    else
                        pos += sprintf(pos, "%d. Cheat %d", i + 1, i + 1);
                }
                *pos = '\0';
            }
            cheat_desc_list_free(&descs);

            if (cheat_text && cheat_text[0]) {
                int cheat_count = 0;
                for (const char *p = cheat_text; *p; p++)
                    if (*p == '\n') cheat_count++;
                cheat_count++;
                snprintf(cheat_title, sizeof(cheat_title), "Cheats (%d)", cheat_count);
                sections[section_count] = (ap_detail_section){
                    .type = AP_SECTION_DESCRIPTION,
                    .title = cheat_title,
                    .description = cheat_text,
                };
                section_count++;
            }
        }
    }

    /* Footer: B=Back, A=Queue (or Re-download) */
    const char *action_label = is_queued ? "QUEUED" :
                               is_installed ? "RE-DOWNLOAD" : "QUEUE";
    ap_footer_item footer[] = {
        {AP_BTN_B, "BACK",       false},
        {AP_BTN_A, action_label, true},
    };
    int footer_count = is_queued ? 1 : 2; /* hide A if already queued */

    ap_detail_opts opts = {
        .title         = rom->display,
        .sections      = sections,
        .section_count = section_count,
        .footer        = footer,
        .footer_count  = footer_count,
    };

    ap_detail_result result;
    ap_detail_screen(&opts, &result);

    if (result.action == AP_DETAIL_ACTION && !is_queued) {
        if (is_installed) {
            /* Confirm re-download */
            char prompt[256];
            snprintf(prompt, sizeof(prompt),
                     "%s already installed. Re-download?",
                     is_art ? "Artwork" : "Cheats");
            ap_footer_item cf[] = {
                {AP_BTN_B, "NO",  false},
                {AP_BTN_A, "YES", true},
            };
            ap_message_opts mopts = {
                .message      = prompt,
                .footer       = cf,
                .footer_count = 2,
            };
            ap_confirm_result cres;
            ap_confirmation(&mopts, &cres);
            if (cres.confirmed) {
                queue_set_settings(settings);
                if (is_art)
                    queue_add_artwork_forced(rom, console);
                else
                    queue_add_cheat_forced(rom, console);
                char msg[256];
                snprintf(msg, sizeof(msg), "Re-queued \"%s\" for %s.",
                         rom->display, is_art ? "artwork" : "cheats");
                if (show_queued_brief(msg)) {
                    free(cheat_text);
                    return true;
                }
            }
        } else {
            queue_set_settings(settings);
            bool added;
            if (is_art)
                added = queue_add_artwork(rom, console);
            else
                added = queue_add_cheat(rom, console);
            if (added) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Queued \"%s\" for %s.",
                         rom->display, is_art ? "artwork" : "cheats");
                if (show_queued_brief(msg)) {
                    free(cheat_text);
                    return true;
                }
            } else {
                show_brief("Already queued.");
            }
        }
    }

    free(cheat_text);
    return false;
}

/* ── Library: System list ─────────────────────────────────── */

static bool show_library_screen(library_mode mode) {
    app_settings settings = load_settings();

    console_dir *consoles = NULL;
    int console_count = scan_console_dirs(settings.show_hidden, &consoles);
    if (console_count <= 0) {
        show_error("No ROM folders found.");
        free(consoles);
        free_settings(&settings);
        return false;
    }

    /* Build menu names and compute stats, filtering by mode */
    char (*names)[512] = malloc(sizeof(char[512]) * (size_t)console_count);
    system_stats *stats = malloc(sizeof(system_stats) * (size_t)console_count);
    build_console_menu_names(consoles, console_count, names);

    for (int i = 0; i < console_count; i++)
        stats[i] = compute_system_stats(&consoles[i], settings.show_hidden);

    /* Filter consoles by mode and build visible list */
    int *visible_map = malloc(sizeof(int) * (size_t)console_count);
    int visible_count = 0;
    for (int i = 0; i < console_count; i++) {
        if (mode == LIB_MODE_ART && !stats[i].has_ss) continue;
        if (mode == LIB_MODE_CHEAT && !stats[i].has_libretro) continue;
        visible_map[visible_count++] = i;
    }

    if (visible_count <= 0) {
        show_error(mode == LIB_MODE_ART
            ? "No systems with artwork support found."
            : "No systems with cheat support found.");
        free(visible_map);
        free(names);
        free(stats);
        free(consoles);
        free_settings(&settings);
        return false;
    }

    /* Build labels (system name) and metadata (counts) */
    char (*labels)[512] = malloc(sizeof(char[512]) * (size_t)visible_count);
    char (*meta)[32]    = malloc(sizeof(char[32])  * (size_t)visible_count);
    for (int vi = 0; vi < visible_count; vi++) {
        int i = visible_map[vi];
        snprintf(labels[vi], 512, "%s", names[i]);
        int count = (mode == LIB_MODE_ART) ? stats[i].art_count : stats[i].cheat_count;
        snprintf(meta[vi], 32, "%d / %d", count, stats[i].rom_count);
    }

    const char *title = (mode == LIB_MODE_ART) ? "Artwork" : "Cheats";

    int initial_idx = 0;
    int visible_start = 0;

    for (;;) {
        ap_list_item *items = calloc((size_t)visible_count, sizeof(ap_list_item));
        for (int vi = 0; vi < visible_count; vi++) {
            items[vi].label    = labels[vi];
            items[vi].trailing_text = meta[vi];
        }

        ap_footer_item footer[] = {
            {AP_BTN_A, "OPEN", true},
            {AP_BTN_B, "BACK", false},
        };

        ap_list_opts opts = ap_list_default_opts(title, items, visible_count);
        opts.footer = footer;
        opts.footer_count = 2;
        opts.initial_index = initial_idx;
        opts.visible_start_index = visible_start;

        ap_list_result result;
        int ret = ap_list(&opts, &result);
        free(items);

        if (ret == AP_CANCELLED) break;

        int sel = result.selected_index;
        if (sel < 0 || sel >= visible_count) break;

        initial_idx = sel;
        visible_start = result.visible_start_index;
        int real_idx = visible_map[sel];

        /* A: Open ROM list */
        if (show_rom_list_screen(&consoles[real_idx], &settings, mode)) {
            free(visible_map);
            free(names);
            free(labels);
            free(meta);
            free(stats);
            free(consoles);
            free_settings(&settings);
            return true;
        }
    }

    free(visible_map);
    free(names);
    free(labels);
    free(meta);
    free(stats);
    free(consoles);
    free_settings(&settings);
    return false;
}

/* ── API Usage screen ─────────────────────────────────────── */

static void show_api_usage_screen(void) {
    queue_api_stats api = queue_get_api_stats();

    if (api.max_requests <= 0) {
        show_brief("No API data available yet.\n\nStats update after the first\nartwork search.");
        return;
    }

    char req_today[32], daily_limit[32], remaining[32], threads[32];
    snprintf(req_today,   sizeof(req_today),   "%d", api.requests_today);
    snprintf(daily_limit, sizeof(daily_limit), "%d", api.max_requests);
    snprintf(remaining,   sizeof(remaining),   "%d", api.max_requests - api.requests_today);
    snprintf(threads,     sizeof(threads),     "%d", api.max_threads);

    ap_detail_info_pair info_pairs[] = {
        {"Requests Today", req_today},
        {"Daily Limit",    daily_limit},
        {"Remaining",      remaining},
        {"Threads",        threads},
    };
    ap_detail_section sections[] = {{
        .type       = AP_SECTION_INFO,
        .title      = NULL,
        .info_pairs = info_pairs,
        .info_count = 4,
    }};
    ap_footer_item footer[] = {
        {AP_BTN_B, "BACK", false},
    };
    ap_detail_opts opts = {
        .title         = "API Usage",
        .sections      = sections,
        .section_count = 1,
        .footer        = footer,
        .footer_count  = 1,
    };
    ap_detail_result result;
    ap_detail_screen(&opts, &result);
}

/* ── Progress screen (custom render loop) ─────────────────── */

static const char *queue_status_text(queue_item_status status) {
    switch (status) {
    case QUEUE_NONE:        return "-";
    case QUEUE_IDLE:        return "Queued";
    case QUEUE_SEARCHING:   return "Searching...";
    case QUEUE_DOWNLOADING: return "Downloading...";
    case QUEUE_CLONING:     return "Cloning db...";
    case QUEUE_MATCHING:    return "Matching...";
    case QUEUE_DONE:        return "Done";
    case QUEUE_NOT_FOUND:   return "Not Found";
    case QUEUE_ERROR:       return "Error";
    case QUEUE_SKIPPED:     return "Skipped";
    }
    return "?";
}

static bool is_item_terminal(queue_item_status status) {
    return status == QUEUE_DONE || status == QUEUE_SKIPPED ||
           status == QUEUE_ERROR || status == QUEUE_NOT_FOUND;
}

static void show_item_detail(const queue_item *item) {
    bool is_error = (item->status == QUEUE_ERROR ||
                     item->status == QUEUE_NOT_FOUND);
    bool is_cheat = (item->type == QUEUE_TYPE_CHEAT);

    ap_detail_section sections[3];
    int section_count = 0;

    /* Info section: status + system */
    const char *status_str = queue_status_text(item->status);
    const char *type_str = is_cheat ? "Cheat" : "Artwork";
    ap_detail_info_pair info_pairs[] = {
        {"Status", status_str},
        {"System", item->system_display},
        {"Type", type_str},
    };
    sections[section_count] = (ap_detail_section){
        .type = AP_SECTION_INFO,
        .title = NULL,
        .info_pairs = info_pairs,
        .info_count = 3,
    };
    section_count++;

    /* Error detail */
    char *cheat_text = NULL;
    char art_path[PATH_MAX] = {0};
    char cheat_title[64] = {0};

    if (is_error) {
        const char *msg = item->error_msg[0] ? item->error_msg :
                          (item->status == QUEUE_NOT_FOUND ? "Not found in libretro database" :
                           "An unknown error occurred");
        sections[section_count] = (ap_detail_section){
            .type = AP_SECTION_DESCRIPTION,
            .title = "Error",
            .description = msg,
        };
        section_count++;
    } else if (is_cheat) {
        /* Parse and display cheat descriptions */
        char cheats_base[PATH_MAX];
        char cht_path[PATH_MAX];
        get_cheats_path(cheats_base, sizeof(cheats_base));
        snprintf(cht_path, sizeof(cht_path), "%s/%s/%s.cht",
                 cheats_base, item->system_tag, item->rom_display);

        cheat_desc_list descs;
        if (parse_cheat_descriptions(cht_path, &descs) == 0 && descs.count > 0) {
            /* Build a numbered list: "1. Description\n2. Description\n..." */
            /* Each line: up to 4 digits + ". " + description + "\n" */
            size_t total_len = 0;
            for (int i = 0; i < descs.count; i++) {
                total_len += 8; /* "NNN. " prefix + newline + safety */
                if (descs.descriptions[i])
                    total_len += strlen(descs.descriptions[i]);
                else
                    total_len += 12; /* "Cheat NNN" fallback */
            }
            cheat_text = malloc(total_len + 1);
            if (cheat_text) {
                cheat_text[0] = '\0';
                char *pos = cheat_text;
                for (int i = 0; i < descs.count; i++) {
                    if (pos != cheat_text) *pos++ = '\n';
                    const char *desc = descs.descriptions[i];
                    if (desc && desc[0])
                        pos += sprintf(pos, "%d. %s", i + 1, desc);
                    else
                        pos += sprintf(pos, "%d. Cheat %d", i + 1, i + 1);
                }
                *pos = '\0';
            }
            cheat_desc_list_free(&descs);

            if (cheat_text && cheat_text[0]) {
                int cheat_count = 0;
                for (const char *p = cheat_text; *p; p++)
                    if (*p == '\n') cheat_count++;
                cheat_count++; /* last line has no newline */
                snprintf(cheat_title, sizeof(cheat_title), "Cheats (%d)", cheat_count);
                sections[section_count] = (ap_detail_section){
                    .type = AP_SECTION_DESCRIPTION,
                    .title = cheat_title,
                    .description = cheat_text,
                };
                section_count++;
            }
        }
    } else {
        /* Artwork: show the image */
        artwork_src_path(item->rom_path, item->rom_display,
                         art_path, sizeof(art_path));
        struct stat st;
        if (stat(art_path, &st) == 0) {
            sections[section_count] = (ap_detail_section){
                .type = AP_SECTION_IMAGE,
                .title = NULL,
                .image_path = art_path,
                .image_w = ap_scale(200),
                .image_h = ap_scale(200),
            };
            section_count++;
        }
    }

    ap_footer_item footer[] = {
        {AP_BTN_B, "BACK", false},
    };

    ap_detail_opts opts = {
        .title = item->rom_display,
        .sections = sections,
        .section_count = section_count,
        .footer = footer,
        .footer_count = 1,
    };

    ap_detail_result result;
    ap_detail_screen(&opts, &result);

    free(cheat_text);
}

/* ── Progress screen (ap_queue_viewer) ────────────────────── */

static ap_queue_status map_queue_status(queue_item_status s) {
    switch (s) {
    case QUEUE_IDLE:        return AP_QUEUE_PENDING;
    case QUEUE_SEARCHING:
    case QUEUE_DOWNLOADING:
    case QUEUE_CLONING:
    case QUEUE_MATCHING:    return AP_QUEUE_RUNNING;
    case QUEUE_DONE:        return AP_QUEUE_DONE;
    case QUEUE_NOT_FOUND:
    case QUEUE_ERROR:       return AP_QUEUE_FAILED;
    case QUEUE_SKIPPED:     return AP_QUEUE_SKIPPED;
    default:                return AP_QUEUE_PENDING;
    }
}

static int progress_snapshot(ap_queue_item *buf, int max, void *userdata) {
    (void)userdata;
    queue_item *items = malloc(sizeof(queue_item) * QUEUE_MAX_ITEMS);
    if (!items) return 0;

    int count = queue_snapshot(items, QUEUE_MAX_ITEMS);
    if (count > max) count = max;

    for (int i = 0; i < count; i++) {
        memset(&buf[i], 0, sizeof(buf[i]));
        snprintf(buf[i].title, sizeof(buf[i].title), "%s", items[i].rom_display);

        const char *type_str = items[i].type == QUEUE_TYPE_ARTWORK ? "art" : "cht";
        snprintf(buf[i].subtitle, sizeof(buf[i].subtitle), "%s  [%s]",
                 items[i].system_display, type_str);

        snprintf(buf[i].status_text, sizeof(buf[i].status_text), "%s",
                 queue_status_text(items[i].status));

        buf[i].status   = map_queue_status(items[i].status);
        buf[i].progress = -1.0f; /* no inline progress bar */
        buf[i].userdata = NULL;
    }

    free(items);
    return count;
}

static void progress_on_detail(const ap_queue_item *item, void *userdata) {
    (void)userdata;
    /* Find the matching queue item to pass to show_item_detail */
    queue_item *items = malloc(sizeof(queue_item) * QUEUE_MAX_ITEMS);
    if (!items) return;

    int count = queue_snapshot(items, QUEUE_MAX_ITEMS);
    for (int i = 0; i < count; i++) {
        if (strcmp(items[i].rom_display, item->title) == 0 &&
            is_item_terminal(items[i].status)) {
            show_item_detail(&items[i]);
            break;
        }
    }
    free(items);
}

static void progress_on_cancel(void *userdata) {
    (void)userdata;
    ap_footer_item cfooter[] = {
        {AP_BTN_B, "NO",  false},
        {AP_BTN_A, "YES", true},
    };
    ap_message_opts mopts = {
        .message = "Cancel all downloads?\n\nIn-progress items will be stopped\n"
                   "and pending items will be skipped.",
        .footer = cfooter,
        .footer_count = 2,
    };
    ap_confirm_result cres;
    ap_confirmation(&mopts, &cres);
    if (cres.confirmed)
        queue_cancel_all();
}

static void progress_on_clear(void *userdata) {
    (void)userdata;
    queue_clear_done();
}

static void show_progress_screen(void) {
    ap_queue_opts opts = {
        .title     = "Downloads",
        .snapshot  = progress_snapshot,
        .max_items = QUEUE_MAX_ITEMS,
        .userdata  = NULL,
        .on_detail = progress_on_detail,
        .on_cancel = progress_on_cancel,
        .on_clear  = progress_on_clear,
    };
    ap_queue_viewer(&opts);
}

/* ── Settings screen ──────────────────────────────────────── */

static void edit_username(app_settings *settings) {
    ap_keyboard_result result;
    int ret = ap_keyboard(settings->ss_username, NULL,
                           AP_KB_GENERAL, &result);
    if (ret != AP_OK) return;

    snprintf(settings->ss_username, sizeof(settings->ss_username), "%s", result.text);
    save_settings(settings);
}

static void edit_password(app_settings *settings) {
    ap_keyboard_result result;
    int ret = ap_keyboard(settings->ss_password, NULL,
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
        {AP_BTN_B, "CANCEL", false},
        {AP_BTN_X, "REORDER", false},
        {AP_BTN_START, "SAVE", true},
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
        {AP_BTN_B, "CANCEL", false},
        {AP_BTN_X, "REORDER", false},
        {AP_BTN_START, "SAVE", true},
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
            {AP_BTN_B, "BACK", false},
            {AP_BTN_A, "EDIT", false},
            {AP_BTN_START, "DONE", true},
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

        return;
    }
}

/* ── Clear cheat cache ────────────────────────────────────── */

static int clear_cache_worker(void *userdata) {
    float *progress = (float *)userdata;
    char repo[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));

    DIR *dir = opendir(repo);
    if (!dir) {
        if (progress) *progress = 1.0f;
        return 0;
    }

    /* Count entries first for progress tracking */
    int total = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        total++;
    }
    rewinddir(dir);

    if (total == 0) {
        closedir(dir);
        rmdir(repo);
        if (progress) *progress = 1.0f;
        return 0;
    }

    int done = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", repo, entry->d_name);
        struct stat st;
        if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            char rm_cmd[PATH_MAX + 10];
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", path);
            (void)system(rm_cmd);
        } else {
            unlink(path);
        }
        done++;
        if (progress) *progress = (float)done / (float)total;
    }
    closedir(dir);
    rmdir(repo);
    if (progress) *progress = 1.0f;
    return 0;
}

static void clear_cheat_cache(void) {
    if (queue_is_active()) {
        show_error("Wait for the queue to finish before clearing the cheat cache.");
        return;
    }

    ap_footer_item footer[] = {
        {AP_BTN_B, "CANCEL", false},
        {AP_BTN_A, "CLEAR", true},
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
    ap_process_message(&proc_opts, clear_cache_worker, &progress);
}

/* ── Settings screen ──────────────────────────────────────── */

static void show_settings_screen(void) {
    for (;;) {
        app_settings settings = load_settings();

        char user_display[260];
        if (settings.ss_username[0])
            snprintf(user_display, sizeof(user_display), "%s", settings.ss_username);
        else
            snprintf(user_display, sizeof(user_display), "(not set)");

        const char *pass_display = settings.ss_password[0] ? "(set)" : "(not set)";

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
            {AP_BTN_B, "BACK", false},
            {AP_BTN_A, "EDIT", false},
            {AP_BTN_START, "SAVE", true},
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
        queue_set_settings(&settings);
        free_settings(&settings);
        return;
    }
}

/* ── Main application loop ────────────────────────────────── */

void run_app(void) {
    /* Load initial settings into queue */
    app_settings settings = load_settings();
    queue_set_settings(&settings);

    if (settings.ss_username[0] == '\0') {
        show_warning("No ScreenScraper.fr user credentials set.\n\nScraping will proceed at basic rate\n(~1 req/min, single-threaded).\n\nFor much faster speeds, go to Settings\nand add your username and password.");
    }
    free_settings(&settings);

    for (;;) {
        main_action action = show_main_menu();
        switch (action) {
        case MAIN_SCRAPE_ART:
            if (show_library_screen(LIB_MODE_ART)) show_progress_screen();
            break;
        case MAIN_DOWNLOAD_CHEATS:
            if (show_library_screen(LIB_MODE_CHEAT)) show_progress_screen();
            break;
        case MAIN_PROGRESS:        show_progress_screen(); break;
        case MAIN_API_USAGE:       show_api_usage_screen(); break;
        case MAIN_SETTINGS:        show_settings_screen(); break;
        case MAIN_QUIT: {
            queue_stats stats = queue_get_stats();
            if (stats.pending > 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "%d items still in the download queue.\n\n"
                    "Exiting will cancel all remaining downloads.",
                    stats.pending);
                ap_footer_item footer[] = {
                    {AP_BTN_B, "CANCEL", false},
                    {AP_BTN_A, "EXIT", true},
                };
                ap_message_opts opts = {
                    .message = msg,
                    .footer = footer,
                    .footer_count = 2,
                };
                ap_confirm_result confirm;
                int ret = ap_confirmation(&opts, &confirm);
                if (ret != AP_OK || !confirm.confirmed)
                    continue;
            }
            return;
        }
        }
    }
}

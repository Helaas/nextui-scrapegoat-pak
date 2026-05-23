#include "ui.h"
#include "cheats.h"
#include "daemon.h"
#include "device.h"
#include "i18n/i18n.h"
#include "queue.h"
#include "screenscraper.h"
#include "systems.h"

#include "apostrophe.h"
#include "apostrophe_widgets.h"

#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Suppress GCC warnings about snprintf truncation when combining
   PATH_MAX-sized strings — truncation is safe by design.
   Also suppress missing-field-initializers for ap_footer_item which
   has an optional button_text field we don't use. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

/* ── Forward declarations ─────────────────────────────────── */

typedef enum { LIB_MODE_ART, LIB_MODE_CHEAT, LIB_MODE_MANUAL } library_mode;

static bool show_rom_list_screen(const console_dir *console,
                                  const app_settings *settings,
                                  library_mode mode);

static bool show_rom_detail_screen(const rom_file *rom,
                                    const console_dir *console,
                                    library_mode mode,
                                    const app_settings *settings);

static ap_status_bar_opts g_status_bar = {
    .show_clock = AP_CLOCK_HIDE,
    .show_battery = false,
    .show_wifi = true,
};

static char g_progress_label[64];

static bool is_flip_layout(void) {
    return ap_get_screen_width() <= 640 && ap_get_screen_height() <= 480;
}

static void refresh_progress_label(void) {
    queue_stats s = queue_get_stats();
    if (s.total > 0) {
        int processed = s.done + s.failed;
        if (s.failed > 0)
            snprintf(g_progress_label, sizeof(g_progress_label),
                     T("sg.progress.with_failed_fmt"),
                     processed, s.total, s.failed);
        else
            snprintf(g_progress_label, sizeof(g_progress_label),
                     T("sg.progress.with_total_fmt"),
                     processed, s.total);
    } else {
        snprintf(g_progress_label, sizeof(g_progress_label), "%s", T("sg.progress.track"));
    }
}

static Uint32 progress_label_timer_cb(Uint32 interval, void *param) {
    (void)param;
    refresh_progress_label();
    SDL_Event ev;
    SDL_memset(&ev, 0, sizeof(ev));
    ev.type = SDL_USEREVENT;
    SDL_PushEvent(&ev);
    return interval;
}

/* ── Helpers ──────────────────────────────────────────────── */

static void show_error(const char *message) {
    ap_footer_item footer[] = {{AP_BTN_B, T("sg.btn.back"), false}};
    ap_message_opts opts = {.message = message, .footer = footer, .footer_count = 1};
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
}

static void show_warning(const char *message) {
    ap_footer_item footer[] = {{AP_BTN_A, T("sg.btn.continue"), false}};
    ap_message_opts opts = {.message = message, .footer = footer, .footer_count = 1};
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
}

static void show_brief(const char *message) {
    ap_footer_item footer[] = {{AP_BTN_A, T("sg.btn.ok"), true}};
    ap_message_opts opts = {.message = message, .footer = footer, .footer_count = 1};
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
}

typedef struct {
    char               **lines;
    int                  count;
    char                 title[64];
} cheat_detail_section_data;

static void free_cheat_detail_section_data(cheat_detail_section_data *data) {
    if (!data)
        return;

    if (data->lines) {
        for (int i = 0; i < data->count; i++)
            free(data->lines[i]);
    }
    free(data->lines);
    memset(data, 0, sizeof(*data));
}

/* Render cheats as one section per entry instead of one large wrapped paragraph.
   This keeps long cheat lists responsive in the detail widget. */
static bool load_cheat_detail_section(const char *cht_path,
                                      cheat_detail_section_data *out) {
    cheat_desc_list descs;

    if (!cht_path || !out)
        return false;

    memset(out, 0, sizeof(*out));
    if (parse_cheat_descriptions(cht_path, &descs) != 0 || descs.count <= 0)
        return false;

    out->count = descs.count;
    out->lines = calloc((size_t)out->count, sizeof(*out->lines));

    if (!out->lines) {
        cheat_desc_list_free(&descs);
        memset(out, 0, sizeof(*out));
        return false;
    }

    for (int i = 0; i < out->count; i++) {
        const char *desc = descs.descriptions[i];
        char fallback[32];
        size_t line_len;

        if (!desc || !desc[0]) {
            snprintf(fallback, sizeof(fallback), "Cheat %d", i + 1);
            desc = fallback;
        }

        line_len = strlen(desc) + 16;
        out->lines[i] = malloc(line_len);
        if (!out->lines[i]) {
            cheat_desc_list_free(&descs);
            free_cheat_detail_section_data(out);
            return false;
        }

        snprintf(out->lines[i], line_len, "%d. %s", i + 1, desc);
    }

    cheat_desc_list_free(&descs);
    snprintf(out->title, sizeof(out->title), "Cheats (%d)", out->count);
    return true;
}

/* Returns true if user chose "Track Progress". */
static bool show_track_progress_prompt(const char *message) {
    ap_footer_item footer[] = {
        {AP_BTN_B, T("sg.btn.back"),           false},
        {AP_BTN_A, T("sg.btn.track_progress"), true},
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
    int manual_count;
    bool has_ss;      /* has ScreenScraper mapping */
    bool has_libretro; /* has libretro cheat directory */
} system_stats;

static system_stats compute_system_stats(const console_dir *console, bool show_hidden,
                                          const char *manual_download_dir) {
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
        if (stats.has_ss && manual_download_dir && manual_download_dir[0] &&
            manual_exists(manual_download_dir, console->tag, roms[i].display))
            stats.manual_count++;
    }

    free(roms);
    return stats;
}

/* ── Main menu ────────────────────────────────────────────── */

typedef enum {
    MAIN_QUIT = 0,
    MAIN_SCRAPE_ART,
    MAIN_DOWNLOAD_CHEATS,
    MAIN_DOWNLOAD_MANUALS,
    MAIN_PROGRESS,
    MAIN_SETTINGS,
    MAIN_API_USAGE,
} main_action;

static main_action show_main_menu(void) {
    refresh_progress_label();

    ap_list_item items[] = {
        {.label = T("sg.menu.artwork")},
        {.label = T("sg.menu.cheats")},
        {.label = T("sg.menu.manuals")},
        {.label = g_progress_label},
        {.label = T("sg.menu.settings")},
        {.label = T("sg.menu.api_usage")},
    };
    ap_footer_item footer[] = {
        {AP_BTN_B, T("sg.btn.quit"), false},
        {AP_BTN_A, T("sg.btn.select"), true},
    };

    ap_list_opts opts = ap_list_default_opts("ScrapeGoat", items, 6);
    opts.footer = footer;
    opts.footer_count = 2;
    opts.status_bar = &g_status_bar;

    SDL_TimerID timer = SDL_AddTimer(500, progress_label_timer_cb, NULL);

    ap_list_result result;
    int ret = ap_list(&opts, &result);

    SDL_RemoveTimer(timer);

    if (ret == AP_CANCELLED || result.selected_index < 0)
        return MAIN_QUIT;

    switch (result.selected_index) {
    case 0: return MAIN_SCRAPE_ART;
    case 1: return MAIN_DOWNLOAD_CHEATS;
    case 2: return MAIN_DOWNLOAD_MANUALS;
    case 3: return MAIN_PROGRESS;
    case 4: return MAIN_SETTINGS;
    case 5: return MAIN_API_USAGE;
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
    case ROM_FILTER_MISSING:   return T("sg.filter.missing");
    case ROM_FILTER_INSTALLED: return T("sg.filter.installed");
    default:                   return T("sg.filter.all");
    }
}

static const char *rom_status_label(const rom_file *rom,
                                     const console_dir *console,
                                     library_mode mode,
                                     const app_settings *settings) {
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
    } else if (mode == LIB_MODE_CHEAT) {
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
    } else {
        queue_item_status qs = queue_get_rom_status(rom->path, QUEUE_TYPE_MANUAL);
        if (qs >= QUEUE_IDLE && qs <= QUEUE_DOWNLOADING) {
            switch (qs) {
            case QUEUE_IDLE:        return "queued";
            case QUEUE_SEARCHING:   return "searching";
            case QUEUE_DOWNLOADING: return "downloading";
            default:                return "queued";
            }
        }
        return manual_exists(settings->manual_download_dir, console->tag, rom->display) ? "pdf" : NULL;
    }
}

static bool show_rom_list_screen(const console_dir *console,
                                  const app_settings *settings,
                                  library_mode mode) {
    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, settings->show_hidden, &roms);
    if (rom_count <= 0) {
        show_error(T("sg.error.no_roms"));
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
                : (mode == LIB_MODE_CHEAT)
                    ? cheat_exists(console->tag, roms[i].display)
                    : manual_exists(settings->manual_download_dir, console->tag, roms[i].display);
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
                : (mode == LIB_MODE_CHEAT)
                    ? cheat_exists(console->tag, roms[i].display)
                    : manual_exists(settings->manual_download_dir, console->tag, roms[i].display);
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
            const char *status = rom_status_label(&roms[i], console, mode, settings);
            bool is_inst = installed[i];
            snprintf(labels[vi], 512, "%s", roms[i].label[0] ? roms[i].label : roms[i].display);
            items[vi].label = labels[vi];
            if (is_inst)
                items[vi].trailing_text = "\u2713";
            else if (status)
                items[vi].trailing_text = status;
        }

        free(installed);

        /* Title always shows filter tag */
        char title[256];
        snprintf(title, sizeof(title), "%s  [%s]",
                 console->display, rom_filter_name(filter));

        /* Footer: B position depends on screen width */
        ap_footer_item footer[4];
        if (ap_get_screen_width() >= 1024) {
            footer[0] = (ap_footer_item){AP_BTN_B, T("sg.btn.back"),      false};
            footer[1] = (ap_footer_item){AP_BTN_Y, T("sg.btn.filter"),    false};
            footer[2] = (ap_footer_item){AP_BTN_X, T("sg.btn.queue_all"), false};
        } else {
            footer[0] = (ap_footer_item){AP_BTN_Y, T("sg.btn.filter"),    false};
            footer[1] = (ap_footer_item){AP_BTN_X, T("sg.btn.queue_all"), false};
            footer[2] = (ap_footer_item){AP_BTN_B, T("sg.btn.back"),      false};
        }
        footer[3] = (ap_footer_item){AP_BTN_A, T("sg.btn.open"), true};

        ap_list_opts opts = ap_list_default_opts(title, items,
                                                  visible_count > 0 ? visible_count : 0);
        opts.footer                  = footer;
        opts.footer_count            = 4;
        opts.status_bar              = &g_status_bar;
        opts.secondary_action_button = AP_BTN_X;
        opts.tertiary_action_button  = AP_BTN_Y;
        opts.initial_index           = initial_idx;
        opts.visible_start_index     = visible_start;

        ap_list_result result;
        int ret = ap_list(&opts, &result);

        initial_idx  = result.selected_index;
        visible_start = result.visible_start_index;

        free(labels);
        free(items);

        if (ret == AP_CANCELLED) break;

        /* Y: cycle filter */
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
            /* X: Queue all visible (filtered) ROMs */
            queue_set_settings(settings);

            /* Count how many visible ROMs are already installed */
            int installed_count = 0;
            for (int vi = 0; vi < visible_count; vi++) {
                int ri = filter_map[vi];
                bool is_inst = (mode == LIB_MODE_ART)
                    ? artwork_exists(roms[ri].path, roms[ri].display)
                    : (mode == LIB_MODE_CHEAT)
                        ? cheat_exists(console->tag, roms[ri].display)
                        : manual_exists(settings->manual_download_dir, console->tag, roms[ri].display);
                if (is_inst) installed_count++;
            }

            bool force = false;
            if (installed_count > 0) {
                ap_list_item choices[] = {
                    {.label = T("sg.queue_all.missing_only")},
                    {.label = T("sg.queue_all.redownload_all")},
                };
                ap_footer_item cf[] = {
                    {AP_BTN_B, T("sg.btn.cancel"), false},
                    {AP_BTN_A, T("sg.btn.select"), true},
                };
                char choice_title[64];
                snprintf(choice_title, sizeof(choice_title),
                         T("sg.queue_all.already_installed_fmt"), installed_count);
                ap_list_opts copts = ap_list_default_opts(choice_title, choices, 2);
                copts.footer       = cf;
                copts.footer_count = 2;
                copts.status_bar   = &g_status_bar;
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
                        : (mode == LIB_MODE_CHEAT)
                            ? queue_add_cheat_forced(&roms[ri], console)
                            : queue_add_manual_forced(&roms[ri], console);
                    if (ok) added++;
                } else {
                    bool ok = (mode == LIB_MODE_ART)
                        ? queue_add_artwork(&roms[ri], console)
                        : (mode == LIB_MODE_CHEAT)
                            ? queue_add_cheat(&roms[ri], console)
                            : queue_add_manual(&roms[ri], console);
                    if (ok) added++;
                }
            }

            char msg[128];
            const char *mode_name = (mode == LIB_MODE_ART) ? T("sg.mode.artwork")
                                   : (mode == LIB_MODE_CHEAT) ? T("sg.mode.cheats")
                                   : T("sg.mode.manuals");
            snprintf(msg, sizeof(msg), T("sg.queue.added_fmt"), added, mode_name);
            if (show_track_progress_prompt(msg)) {
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
    bool is_cheat = (mode == LIB_MODE_CHEAT);
    bool is_installed = is_art
        ? artwork_exists(rom->path, rom->display)
        : is_cheat
            ? cheat_exists(console->tag, rom->display)
            : manual_exists(settings->manual_download_dir, console->tag, rom->display);

    queue_item_type qtype = is_art ? QUEUE_TYPE_ARTWORK
                          : is_cheat ? QUEUE_TYPE_CHEAT
                          : QUEUE_TYPE_MANUAL;
    queue_item_status qs = queue_get_rom_status(rom->path, qtype);
    bool is_queued = (qs >= QUEUE_IDLE && qs <= QUEUE_MATCHING);

    const char *status_str;
    if (is_queued)
        status_str = rom_status_label(rom, console, mode, settings);
    else if (is_installed)
        status_str = T("sg.filter.installed");
    else
        status_str = T("sg.filter.missing");
    if (!status_str) status_str = T("sg.filter.missing");

    /* Image section — show artwork if present */
    char art_path[PATH_MAX] = {0};
    bool has_art_image = false;
    if (is_art) {
        artwork_src_path(rom->path, rom->display, art_path, sizeof(art_path));
        struct stat st;
        has_art_image = (stat(art_path, &st) == 0);
    }

    /* Cheat list section — if cheat mode and installed */
    cheat_detail_section_data cheat_detail = {0};
    if (is_cheat && is_installed) {
        char cheats_base[PATH_MAX];
        char cht_path[PATH_MAX];
        get_cheats_path(cheats_base, sizeof(cheats_base));
        snprintf(cht_path, sizeof(cht_path), "%s/%s/%s.cht",
                 cheats_base, console->tag, rom->display);

        load_cheat_detail_section(cht_path, &cheat_detail);
    }

    int max_sections = 1 + (has_art_image ? 1 : 0) + cheat_detail.count;
    ap_detail_section *sections =
        calloc((size_t)(max_sections > 0 ? max_sections : 1), sizeof(*sections));
    if (!sections) {
        free_cheat_detail_section_data(&cheat_detail);
        show_error(T("sg.error.oom"));
        return false;
    }

    int section_count = 0;
    if (has_art_image) {
        sections[section_count] = (ap_detail_section){
            .type = AP_SECTION_IMAGE,
            .title = NULL,
            .image_path = art_path,
            .image_w = ap_scale(320),
            .image_h = ap_scale(320),
        };
        section_count++;
    }

    const char *type_str = is_art ? T("sg.type.artwork") : is_cheat ? T("sg.type.cheat") : T("sg.type.manual");
    ap_detail_info_pair info_pairs[] = {
        {T("sg.info.status"), status_str},
        {T("sg.info.system"), console->display},
        {T("sg.info.type"),   type_str},
    };
    sections[section_count] = (ap_detail_section){
        .type = AP_SECTION_INFO,
        .title = NULL,
        .info_pairs = info_pairs,
        .info_count = 3,
    };
    section_count++;

    for (int i = 0; i < cheat_detail.count; i++) {
        sections[section_count] = (ap_detail_section){
            .type = AP_SECTION_DESCRIPTION,
            .title = (i == 0) ? cheat_detail.title : NULL,
            .description = cheat_detail.lines[i],
        };
        section_count++;
    }

    /* Footer: B=Back, A=Queue (or Re-download) */
    const char *action_label = is_queued ? T("sg.btn.queued") :
                               is_installed ? T("sg.btn.redownload") : T("sg.btn.queue");
    ap_footer_item footer[] = {
        {AP_BTN_B, T("sg.btn.back"),       false},
        {AP_BTN_A, action_label, true},
    };
    int footer_count = is_queued ? 1 : 2; /* hide A if already queued */

    ap_detail_opts opts = {
        .title         = rom->label[0] ? rom->label : rom->display,
        .sections      = sections,
        .section_count = section_count,
        .footer        = footer,
        .footer_count  = footer_count,
        .status_bar    = &g_status_bar,
    };

    ap_detail_result result;
    ap_detail_screen(&opts, &result);

    const char *mode_name = is_art ? T("sg.mode.artwork") : is_cheat ? T("sg.mode.cheats") : T("sg.mode.manuals");
    if (result.action == AP_DETAIL_ACTION && !is_queued) {
        if (is_installed) {
            queue_set_settings(settings);
            if (is_art)
                queue_add_artwork_forced(rom, console);
            else if (is_cheat)
                queue_add_cheat_forced(rom, console);
            else
                queue_add_manual_forced(rom, console);
            char msg[256];
            snprintf(msg, sizeof(msg), T("sg.queue.requeued_fmt"),
                     rom->label[0] ? rom->label : rom->display, mode_name);
            if (show_track_progress_prompt(msg)) {
                free(sections);
                free_cheat_detail_section_data(&cheat_detail);
                return true;
            }
        } else {
            queue_set_settings(settings);
            bool added;
            if (is_art)
                added = queue_add_artwork(rom, console);
            else if (is_cheat)
                added = queue_add_cheat(rom, console);
            else
                added = queue_add_manual(rom, console);
            if (added) {
                char msg[256];
                snprintf(msg, sizeof(msg), T("sg.queue.queued_fmt"),
                         rom->label[0] ? rom->label : rom->display, mode_name);
                if (show_track_progress_prompt(msg)) {
                    free(sections);
                    free_cheat_detail_section_data(&cheat_detail);
                    return true;
                }
            } else {
                show_brief(T("sg.queue.already_queued"));
            }
        }
    }

    free(sections);
    free_cheat_detail_section_data(&cheat_detail);
    return false;
}

/* ── Library: System list ─────────────────────────────────── */

static bool show_library_screen(library_mode mode) {
    app_settings settings = load_settings();

    console_dir *consoles = NULL;
    int console_count = scan_console_dirs(settings.show_hidden, &consoles);
    if (console_count <= 0) {
        show_error(T("sg.error.no_rom_folders"));
        free(consoles);
        free_settings(&settings);
        return false;
    }

    /* Build menu names and compute stats, filtering by mode */
    char (*names)[512] = malloc(sizeof(char[512]) * (size_t)console_count);
    system_stats *stats = malloc(sizeof(system_stats) * (size_t)console_count);
    build_console_menu_names(consoles, console_count, names);

    /* Only compute manual counts in manual mode to avoid extra stat() calls
       on large libraries during artwork/cheat browsing. */
    const char *manual_dir_for_stats = (mode == LIB_MODE_MANUAL)
        ? settings.manual_download_dir : NULL;
    for (int i = 0; i < console_count; i++)
        stats[i] = compute_system_stats(&consoles[i], settings.show_hidden,
                                        manual_dir_for_stats);

    /* Filter consoles by mode and build visible list */
    int *visible_map = malloc(sizeof(int) * (size_t)console_count);
    int visible_count = 0;
    for (int i = 0; i < console_count; i++) {
        if (mode == LIB_MODE_ART && !stats[i].has_ss) continue;
        if (mode == LIB_MODE_CHEAT && !stats[i].has_libretro) continue;
        if (mode == LIB_MODE_MANUAL && !stats[i].has_ss) continue;
        visible_map[visible_count++] = i;
    }

    if (visible_count <= 0) {
        const char *err_msg = (mode == LIB_MODE_ART)
            ? T("sg.error.no_systems_artwork")
            : (mode == LIB_MODE_CHEAT)
                ? T("sg.error.no_systems_cheat")
                : T("sg.error.no_systems_manual");
        show_error(err_msg);
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
        int count = (mode == LIB_MODE_ART) ? stats[i].art_count
                  : (mode == LIB_MODE_CHEAT) ? stats[i].cheat_count
                  : stats[i].manual_count;
        snprintf(meta[vi], 32, "%d / %d", count, stats[i].rom_count);
    }

    const char *title = (mode == LIB_MODE_ART) ? T("sg.menu.artwork")
                      : (mode == LIB_MODE_CHEAT) ? T("sg.menu.cheats")
                      : T("sg.menu.manuals");

    int initial_idx = 0;
    int visible_start = 0;

    for (;;) {
        ap_list_item *items = calloc((size_t)visible_count, sizeof(ap_list_item));
        for (int vi = 0; vi < visible_count; vi++) {
            items[vi].label    = labels[vi];
            items[vi].trailing_text = meta[vi];
        }

        ap_footer_item footer[] = {
            {AP_BTN_A, T("sg.btn.open"), true},
            {AP_BTN_B, T("sg.btn.back"), false},
        };

        ap_list_opts opts = ap_list_default_opts(title, items, visible_count);
        opts.footer = footer;
        opts.footer_count = 2;
        opts.status_bar = &g_status_bar;
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
        show_brief(T("sg.brief.no_api_data"));
        return;
    }

    char req_today[32], daily_limit[32], remaining[32], threads[32];
    snprintf(req_today,   sizeof(req_today),   "%d", api.requests_today);
    snprintf(daily_limit, sizeof(daily_limit), "%d", api.max_requests);
    snprintf(remaining,   sizeof(remaining),   "%d", api.max_requests - api.requests_today);
    snprintf(threads,     sizeof(threads),     "%d", api.max_threads);

    ap_detail_info_pair info_pairs[] = {
        {T("sg.api.requests_today"), req_today},
        {T("sg.api.daily_limit"),    daily_limit},
        {T("sg.api.remaining"),      remaining},
        {T("sg.api.threads"),        threads},
    };
    ap_detail_section sections[] = {{
        .type       = AP_SECTION_INFO,
        .title      = NULL,
        .info_pairs = info_pairs,
        .info_count = 4,
    }};
    ap_footer_item footer[] = {
        {AP_BTN_B, T("sg.btn.back"), false},
    };
    ap_detail_opts opts = {
        .title         = T("sg.title.api_usage"),
        .sections      = sections,
        .section_count = 1,
        .footer        = footer,
        .footer_count  = 1,
        .status_bar    = &g_status_bar,
    };
    ap_detail_result result;
    ap_detail_screen(&opts, &result);
}

/* ── Progress screen (custom render loop) ─────────────────── */

static const char *queue_status_text(queue_item_status status) {
    switch (status) {
    case QUEUE_NONE:        return "-";
    case QUEUE_IDLE:        return T("sg.status.queued");
    case QUEUE_SEARCHING:   return T("sg.status.searching");
    case QUEUE_DOWNLOADING: return T("sg.status.downloading");
    case QUEUE_CLONING:     return T("sg.status.cloning");
    case QUEUE_MATCHING:    return T("sg.status.matching");
    case QUEUE_DONE:        return T("sg.status.done");
    case QUEUE_NOT_FOUND:   return T("sg.status.not_found");
    case QUEUE_ERROR:       return T("sg.status.error");
    case QUEUE_SKIPPED:     return T("sg.status.skipped");
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
    bool is_art = (item->type == QUEUE_TYPE_ARTWORK);

    /* Error detail */
    cheat_detail_section_data cheat_detail = {0};
    char art_path[PATH_MAX] = {0};
    bool has_art_image = false;
    bool has_error = false;

    if (is_error) {
        has_error = true;
    } else if (is_cheat) {
        /* Parse and display cheat descriptions */
        char cheats_base[PATH_MAX];
        char cht_path[PATH_MAX];
        get_cheats_path(cheats_base, sizeof(cheats_base));
        snprintf(cht_path, sizeof(cht_path), "%s/%s/%s.cht",
                 cheats_base, item->system_tag, item->rom_display);

        load_cheat_detail_section(cht_path, &cheat_detail);
    } else if (is_art) {
        /* Artwork: show the image */
        artwork_src_path(item->rom_path, item->rom_display,
                         art_path, sizeof(art_path));
        struct stat st;
        has_art_image = (stat(art_path, &st) == 0);
    }

    int max_sections = 1 + (has_error ? 1 : 0) + (has_art_image ? 1 : 0) + cheat_detail.count;
    ap_detail_section *sections =
        calloc((size_t)(max_sections > 0 ? max_sections : 1), sizeof(*sections));
    if (!sections) {
        free_cheat_detail_section_data(&cheat_detail);
        show_error(T("sg.error.oom"));
        return;
    }

    int section_count = 0;
    const char *status_str = queue_status_text(item->status);
    const char *type_str = is_art ? T("sg.type.artwork") : is_cheat ? T("sg.type.cheat") : T("sg.type.manual");
    ap_detail_info_pair info_pairs[] = {
        {T("sg.info.status"), status_str},
        {T("sg.info.system"), item->system_display},
        {T("sg.info.type"), type_str},
    };
    sections[section_count] = (ap_detail_section){
        .type = AP_SECTION_INFO,
        .title = NULL,
        .info_pairs = info_pairs,
        .info_count = 3,
    };
    section_count++;

    if (has_error) {
        const char *not_found_msg = is_cheat
            ? T("sg.error.not_found_libretro")
            : T("sg.error.not_found_screenscraper");
        const char *msg = item->error_msg[0] ? item->error_msg :
                          (item->status == QUEUE_NOT_FOUND
                              ? not_found_msg
                              : T("sg.error.unknown"));
        sections[section_count] = (ap_detail_section){
            .type = AP_SECTION_DESCRIPTION,
            .title = T("sg.title.error"),
            .description = msg,
        };
        section_count++;
    } else if (is_cheat) {
        for (int i = 0; i < cheat_detail.count; i++) {
            sections[section_count] = (ap_detail_section){
                .type = AP_SECTION_DESCRIPTION,
                .title = (i == 0) ? cheat_detail.title : NULL,
                .description = cheat_detail.lines[i],
            };
            section_count++;
        }
    } else if (has_art_image) {
        sections[section_count] = (ap_detail_section){
            .type = AP_SECTION_IMAGE,
            .title = NULL,
            .image_path = art_path,
            .image_w = ap_scale(320),
            .image_h = ap_scale(320),
        };
        section_count++;
    }

    ap_footer_item footer[] = {
        {AP_BTN_B, T("sg.btn.back"), false},
    };

    ap_detail_opts opts = {
        .title = item->rom_display,
        .sections = sections,
        .section_count = section_count,
        .footer = footer,
        .footer_count = 1,
        .status_bar = &g_status_bar,
    };

    ap_detail_result result;
    ap_detail_screen(&opts, &result);

    free(sections);
    free_cheat_detail_section_data(&cheat_detail);
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

        const char *type_str = items[i].type == QUEUE_TYPE_ARTWORK ? "art"
                             : items[i].type == QUEUE_TYPE_CHEAT ? "cht"
                             : "pdf";
        snprintf(buf[i].subtitle, sizeof(buf[i].subtitle), "%s  [%s]",
                 items[i].system_display, type_str);

        snprintf(buf[i].status_text, sizeof(buf[i].status_text), "%s",
                 queue_status_text(items[i].status));

        buf[i].status   = map_queue_status(items[i].status);
        buf[i].progress = -1.0f; /* no inline progress bar */
        buf[i].userdata = (void *)(uintptr_t)items[i].id;
    }

    free(items);
    return count;
}

static void progress_on_detail(const ap_queue_item *item, void *userdata) {
    (void)userdata;
    uint32_t item_id = (uint32_t)(uintptr_t)item->userdata;
    if (item_id == 0)
        return;

    /* Find the matching queue item to pass to show_item_detail */
    queue_item *items = malloc(sizeof(queue_item) * QUEUE_MAX_ITEMS);
    if (!items) return;

    int count = queue_snapshot(items, QUEUE_MAX_ITEMS);
    for (int i = 0; i < count; i++) {
        if (items[i].id == item_id &&
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
        {AP_BTN_B, T("sg.btn.no"),  false},
        {AP_BTN_A, T("sg.btn.yes"), true},
    };
    ap_message_opts mopts = {
        .message = T("sg.dialog.cancel_all"),
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
        .title         = T("sg.title.progress"),
        .snapshot      = progress_snapshot,
        .max_items     = QUEUE_MAX_ITEMS,
        .status_bar    = &g_status_bar,
        .userdata      = NULL,
        .on_detail     = progress_on_detail,
        .on_cancel     = progress_on_cancel,
        .on_clear      = progress_on_clear,
        .filter_labels = { T("sg.queue.filter.all"), T("sg.queue.filter.busy"),
                           T("sg.queue.filter.done"), T("sg.queue.filter.fail") },
        .empty_message        = T("sg.queue.empty"),
        .empty_filter_message = T("sg.queue.empty_filter"),
    };
    ap_queue_viewer(&opts);
}

typedef enum {
    QUIT_QUEUE_KEEP_OPEN = 0,
    QUIT_QUEUE_EXIT_AND_CANCEL,
    QUIT_QUEUE_BACKGROUND,
} quit_queue_action;

static quit_queue_action show_quit_queue_dialog(int pending_count) {
    ap_list_item items[] = {
        { .label = T("sg.quit.keep_open") },
        { .label = T("sg.quit.exit_cancel") },
        { .label = T("sg.quit.exit_bg") },
    };

    char title[64];
    snprintf(title, sizeof(title),
             pending_count == 1 ? T("sg.quit.title_one") : T("sg.quit.title_many_fmt"),
             pending_count);

    ap_footer_item footer[] = {
        {AP_BTN_B, T("sg.btn.keep_open"), false},
        {AP_BTN_A, T("sg.btn.select"), true},
    };

    ap_list_opts opts = ap_list_default_opts(title, items, 3);
    opts.footer = footer;
    opts.footer_count = 2;
    opts.status_bar = &g_status_bar;

    ap_list_result result;
    int ret = ap_list(&opts, &result);
    if (ret == AP_CANCELLED || result.selected_index < 0)
        return QUIT_QUEUE_KEEP_OPEN;

    switch (result.selected_index) {
    case 1: return QUIT_QUEUE_EXIT_AND_CANCEL;
    case 2: return QUIT_QUEUE_BACKGROUND;
    default: return QUIT_QUEUE_KEEP_OPEN;
    }
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
        {AP_BTN_B, T("sg.btn.cancel"), false},
        {AP_BTN_X, T("sg.btn.reorder"), false},
        {AP_BTN_START, T("sg.btn.save"), true},
    };

    ap_list_opts opts = ap_list_default_opts(T("sg.title.artwork_priority"), items, count);
    opts.reorder_button = AP_BTN_X;
    opts.action_button = AP_BTN_START;
    opts.footer = footer;
    opts.footer_count = 3;
    opts.status_bar = &g_status_bar;

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
        {AP_BTN_B, T("sg.btn.cancel"), false},
        {AP_BTN_X, T("sg.btn.reorder"), false},
        {AP_BTN_START, T("sg.btn.save"), true},
    };

    ap_list_opts opts = ap_list_default_opts(T("sg.title.region_priority"), items, list_count);
    opts.reorder_button = AP_BTN_X;
    opts.action_button = AP_BTN_START;
    opts.footer = footer;
    opts.footer_count = 3;
    opts.status_bar = &g_status_bar;

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

/* ── Manual download directory editor ─────────────────────── */

static void edit_manual_download_dir(app_settings *settings) {
    ap_file_picker_opts fp = ap_file_picker_default_opts(NULL);
    fp.mode = AP_FILE_PICKER_DIRS;
    fp.allow_create = true;
    fp.status_bar = &g_status_bar;
    if (settings->manual_download_dir[0])
        fp.initial_path = settings->manual_download_dir;

    ap_file_picker_result result;
    int ret = ap_file_picker(&fp, &result);
    if (ret != AP_OK)
        return;

    snprintf(settings->manual_download_dir, sizeof(settings->manual_download_dir),
             "%s", result.path);
    save_settings(settings);
}

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
            {.label = T("sg.settings.artwork_priority"), .type = AP_OPT_CLICKABLE,
             .options = &art_opt, .option_count = 1, .selected_option = 0},
            {.label = T("sg.settings.region_priority"), .type = AP_OPT_CLICKABLE,
             .options = &reg_opt, .option_count = 1, .selected_option = 0},
        };

        ap_footer_item footer[] = {
            {AP_BTN_B, T("sg.btn.back"), false},
            {AP_BTN_A, T("sg.btn.edit"), false},
            {AP_BTN_START, T("sg.btn.done"), true},
        };

        ap_options_list_opts opts = {
            .title = T("sg.title.artwork_options"),
            .items = items,
            .item_count = 2,
            .footer = footer,
            .footer_count = 3,
            .confirm_button = AP_BTN_START,
            .status_bar = &g_status_bar,
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

typedef struct {
    float *progress;
    char   error[256];
} clear_cache_ctx;

static int remove_path_recursive(const char *path, char *error, size_t error_len) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "Failed to inspect cache entry:\n%s",
                     strerror(errno));
        }
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            if (error && error_len > 0) {
                snprintf(error, error_len, "Failed to open cache directory:\n%s",
                         strerror(errno));
            }
            return -1;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char child[PATH_MAX];
            snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
            if (remove_path_recursive(child, error, error_len) != 0) {
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);

        if (rmdir(path) != 0) {
            if (error && error_len > 0) {
                snprintf(error, error_len, "Failed to remove cache directory:\n%s",
                         strerror(errno));
            }
            return -1;
        }
        return 0;
    }

    if (unlink(path) != 0) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "Failed to remove cache file:\n%s",
                     strerror(errno));
        }
        return -1;
    }
    return 0;
}

static int clear_cache_worker(void *userdata) {
    clear_cache_ctx *ctx = (clear_cache_ctx *)userdata;
    float *progress = ctx ? ctx->progress : NULL;
    char repo[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));

    if (ctx)
        ctx->error[0] = '\0';

    DIR *dir = opendir(repo);
    if (!dir) {
        if (errno != ENOENT) {
            if (ctx) {
                snprintf(ctx->error, sizeof(ctx->error),
                         "Failed to open cheat cache directory:\n%s",
                         strerror(errno));
            }
            return -1;
        }
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
        if (rmdir(repo) != 0 && errno != ENOENT) {
            if (ctx) {
                snprintf(ctx->error, sizeof(ctx->error),
                         "Failed to remove cheat cache directory:\n%s",
                         strerror(errno));
            }
            return -1;
        }
        if (progress) *progress = 1.0f;
        return 0;
    }

    int done = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", repo, entry->d_name);
        if (remove_path_recursive(path,
                                  ctx ? ctx->error : NULL,
                                  ctx ? sizeof(ctx->error) : 0) != 0) {
            closedir(dir);
            return -1;
        }
        done++;
        if (progress) *progress = (float)done / (float)total;
    }
    closedir(dir);
    if (rmdir(repo) != 0 && errno != ENOENT) {
        if (ctx) {
            snprintf(ctx->error, sizeof(ctx->error),
                     "Failed to remove cheat cache directory:\n%s",
                     strerror(errno));
        }
        return -1;
    }
    if (progress) *progress = 1.0f;
    return 0;
}

static void clear_cheat_cache(void) {
    if (queue_is_active()) {
        show_error(T("sg.error.queue_active_clear"));
        return;
    }

    ap_footer_item footer[] = {
        {AP_BTN_B, T("sg.btn.cancel"), false},
        {AP_BTN_A, T("sg.btn.clear"), true},
    };
    ap_message_opts msg_opts = {
        .message = T("sg.dialog.clear_cheat_cache"),
        .footer = footer,
        .footer_count = 2,
    };
    ap_confirm_result confirm;
    int ret = ap_confirmation(&msg_opts, &confirm);
    if (ret != AP_OK || !confirm.confirmed)
        return;

    float progress = 0.0f;
    clear_cache_ctx ctx = {.progress = &progress};
    ap_process_opts proc_opts = {
        .message = T("sg.progress.clearing_cache"),
        .show_progress = true,
        .progress = &progress,
    };
    if (ap_process_message(&proc_opts, clear_cache_worker, &ctx) != 0) {
        show_error(ctx.error[0]
            ? ctx.error
            : T("sg.error.clear_failed"));
        return;
    }

    if (!queue_invalidate_cheat_repo_state()) {
        show_error(T("sg.error.cache_cleared_stale"));
    }
}

/* ── Settings screen ──────────────────────────────────────── */

static void show_settings_screen(void) {
    for (;;) {
        app_settings settings = load_settings();

        char user_display[260];
        if (settings.ss_username[0])
            snprintf(user_display, sizeof(user_display), "%s", settings.ss_username);
        else
            snprintf(user_display, sizeof(user_display), "%s", T("sg.value.not_set"));

        const char *pass_display = settings.ss_password[0] ? T("sg.value.set") : T("sg.value.not_set");

        char manual_dir_display[260];
        if (settings.manual_download_dir[0])
            snprintf(manual_dir_display, sizeof(manual_dir_display),
                     "%s", settings.manual_download_dir);
        else
            snprintf(manual_dir_display, sizeof(manual_dir_display), "%s", T("sg.value.not_set"));

        ap_option user_opt = {.label = user_display, .value = "edit"};
        ap_option pass_opt = {.label = pass_display, .value = "edit"};
        ap_option art_opt = {.label = "...", .value = "edit"};
        ap_option manual_dir_opt = {.label = manual_dir_display, .value = "edit"};
        ap_option clear_opt = {.label = "...", .value = "clear"};
        ap_option hidden_opts[2] = {
            {.label = T("sg.toggle.off"), .value = "0"},
            {.label = T("sg.toggle.on"), .value = "1"},
        };

        ap_options_item items[6] = {
            {.label = T("sg.settings.username"), .type = AP_OPT_CLICKABLE,
             .options = &user_opt, .option_count = 1, .selected_option = 0},
            {.label = T("sg.settings.password"), .type = AP_OPT_CLICKABLE,
             .options = &pass_opt, .option_count = 1, .selected_option = 0},
            {.label = T("sg.settings.artwork_options"), .type = AP_OPT_CLICKABLE,
             .options = &art_opt, .option_count = 1, .selected_option = 0},
            {.label = T("sg.settings.manual_dir"), .type = AP_OPT_CLICKABLE,
             .options = &manual_dir_opt, .option_count = 1, .selected_option = 0},
            {.label = T("sg.settings.clear_cheat_cache"), .type = AP_OPT_CLICKABLE,
             .options = &clear_opt, .option_count = 1, .selected_option = 0},
            {.label = T("sg.settings.include_hidden"), .type = AP_OPT_STANDARD,
             .options = hidden_opts, .option_count = 2,
             .selected_option = settings.show_hidden ? 1 : 0},
        };

        ap_footer_item footer[] = {
            {AP_BTN_B, T("sg.btn.back"), false},
            {AP_BTN_A, T("sg.btn.edit"), false},
            {AP_BTN_START, T("sg.btn.save"), true},
        };

        ap_options_list_opts opts = {
            .title = T("sg.title.settings"),
            .items = items,
            .item_count = 6,
            .footer = footer,
            .footer_count = 3,
            .confirm_button = AP_BTN_START,
            .status_bar = &g_status_bar,
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
            case 3: edit_manual_download_dir(&settings); break;
            case 4: clear_cheat_cache(); break;
            }
            free_settings(&settings);
            continue;
        }

        /* START pressed: save show_hidden and exit */
        settings.show_hidden = (result.items[5].selected_option == 1);
        save_settings(&settings);
        queue_set_settings(&settings);
        free_settings(&settings);
        return;
    }
}

/* ── Main application loop ────────────────────────────────── */

#ifndef PLATFORM_MAC
/* Returns true if the device has a default gateway (i.e. is likely online).
 * Reads /proc/net/route and looks for a route with destination 0.0.0.0
 * and a non-zero gateway. No network traffic is generated. */
static bool has_default_route(void) {
    FILE *f = fopen("/proc/net/route", "r");
    if (!f)
        return false;
    char line[256];
    fgets(line, sizeof(line), f); /* skip header */
    while (fgets(line, sizeof(line), f)) {
        char iface[32];
        unsigned long dest, gw;
        if (sscanf(line, "%31s %lx %lx", iface, &dest, &gw) == 3 &&
                dest == 0 && gw != 0) {
            fclose(f);
            return true;
        }
    }
    fclose(f);
    return false;
}
#endif

/* ── Daemon status screen ────────────────────────────────── */

static int daemon_snapshot(ap_queue_item *buf, int max, void *userdata) {
    (void)userdata;
    int limit = max < QUEUE_MAX_ITEMS ? max : QUEUE_MAX_ITEMS;
    queue_item *items = malloc(sizeof(queue_item) * (size_t)limit);
    if (!items) return 0;

    queue_stats stats;
    int count = daemon_read_queue(items, limit, &stats);
    if (count > limit) count = limit;

    for (int i = 0; i < count; i++) {
        memset(&buf[i], 0, sizeof(buf[i]));
        snprintf(buf[i].title, sizeof(buf[i].title), "%s", items[i].rom_display);

        const char *type_str = items[i].type == QUEUE_TYPE_ARTWORK ? "art"
                             : items[i].type == QUEUE_TYPE_CHEAT ? "cht"
                             : "pdf";
        snprintf(buf[i].subtitle, sizeof(buf[i].subtitle), "%s  [%s]",
                 items[i].system_display, type_str);

        snprintf(buf[i].status_text, sizeof(buf[i].status_text), "%s",
                 queue_status_text(items[i].status));

        buf[i].status   = map_queue_status(items[i].status);
        buf[i].progress = -1.0f;
        buf[i].userdata = (void *)(uintptr_t)items[i].id;
    }

    free(items);
    return count;
}

static bool wait_for_daemon_exit(int timeout_ms) {
    int steps = timeout_ms / 100;
    if (steps < 1)
        steps = 1;

    for (int i = 0; i < steps; i++) {
        usleep(100000);
        if (!daemon_is_running())
            return true;
    }
    return !daemon_is_running();
}

static void daemon_on_stop(void *userdata) {
    (void)userdata;
    ap_footer_item cfooter[] = {
        {AP_BTN_B, T("sg.btn.no"),  false},
        {AP_BTN_A, T("sg.btn.yes"), true},
    };
    ap_message_opts mopts = {
        .message = T("sg.dialog.stop_bg"),
        .footer = cfooter,
        .footer_count = 2,
    };
    ap_confirm_result cres;
    ap_confirmation(&mopts, &cres);
    if (!cres.confirmed)
        return;

    if (!daemon_request_stop()) {
        show_error(T("sg.error.bg_stop_failed"));
        return;
    }

    if (!wait_for_daemon_exit(10000))
        show_error(T("sg.error.bg_stop_timeout"));
}

static void show_daemon_status_screen(void) {
    ap_queue_opts opts = {
        .title         = T("sg.title.bg_scraping"),
        .snapshot      = daemon_snapshot,
        .max_items     = QUEUE_MAX_ITEMS,
        .status_bar    = &g_status_bar,
        .userdata      = NULL,
        .on_detail     = NULL,
        .on_cancel     = daemon_on_stop,
        .on_clear      = NULL,
        .filter_labels = { T("sg.queue.filter.all"), T("sg.queue.filter.busy"),
                           T("sg.queue.filter.done"), T("sg.queue.filter.fail") },
        .empty_message        = T("sg.queue.empty"),
        .empty_filter_message = T("sg.queue.empty_filter"),
    };
    ap_queue_viewer(&opts);
}

static bool restore_daemon_queue(bool show_progress_once,
                                 const char *summary_label) {
    queue_item *items = malloc(sizeof(queue_item) * QUEUE_MAX_ITEMS);
    if (!items) {
        show_error(T("sg.error.oom"));
        return false;
    }

    queue_stats stats;
    int count = daemon_read_queue(items, QUEUE_MAX_ITEMS, &stats);
    if (count <= 0) {
        free(items);
        return false;
    }

    queue_load_items(items, count);
    free(items);
    daemon_cleanup_all();

    if (show_progress_once || stats.pending > 0) {
        show_progress_screen();
        return true;
    }

    char msg[256];
    snprintf(msg, sizeof(msg), T("sg.daemon.summary_fmt"),
        summary_label, stats.done, stats.failed, stats.total);
    show_brief(msg);
    return true;
}

typedef struct {
    float progress;
    char  error[256];
} daemon_takeover_ctx;

static int daemon_takeover_worker(void *userdata) {
    daemon_takeover_ctx *ctx = (daemon_takeover_ctx *)userdata;
    if (!ctx)
        return -1;

    ctx->progress = 0.0f;
    if (!daemon_request_handoff()) {
        snprintf(ctx->error, sizeof(ctx->error), "%s",
                 T("sg.error.handoff_failed"));
        return -1;
    }

    for (int i = 0; i < 100; i++) {
        if (!daemon_is_running()) {
            ctx->progress = 1.0f;
            return 0;
        }
        ctx->progress = (float)(i + 1) / 100.0f;
        usleep(100000);
    }

    if (daemon_is_running()) {
        snprintf(ctx->error, sizeof(ctx->error), "%s",
                 T("sg.error.handoff_timeout"));
        return -1;
    }

    ctx->progress = 1.0f;
    return 0;
}

static bool check_daemon_on_startup(void) {
    /* Reclaim background work immediately on relaunch. */
    if (daemon_is_running()) {
        daemon_takeover_ctx ctx = {0};
        ap_process_opts opts = {
            .message = T("sg.daemon.resuming"),
            .show_progress = true,
            .progress = &ctx.progress,
        };

        if (ap_process_message(&opts, daemon_takeover_worker, &ctx) == 0) {
            if (restore_daemon_queue(true, NULL))
                return true;

            show_error(T("sg.error.bg_queue_lost"));
            daemon_cleanup_all();
            return true;
        }

        if (!daemon_is_running()) {
            if (restore_daemon_queue(true, NULL))
                return true;

            show_error(ctx.error[0]
                ? ctx.error
                : T("sg.error.bg_queue_lost"));
            daemon_cleanup_all();
            return true;
        }

        show_daemon_status_screen();
        if (!daemon_is_running()) {
            if (restore_daemon_queue(true, NULL))
                return true;

            show_error(T("sg.error.bg_queue_lost"));
            daemon_cleanup_all();
            return true;
        }

        show_warning(T("sg.warn.bg_still_active"));
        return false;
    }

    /* Check for a daemon that already finished while the app was closed. */
    if (restore_daemon_queue(false, T("sg.daemon.completed_label")))
        return true;

    return true;
}

/* ── App entry ───────────────────────────────────────────── */

void run_app(void) {
    /* Load initial settings into queue */
    app_settings settings = load_settings();
    queue_set_settings(&settings);

    /* Check for background daemon from previous session */
    if (!check_daemon_on_startup()) {
        free_settings(&settings);
        return;
    }

#ifndef PLATFORM_MAC
    if (!has_default_route()) {
        show_warning(T("sg.warn.no_internet"));
    } else
#endif
    if (settings.ss_username[0] == '\0') {
        show_warning(T("sg.warn.no_credentials"));
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
        case MAIN_DOWNLOAD_MANUALS: {
            app_settings s = load_settings();
            if (s.manual_download_dir[0] == '\0') {
                show_warning(T("sg.warn.no_manual_dir"));
            } else {
                if (show_library_screen(LIB_MODE_MANUAL)) show_progress_screen();
            }
            free_settings(&s);
            break;
        }
        case MAIN_PROGRESS:        show_progress_screen(); break;
        case MAIN_API_USAGE:       show_api_usage_screen(); break;
        case MAIN_SETTINGS:        show_settings_screen(); break;
        case MAIN_QUIT: {
            queue_stats stats = queue_get_stats();
            if (stats.pending > 0) {
                quit_queue_action action = show_quit_queue_dialog(stats.pending);
                if (action == QUIT_QUEUE_KEEP_OPEN)
                    continue;

                if (action == QUIT_QUEUE_BACKGROUND) {
                    /* Performance warning */
                    const char *warn_message = is_flip_layout()
                        ? T("sg.dialog.bg_warning_compact")
                        : T("sg.dialog.bg_warning");
                    ap_footer_item warn_footer[] = {
                        {AP_BTN_B, T("sg.btn.cancel"), false},
                        {AP_BTN_A, T("sg.btn.continue"), true},
                    };
                    ap_message_opts warn_opts = {
                        .message = warn_message,
                        .footer = warn_footer,
                        .footer_count = 2,
                    };
                    ap_confirm_result warn_confirm;
                    int ret = ap_confirmation(&warn_opts, &warn_confirm);
                    if (ret != AP_OK || !warn_confirm.confirmed)
                        continue;

                    /* Stop local workers and hand off the full queue state. */
                    queue_item *snapshot = malloc(sizeof(queue_item) * QUEUE_MAX_ITEMS);
                    if (!snapshot) {
                        show_error(T("sg.error.oom"));
                        continue;
                    }
                    int snap_count = queue_handoff_snapshot(snapshot, QUEUE_MAX_ITEMS);

                    int pending_count = 0;
                    for (int i = 0; i < snap_count; i++) {
                        if (!is_item_terminal(snapshot[i].status))
                            pending_count++;
                    }
                    if (pending_count <= 0) {
                        free(snapshot);
                        return;
                    }

                    int launch_ret = daemon_launch(snapshot, snap_count);

                    if (launch_ret != 0) {
                        daemon_cleanup_all();
                        queue_load_items(snapshot, snap_count);
                        free(snapshot);
                        show_error(T("sg.error.bg_start_failed"));
                        continue;
                    }
                    free(snapshot);
                }

                if (action == QUIT_QUEUE_EXIT_AND_CANCEL) {
                    ap_footer_item exit_footer[] = {
                        {AP_BTN_B, T("sg.btn.keep_open"), false},
                        {AP_BTN_A, T("sg.btn.exit"), true},
                    };
                    ap_message_opts exit_opts = {
                        .message = T("sg.dialog.exit_cancel"),
                        .footer = exit_footer,
                        .footer_count = 2,
                    };
                    ap_confirm_result exit_confirm;
                    int ret = ap_confirmation(&exit_opts, &exit_confirm);
                    if (ret != AP_OK || !exit_confirm.confirmed)
                        continue;

                    queue_cancel_all();
                }
                /* Exit or Background (after daemon launched) */
            }
            return;
        }
        }
    }
}

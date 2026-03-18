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

static void show_rom_list_screen(const console_dir *console,
                                  const app_settings *settings,
                                  library_mode mode);

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

static void show_brief(const char *message) {
    ap_footer_item footer[] = {{AP_BTN_A, "OK", true}};
    ap_message_opts opts = {.message = message, .footer = footer, .footer_count = 1};
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
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
    MAIN_SETTINGS,
} main_action;

static main_action show_main_menu(void) {
    queue_stats qstats = queue_get_stats();

    char progress_label[64];
    if (qstats.total > 0)
        snprintf(progress_label, sizeof(progress_label), "Progress  (%d/%d)",
                 qstats.done, qstats.total);
    else
        snprintf(progress_label, sizeof(progress_label), "Progress");

    ap_list_item items[] = {
        {.label = "Scrape Artwork"},
        {.label = "Download Cheats"},
        {.label = progress_label},
        {.label = "Settings"},
    };
    ap_footer_item footer[] = {
        {AP_BTN_B, "Quit", false},
        {AP_BTN_A, "Select", true},
    };

    ap_list_opts opts = ap_list_default_opts("ScrapeGoat", items, 4);
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
    case 3: return MAIN_SETTINGS;
    default: return MAIN_QUIT;
    }
}

/* ── Library: ROM list ────────────────────────────────────── */

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

static void show_rom_list_screen(const console_dir *console,
                                  const app_settings *settings,
                                  library_mode mode) {
    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, settings->show_hidden, &roms);
    if (rom_count <= 0) {
        show_error("No ROMs found in this system.");
        free(roms);
        return;
    }

    const char *queue_label = (mode == LIB_MODE_ART) ? "Queue Art" : "Queue Cheat";
    const char *queue_all_label = (mode == LIB_MODE_ART) ? "Queue All Art" : "Queue All Cheats";

    int initial_idx = 0;
    int visible_start = 0;

    for (;;) {
        /* Build list items with embedded status */
        char (*labels)[512] = malloc(sizeof(char[512]) * (size_t)rom_count);
        ap_list_item *items = calloc((size_t)rom_count, sizeof(ap_list_item));

        for (int i = 0; i < rom_count; i++) {
            const char *status = rom_status_label(&roms[i], console, mode);
            if (status)
                snprintf(labels[i], 512, "%s   [%s]", roms[i].display, status);
            else
                snprintf(labels[i], 512, "%s", roms[i].display);
            items[i].label = labels[i];
        }

        ap_footer_item footer[] = {
            {AP_BTN_B, "Back", false},
            {AP_BTN_A, queue_label, false},
            {AP_BTN_Y, queue_all_label, true},
        };

        char title[256];
        snprintf(title, sizeof(title), "%s", console->display);

        ap_list_opts opts = ap_list_default_opts(title, items, rom_count);
        opts.footer = footer;
        opts.footer_count = 3;
        opts.secondary_action_button = AP_BTN_Y;
        opts.initial_index = initial_idx;
        opts.visible_start_index = visible_start;

        ap_list_result result;
        int ret = ap_list(&opts, &result);

        initial_idx = result.selected_index;
        visible_start = result.visible_start_index;

        free(labels);
        free(items);

        if (ret == AP_CANCELLED) break;

        int sel = result.selected_index;
        if (sel < 0 || sel >= rom_count) break;

        if (result.action == AP_ACTION_SELECTED || result.action == AP_ACTION_TRIGGERED) {
            /* A: Queue single ROM */
            queue_set_settings(settings);
            bool added;
            if (mode == LIB_MODE_ART)
                added = queue_add_artwork(&roms[sel], console);
            else
                added = queue_add_cheat(&roms[sel], console);

            if (added) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Queued \"%s\" for %s.",
                         roms[sel].display,
                         mode == LIB_MODE_ART ? "artwork" : "cheats");
                show_brief(msg);
            } else {
                show_brief(mode == LIB_MODE_ART
                    ? "Already queued or artwork exists."
                    : "Already queued or cheat exists.");
            }
            continue;
        }

        if (result.action == AP_ACTION_SECONDARY_TRIGGERED) {
            /* Y: Queue all */
            queue_set_settings(settings);
            int added;
            if (mode == LIB_MODE_ART)
                added = queue_add_all_artwork(console, settings->show_hidden);
            else
                added = queue_add_all_cheats(console, settings->show_hidden);

            char msg[128];
            snprintf(msg, sizeof(msg), "Queued %d ROMs for %s.", added,
                     mode == LIB_MODE_ART ? "artwork" : "cheats");
            show_brief(msg);
            continue;
        }
    }

    free(roms);
}

/* ── Library: System list ─────────────────────────────────── */

static void show_library_screen(library_mode mode) {
    app_settings settings = load_settings();

    console_dir *consoles = NULL;
    int console_count = scan_console_dirs(settings.show_hidden, &consoles);
    if (console_count <= 0) {
        show_error("No ROM folders found.");
        free(consoles);
        free_settings(&settings);
        return;
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
        return;
    }

    /* Build labels with relevant badge only */
    char (*labels)[512] = malloc(sizeof(char[512]) * (size_t)visible_count);
    for (int vi = 0; vi < visible_count; vi++) {
        int i = visible_map[vi];
        if (mode == LIB_MODE_ART)
            snprintf(labels[vi], 512, "%s   %d/%d art",
                     names[i], stats[i].art_count, stats[i].rom_count);
        else
            snprintf(labels[vi], 512, "%s   %d/%d cht",
                     names[i], stats[i].cheat_count, stats[i].rom_count);
    }

    const char *title = (mode == LIB_MODE_ART) ? "Scrape Artwork" : "Download Cheats";
    const char *queue_all = (mode == LIB_MODE_ART) ? "Queue All Art" : "Queue All Cheats";

    int initial_idx = 0;
    int visible_start = 0;

    for (;;) {
        ap_list_item *items = calloc((size_t)visible_count, sizeof(ap_list_item));
        for (int vi = 0; vi < visible_count; vi++)
            items[vi].label = labels[vi];

        ap_footer_item footer[] = {
            {AP_BTN_B, "Back", false},
            {AP_BTN_A, "Open", false},
            {AP_BTN_Y, queue_all, true},
        };

        ap_list_opts opts = ap_list_default_opts(title, items, visible_count);
        opts.footer = footer;
        opts.footer_count = 3;
        opts.secondary_action_button = AP_BTN_Y;
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

        if (result.action == AP_ACTION_SECONDARY_TRIGGERED) {
            /* Y: Queue all for this system */
            queue_set_settings(&settings);
            int added;
            if (mode == LIB_MODE_ART)
                added = queue_add_all_artwork(&consoles[real_idx], settings.show_hidden);
            else
                added = queue_add_all_cheats(&consoles[real_idx], settings.show_hidden);

            char msg[128];
            snprintf(msg, sizeof(msg), "Queued %d ROMs for %s.", added,
                     mode == LIB_MODE_ART ? "artwork" : "cheats");
            show_brief(msg);

            /* Refresh stats for this system */
            stats[real_idx] = compute_system_stats(&consoles[real_idx], settings.show_hidden);
            if (mode == LIB_MODE_ART)
                snprintf(labels[sel], 512, "%s   %d/%d art",
                         names[real_idx], stats[real_idx].art_count, stats[real_idx].rom_count);
            else
                snprintf(labels[sel], 512, "%s   %d/%d cht",
                         names[real_idx], stats[real_idx].cheat_count, stats[real_idx].rom_count);
            continue;
        }

        /* A: Open ROM list */
        show_rom_list_screen(&consoles[real_idx], &settings, mode);
    }

    free(visible_map);
    free(names);
    free(labels);
    free(stats);
    free(consoles);
    free_settings(&settings);
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

static ap_color status_color(queue_item_status status, const ap_theme *theme) {
    switch (status) {
    case QUEUE_DONE:
    case QUEUE_SKIPPED:
        return (ap_color){100, 200, 100, 255}; /* green */
    case QUEUE_NOT_FOUND:
    case QUEUE_ERROR:
        return (ap_color){220, 100, 100, 255}; /* red */
    case QUEUE_SEARCHING:
    case QUEUE_DOWNLOADING:
    case QUEUE_CLONING:
    case QUEUE_MATCHING:
        return (ap_color){220, 200, 80, 255};  /* yellow */
    case QUEUE_IDLE:
    default:
        return theme->hint;
    }
}

static void show_progress_screen(void) {
    int selected = 0;
    int scroll_offset = 0;

    for (;;) {
        bool can_clear = !queue_is_active();

        /* Compute layout early so visible_rows is available for page-skip */
        const ap_theme *theme = ap_get_theme();
        TTF_Font *font_large = ap_get_font(AP_FONT_LARGE);
        TTF_Font *font_small = ap_get_font(AP_FONT_SMALL);
        TTF_Font *font_tiny  = ap_get_font(AP_FONT_TINY);
        int screen_w = ap_get_screen_width();
        SDL_Rect content = ap_get_content_rect(true, true, false);
        int row_height = TTF_FontHeight(font_large) + TTF_FontHeight(font_tiny) + ap_scale(6);
        int visible_rows = content.h / row_height;
        if (visible_rows < 1) visible_rows = 1;

        /* Handle input */
        ap_input_event ev;
        bool quit = false;
        bool clear = false;
        while (ap_poll_input(&ev)) {
            if (!ev.pressed) continue;
            switch (ev.button) {
            case AP_BTN_B:     quit = true; break;
            case AP_BTN_X:     clear = true; break;
            case AP_BTN_DOWN:  selected++; break;
            case AP_BTN_UP:    selected--; break;
            case AP_BTN_L1:    /* fall through */
            case AP_BTN_LEFT:  selected -= visible_rows; break;
            case AP_BTN_R1:    /* fall through */
            case AP_BTN_RIGHT: selected += visible_rows; break;
            default: break;
            }
        }

        if (quit) break;
        if (clear && can_clear) queue_clear_done();

        /* Get queue snapshot */
        queue_item *items = malloc(sizeof(queue_item) * QUEUE_MAX_ITEMS);
        int count = queue_snapshot(items, QUEUE_MAX_ITEMS);
        queue_stats stats = queue_get_stats();
        can_clear = !queue_is_active();

        /* Clamp selection */
        if (count == 0) selected = 0;
        else {
            if (selected < 0) selected = 0;
            if (selected >= count) selected = count - 1;
        }

        ap_draw_background();
        ap_draw_screen_title("Progress", NULL);

        /* Footer */
        ap_footer_item footer[] = {
            {AP_BTN_B, "Back", false},
            {AP_BTN_X, can_clear ? "Clear Done" : "Clear When Idle", true},
        };
        ap_draw_footer(footer, 2);

        /* Adjust scroll to keep selection visible */
        if (selected < scroll_offset)
            scroll_offset = selected;
        if (selected >= scroll_offset + visible_rows)
            scroll_offset = selected - visible_rows + 1;
        if (scroll_offset < 0) scroll_offset = 0;

        if (count == 0) {
            /* Empty state */
            ap_draw_text(font_large, "No items in queue.",
                         content.x + ap_scale(16), content.y + ap_scale(16), theme->hint);
        } else {
            /* Draw items */
            int y = content.y;
            int padding = ap_scale(12);
            int status_max_w = ap_scale(120);

            for (int i = scroll_offset; i < count && i < scroll_offset + visible_rows; i++) {
                queue_item *item = &items[i];
                int row_y = y;

                /* Selection pill */
                if (i == selected) {
                    ap_draw_pill(content.x, row_y, content.w, row_height, theme->highlight);
                }

                ap_color text_color = (i == selected) ? theme->highlighted_text : theme->text;

                /* ROM name (left) */
                int name_max_w = content.w - status_max_w - padding * 3;
                ap_draw_text_ellipsized(font_large, item->rom_display,
                    content.x + padding, row_y + ap_scale(2), text_color, name_max_w);

                /* Status (right) */
                const char *status_str = queue_status_text(item->status);
                ap_color sc = (i == selected) ? theme->highlighted_text :
                              status_color(item->status, theme);
                int status_w;
                TTF_SizeUTF8(font_small, status_str, &status_w, NULL);
                ap_draw_text(font_small, status_str,
                    content.x + content.w - padding - status_w,
                    row_y + ap_scale(4), sc);

                /* System name (below, smaller) */
                const char *type_str = item->type == QUEUE_TYPE_ARTWORK ? "art" : "cht";
                char desc[300];
                snprintf(desc, sizeof(desc), "%s  [%s]", item->system_display, type_str);
                ap_color desc_color = (i == selected) ? theme->highlighted_text : theme->hint;
                ap_draw_text_ellipsized(font_tiny, desc,
                    content.x + padding,
                    row_y + TTF_FontHeight(font_large) + ap_scale(2),
                    desc_color, name_max_w);

                y += row_height;
            }

            /* Scrollbar */
            if (count > visible_rows) {
                ap_draw_scrollbar(
                    content.x + content.w - ap_scale(4),
                    content.y, content.h,
                    visible_rows, count, scroll_offset);
            }

            /* Summary bar */
            char summary[128];
            snprintf(summary, sizeof(summary), "%d/%d complete, %d failed",
                     stats.done, stats.total, stats.failed);
            int summary_w;
            TTF_SizeUTF8(font_tiny, summary, &summary_w, NULL);
            ap_draw_text(font_tiny, summary,
                (screen_w - summary_w) / 2,
                content.y + content.h - TTF_FontHeight(font_tiny) - ap_scale(2),
                theme->hint);
        }

        ap_present();
        free(items);
        SDL_Delay(33); /* ~30fps */
    }
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
        case MAIN_SCRAPE_ART:      show_library_screen(LIB_MODE_ART); break;
        case MAIN_DOWNLOAD_CHEATS: show_library_screen(LIB_MODE_CHEAT); break;
        case MAIN_PROGRESS:        show_progress_screen(); break;
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
                    {AP_BTN_B, "Cancel", false},
                    {AP_BTN_A, "Exit", true},
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

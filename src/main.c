/* ScrapeGoat — A ScreenScraper.fr artwork scraper and Libretro cheat
 * downloader for NextUI handhelds.
 *
 * This file provides the Apostrophe UI implementation and program entry point.
 */

#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"

#include "daemon.h"
#include "device.h"
#include "queue.h"
#include "screenscraper.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PLATFORM_MAC
#include <limits.h>
#include <unistd.h>
#endif

#ifdef PLATFORM_MAC
static void configure_desktop_nextui_preview(void) {
    const char *cache_root = ".cache/nextui-preview";
    const char *cache_assets_dir = ".cache/nextui-preview/assets";
    const char *env_assets = getenv("AP_STATUS_ASSETS_DIR");
    const char *env_nextval = getenv("AP_NEXTVAL_PATH");
    const char *env_settings = getenv("AP_MINUI_SETTINGS_PATH");
    char path[PATH_MAX];
    bool have_cache_assets;
    bool have_cache_nextval;
    bool have_cache_settings;

    have_cache_assets = access(cache_assets_dir, R_OK) == 0;

    snprintf(path, sizeof(path), "%s/nextval.json", cache_root);
    have_cache_nextval = access(path, R_OK) == 0;
    if ((!env_nextval || !env_nextval[0]) && have_cache_nextval)
        setenv("AP_NEXTVAL_PATH", path, 0);

    snprintf(path, sizeof(path), "%s/minuisettings.txt", cache_root);
    have_cache_settings = access(path, R_OK) == 0;
    if ((!env_settings || !env_settings[0]) && have_cache_settings)
        setenv("AP_MINUI_SETTINGS_PATH", path, 0);

    if ((!env_assets || !env_assets[0]) && have_cache_assets)
        setenv("AP_STATUS_ASSETS_DIR", cache_assets_dir, 0);

    if ((!env_assets || !env_assets[0] || !env_nextval || !env_nextval[0]
         || !env_settings || !env_settings[0])
        && (!have_cache_assets || !have_cache_nextval || !have_cache_settings)) {
        fprintf(stderr,
                "scrapegoat: desktop NextUI preview cache missing; run `make setup-nextui-preview-cache`\n");
    }

    setenv("AP_PREVIEW_WIFI_STRENGTH", "3", 0);
    setenv("AP_PREVIEW_BATTERY_PERCENT", "100", 0);
    setenv("AP_PREVIEW_CHARGING", "0", 0);
}
#endif

int main(int argc, char *argv[]) {
    /* Background daemon mode: headless, no UI */
    if (argc > 1 && strcmp(argv[1], "--daemon") == 0) {
        int ready_fd = -1;
        if (argc > 2 && strncmp(argv[2], "--ready-fd=", 11) == 0) {
            char *end;
            long val = strtol(argv[2] + 11, &end, 10);
            if (*end == '\0' && val >= 0)
                ready_fd = (int)val;
        }
        return daemon_main(ready_fd);
    }

    fprintf(stderr, "scrapegoat: starting (platform=%s)\n", AP_PLATFORM_NAME);

    /* Verify developer credentials */
    if (ss_check_dev_credentials() != 0) {
        fprintf(stderr, "scrapegoat: warning: dev credentials not set\n");
    }

    /* Initialize Apostrophe UI */
    ap_config cfg = {0};
    cfg.window_title = "ScrapeGoat";
    cfg.log_path = ap_resolve_log_path("scrapegoat");
    cfg.is_nextui = true;
    cfg.cpu_speed = AP_CPU_SPEED_MENU;

#ifndef PLATFORM_MAC
    /* On device: use system font (Apostrophe resolves from NextUI theme) */
#else
    configure_desktop_nextui_preview();
    /* On macOS: use bundled font for development */
    cfg.font_path = "third_party/apostrophe/res/font.ttf";
#endif

    if (ap_init(&cfg) != AP_OK) {
        fprintf(stderr, "scrapegoat: failed to initialize UI\n");
        return 1;
    }

    queue_init();
    run_app();
    queue_shutdown();

    ap_quit();
    return 0;
}

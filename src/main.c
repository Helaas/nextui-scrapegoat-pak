/* ScrapeGoat — A ScreenScraper.fr artwork scraper and Libretro cheat
 * downloader for NextUI handhelds.
 *
 * This file provides the Apostrophe UI implementation and program entry point.
 */

#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"

#include "device.h"
#include "queue.h"
#include "screenscraper.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    fprintf(stderr, "scrapegoat: starting (platform=%s)\n", AP_PLATFORM_NAME);

    /* Verify developer credentials */
    if (ss_check_dev_credentials() != 0) {
        fprintf(stderr, "scrapegoat: warning: dev credentials not set\n");
    }

    /* Initialize Apostrophe UI */
    ap_config cfg = {0};
    cfg.window_title = "ScrapeGoat";
    cfg.log_path = ap_resolve_log_path("scrapegoat");
    cfg.is_nextui = AP_PLATFORM_IS_DEVICE;
    cfg.cpu_speed = AP_CPU_SPEED_MENU;

#ifndef PLATFORM_MAC
    /* On device: use system font (Apostrophe resolves from NextUI theme) */
#else
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

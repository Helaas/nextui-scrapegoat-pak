/* Exercise captured targets and persistence with a local cheat checkout. */
#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"
#include "cheats.h"
#include <assert.h>

static int fixture_checkout(const char *dir, atomic_int *interrupt,
                             cheat_message_fn message, cheat_progress_fn progress,
                             float scale, float offset) {
    (void)dir; (void)interrupt; (void)message; (void)progress; (void)scale; (void)offset;
    return CHEAT_OP_OK;
}

/* Keep the actual enqueue/cache/restore code; substitute only git checkout. */
#define ensure_system_checked_out fixture_checkout
#include "../src/queue.c"
#undef ensure_system_checked_out
#include "../src/daemon.c"

static void put_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    assert(f);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}

int main(void) {
    char root[] = "/tmp/scrapegoat-queue-XXXXXX";
    assert(mkdtemp(root));
    setenv("SDCARD_PATH", root, 1);
    setenv("SCRAPEGOAT_SYSTEMS_JSON", "resources/systems.json", 1);
    assert(systems_init() == 0);
    queue_init();

    console_dir consoles[2] = {0};
    rom_file roms[2] = {0};
    const char *names[] = {"Mega Drive", "Master System"};
    const char *ids[] = {"megadrive", "mastersystem"};
    const char *dirs[] = {"Sega - Mega Drive - Genesis", "Sega - Master System - Mark III"};
    char path[PATH_MAX], repo[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));
    for (int i = 0; i < 2; i++) {
        snprintf(consoles[i].path, sizeof(consoles[i].path), "%s/Roms/%s (GPGX)", root, names[i]);
        snprintf(consoles[i].tag, sizeof(consoles[i].tag), "GPGX");
        ensure_dir_exists(consoles[i].path);
        assert(systems_set_folder_platform(consoles[i].path, ids[i]) == 0);
        snprintf(roms[i].display, sizeof(roms[i].display), "Example %d", i);
        snprintf(roms[i].path, sizeof(roms[i].path), "%s/Example %d.rom", consoles[i].path, i);
        snprintf(path, sizeof(path), "%s/cht/%s", repo, dirs[i]);
        ensure_dir_exists(path);
        snprintf(path, sizeof(path), "%s/cht/%s/Example %d.cht", repo, dirs[i], i);
        put_file(path, "cheats = 1\n");
    }

    /* Hold the manager idle so these real enqueue calls cannot start workers. */
    g_manager_started = true;
    for (int i = 0; i < 2; i++) assert(queue_add_cheat_forced(&roms[i], &consoles[i]));
    g_manager_started = false;
    assert(!queue_begin_mapping_edit());
    for (int i = 0; i < 2; i++) assert(strcmp(g_items[i].cheat_dir, dirs[i]) == 0);

    snprintf(path, sizeof(path), "%s/queue.json", root);
    assert(serialize_queue(path, g_items, g_count) == 0);
    queue_item restored[2];
    assert(deserialize_queue(path, restored, 2) == 2);
    for (int i = 0; i < 2; i++) {
        restore_cheat_target_locked(&restored[i]);
        assert(restored[i].status == QUEUE_IDLE);
        assert(strcmp(restored[i].cheat_dir, dirs[i]) == 0);
    }

    g_cheat_repo_ready = true;
    cheat_list *md = ensure_cheat_list(restored[0].cheat_dir, &g_interrupt);
    cheat_list *sms = ensure_cheat_list(restored[1].cheat_dir, &g_interrupt);
    assert(md && sms && md != sms && md->count == 1 && sms->count == 1);
    assert(strstr(md->entries[0].candidates[0].path, dirs[0]));
    assert(strstr(sms->entries[0].candidates[0].path, dirs[1]));
    assert(ensure_cheat_list(dirs[0], &g_interrupt) == md);

    for (int i = 0; i < g_count; i++) g_items[i].status = QUEUE_DONE;
    assert(queue_begin_mapping_edit());
    assert(g_cheat_cache_count == 0);
    assert(systems_set_folder_platform(consoles[0].path, ids[1]) == 0);
    restore_cheat_target_locked(&restored[0]);
    assert(strcmp(restored[0].cheat_dir, dirs[0]) == 0); /* captured, never retargeted */
    restored[0].cheat_dir[0] = '\0';
    restore_cheat_target_locked(&restored[0]);
    assert(strcmp(restored[0].cheat_dir, dirs[1]) == 0); /* legacy resolves once */
    g_cheat_repo_ready = true;
    assert(ensure_cheat_list(restored[0].cheat_dir, &g_interrupt));
    assert(g_cheat_cache_count == 1 && strcmp(g_cheat_cache[0].dir, dirs[1]) == 0);

    const char *invalid[] = {"..", ".", "bad/path", "bad\\path"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        snprintf(restored[0].cheat_dir, sizeof(restored[0].cheat_dir), "%s", invalid[i]);
        restored[0].status = QUEUE_IDLE;
        restore_cheat_target_locked(&restored[0]);
        assert(restored[0].status == QUEUE_ERROR);
    }
    const char *bad_values[] = {"123", "null", "\"..\"", "\"bad/path\"", "\"bad\\\\path\""};
    char json[1024];
    for (size_t i = 0; i < sizeof(bad_values) / sizeof(bad_values[0]); i++) {
        snprintf(json, sizeof(json), "[{\"type\":\"cheat\",\"cheat_dir\":%s,\"status\":\"idle\",\"error_msg\":\"\"}]", bad_values[i]);
        put_file(path, json);
        assert(deserialize_queue(path, restored, 1) == 1);
        assert(restored[0].status == QUEUE_ERROR && restored[0].error_msg[0]);
    }
    char long_dir[301]; memset(long_dir, 'x', 300); long_dir[300] = '\0';
    snprintf(json, sizeof(json), "[{\"type\":\"cheat\",\"cheat_dir\":\"%s\",\"status\":\"idle\"}]", long_dir);
    put_file(path, json);
    assert(deserialize_queue(path, restored, 1) == 1 && restored[0].status == QUEUE_ERROR);
    assert(restored[0].cheat_dir[0] == '\0');
    put_file(path, "[{\"type\":\"artwork\",\"system_id\":9876,\"status\":\"idle\"}]");
    assert(deserialize_queue(path, restored, 1) == 1);
    restore_cheat_target_locked(&restored[0]);
    assert(restored[0].system_id == 9876 && restored[0].status == QUEUE_IDLE);

    queue_shutdown();
    systems_shutdown();
    printf("Queue checks passed: captures, handoff, legacy restore, invalid targets, cache isolation and edit guard.\n");
    /* root is the fixed-template mkdtemp directory created above. */
    snprintf(json, sizeof(json), "rm -rf '%s'", root);
    assert(system(json) == 0);
    return 0;
}

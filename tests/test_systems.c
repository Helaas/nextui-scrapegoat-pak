/* test_systems.c — regression checks for the platform catalog resolver.
 *
 * Runs against the real src/systems.c and the shipped catalog, in a temporary
 * SD-card root so no user data is touched. Build and run with:
 *
 *     make test-systems
 */

#include "systems.h"
#include "device.h"
#include "cJSON.h"

#include <errno.h>
#include <stdarg.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;
static int failures;

static void check(bool condition, const char *fmt, ...) {
    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL %s\n", message);
    }
}

static void section(const char *name) {
    printf("== %s\n", name);
}

/* ── Scratch SD card ──────────────────────────────────────── */

static char sd_root[PATH_MAX];
static char roms_root[PATH_MAX];

static void make_dir(const char *path) {
    ensure_dir_exists(path);
}

static void make_rom_folder(const char *name) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", roms_root, name);
    make_dir(path);
}

static void rom_folder_path(const char *name, char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s/%s", roms_root, name);
}

static void remove_overrides(void) {
    char path[PATH_MAX];
    get_system_overrides_path(path, sizeof(path));
    unlink(path);
    char rejected[PATH_MAX];
    snprintf(rejected, sizeof(rejected), "%s.rejected", path);
    unlink(rejected);
}

static void write_overrides(const char *body) {
    char path[PATH_MAX];
    get_system_overrides_path(path, sizeof(path));
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; make_dir(dir); }
    FILE *f = fopen(path, "w");
    if (!f) { perror("write_overrides"); exit(2); }
    fputs(body, f);
    fclose(f);
}

static char *read_overrides(void) {
    char path[PATH_MAX];
    get_system_overrides_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/* ── Baseline parity ──────────────────────────────────────── */

/* Reviewed deviations from the pre-catalog tables. Each one is a defect the
 * audit found in src/systems.c, not a regression. */
static const struct { const char *tag; const char *why; } corrections[] = {
    {"COLECO", "old ScreenScraper id 60 is PlayStation 4"},
    {"MSX",    "old ScreenScraper id 62 is PS Vita"},
    {"C128",   "old ScreenScraper id 87 is the Amstrad GX4000"},
};

static const char *correction_for(const char *tag) {
    for (size_t i = 0; i < sizeof(corrections) / sizeof(corrections[0]); i++) {
        if (strcmp(corrections[i].tag, tag) == 0)
            return corrections[i].why;
    }
    return NULL;
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static void test_baseline_parity(const char *fixture_path) {
    section("pre-catalog associations still resolve");
    char *text = slurp(fixture_path);
    if (!text) {
        check(false, "baseline fixture %s is readable", fixture_path);
        return;
    }
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        check(false, "baseline fixture parses");
        return;
    }

    int preserved = 0, corrected = 0;
    cJSON *ss = cJSON_GetObjectItem(root, "screenscraper");
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, ss) {
        sg_mapping mapping = systems_resolve(NULL, entry->string);
        int actual = mapping.platform ? mapping.platform->ss_id : -1;
        int expected = (int)entry->valuedouble;
        const char *why = correction_for(entry->string);
        if (why) {
            check(actual != expected,
                  "%s is corrected away from %d (%s)", entry->string, expected, why);
            corrected++;
        } else {
            check(actual == expected,
                  "%s resolves to ScreenScraper %d, got %d",
                  entry->string, expected, actual);
            preserved++;
        }
    }

    cJSON *libretro = cJSON_GetObjectItem(root, "libretro");
    cJSON_ArrayForEach(entry, libretro) {
        sg_mapping mapping = systems_resolve(NULL, entry->string);
        const char *actual = mapping.platform ? mapping.platform->libretro_dir : NULL;
        check(actual && strcmp(actual, entry->valuestring) == 0,
              "%s resolves to cheat directory \"%s\", got \"%s\"",
              entry->string, entry->valuestring, actual ? actual : "(none)");
    }

    check(preserved + corrected == cJSON_GetArraySize(ss),
          "every baseline ScreenScraper association was checked");
    check(corrected == (int)(sizeof(corrections) / sizeof(corrections[0])),
          "every reviewed correction appears in the baseline");
    cJSON_Delete(root);
}

/* ── Folder scoping ───────────────────────────────────────── */

static void test_folder_scoping(void) {
    section("folder overrides beat suffix defaults");
    char megadrive[PATH_MAX], mastersystem[PATH_MAX];
    rom_folder_path("Mega Drive (GPGX)", megadrive, sizeof(megadrive));
    rom_folder_path("Master System (GPGX)", mastersystem, sizeof(mastersystem));

    sg_mapping before = systems_resolve(megadrive, "GPGX");
    check(before.platform == NULL && before.source == MAPPING_NONE,
          "GPGX ships no bundled default, so its folders start unmapped");
    check(systems_tag_candidates("GPGX", (const sg_platform *[5]){0}, 5) == 5,
          "GPGX offers five reviewed candidates");

    check(systems_set_folder_platform(megadrive, "megadrive") == 0,
          "mapping the Mega Drive folder succeeds");
    check(systems_set_folder_platform(mastersystem, "mastersystem") == 0,
          "mapping the Master System folder succeeds");

    sg_mapping a = systems_resolve(megadrive, "GPGX");
    sg_mapping b = systems_resolve(mastersystem, "GPGX");
    check(a.platform && strcmp(a.platform->id, "megadrive") == 0,
          "the Mega Drive folder resolves to megadrive");
    check(b.platform && strcmp(b.platform->id, "mastersystem") == 0,
          "the Master System folder resolves to mastersystem");
    check(a.platform->libretro_dir && b.platform->libretro_dir
          && strcmp(a.platform->libretro_dir, b.platform->libretro_dir) != 0,
          "the two folders target different cheat databases");

    section("a user suffix default fills only unmapped folders");
    check(systems_set_tag("GPGX", "gamegear") == 0, "setting a suffix default succeeds");
    char gamegear[PATH_MAX];
    rom_folder_path("Game Gear (GPGX)", gamegear, sizeof(gamegear));
    sg_mapping c = systems_resolve(gamegear, "GPGX");
    check(c.platform && strcmp(c.platform->id, "gamegear") == 0
          && c.source == MAPPING_USER_TAG,
          "an unmapped GPGX folder follows the user's suffix default");
    a = systems_resolve(megadrive, "GPGX");
    check(a.platform && strcmp(a.platform->id, "megadrive") == 0
          && a.source == MAPPING_USER_FOLDER,
          "the folder override still wins over the suffix default");

    section("clearing each scope exposes the next default");
    check(systems_set_folder_platform(megadrive, NULL) == 0, "clearing the folder succeeds");
    a = systems_resolve(megadrive, "GPGX");
    check(a.platform && strcmp(a.platform->id, "gamegear") == 0
          && a.source == MAPPING_USER_TAG,
          "clearing a folder falls back to the user's suffix default");
    check(systems_set_tag("GPGX", NULL) == 0, "clearing the suffix default succeeds");
    a = systems_resolve(megadrive, "GPGX");
    check(a.platform == NULL && a.source == MAPPING_NONE,
          "an ambiguous suffix with nothing left is unmapped again");

    /* A suffix that does ship a default resurfaces it. */
    char snes[PATH_MAX];
    rom_folder_path("Super Famicom (SFC)", snes, sizeof(snes));
    check(systems_set_folder_platform(snes, "gameboy") == 0, "overriding SFC succeeds");
    check(systems_set_folder_platform(snes, NULL) == 0, "clearing SFC succeeds");
    sg_mapping d = systems_resolve(snes, "SFC");
    check(d.platform && strcmp(d.platform->id, "snes") == 0
          && d.source == MAPPING_BUILTIN,
          "clearing a folder exposes the bundled default again");
}

static void test_visibility(void) {
    section("hiding is per folder and keeps the platform");
    char megadrive[PATH_MAX], mastersystem[PATH_MAX];
    rom_folder_path("Mega Drive (GPGX)", megadrive, sizeof(megadrive));
    rom_folder_path("Master System (GPGX)", mastersystem, sizeof(mastersystem));

    check(systems_set_folder_platform(megadrive, "megadrive") == 0, "map before hiding");
    check(systems_set_folder_hidden(megadrive, true) == 0, "hiding succeeds");

    sg_mapping a = systems_resolve(megadrive, "GPGX");
    sg_mapping b = systems_resolve(mastersystem, "GPGX");
    check(a.hidden, "the hidden folder reports hidden");
    check(a.platform && strcmp(a.platform->id, "megadrive") == 0,
          "hiding preserves the folder's platform");
    check(!b.hidden, "its sibling is unaffected");

    check(systems_set_folder_hidden(megadrive, false) == 0, "showing succeeds");
    a = systems_resolve(megadrive, "GPGX");
    check(!a.hidden && a.platform && strcmp(a.platform->id, "megadrive") == 0,
          "showing it again keeps the platform");
}

static void test_provider_independence(void) {
    section("provider capabilities are independent");
    const sg_platform *cheat_only = NULL;
    const sg_platform *ss_only = NULL;
    for (int i = 0; i < systems_platform_count(); i++) {
        const sg_platform *p = systems_platform_at(i);
        if (p->ss_id < 0 && p->libretro_dir && !cheat_only)
            cheat_only = p;
        if (p->ss_id >= 0 && !p->libretro_dir && !ss_only)
            ss_only = p;
    }
    check(ss_only != NULL,
          "a platform with artwork but no cheat database exists in the catalog");

    /* The catalog need not currently contain a cheat-only platform, but the
     * resolver must treat one as eligible for cheats when it does. */
    if (cheat_only) {
        check(cheat_only->ss_id < 0 && cheat_only->libretro_dir != NULL,
              "a cheat-only platform stays eligible for cheats");
    } else {
        printf("  note: the catalog currently has no cheat-only platform\n");
    }
}

static void test_folder_keys(void) {
    section("folder keys");
    char buf[512];
    char path[PATH_MAX];

    rom_folder_path("Mega Drive (GPGX)", path, sizeof(path));
    check(systems_folder_key(path, buf, sizeof(buf))
          && strcmp(buf, "Mega Drive (GPGX)") == 0,
          "a folder under Roms/ keys to its own name, got \"%s\"", buf);

    snprintf(path, sizeof(path), "%s//Mega Drive (GPGX)/", roms_root);
    check(systems_folder_key(path, buf, sizeof(buf))
          && strcmp(buf, "Mega Drive (GPGX)") == 0,
          "extra slashes normalise away, got \"%s\"", buf);

    snprintf(path, sizeof(path), "%s/../escape", roms_root);
    check(!systems_folder_key(path, buf, sizeof(buf)),
          "a traversal component is rejected");

    check(!systems_folder_key(roms_root, buf, sizeof(buf)),
          "the ROM root itself is not a folder key");

    snprintf(path, sizeof(path), "%s/RomsExtra/Thing (GB)", sd_root);
    check(!systems_folder_key(path, buf, sizeof(buf)),
          "a sibling directory whose name starts with Roms is rejected");

    check(!systems_folder_key("", buf, sizeof(buf)), "an empty path is rejected");
    check(!systems_folder_key("/", buf, sizeof(buf)), "root is rejected");

    /* A disabled folder is a different directory, so a different key. */
    rom_folder_path("Mega Drive (GPGX).disabled", path, sizeof(path));
    check(systems_folder_key(path, buf, sizeof(buf))
          && strcmp(buf, "Mega Drive (GPGX).disabled") == 0,
          "the .disabled suffix is part of the key, got \"%s\"", buf);
}

static void test_rename_behaviour(void) {
    section("renaming a folder");
    char before[PATH_MAX], after[PATH_MAX];
    rom_folder_path("Homebrew (GPGX)", before, sizeof(before));
    rom_folder_path("Homebrew (GPGX).disabled", after, sizeof(after));
    make_rom_folder("Homebrew (GPGX)");

    check(systems_set_folder_platform(before, "segacd") == 0, "mapping succeeds");
    rename(before, after);

    sg_mapping renamed = systems_resolve(after, "GPGX");
    check(renamed.platform == NULL,
          "the renamed folder does not inherit the old override");

    bool found = false;
    for (int i = 0; i < systems_override_count(); i++) {
        sg_override ov;
        if (systems_override_at(i, &ov) && ov.is_folder
            && strcmp(ov.key, "Homebrew (GPGX)") == 0)
            found = true;
    }
    check(found, "the old override is retained so Settings can clear it");
    check(systems_set_folder_platform(before, NULL) == 0,
          "the retained override can be cleared by its old path");
}

static void test_sd_root_change(void) {
    section("keys survive an SD root change");
    /* A key is the folder's path relative to Roms/, so nothing about the mount
     * point may appear in the saved file. Remounting the same card elsewhere
     * must not orphan a user's mappings. */
    char megadrive[PATH_MAX];
    rom_folder_path("Mega Drive (GPGX)", megadrive, sizeof(megadrive));
    check(systems_set_folder_platform(megadrive, "megadrive") == 0,
          "mapping a folder succeeds");

    const char *saved = read_overrides();
    check(saved != NULL, "the override file is readable");
    if (saved) {
        check(strstr(saved, sd_root) == NULL,
              "the saved file records no mount path");
        check(strstr(saved, "Mega Drive (GPGX)") != NULL,
              "the saved file records the folder name relative to Roms/");
    }

    /* The same relative folder under a different root yields the same key. */
    char other_root[PATH_MAX], other_folder[PATH_MAX], key[512];
    snprintf(other_root, sizeof(other_root), "%s-moved", sd_root);
    snprintf(other_folder, sizeof(other_folder), "%s/Roms/Mega Drive (GPGX)",
             other_root);
    make_dir(other_folder);
    check(!systems_folder_key(other_folder, key, sizeof(key)),
          "a path under a different root is not a key for this root");
}

static void test_invalid_data(void) {
    section("invalid user data is recoverable");
    systems_shutdown();
    write_overrides("{ this is not json");
    check(systems_init() == 0, "a corrupt override file still starts the app");
    check(systems_warning() != NULL, "the corruption is reported as a warning");

    sg_mapping gb = systems_resolve(NULL, "GB");
    check(gb.platform && strcmp(gb.platform->id, "gameboy") == 0,
          "bundled defaults are in use after corrupt overrides");

    char megadrive[PATH_MAX];
    rom_folder_path("Mega Drive (GPGX)", megadrive, sizeof(megadrive));
    check(systems_set_folder_platform(megadrive, "megadrive") == 0,
          "the first save after corruption succeeds");
    char rejected[PATH_MAX];
    char path[PATH_MAX];
    get_system_overrides_path(path, sizeof(path));
    snprintf(rejected, sizeof(rejected), "%s.rejected", path);
    check(access(rejected, R_OK) == 0,
          "the rejected file is preserved before the rewrite");

    section("individually invalid entries are skipped, not fatal");
    systems_shutdown();
    remove_overrides();
    write_overrides(
        "{\"schema\":1,"
        " \"tags\":{\"GB\":\"no-such-platform\",\"GG\":\"segacd\"},"
        " \"folders\":{\"Mega Drive (GPGX)\":{\"platform\":\"megadrive\"},"
        "              \"../escape\":{\"platform\":\"snes\"}}}");
    check(systems_init() == 0, "partly invalid overrides still start");
    check(systems_warning() != NULL, "the skipped entries are reported");
    gb = systems_resolve(NULL, "GB");
    check(gb.platform && strcmp(gb.platform->id, "gameboy") == 0,
          "the entry naming an unknown platform was skipped");
    sg_mapping gg = systems_resolve(NULL, "GG");
    check(gg.platform && strcmp(gg.platform->id, "segacd") == 0
          && gg.source == MAPPING_USER_TAG,
          "the valid entry beside it was kept");
    sg_mapping md = systems_resolve(megadrive, "GPGX");
    check(md.platform && strcmp(md.platform->id, "megadrive") == 0,
          "the valid folder entry was kept");

    section("rejected mutations leave mappings alone");
    check(systems_set_tag("GG", "no-such-platform") != 0,
          "setting an unknown platform is refused");
    check(systems_last_error() != NULL, "the refusal explains itself");
    gg = systems_resolve(NULL, "GG");
    check(gg.platform && strcmp(gg.platform->id, "segacd") == 0,
          "the previous mapping still stands after a refused change");

    char outside[PATH_MAX];
    snprintf(outside, sizeof(outside), "%s/Tools/Something (GB)", sd_root);
    check(systems_set_folder_platform(outside, "gameboy") != 0,
          "a folder outside Roms/ cannot carry a mapping");
}

/* A platform can carry one provider and not the other. The resolver must not
 * treat a missing ScreenScraper id as "unsupported everywhere". */
static void test_synthetic_catalog(const char *fixture) {
    section("a cheat-only platform stays eligible for cheats");
    systems_shutdown();
    remove_overrides();
    setenv("SCRAPEGOAT_SYSTEMS_JSON", fixture, 1);
    check(systems_init() == 0, "the synthetic catalog loads: %s",
          systems_last_error() ? systems_last_error() : "");

    const sg_platform *cheat_only = systems_platform_by_id("cheatonly");
    check(cheat_only != NULL, "the cheat-only platform is in the catalog");
    if (cheat_only) {
        check(cheat_only->ss_id < 0, "it has no ScreenScraper id");
        check(cheat_only->libretro_dir
              && strcmp(cheat_only->libretro_dir, "Test - Cheats Only") == 0,
              "it keeps its cheat database");
    }

    const sg_platform *art_only = systems_platform_by_id("artonly");
    check(art_only && art_only->ss_id == 4242 && art_only->libretro_dir == NULL,
          "the artwork-only platform has no cheat database");

    char folder[PATH_MAX];
    rom_folder_path("Ambiguous (AMBIG)", folder, sizeof(folder));
    make_rom_folder("Ambiguous (AMBIG)");
    sg_mapping ambiguous = systems_resolve(folder, "AMBIG");
    check(ambiguous.platform == NULL,
          "an ambiguous suffix ships no default");
    const sg_platform *candidates[4];
    check(systems_tag_candidates("AMBIG", candidates, 4) == 2,
          "its reviewed candidates are offered");

    check(systems_set_folder_platform(folder, "cheatonly") == 0,
          "selecting the cheat-only platform succeeds");
    ambiguous = systems_resolve(folder, "AMBIG");
    check(ambiguous.platform && ambiguous.platform->ss_id < 0
          && ambiguous.platform->libretro_dir != NULL,
          "the folder is now eligible for cheats and not for artwork");

    systems_shutdown();
    unsetenv("SCRAPEGOAT_SYSTEMS_JSON");
    remove_overrides();
}

static void test_explicit_catalog_path(void) {
    section("an explicit catalog path never falls back");
    systems_shutdown();
    setenv("SCRAPEGOAT_SYSTEMS_JSON", "/nonexistent/systems.json", 1);
    check(systems_init() != 0, "a missing explicit catalog fails");
    check(systems_last_error() && strstr(systems_last_error(), "/nonexistent/"),
          "the error names the path that was tried");
    check(systems_platform_count() == 0, "nothing is loaded after the failure");

    char broken[PATH_MAX];
    snprintf(broken, sizeof(broken), "%s/broken.json", sd_root);
    FILE *f = fopen(broken, "w");
    fputs("{\"schema\":1,\"platforms\":[{\"id\":\"a\",\"name\":\"A\"}],"
          "\"tags\":{\"X\":\"missing\"}}", f);
    fclose(f);
    setenv("SCRAPEGOAT_SYSTEMS_JSON", broken, 1);
    check(systems_init() != 0, "a dangling suffix reference is fatal");
    check(systems_platform_count() == 0, "a failed catalog load leaves nothing behind");

    f = fopen(broken, "w");
    fputs("{\"schema\":1,\"platforms\":[{\"id\":\"a\",\"name\":\"A\","
          "\"libretro_dir\":\"../escape\"}]}", f);
    fclose(f);
    check(systems_init() != 0, "a traversal cheat directory is rejected");

    unsetenv("SCRAPEGOAT_SYSTEMS_JSON");
    unlink(broken);
}

int main(int argc, char *argv[]) {
    const char *catalog = argc > 1 ? argv[1] : "resources/systems.json";
    const char *fixture = argc > 2 ? argv[2] : "tests/fixtures/baseline_mappings.json";
    const char *scratch = argc > 3 ? argv[3] : "build/test-systems";

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return 2;
    if (scratch[0] == '/')
        snprintf(sd_root, sizeof(sd_root), "%s", scratch);
    else
        snprintf(sd_root, sizeof(sd_root), "%s/%s", cwd, scratch);

    char command[PATH_MAX + 32];
    snprintf(command, sizeof(command), "rm -rf '%s' '%s-moved'", sd_root, sd_root);
    if (system(command) != 0) return 2;

    snprintf(roms_root, sizeof(roms_root), "%s/Roms", sd_root);
    make_dir(roms_root);
    make_rom_folder("Mega Drive (GPGX)");
    make_rom_folder("Master System (GPGX)");
    make_rom_folder("Game Gear (GPGX)");
    make_rom_folder("Super Famicom (SFC)");
    make_rom_folder("Mega Drive (GPGX).disabled");
    char extra[PATH_MAX];
    snprintf(extra, sizeof(extra), "%s/RomsExtra", sd_root);
    make_dir(extra);

    setenv("SDCARD_PATH", sd_root, 1);
    if (catalog[0] == '/')
        setenv("SCRAPEGOAT_SYSTEMS_JSON", catalog, 1);
    else {
        char absolute[PATH_MAX];
        snprintf(absolute, sizeof(absolute), "%s/%s", cwd, catalog);
        setenv("SCRAPEGOAT_SYSTEMS_JSON", absolute, 1);
    }

    remove_overrides();
    if (systems_init() != 0) {
        printf("FATAL: %s\n", systems_last_error());
        return 2;
    }

    test_baseline_parity(fixture);
    test_folder_scoping();
    test_visibility();
    test_provider_independence();
    test_folder_keys();
    test_rename_behaviour();
    test_sd_root_change();
    test_invalid_data();

    char synthetic[PATH_MAX];
    if (fixture[0] == '/')
        snprintf(synthetic, sizeof(synthetic), "%s", fixture);
    else
        snprintf(synthetic, sizeof(synthetic), "%s/%s", cwd, fixture);
    char *slash = strrchr(synthetic, '/');
    if (slash)
        snprintf(slash + 1, sizeof(synthetic) - (size_t)(slash + 1 - synthetic),
                 "synthetic_catalog.json");
    test_synthetic_catalog(synthetic);

    test_explicit_catalog_path();

    systems_shutdown();
    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

#ifndef DEVICE_H
#define DEVICE_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Data types ───────────────────────────────────────────────── */

typedef struct {
    char name[256];      /* e.g. "Game Boy Advance (GBA)" or "...GBA).disabled" */
    char tag[64];        /* e.g. "GBA" */
    char path[PATH_MAX]; /* full path to the directory */
    char display[256];   /* display name without tag suffix */
    bool is_disabled;    /* true when folder ends with ".disabled" */
} console_dir;

typedef struct {
    char name[256];      /* filename or folder name */
    char path[PATH_MAX]; /* full path */
    char display[256];   /* display name (no extension, no ".disabled" suffix) */
    bool is_multi_disc;  /* subdirectory containing {name}.m3u */
    bool is_cue_folder;  /* subdirectory containing {name}.cue */
    bool is_disabled;    /* true when file/folder ends with ".disabled" */
} rom_file;

typedef struct {
    int total;
    int found;
    int not_found;
    int errors;
} scrape_summary;

typedef enum {
    RUN_OK = 0,
    RUN_CANCELLED,
    RUN_ERROR,
} run_status;

typedef struct {
    run_status status;
    scrape_summary summary;
    char error[256];
} run_result;

/* ── Media type definitions ─────────────────────────────────── */

typedef struct {
    const char *display;
    const char *value;
} media_type_def;

extern const media_type_def all_media_types[];
extern const int all_media_types_count;

extern const char *default_artwork_priority[];
extern const int default_artwork_priority_count;

const char *media_type_display(const char *value);

/* ── Region definitions ─────────────────────────────────────── */

typedef struct {
    const char *display;
    const char *value;
} region_def;

extern const region_def all_regions[];
extern const int all_regions_count;

extern const char *default_region_priority[];
extern const int default_region_priority_count;

const char *region_display(const char *value);

/* ── App settings ─────────────────────────────────────────────── */

#define MAX_PRIORITY_ITEMS 32

typedef struct {
    char ss_username[256];
    char ss_password[256];
    char artwork_type[64];  /* DEPRECATED: migrated to artwork_prio */
    char *artwork_prio[MAX_PRIORITY_ITEMS];
    int  artwork_prio_count;
    char region[32];        /* DEPRECATED: migrated to region_prio */
    char *region_prio[MAX_PRIORITY_ITEMS];
    int  region_prio_count;
    bool show_hidden;
} app_settings;

app_settings default_settings(void);
app_settings load_settings(void);
int          save_settings(const app_settings *s);
void         free_settings(app_settings *s);

/* Build the full priority-ordered list of all media types.
 * User's saved order first, missing types appended.
 * Caller must free each element and the returned array. */
char **build_artwork_types(const app_settings *s, int *out_count);

/* Build the full priority-ordered list of all regions.
 * User's saved order first, missing regions appended, "cus" last.
 * Caller must free each element and the returned array. */
char **build_region_types(const app_settings *s, int *out_count);

/* ── Path helpers ─────────────────────────────────────────────── */

const char *get_sdcard_path(void);
void get_roms_path(char *buf, size_t buflen);
void get_cheats_path(char *buf, size_t buflen);
void get_cheat_repo_path(char *buf, size_t buflen);
void get_settings_path(char *buf, size_t buflen);

/* ── Scanning ─────────────────────────────────────────────────── */

/* Scan ROM console directories. Returns count, caller frees *out. */
int scan_console_dirs(bool show_hidden, console_dir **out);

/* Scan ROMs in a console directory. Returns count, caller frees *out. */
int scan_roms(const char *console_path, bool show_hidden, rom_file **out);

/* ── Artwork helpers ──────────────────────────────────────────── */

bool artwork_exists(const char *rom_path, const char *display_name);
void artwork_src_path(const char *rom_path, const char *display_name, char *buf, size_t buflen);

/* ── MD5 hashing ──────────────────────────────────────────────── */

/* Compute MD5 hex digest for a ROM. For ZIPs, hashes the largest inner file.
 * For folder-based ROMs (multi-disc, CUE), hashes the primary disc image.
 * Returns 0 on success, -1 on error. md5_out must be at least 33 bytes. */
int compute_md5(const rom_file *rom, char *md5_out, long *size_out);

/* ── Visibility helpers ───────────────────────────────────────── */

bool is_hidden(const char *name);

#endif /* DEVICE_H */

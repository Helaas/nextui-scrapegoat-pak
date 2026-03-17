#include "device.h"
#include "systems.h"
#include "md5.h"
#include "cJSON.h"
#include "miniz.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Media type definitions ──────────────────────────────── */

const media_type_def all_media_types[] = {
    {"Screenshot (title)",  "sstitle"},
    {"Screenshot (in-game)","ss"},
    {"Fan art",             "fanart"},
    {"Wheel",               "wheel"},
    {"Wheel (carbon)",      "wheel-carbon"},
    {"Wheel (steel)",       "wheel-steel"},
    {"Box art (2D)",        "box-2D"},
    {"Box art (3D)",        "box-3D"},
    {"Mix Recalbox V1",     "mixrbv1"},
    {"Mix Recalbox V2",     "mixrbv2"},
};
const int all_media_types_count = sizeof(all_media_types) / sizeof(all_media_types[0]);

const char *default_artwork_priority[] = {"box-2D", "box-3D", "mixrbv1", "ss"};
const int default_artwork_priority_count = 4;

const char *media_type_display(const char *value) {
    for (int i = 0; i < all_media_types_count; i++) {
        if (strcmp(all_media_types[i].value, value) == 0)
            return all_media_types[i].display;
    }
    return value;
}

/* ── Region definitions ──────────────────────────────────── */

const region_def all_regions[] = {
    {"World",         "wor"},
    {"USA",           "us"},
    {"Europe",        "eu"},
    {"Japan",         "jp"},
    {"France",        "fr"},
    {"Germany",       "de"},
    {"Spain",         "es"},
    {"Italy",         "it"},
    {"Portugal",      "pt"},
    {"ScreenScraper", "ss"},
};
const int all_regions_count = sizeof(all_regions) / sizeof(all_regions[0]);

const char *default_region_priority[] = {"us", "eu", "jp", "wor"};
const int default_region_priority_count = 4;

const char *region_display(const char *value) {
    for (int i = 0; i < all_regions_count; i++) {
        if (strcmp(all_regions[i].value, value) == 0)
            return all_regions[i].display;
    }
    return value;
}

/* ── Path helpers ────────────────────────────────────────── */

static const char *sdcard_path_cached = NULL;

const char *get_sdcard_path(void) {
    if (sdcard_path_cached)
        return sdcard_path_cached;

    const char *env = getenv("SDCARD_PATH");
    if (env && env[0]) {
        sdcard_path_cached = env;
        return sdcard_path_cached;
    }

#if defined(PLATFORM_MAC)
    static char mac_path[PATH_MAX];
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        snprintf(mac_path, sizeof(mac_path), "%s/mock_sdcard", cwd);
        sdcard_path_cached = mac_path;
    } else {
        sdcard_path_cached = "./mock_sdcard";
    }
#else
    sdcard_path_cached = "/mnt/SDCARD";
#endif
    return sdcard_path_cached;
}

void get_roms_path(char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s/Roms", get_sdcard_path());
}

void get_cheats_path(char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s/Cheats", get_sdcard_path());
}

void get_cheat_repo_path(char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s/.userdata/shared/ScrapeGoat/libretro-database",
             get_sdcard_path());
}

void get_settings_path(char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s/.userdata/shared/ScrapeGoat/settings.json",
             get_sdcard_path());
}

/* ── String utilities ────────────────────────────────────── */

static const char shortcut_marker[] = ".shortcut";

static bool is_shortcut_folder(const char *path) {
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    /* Check BOM or star prefix */
    if ((unsigned char)name[0] == 0xEF && (unsigned char)name[1] == 0xBB &&
        (unsigned char)name[2] == 0xBF)
        return true;
    if (name[0] == '\xe2' && name[1] == '\x98' && name[2] == '\x85' && name[3] == ' ')
        return true;

    char marker[PATH_MAX];
    snprintf(marker, sizeof(marker), "%s/%s", path, shortcut_marker);
    struct stat st;
    return stat(marker, &st) == 0;
}

static const char *extract_tag(const char *name) {
    static char tag_buf[64];
    const char *open = NULL;
    const char *close = NULL;

    /* Find the LAST '(' and ')' pair */
    for (const char *p = name; *p; p++) {
        if (*p == '(') open = p;
        if (*p == ')') close = p;
    }
    if (!open || !close || close <= open)
        return NULL;

    size_t len = (size_t)(close - open - 1);
    if (len == 0 || len >= sizeof(tag_buf))
        return NULL;

    /* Trim whitespace */
    const char *start = open + 1;
    while (start < close && *start == ' ') start++;
    const char *end = close - 1;
    while (end > start && *end == ' ') end--;

    len = (size_t)(end - start + 1);
    if (len == 0 || len >= sizeof(tag_buf))
        return NULL;

    memcpy(tag_buf, start, len);
    tag_buf[len] = '\0';
    return tag_buf;
}

static void extract_display_name(const char *name, char *buf, size_t buflen) {
    const char *open = NULL;
    for (const char *p = name; *p; p++) {
        if (*p == '(') open = p;
    }
    if (!open) {
        snprintf(buf, buflen, "%s", name);
        return;
    }
    size_t len = (size_t)(open - name);
    while (len > 0 && name[len - 1] == ' ') len--;
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, name, len);
    buf[len] = '\0';
}

static void strip_extension(const char *name, char *buf, size_t buflen) {
    const char *dot = strrchr(name, '.');
    if (dot && dot != name) {
        size_t extlen = strlen(dot);
        if (extlen >= 2 && extlen <= 5) {
            size_t len = (size_t)(dot - name);
            if (len >= buflen) len = buflen - 1;
            memcpy(buf, name, len);
            buf[len] = '\0';
            return;
        }
    }
    snprintf(buf, buflen, "%s", name);
}

bool is_hidden(const char *name) {
    if (name[0] == '.')
        return true;
    size_t len = strlen(name);
    if (len > 9 && strcmp(name + len - 9, ".disabled") == 0)
        return true;
    if (strcmp(name, "map.txt") == 0)
        return true;
    return false;
}

static bool is_mac_dotfile(const char *name) {
    if (name[0] != '.')
        return false;
    return extract_tag(name) == NULL;
}

static bool dir_has_visible_content(const char *path) {
    DIR *d = opendir(path);
    if (!d) return false;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' ||
            (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
            continue;
        if (!is_hidden(entry->d_name)) {
            closedir(d);
            return true;
        }
    }
    closedir(d);
    return false;
}

/* ── Scanning ────────────────────────────────────────────── */

static int console_cmp(const void *a, const void *b) {
    const console_dir *ca = (const console_dir *)a;
    const console_dir *cb = (const console_dir *)b;
    /* Case-insensitive sort by display name */
    return strcasecmp(ca->display, cb->display);
}

int scan_console_dirs(bool show_hidden, console_dir **out) {
    char roms_path[PATH_MAX];
    get_roms_path(roms_path, sizeof(roms_path));

    DIR *d = opendir(roms_path);
    if (!d) {
        *out = NULL;
        return 0;
    }

    int count = 0;
    int capacity = 64;
    console_dir *consoles = malloc(sizeof(console_dir) * (size_t)capacity);

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' ||
            (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
            continue;

        /* Only directories */
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", roms_path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        if (is_shortcut_folder(full_path))
            continue;

        const char *name = entry->d_name;

        if (!show_hidden) {
            if (is_hidden(name))
                continue;
            if (!dir_has_visible_content(full_path))
                continue;
        } else {
            if (is_mac_dotfile(name) || strcmp(name, "map.txt") == 0)
                continue;
        }

        /* Strip .disabled suffix before extracting tag/display */
        char base_name[256];
        snprintf(base_name, sizeof(base_name), "%s", name);
        bool disabled = false;
        size_t nlen = strlen(base_name);
        if (nlen > 9 && strcmp(base_name + nlen - 9, ".disabled") == 0) {
            disabled = true;
            base_name[nlen - 9] = '\0';
        }

        const char *tag = extract_tag(base_name);
        if (!tag)
            continue;

        if (count >= capacity) {
            capacity *= 2;
            consoles = realloc(consoles, sizeof(console_dir) * (size_t)capacity);
        }

        console_dir *c = &consoles[count++];
        snprintf(c->name, sizeof(c->name), "%s", name);
        snprintf(c->tag, sizeof(c->tag), "%s", tag);
        snprintf(c->path, sizeof(c->path), "%s", full_path);
        extract_display_name(base_name, c->display, sizeof(c->display));
        c->is_disabled = disabled;
    }
    closedir(d);

    qsort(consoles, (size_t)count, sizeof(console_dir), console_cmp);
    *out = consoles;
    return count;
}

static int rom_cmp(const void *a, const void *b) {
    const rom_file *ra = (const rom_file *)a;
    const rom_file *rb = (const rom_file *)b;
    return strcasecmp(ra->display, rb->display);
}

/* Internal recursive scanner */
static int scan_roms_internal(const char *dir_path, bool show_hidden,
                              rom_file **out, int *count, int *capacity) {
    DIR *d = opendir(dir_path);
    if (!d) return -1;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        const char *name = entry->d_name;
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            continue;

        if (!show_hidden) {
            if (is_hidden(name))
                continue;
        } else {
            if (name[0] == '.' || strcmp(name, "map.txt") == 0)
                continue;
        }

        /* Strip .disabled suffix */
        char base_name[256];
        snprintf(base_name, sizeof(base_name), "%s", name);
        bool disabled = false;
        size_t nlen = strlen(base_name);
        if (nlen > 9 && strcmp(base_name + nlen - 9, ".disabled") == 0) {
            disabled = true;
            base_name[nlen - 9] = '\0';
        }

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, name);  

        struct stat st;
        if (stat(full_path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            /* Check for multi-disc (.m3u) */
            char m3u_check[PATH_MAX];
            snprintf(m3u_check, sizeof(m3u_check), "%s/%s.m3u", full_path, base_name);
            struct stat m3u_st;

            char cue_check[PATH_MAX];
            snprintf(cue_check, sizeof(cue_check), "%s/%s.cue", full_path, base_name);
            struct stat cue_st;

            if (*count >= *capacity) {
                *capacity *= 2;
                *out = realloc(*out, sizeof(rom_file) * (size_t)(*capacity));
            }

            if (stat(m3u_check, &m3u_st) == 0) {
                rom_file *r = &(*out)[(*count)++];
                snprintf(r->name, sizeof(r->name), "%s", name);
                snprintf(r->path, sizeof(r->path), "%s", full_path);
                snprintf(r->display, sizeof(r->display), "%s", base_name);
                r->is_multi_disc = true;
                r->is_cue_folder = false;
                r->is_disabled = disabled;
            } else if (stat(cue_check, &cue_st) == 0) {
                rom_file *r = &(*out)[(*count)++];
                snprintf(r->name, sizeof(r->name), "%s", name);
                snprintf(r->path, sizeof(r->path), "%s", full_path);
                snprintf(r->display, sizeof(r->display), "%s", base_name);
                r->is_multi_disc = false;
                r->is_cue_folder = true;
                r->is_disabled = disabled;
            } else {
                /* Plain subfolder — recurse */
                scan_roms_internal(full_path, show_hidden, out, count, capacity);
            }
            continue;
        }

        /* Regular file */
        if (*count >= *capacity) {
            *capacity *= 2;
            *out = realloc(*out, sizeof(rom_file) * (size_t)(*capacity));
        }
        rom_file *r = &(*out)[(*count)++];
        snprintf(r->name, sizeof(r->name), "%s", name);
        snprintf(r->path, sizeof(r->path), "%s", full_path);
        strip_extension(base_name, r->display, sizeof(r->display));
        r->is_multi_disc = false;
        r->is_cue_folder = false;
        r->is_disabled = disabled;
    }
    closedir(d);
    return 0;
}

int scan_roms(const char *console_path, bool show_hidden, rom_file **out) {
    int count = 0;
    int capacity = 256;
    *out = malloc(sizeof(rom_file) * (size_t)capacity);

    scan_roms_internal(console_path, show_hidden, out, &count, &capacity);
    qsort(*out, (size_t)count, sizeof(rom_file), rom_cmp);
    return count;
}

/* ── Artwork helpers ─────────────────────────────────────── */

bool artwork_exists(const char *rom_path, const char *display_name) {
    char path[PATH_MAX];
    artwork_src_path(rom_path, display_name, path, sizeof(path));
    struct stat st;
    return stat(path, &st) == 0;
}

void artwork_src_path(const char *rom_path, const char *display_name,
                      char *buf, size_t buflen) {
    /* Find the directory containing the ROM */
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", rom_path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';

    /* For folder-based ROMs, dir IS the ROM path's parent */
    struct stat st;
    if (stat(rom_path, &st) == 0 && S_ISDIR(st.st_mode))
        snprintf(dir, sizeof(dir), "%s", rom_path);

    /* Actually, Go code uses filepath.Dir(romPath) which for a dir gives its parent.
     * Let's fix: for folder ROMs the path IS the folder, Dir gives parent. */
    snprintf(dir, sizeof(dir), "%s", rom_path);
    slash = strrchr(dir, '/');
    if (slash) *slash = '\0';

    snprintf(buf, buflen, "%s/.media/%s.png", dir, display_name);
}

/* ── MD5 hashing ─────────────────────────────────────────── */

static int find_primary_rom_file(const char *folder_path, bool is_cue,
                                 char *result, size_t result_len) {
    DIR *d = opendir(folder_path);
    if (!d) return -1;

    if (is_cue) {
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            size_t nlen = strlen(entry->d_name);
            if (nlen < 4) continue;
            const char *ext = entry->d_name + nlen - 4;
            if (strcasecmp(ext, ".cue") != 0) continue;

            char cue_path[PATH_MAX];
            snprintf(cue_path, sizeof(cue_path), "%s/%s", folder_path, entry->d_name);

            FILE *f = fopen(cue_path, "r");
            if (!f) continue;

            char line[1024];
            while (fgets(line, sizeof(line), f)) {
                /* Parse FILE "filename.bin" BINARY */
                char *trimmed = line;
                while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

                char upper_prefix[8];
                size_t copy_len = strlen(trimmed) < 5 ? strlen(trimmed) : 5;
                memcpy(upper_prefix, trimmed, copy_len);
                upper_prefix[copy_len] = '\0';
                for (size_t k = 0; k < copy_len; k++)
                    upper_prefix[k] = (char)toupper((unsigned char)upper_prefix[k]);

                if (strncmp(upper_prefix, "FILE ", 5) != 0)
                    continue;

                /* Extract filename between quotes or first token */
                char *start = trimmed + 5;
                while (*start == ' ') start++;
                char fname[256] = {0};
                if (*start == '"') {
                    start++;
                    char *end = strchr(start, '"');
                    if (end) {
                        size_t flen = (size_t)(end - start);
                        if (flen >= sizeof(fname)) flen = sizeof(fname) - 1;
                        memcpy(fname, start, flen);
                        fname[flen] = '\0';
                    }
                } else {
                    char *end = start;
                    while (*end && *end != ' ' && *end != '\t') end++;
                    size_t flen = (size_t)(end - start);
                    if (flen >= sizeof(fname)) flen = sizeof(fname) - 1;
                    memcpy(fname, start, flen);
                    fname[flen] = '\0';
                }

                if (fname[0]) {
                    char candidate[PATH_MAX];
                    snprintf(candidate, sizeof(candidate), "%s/%s", folder_path, fname);
                    struct stat cst;
                    if (stat(candidate, &cst) == 0) {
                        fclose(f);
                        closedir(d);
                        snprintf(result, result_len, "%s", candidate);
                        return 0;
                    }
                }
            }
            fclose(f);
        }
    }

    closedir(d);

    /* Fallback: find first disc image by extension priority */
    const char *prio_exts[] = {".chd", ".iso", ".bin", ".img"};
    for (int e = 0; e < 4; e++) {
        d = opendir(folder_path);
        if (!d) return -1;
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            size_t nlen = strlen(entry->d_name);
            size_t elen = strlen(prio_exts[e]);
            if (nlen < elen) continue;
            if (strcasecmp(entry->d_name + nlen - elen, prio_exts[e]) == 0) {
                snprintf(result, result_len, "%s/%s", folder_path, entry->d_name);
                closedir(d);
                return 0;
            }
        }
        closedir(d);
    }

    return -1;
}

static int compute_md5_file(const char *path, char *md5_out, long *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    md5_ctx ctx;
    md5_init(&ctx);

    unsigned char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        md5_update(&ctx, buf, n);
    }
    fclose(f);

    unsigned char digest[16];
    md5_final(&ctx, digest);

    for (int i = 0; i < 16; i++)
        sprintf(md5_out + i * 2, "%02x", digest[i]);
    md5_out[32] = '\0';

    *size_out = file_size;
    return 0;
}

static int compute_md5_zip(const char *zip_path, char *md5_out, long *size_out) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, zip_path, 0))
        return -1;

    /* Find the largest file in the archive */
    int num_files = (int)mz_zip_reader_get_num_files(&zip);
    int largest_idx = -1;
    mz_uint64 largest_size = 0;

    for (int i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, (mz_uint)i, &file_stat))
            continue;
        if (file_stat.m_is_directory)
            continue;
        if (file_stat.m_uncomp_size > largest_size) {
            largest_size = file_stat.m_uncomp_size;
            largest_idx = i;
        }
    }

    if (largest_idx < 0) {
        mz_zip_reader_end(&zip);
        return -1;
    }

    /* Extract to memory and compute MD5 */
    size_t uncomp_size = 0;
    void *data = mz_zip_reader_extract_to_heap(&zip, (mz_uint)largest_idx, &uncomp_size, 0);
    mz_zip_reader_end(&zip);

    if (!data)
        return -1;

    md5_ctx ctx;
    md5_init(&ctx);
    md5_update(&ctx, (const unsigned char *)data, uncomp_size);
    unsigned char digest[16];
    md5_final(&ctx, digest);
    free(data);

    for (int i = 0; i < 16; i++)
        sprintf(md5_out + i * 2, "%02x", digest[i]);
    md5_out[32] = '\0';

    *size_out = (long)uncomp_size;
    return 0;
}

int compute_md5(const rom_file *rom, char *md5_out, long *size_out) {
    char target_path[PATH_MAX];
    snprintf(target_path, sizeof(target_path), "%s", rom->path);

    if (rom->is_multi_disc || rom->is_cue_folder) {
        if (find_primary_rom_file(rom->path, rom->is_cue_folder,
                                  target_path, sizeof(target_path)) != 0)
            return -1;
    }

    /* Check for .zip extension */
    size_t len = strlen(target_path);
    if (len > 4 && strcasecmp(target_path + len - 4, ".zip") == 0)
        return compute_md5_zip(target_path, md5_out, size_out);

    return compute_md5_file(target_path, md5_out, size_out);
}

/* ── Settings ────────────────────────────────────────────── */

static char *my_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    memcpy(dup, s, len + 1);
    return dup;
}

static void ensure_dir_exists(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

app_settings default_settings(void) {
    app_settings s = {0};
    for (int i = 0; i < default_artwork_priority_count && i < MAX_PRIORITY_ITEMS; i++)
        s.artwork_prio[i] = my_strdup(default_artwork_priority[i]);
    s.artwork_prio_count = default_artwork_priority_count;

    for (int i = 0; i < default_region_priority_count && i < MAX_PRIORITY_ITEMS; i++)
        s.region_prio[i] = my_strdup(default_region_priority[i]);
    s.region_prio_count = default_region_priority_count;

    return s;
}

void free_settings(app_settings *s) {
    for (int i = 0; i < s->artwork_prio_count; i++)
        free(s->artwork_prio[i]);
    s->artwork_prio_count = 0;
    for (int i = 0; i < s->region_prio_count; i++)
        free(s->region_prio[i]);
    s->region_prio_count = 0;
}

app_settings load_settings(void) {
    app_settings defaults = default_settings();
    char path[PATH_MAX];
    get_settings_path(path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return defaults;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc((size_t)fsize + 1);
    if (!data) { fclose(f); return defaults; }
    fread(data, 1, (size_t)fsize, f);
    data[fsize] = '\0';
    fclose(f);

    cJSON *json = cJSON_Parse(data);
    free(data);
    if (!json) return defaults;

    app_settings s = {0};

    cJSON *item;
    if ((item = cJSON_GetObjectItem(json, "ss_username")) && cJSON_IsString(item))
        snprintf(s.ss_username, sizeof(s.ss_username), "%s", item->valuestring);
    if ((item = cJSON_GetObjectItem(json, "ss_password")) && cJSON_IsString(item))
        snprintf(s.ss_password, sizeof(s.ss_password), "%s", item->valuestring);
    if ((item = cJSON_GetObjectItem(json, "show_hidden")) && cJSON_IsBool(item))
        s.show_hidden = cJSON_IsTrue(item);

    /* Read artwork_priority array */
    cJSON *arr = cJSON_GetObjectItem(json, "artwork_priority");
    if (arr && cJSON_IsArray(arr)) {
        int n = cJSON_GetArraySize(arr);
        for (int i = 0; i < n && s.artwork_prio_count < MAX_PRIORITY_ITEMS; i++) {
            cJSON *el = cJSON_GetArrayItem(arr, i);
            if (cJSON_IsString(el))
                s.artwork_prio[s.artwork_prio_count++] = my_strdup(el->valuestring);
        }
    }

    /* Migrate old single artwork_type → priority list */
    if (s.artwork_prio_count == 0) {
        if ((item = cJSON_GetObjectItem(json, "artwork_type")) && cJSON_IsString(item) && item->valuestring[0]) {
            s.artwork_prio[s.artwork_prio_count++] = my_strdup(item->valuestring);
        } else {
            for (int i = 0; i < default_artwork_priority_count; i++)
                s.artwork_prio[s.artwork_prio_count++] = my_strdup(default_artwork_priority[i]);
        }
    }

    /* Read region_priority array */
    arr = cJSON_GetObjectItem(json, "region_priority");
    if (arr && cJSON_IsArray(arr)) {
        int n = cJSON_GetArraySize(arr);
        for (int i = 0; i < n && s.region_prio_count < MAX_PRIORITY_ITEMS; i++) {
            cJSON *el = cJSON_GetArrayItem(arr, i);
            if (cJSON_IsString(el))
                s.region_prio[s.region_prio_count++] = my_strdup(el->valuestring);
        }
    }

    /* Migrate old single region → priority list */
    if (s.region_prio_count == 0) {
        if ((item = cJSON_GetObjectItem(json, "region")) && cJSON_IsString(item) && item->valuestring[0]) {
            s.region_prio[s.region_prio_count++] = my_strdup(item->valuestring);
        } else {
            for (int i = 0; i < default_region_priority_count; i++)
                s.region_prio[s.region_prio_count++] = my_strdup(default_region_priority[i]);
        }
    }

    cJSON_Delete(json);
    free_settings(&defaults);
    return s;
}

int save_settings(const app_settings *s) {
    cJSON *json = cJSON_CreateObject();
    if (!json) return -1;

    cJSON_AddStringToObject(json, "ss_username", s->ss_username);
    cJSON_AddStringToObject(json, "ss_password", s->ss_password);
    cJSON_AddBoolToObject(json, "show_hidden", s->show_hidden);

    cJSON *art_arr = cJSON_AddArrayToObject(json, "artwork_priority");
    for (int i = 0; i < s->artwork_prio_count; i++)
        cJSON_AddItemToArray(art_arr, cJSON_CreateString(s->artwork_prio[i]));

    cJSON *reg_arr = cJSON_AddArrayToObject(json, "region_priority");
    for (int i = 0; i < s->region_prio_count; i++)
        cJSON_AddItemToArray(reg_arr, cJSON_CreateString(s->region_prio[i]));

    char *str = cJSON_Print(json);
    cJSON_Delete(json);
    if (!str) return -1;

    char path[PATH_MAX];
    get_settings_path(path, sizeof(path));

    /* Ensure parent directory exists */
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        ensure_dir_exists(dir);
    }

    FILE *f = fopen(path, "w");
    if (!f) { free(str); return -1; }
    fputs(str, f);
    fclose(f);
    free(str);
    return 0;
}

/* ── Priority list builders ──────────────────────────────── */

static bool str_in_list(const char *s, char **list, int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(list[i], s) == 0) return true;
    }
    return false;
}

char **build_artwork_types(const app_settings *s, int *out_count) {
    char **result = malloc(sizeof(char *) * (size_t)(all_media_types_count + MAX_PRIORITY_ITEMS));
    int n = 0;

    /* Start with user's saved order */
    if (s->artwork_prio_count > 0) {
        for (int i = 0; i < s->artwork_prio_count; i++)
            result[n++] = my_strdup(s->artwork_prio[i]);
    } else {
        for (int i = 0; i < default_artwork_priority_count; i++)
            result[n++] = my_strdup(default_artwork_priority[i]);
    }

    /* Append any missing types */
    for (int i = 0; i < all_media_types_count; i++) {
        if (!str_in_list(all_media_types[i].value, result, n))
            result[n++] = my_strdup(all_media_types[i].value);
    }

    *out_count = n;
    return result;
}

char **build_region_types(const app_settings *s, int *out_count) {
    char **result = malloc(sizeof(char *) * (size_t)(all_regions_count + MAX_PRIORITY_ITEMS + 1));
    int n = 0;

    /* Start with user's saved order */
    if (s->region_prio_count > 0) {
        for (int i = 0; i < s->region_prio_count; i++)
            result[n++] = my_strdup(s->region_prio[i]);
    } else {
        for (int i = 0; i < default_region_priority_count; i++)
            result[n++] = my_strdup(default_region_priority[i]);
    }

    /* Append any missing regions */
    for (int i = 0; i < all_regions_count; i++) {
        if (!str_in_list(all_regions[i].value, result, n))
            result[n++] = my_strdup(all_regions[i].value);
    }

    /* "cus" always last as automatic catch-all */
    if (!str_in_list("cus", result, n))
        result[n++] = my_strdup("cus");

    *out_count = n;
    return result;
}

#include "systems.h"

#include "cJSON.h"
#include "device.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

/* systems.c — runtime platform catalog, folder-scoped resolution, and
 * persistence of the user's mapping choices. See systems.h for the contract. */

#define CATALOG_SCHEMA 1
#define MAX_TAG_LEN 64
#define MAX_ID_LEN 128
#define MAX_KEY_LEN 512
#define MAX_DIR_LEN 200

/* ── Catalog storage ──────────────────────────────────────────── */

typedef struct {
    sg_platform pub;   /* first member: &rec->pub is the public handle */
    char **aliases;
    int alias_count;
} platform_rec;

typedef struct {
    char *tag;
    int platform;      /* index into platforms */
} tag_default;

typedef struct {
    char *tag;
    int *platforms;
    int count;
} tag_candidate_set;

/* ── Override storage ─────────────────────────────────────────── */

typedef struct {
    char *tag;
    char *platform_id;
} tag_ov;

typedef struct {
    char *key;
    char *platform_id;   /* NULL when the entry only hides the folder */
    bool hidden;
} folder_ov;

typedef struct {
    tag_ov *tags;
    int tag_count;
    folder_ov *folders;
    int folder_count;
} override_set;

static platform_rec *platforms;
static int platform_count;
static tag_default *tag_defaults;
static int tag_default_count;
static tag_candidate_set *tag_candidates;
static int tag_candidate_count;

static override_set overrides;

static bool initialized;
static char last_error[512];
static char warning_message[512];
static char overrides_path[PATH_MAX];
static bool overrides_need_backup;

/* ── Small helpers ────────────────────────────────────────────── */

static void set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, args);
    va_end(args);
}

static void set_warning(const char *fmt, ...) {
    if (warning_message[0])
        return;   /* keep the first problem; it is the most specific */
    va_list args;
    va_start(args, fmt);
    vsnprintf(warning_message, sizeof(warning_message), fmt, args);
    va_end(args);
}

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

static bool str_eq(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static bool contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0])
        return false;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i]
               && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen)
            return true;
    }
    return false;
}

static bool equals_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* True when the object has two members with the same name, which would make
 * precedence depend on parser order. */
static bool has_duplicate_keys(const cJSON *object) {
    for (const cJSON *a = object ? object->child : NULL; a; a = a->next) {
        if (!a->string) continue;
        for (const cJSON *b = a->next; b; b = b->next) {
            if (b->string && strcmp(a->string, b->string) == 0)
                return true;
        }
    }
    return false;
}

static char *read_file(const char *path, long *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0 || size > 32L * 1024 * 1024) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';
    if (size_out) *size_out = (long)read;
    return buf;
}

/* ── Catalog loading ──────────────────────────────────────────── */

static void free_catalog(void) {
    for (int i = 0; i < platform_count; i++) {
        free((char *)platforms[i].pub.id);
        free((char *)platforms[i].pub.name);
        free((char *)platforms[i].pub.libretro_dir);
        for (int a = 0; a < platforms[i].alias_count; a++)
            free(platforms[i].aliases[a]);
        free(platforms[i].aliases);
    }
    free(platforms);
    platforms = NULL;
    platform_count = 0;

    for (int i = 0; i < tag_default_count; i++)
        free(tag_defaults[i].tag);
    free(tag_defaults);
    tag_defaults = NULL;
    tag_default_count = 0;

    for (int i = 0; i < tag_candidate_count; i++) {
        free(tag_candidates[i].tag);
        free(tag_candidates[i].platforms);
    }
    free(tag_candidates);
    tag_candidates = NULL;
    tag_candidate_count = 0;
}

static int platform_index_by_id(const char *id) {
    for (int i = 0; i < platform_count; i++) {
        if (str_eq(platforms[i].pub.id, id))
            return i;
    }
    return -1;
}

/* A cheat directory is one path component, never a traversal. */
static bool valid_provider_dir(const char *dir) {
    if (!dir || !dir[0] || strlen(dir) > MAX_DIR_LEN)
        return false;
    if (strchr(dir, '/') || strchr(dir, '\\'))
        return false;
    return strcmp(dir, ".") != 0 && strcmp(dir, "..") != 0;
}

static bool load_platforms(const cJSON *root, const char *path) {
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "platforms");
    if (!cJSON_IsArray(array)) {
        set_error("%s: \"platforms\" is missing or not an array", path);
        return false;
    }
    int count = cJSON_GetArraySize(array);
    if (count <= 0) {
        set_error("%s: the catalog has no platforms", path);
        return false;
    }
    platforms = calloc((size_t)count, sizeof(*platforms));
    if (!platforms) {
        set_error("%s: out of memory reading %d platforms", path, count);
        return false;
    }

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsObject(item)) {
            set_error("%s: platform %d is not an object", path, platform_count);
            return false;
        }
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (!cJSON_IsString(id) || !id->valuestring[0]
            || strlen(id->valuestring) > MAX_ID_LEN) {
            set_error("%s: platform %d has no usable \"id\"", path, platform_count);
            return false;
        }
        if (!cJSON_IsString(name) || !name->valuestring[0]) {
            set_error("%s: platform \"%s\" has no \"name\"", path, id->valuestring);
            return false;
        }
        if (platform_index_by_id(id->valuestring) >= 0) {
            set_error("%s: platform id \"%s\" appears twice", path, id->valuestring);
            return false;
        }

        platform_rec *rec = &platforms[platform_count];
        rec->pub.ss_id = -1;
        rec->pub.id = dup_str(id->valuestring);
        rec->pub.name = dup_str(name->valuestring);
        if (!rec->pub.id || !rec->pub.name) {
            set_error("%s: out of memory", path);
            return false;
        }
        platform_count++;

        const cJSON *ss = cJSON_GetObjectItemCaseSensitive(item, "ss_id");
        if (ss && !cJSON_IsNull(ss)) {
            if (!cJSON_IsNumber(ss) || ss->valuedouble < 1
                || ss->valuedouble != (double)(int)ss->valuedouble) {
                set_error("%s: platform \"%s\" has a non-positive ScreenScraper id",
                          path, rec->pub.id);
                return false;
            }
            rec->pub.ss_id = (int)ss->valuedouble;
        }

        const cJSON *dir = cJSON_GetObjectItemCaseSensitive(item, "libretro_dir");
        if (dir && !cJSON_IsNull(dir)) {
            if (!cJSON_IsString(dir) || !valid_provider_dir(dir->valuestring)) {
                set_error("%s: platform \"%s\" has an unusable cheat directory",
                          path, rec->pub.id);
                return false;
            }
            rec->pub.libretro_dir = dup_str(dir->valuestring);
            if (!rec->pub.libretro_dir) {
                set_error("%s: out of memory", path);
                return false;
            }
        }

        const cJSON *aliases = cJSON_GetObjectItemCaseSensitive(item, "aliases");
        if (aliases && !cJSON_IsNull(aliases)) {
            if (!cJSON_IsArray(aliases)) {
                set_error("%s: platform \"%s\" has a non-array \"aliases\"",
                          path, rec->pub.id);
                return false;
            }
            int alias_total = cJSON_GetArraySize(aliases);
            if (alias_total > 0) {
                rec->aliases = calloc((size_t)alias_total, sizeof(char *));
                if (!rec->aliases) {
                    set_error("%s: out of memory", path);
                    return false;
                }
                const cJSON *alias = NULL;
                cJSON_ArrayForEach(alias, aliases) {
                    if (!cJSON_IsString(alias) || !alias->valuestring[0])
                        continue;
                    rec->aliases[rec->alias_count] = dup_str(alias->valuestring);
                    if (!rec->aliases[rec->alias_count]) {
                        set_error("%s: out of memory", path);
                        return false;
                    }
                    rec->alias_count++;
                }
            }
        }
    }
    return true;
}

static bool load_tag_defaults(const cJSON *root, const char *path) {
    const cJSON *object = cJSON_GetObjectItemCaseSensitive(root, "tags");
    if (!object || cJSON_IsNull(object))
        return true;
    if (!cJSON_IsObject(object)) {
        set_error("%s: \"tags\" is not an object", path);
        return false;
    }
    if (has_duplicate_keys(object)) {
        set_error("%s: \"tags\" repeats a suffix, so its default is ambiguous", path);
        return false;
    }
    int count = cJSON_GetArraySize(object);
    if (count == 0)
        return true;
    tag_defaults = calloc((size_t)count, sizeof(*tag_defaults));
    if (!tag_defaults) {
        set_error("%s: out of memory", path);
        return false;
    }

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, object) {
        if (!item->string || !item->string[0] || strlen(item->string) > MAX_TAG_LEN) {
            set_error("%s: \"tags\" has an unusable suffix key", path);
            return false;
        }
        if (!cJSON_IsString(item)) {
            set_error("%s: suffix \"%s\" does not name a platform", path, item->string);
            return false;
        }
        int index = platform_index_by_id(item->valuestring);
        if (index < 0) {
            set_error("%s: suffix \"%s\" names unknown platform \"%s\"",
                      path, item->string, item->valuestring);
            return false;
        }
        tag_defaults[tag_default_count].tag = dup_str(item->string);
        if (!tag_defaults[tag_default_count].tag) {
            set_error("%s: out of memory", path);
            return false;
        }
        tag_defaults[tag_default_count].platform = index;
        tag_default_count++;
    }
    return true;
}

static bool load_tag_candidates(const cJSON *root, const char *path) {
    const cJSON *object = cJSON_GetObjectItemCaseSensitive(root, "tag_candidates");
    if (!object || cJSON_IsNull(object))
        return true;
    if (!cJSON_IsObject(object)) {
        set_error("%s: \"tag_candidates\" is not an object", path);
        return false;
    }
    if (has_duplicate_keys(object)) {
        set_error("%s: \"tag_candidates\" repeats a suffix", path);
        return false;
    }
    int count = cJSON_GetArraySize(object);
    if (count == 0)
        return true;
    tag_candidates = calloc((size_t)count, sizeof(*tag_candidates));
    if (!tag_candidates) {
        set_error("%s: out of memory", path);
        return false;
    }

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, object) {
        if (!item->string || !item->string[0] || strlen(item->string) > MAX_TAG_LEN) {
            set_error("%s: \"tag_candidates\" has an unusable suffix key", path);
            return false;
        }
        if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) == 0) {
            set_error("%s: suffix \"%s\" has an empty candidate list",
                      path, item->string);
            return false;
        }
        tag_candidate_set *set = &tag_candidates[tag_candidate_count];
        set->platforms = calloc((size_t)cJSON_GetArraySize(item), sizeof(int));
        set->tag = dup_str(item->string);
        if (!set->platforms || !set->tag) {
            set_error("%s: out of memory", path);
            return false;
        }
        tag_candidate_count++;

        const cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, item) {
            if (!cJSON_IsString(entry)) {
                set_error("%s: suffix \"%s\" has a non-string candidate",
                          path, set->tag);
                return false;
            }
            int index = platform_index_by_id(entry->valuestring);
            if (index < 0) {
                set_error("%s: suffix \"%s\" offers unknown platform \"%s\"",
                          path, set->tag, entry->valuestring);
                return false;
            }
            set->platforms[set->count++] = index;
        }
    }
    return true;
}

/* Choose the catalog file. An explicit path is used exactly as given; a parse
 * failure is never a reason to fall back to another file. */
static bool resolve_catalog_path(char *buf, size_t buflen) {
    const char *explicit_path = getenv("SCRAPEGOAT_SYSTEMS_JSON");
    if (explicit_path && explicit_path[0]) {
        if (strlen(explicit_path) >= buflen) {
            set_error("SCRAPEGOAT_SYSTEMS_JSON is too long");
            return false;
        }
        snprintf(buf, buflen, "%s", explicit_path);
        if (access(buf, R_OK) != 0) {
            set_error("SCRAPEGOAT_SYSTEMS_JSON points at %s, which cannot be read",
                      buf);
            return false;
        }
        return true;
    }

    char exe_dir[PATH_MAX];
    char beside[PATH_MAX];
    beside[0] = '\0';
    if (get_executable_dir(exe_dir, sizeof(exe_dir)) == 0) {
        snprintf(beside, sizeof(beside), "%s/resources/systems.json", exe_dir);
        if (access(beside, R_OK) == 0) {
            snprintf(buf, buflen, "%s", beside);
            return true;
        }
    }

#ifdef PLATFORM_MAC
    /* Development convenience: run from the repository root. */
    const char *repo = "resources/systems.json";
    if (access(repo, R_OK) == 0) {
        snprintf(buf, buflen, "%s", repo);
        return true;
    }
    set_error("no platform catalog: tried %s and ./resources/systems.json",
              beside[0] ? beside : "<executable directory>");
#else
    set_error("no platform catalog at %s",
              beside[0] ? beside : "<executable directory>/resources/systems.json");
#endif
    return false;
}

static bool load_catalog(void) {
    char path[PATH_MAX];
    if (!resolve_catalog_path(path, sizeof(path)))
        return false;

    char *text = read_file(path, NULL);
    if (!text) {
        set_error("%s could not be read: %s", path, strerror(errno));
        return false;
    }
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        set_error("%s is not valid JSON", path);
        return false;
    }

    bool ok = false;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    if (!cJSON_IsNumber(schema) || (int)schema->valuedouble != CATALOG_SCHEMA) {
        set_error("%s has schema %s, expected %d", path,
                  cJSON_IsNumber(schema) ? "an unsupported version" : "none",
                  CATALOG_SCHEMA);
    } else if (has_duplicate_keys(root)) {
        set_error("%s repeats a top-level key", path);
    } else {
        ok = load_platforms(root, path)
             && load_tag_defaults(root, path)
             && load_tag_candidates(root, path);
    }
    cJSON_Delete(root);
    if (!ok)
        free_catalog();   /* initialize all or nothing */
    return ok;
}

/* ── Folder keys ──────────────────────────────────────────────── */

static void normalize_path(const char *in, char *out, size_t outlen) {
    size_t n = 0;
    for (size_t i = 0; in[i] && n + 1 < outlen; i++) {
        if (in[i] == '/' && n > 0 && out[n - 1] == '/')
            continue;
        out[n++] = in[i];
    }
    while (n > 1 && out[n - 1] == '/')
        n--;
    out[n] = '\0';
}

static bool key_is_safe(const char *key) {
    if (!key || !key[0] || key[0] == '/')
        return false;
    const char *start = key;
    for (;;) {
        const char *slash = strchr(start, '/');
        size_t len = slash ? (size_t)(slash - start) : strlen(start);
        if (len == 0)
            return false;
        if ((len == 1 && start[0] == '.')
            || (len == 2 && start[0] == '.' && start[1] == '.'))
            return false;
        if (!slash)
            break;
        start = slash + 1;
    }
    return true;
}

static bool strip_root(const char *root, const char *path,
                       char *buf, size_t buflen) {
    size_t root_len = strlen(root);
    if (root_len == 0 || strncmp(path, root, root_len) != 0)
        return false;
    const char *rest = path + root_len;
    if (rest[0] != '/')
        return false;   /* "Roms2/..." must not match "Roms" */
    rest++;
    if (strlen(rest) >= buflen)
        return false;
    snprintf(buf, buflen, "%s", rest);
    return key_is_safe(buf);
}

bool systems_folder_key(const char *console_path, char *buf, size_t buflen) {
    if (!console_path || !console_path[0] || !buf || buflen == 0)
        return false;
    buf[0] = '\0';

    char roms[PATH_MAX];
    get_roms_path(roms, sizeof(roms));

    char root[PATH_MAX];
    char path[PATH_MAX];
    normalize_path(roms, root, sizeof(root));
    normalize_path(console_path, path, sizeof(path));
    if (strip_root(root, path, buf, buflen))
        return true;

    /* The ROM root and the folder can be written differently — a relative
     * mock root on macOS against an absolute scan result, say. Compare the
     * resolved paths before giving up. Symlinks are not followed further:
     * two differently named folders must stay two different keys. */
    char real_root[PATH_MAX];
    char real_path[PATH_MAX];
    if (realpath(root, real_root) && realpath(path, real_path)
        && strip_root(real_root, real_path, buf, buflen))
        return true;

    buf[0] = '\0';
    return false;
}

/* ── Override storage ─────────────────────────────────────────── */

static void ov_free(override_set *set) {
    for (int i = 0; i < set->tag_count; i++) {
        free(set->tags[i].tag);
        free(set->tags[i].platform_id);
    }
    free(set->tags);
    for (int i = 0; i < set->folder_count; i++) {
        free(set->folders[i].key);
        free(set->folders[i].platform_id);
    }
    free(set->folders);
    memset(set, 0, sizeof(*set));
}

static bool ov_clone(const override_set *src, override_set *dst) {
    memset(dst, 0, sizeof(*dst));
    if (src->tag_count > 0) {
        dst->tags = calloc((size_t)src->tag_count, sizeof(tag_ov));
        if (!dst->tags) return false;
        for (int i = 0; i < src->tag_count; i++) {
            dst->tags[i].tag = dup_str(src->tags[i].tag);
            dst->tags[i].platform_id = dup_str(src->tags[i].platform_id);
            if (!dst->tags[i].tag || !dst->tags[i].platform_id) {
                dst->tag_count = i + 1;
                ov_free(dst);
                return false;
            }
        }
        dst->tag_count = src->tag_count;
    }
    if (src->folder_count > 0) {
        dst->folders = calloc((size_t)src->folder_count, sizeof(folder_ov));
        if (!dst->folders) { ov_free(dst); return false; }
        for (int i = 0; i < src->folder_count; i++) {
            dst->folders[i].key = dup_str(src->folders[i].key);
            dst->folders[i].hidden = src->folders[i].hidden;
            if (src->folders[i].platform_id) {
                dst->folders[i].platform_id = dup_str(src->folders[i].platform_id);
                if (!dst->folders[i].platform_id) {
                    dst->folder_count = i + 1;
                    ov_free(dst);
                    return false;
                }
            }
            if (!dst->folders[i].key) {
                dst->folder_count = i + 1;
                ov_free(dst);
                return false;
            }
        }
        dst->folder_count = src->folder_count;
    }
    return true;
}

static folder_ov *ov_find_folder(override_set *set, const char *key) {
    for (int i = 0; i < set->folder_count; i++) {
        if (str_eq(set->folders[i].key, key))
            return &set->folders[i];
    }
    return NULL;
}

static tag_ov *ov_find_tag(override_set *set, const char *tag) {
    for (int i = 0; i < set->tag_count; i++) {
        if (str_eq(set->tags[i].tag, tag))
            return &set->tags[i];
    }
    return NULL;
}

static folder_ov *ov_folder_slot(override_set *set, const char *key) {
    folder_ov *existing = ov_find_folder(set, key);
    if (existing)
        return existing;
    folder_ov *grown = realloc(set->folders,
                               sizeof(folder_ov) * (size_t)(set->folder_count + 1));
    if (!grown)
        return NULL;
    set->folders = grown;
    folder_ov *slot = &set->folders[set->folder_count];
    memset(slot, 0, sizeof(*slot));
    slot->key = dup_str(key);
    if (!slot->key)
        return NULL;
    set->folder_count++;
    return slot;
}

/* Drop entries that no longer say anything. */
static void ov_compact(override_set *set) {
    int kept = 0;
    for (int i = 0; i < set->folder_count; i++) {
        if (!set->folders[i].platform_id && !set->folders[i].hidden) {
            free(set->folders[i].key);
            free(set->folders[i].platform_id);
            continue;
        }
        if (kept != i)
            set->folders[kept] = set->folders[i];
        kept++;
    }
    set->folder_count = kept;

    kept = 0;
    for (int i = 0; i < set->tag_count; i++) {
        if (!set->tags[i].platform_id) {
            free(set->tags[i].tag);
            continue;
        }
        if (kept != i)
            set->tags[kept] = set->tags[i];
        kept++;
    }
    set->tag_count = kept;
}

/* ── Override loading ─────────────────────────────────────────── */

static void load_overrides(void) {
    get_system_overrides_path(overrides_path, sizeof(overrides_path));
    if (access(overrides_path, F_OK) != 0)
        return;   /* absent is normal */

    char *text = read_file(overrides_path, NULL);
    if (!text) {
        set_warning("Your saved system mappings could not be read (%s). "
                    "Bundled defaults are in use.", strerror(errno));
        overrides_need_backup = true;
        return;
    }
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        set_warning("Your saved system mappings are not valid JSON. "
                    "Bundled defaults are in use; the file is kept until you "
                    "change a mapping.");
        overrides_need_backup = true;
        return;
    }

    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    if (!cJSON_IsNumber(schema) || (int)schema->valuedouble != CATALOG_SCHEMA) {
        cJSON_Delete(root);
        set_warning("Your saved system mappings use an unsupported format. "
                    "Bundled defaults are in use.");
        overrides_need_backup = true;
        return;
    }

    const cJSON *tags = cJSON_GetObjectItemCaseSensitive(root, "tags");
    if (cJSON_IsObject(tags)) {
        if (has_duplicate_keys(tags)) {
            set_warning("Your saved suffix defaults repeat a suffix; they were "
                        "skipped.");
            overrides_need_backup = true;
        } else {
            const cJSON *item = NULL;
            cJSON_ArrayForEach(item, tags) {
                if (!item->string || !item->string[0]
                    || strlen(item->string) > MAX_TAG_LEN
                    || !cJSON_IsString(item)
                    || platform_index_by_id(item->valuestring) < 0) {
                    set_warning("Saved suffix default for \"%s\" names a platform "
                                "this version does not have; it was skipped.",
                                item->string ? item->string : "?");
                    overrides_need_backup = true;
                    continue;
                }
                tag_ov *grown = realloc(overrides.tags,
                                        sizeof(tag_ov) * (size_t)(overrides.tag_count + 1));
                if (!grown) continue;
                overrides.tags = grown;
                overrides.tags[overrides.tag_count].tag = dup_str(item->string);
                overrides.tags[overrides.tag_count].platform_id = dup_str(item->valuestring);
                if (overrides.tags[overrides.tag_count].tag
                    && overrides.tags[overrides.tag_count].platform_id)
                    overrides.tag_count++;
            }
        }
    } else if (tags && !cJSON_IsNull(tags)) {
        set_warning("Your saved suffix defaults are malformed; they were skipped.");
        overrides_need_backup = true;
    }

    const cJSON *folders = cJSON_GetObjectItemCaseSensitive(root, "folders");
    if (cJSON_IsObject(folders)) {
        if (has_duplicate_keys(folders)) {
            set_warning("Your saved folder mappings repeat a folder; they were "
                        "skipped.");
            overrides_need_backup = true;
        } else {
            const cJSON *item = NULL;
            cJSON_ArrayForEach(item, folders) {
                if (!item->string || !key_is_safe(item->string)
                    || strlen(item->string) > MAX_KEY_LEN
                    || !cJSON_IsObject(item)) {
                    set_warning("Saved mapping for folder \"%s\" is unusable; it "
                                "was skipped.", item->string ? item->string : "?");
                    overrides_need_backup = true;
                    continue;
                }
                const cJSON *platform = cJSON_GetObjectItemCaseSensitive(item, "platform");
                const cJSON *hidden = cJSON_GetObjectItemCaseSensitive(item, "hidden");
                const char *platform_id = NULL;
                if (platform && !cJSON_IsNull(platform)) {
                    if (!cJSON_IsString(platform)
                        || platform_index_by_id(platform->valuestring) < 0) {
                        set_warning("Saved mapping for folder \"%s\" names a "
                                    "platform this version does not have; only "
                                    "its visibility was kept.", item->string);
                        overrides_need_backup = true;
                    } else {
                        platform_id = platform->valuestring;
                    }
                }
                bool is_hidden = cJSON_IsTrue(hidden);
                if (!platform_id && !is_hidden)
                    continue;

                folder_ov *slot = ov_folder_slot(&overrides, item->string);
                if (!slot) continue;
                slot->hidden = is_hidden;
                if (platform_id) {
                    free(slot->platform_id);
                    slot->platform_id = dup_str(platform_id);
                }
            }
        }
    } else if (folders && !cJSON_IsNull(folders)) {
        set_warning("Your saved folder mappings are malformed; they were skipped.");
        overrides_need_backup = true;
    }

    cJSON_Delete(root);
}

/* ── Persistence ──────────────────────────────────────────────── */

static bool copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb");
    if (!in) return false;
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); return false; }
    char buf[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (ferror(in)) ok = false;
    if (fflush(out) != 0) ok = false;
    if (fclose(out) != 0) ok = false;
    fclose(in);
    if (!ok) unlink(to);
    return ok;
}

static char *ov_to_json(const override_set *set) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "schema", CATALOG_SCHEMA);

    cJSON *tags = cJSON_AddObjectToObject(root, "tags");
    for (int i = 0; tags && i < set->tag_count; i++)
        cJSON_AddStringToObject(tags, set->tags[i].tag, set->tags[i].platform_id);

    cJSON *folders = cJSON_AddObjectToObject(root, "folders");
    for (int i = 0; folders && i < set->folder_count; i++) {
        cJSON *entry = cJSON_AddObjectToObject(folders, set->folders[i].key);
        if (!entry) continue;
        if (set->folders[i].platform_id)
            cJSON_AddStringToObject(entry, "platform", set->folders[i].platform_id);
        if (set->folders[i].hidden)
            cJSON_AddBoolToObject(entry, "hidden", true);
    }

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    return text;
}

/* Write the proposed state, then swap it in. The previous file and the
 * effective mappings survive every failure path. */
static int ov_persist(override_set *proposed) {
    if (!overrides_path[0])
        get_system_overrides_path(overrides_path, sizeof(overrides_path));

    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", overrides_path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        ensure_dir_exists(dir);
    }

    if (overrides_need_backup && access(overrides_path, F_OK) == 0) {
        char backup[PATH_MAX];
        int written = snprintf(backup, sizeof(backup), "%s.rejected", overrides_path);
        if (written < 0 || (size_t)written >= sizeof(backup)
            || !copy_file(overrides_path, backup)) {
            set_error("Could not preserve the unreadable mapping file at %s.rejected, "
                      "so it was left untouched.", overrides_path);
            return -1;
        }
        overrides_need_backup = false;
    }

    char *text = ov_to_json(proposed);
    if (!text) {
        set_error("Could not build the mapping file contents.");
        return -1;
    }

    char tmp[PATH_MAX];
    int written = snprintf(tmp, sizeof(tmp), "%s.tmp", overrides_path);
    if (written < 0 || (size_t)written >= sizeof(tmp)) {
        free(text);
        set_error("The mapping file path is too long to write safely.");
        return -1;
    }

    FILE *f = fopen(tmp, "w");
    if (!f) {
        set_error("Could not write %s: %s", tmp, strerror(errno));
        free(text);
        return -1;
    }
    size_t len = strlen(text);
    bool ok = fwrite(text, 1, len, f) == len;
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    free(text);
    if (!ok) {
        unlink(tmp);
        set_error("Could not write %s: %s", tmp, strerror(errno));
        return -1;
    }
    if (rename(tmp, overrides_path) != 0) {
        unlink(tmp);
        set_error("Could not save %s: %s", overrides_path, strerror(errno));
        return -1;
    }
    return 0;
}

static int commit(override_set *proposed) {
    ov_compact(proposed);
    if (ov_persist(proposed) != 0) {
        ov_free(proposed);
        return -1;
    }
    ov_free(&overrides);
    overrides = *proposed;
    memset(proposed, 0, sizeof(*proposed));
    return 0;
}

/* ── Lifecycle ────────────────────────────────────────────────── */

int systems_init(void) {
    if (initialized)
        return 0;
    last_error[0] = '\0';
    warning_message[0] = '\0';
    overrides_need_backup = false;

    if (!load_catalog())
        return -1;
    load_overrides();
    initialized = true;
    return 0;
}

const char *systems_last_error(void) {
    return last_error[0] ? last_error : NULL;
}

const char *systems_warning(void) {
    return warning_message[0] ? warning_message : NULL;
}

void systems_clear_warning(void) {
    warning_message[0] = '\0';
}

void systems_shutdown(void) {
    free_catalog();
    ov_free(&overrides);
    initialized = false;
}

/* ── Resolution ───────────────────────────────────────────────── */

const sg_platform *systems_builtin_tag(const char *tag) {
    if (!tag || !tag[0])
        return NULL;
    for (int i = 0; i < tag_default_count; i++) {
        if (str_eq(tag_defaults[i].tag, tag))
            return &platforms[tag_defaults[i].platform].pub;
    }
    return NULL;
}

sg_mapping systems_resolve(const char *console_path, const char *tag) {
    sg_mapping mapping = {NULL, MAPPING_NONE, false};
    if (!initialized)
        return mapping;

    if (console_path && console_path[0]) {
        char key[MAX_KEY_LEN];
        if (systems_folder_key(console_path, key, sizeof(key))) {
            folder_ov *folder = ov_find_folder(&overrides, key);
            if (folder) {
                mapping.hidden = folder->hidden;
                if (folder->platform_id) {
                    int index = platform_index_by_id(folder->platform_id);
                    if (index >= 0) {
                        mapping.platform = &platforms[index].pub;
                        mapping.source = MAPPING_USER_FOLDER;
                        return mapping;
                    }
                }
            }
        }
    }

    tag_ov *user = tag && tag[0] ? ov_find_tag(&overrides, tag) : NULL;
    if (user && user->platform_id) {
        int index = platform_index_by_id(user->platform_id);
        if (index >= 0) {
            mapping.platform = &platforms[index].pub;
            mapping.source = MAPPING_USER_TAG;
            return mapping;
        }
    }

    const sg_platform *builtin = systems_builtin_tag(tag);
    if (builtin) {
        mapping.platform = builtin;
        mapping.source = MAPPING_BUILTIN;
    }
    return mapping;
}

/* ── Catalog access ───────────────────────────────────────────── */

int systems_platform_count(void) {
    return platform_count;
}

const sg_platform *systems_platform_at(int index) {
    if (index < 0 || index >= platform_count)
        return NULL;
    return &platforms[index].pub;
}

const sg_platform *systems_platform_by_id(const char *id) {
    int index = platform_index_by_id(id);
    return index < 0 ? NULL : &platforms[index].pub;
}

int systems_tag_candidates(const char *tag, const sg_platform **out, int max) {
    if (!tag || !tag[0] || !out || max <= 0)
        return 0;
    for (int i = 0; i < tag_candidate_count; i++) {
        if (!str_eq(tag_candidates[i].tag, tag))
            continue;
        int n = 0;
        for (int c = 0; c < tag_candidates[i].count && n < max; c++)
            out[n++] = &platforms[tag_candidates[i].platforms[c]].pub;
        return n;
    }
    return 0;
}

/* ── Suggestions ──────────────────────────────────────────────── */

static bool already_listed(const sg_platform **out, int count,
                           const sg_platform *candidate) {
    for (int i = 0; i < count; i++) {
        if (out[i] == candidate)
            return true;
    }
    return false;
}

static bool matches_filter(const platform_rec *rec, const char *filter) {
    if (!filter || !filter[0])
        return true;
    if (contains_ci(rec->pub.name, filter) || contains_ci(rec->pub.id, filter))
        return true;
    for (int i = 0; i < rec->alias_count; i++) {
        if (contains_ci(rec->aliases[i], filter))
            return true;
    }
    return false;
}

static bool names_match(const platform_rec *rec, const char *text) {
    if (!text || !text[0])
        return false;
    if (equals_ci(rec->pub.name, text))
        return true;
    for (int i = 0; i < rec->alias_count; i++) {
        if (equals_ci(rec->aliases[i], text))
            return true;
    }
    return false;
}

bool systems_platform_matches(const sg_platform *platform, const char *text) {
    if (!platform)
        return false;
    if (!text || !text[0])
        return true;
    for (int i = 0; i < platform_count; i++) {
        if (&platforms[i].pub == platform)
            return matches_filter(&platforms[i], text);
    }
    return false;
}

int systems_suggest(const char *tag, const char *display, const char *filter,
                    const sg_platform **out, int max) {
    if (!out || max <= 0)
        return 0;
    int count = 0;

    /* Exact display-name and alias matches first: a folder called
     * "Mega Drive (GPGX)" almost certainly holds Mega Drive games. */
    for (int i = 0; i < platform_count && count < max; i++) {
        if (names_match(&platforms[i], display) && matches_filter(&platforms[i], filter))
            out[count++] = &platforms[i].pub;
    }

    /* Then the reviewed candidates and default for the suffix. */
    const sg_platform *candidates[16];
    int candidate_count = systems_tag_candidates(tag, candidates, 16);
    for (int i = 0; i < candidate_count && count < max; i++) {
        int index = platform_index_by_id(candidates[i]->id);
        if (index >= 0 && !already_listed(out, count, candidates[i])
            && matches_filter(&platforms[index], filter))
            out[count++] = candidates[i];
    }
    const sg_platform *builtin = systems_builtin_tag(tag);
    if (builtin && count < max && !already_listed(out, count, builtin)) {
        int index = platform_index_by_id(builtin->id);
        if (index >= 0 && matches_filter(&platforms[index], filter))
            out[count++] = builtin;
    }

    /* Finally substring matches on the folder's display name. */
    for (int i = 0; i < platform_count && count < max; i++) {
        if (already_listed(out, count, &platforms[i].pub))
            continue;
        if (!display || !display[0])
            break;
        if (!contains_ci(platforms[i].pub.name, display))
            continue;
        if (matches_filter(&platforms[i], filter))
            out[count++] = &platforms[i].pub;
    }
    return count;
}

/* ── Mutations ────────────────────────────────────────────────── */

int systems_set_tag(const char *tag, const char *platform_id) {
    if (!initialized) {
        set_error("The platform catalog is not loaded.");
        return -1;
    }
    if (!tag || !tag[0] || strlen(tag) > MAX_TAG_LEN) {
        set_error("That folder has no usable suffix to set a default for.");
        return -1;
    }
    if (platform_id && platform_index_by_id(platform_id) < 0) {
        set_error("Unknown platform \"%s\".", platform_id);
        return -1;
    }

    override_set proposed;
    if (!ov_clone(&overrides, &proposed)) {
        set_error("Out of memory preparing the change.");
        return -1;
    }

    tag_ov *existing = ov_find_tag(&proposed, tag);
    if (!platform_id) {
        if (existing) {
            free(existing->platform_id);
            existing->platform_id = NULL;
        }
    } else if (existing) {
        char *copy = dup_str(platform_id);
        if (!copy) { ov_free(&proposed); set_error("Out of memory."); return -1; }
        free(existing->platform_id);
        existing->platform_id = copy;
    } else {
        tag_ov *grown = realloc(proposed.tags,
                                sizeof(tag_ov) * (size_t)(proposed.tag_count + 1));
        if (!grown) { ov_free(&proposed); set_error("Out of memory."); return -1; }
        proposed.tags = grown;
        proposed.tags[proposed.tag_count].tag = dup_str(tag);
        proposed.tags[proposed.tag_count].platform_id = dup_str(platform_id);
        if (!proposed.tags[proposed.tag_count].tag
            || !proposed.tags[proposed.tag_count].platform_id) {
            proposed.tag_count++;
            ov_free(&proposed);
            set_error("Out of memory.");
            return -1;
        }
        proposed.tag_count++;
    }
    return commit(&proposed);
}

static int set_folder(const char *console_path, const char *platform_id,
                      bool set_platform, bool hidden, bool set_hidden) {
    if (!initialized) {
        set_error("The platform catalog is not loaded.");
        return -1;
    }
    char key[MAX_KEY_LEN];
    if (!systems_folder_key(console_path, key, sizeof(key))) {
        set_error("That folder is not inside the Roms directory, so it cannot "
                  "carry a saved mapping.");
        return -1;
    }
    if (set_platform && platform_id && platform_index_by_id(platform_id) < 0) {
        set_error("Unknown platform \"%s\".", platform_id);
        return -1;
    }

    override_set proposed;
    if (!ov_clone(&overrides, &proposed)) {
        set_error("Out of memory preparing the change.");
        return -1;
    }

    folder_ov *slot = ov_folder_slot(&proposed, key);
    if (!slot) {
        ov_free(&proposed);
        set_error("Out of memory.");
        return -1;
    }
    if (set_platform) {
        char *copy = platform_id ? dup_str(platform_id) : NULL;
        if (platform_id && !copy) {
            ov_free(&proposed);
            set_error("Out of memory.");
            return -1;
        }
        free(slot->platform_id);
        slot->platform_id = copy;
    }
    if (set_hidden)
        slot->hidden = hidden;

    return commit(&proposed);
}

int systems_set_folder_platform(const char *console_path, const char *platform_id) {
    return set_folder(console_path, platform_id, true, false, false);
}

int systems_set_folder_hidden(const char *console_path, bool hidden) {
    return set_folder(console_path, NULL, false, hidden, true);
}

int systems_clear_folder(const char *console_path) {
    return set_folder(console_path, NULL, true, false, true);
}

/* ── Override enumeration ─────────────────────────────────────── */

int systems_override_count(void) {
    return overrides.folder_count + overrides.tag_count;
}

bool systems_override_at(int index, sg_override *out) {
    if (!out || index < 0)
        return false;
    if (index < overrides.folder_count) {
        out->is_folder = true;
        out->key = overrides.folders[index].key;
        out->platform_id = overrides.folders[index].platform_id;
        out->hidden = overrides.folders[index].hidden;
        return true;
    }
    index -= overrides.folder_count;
    if (index >= overrides.tag_count)
        return false;
    out->is_folder = false;
    out->key = overrides.tags[index].tag;
    out->platform_id = overrides.tags[index].platform_id;
    out->hidden = false;
    return true;
}

#ifndef SYSTEMS_H
#define SYSTEMS_H

#include <stdbool.h>
#include <stddef.h>

/* systems.h — scraping-platform catalog and per-folder mappings.
 *
 * A ROM folder's suffix, e.g. the "GPGX" in "Roms/Mega Drive (GPGX)",
 * identifies the emulator NextUI launches. It does not necessarily identify
 * one scraping platform: the same emulator pak can serve several systems.
 *
 * The catalog of platforms ships as resources/systems.json beside the
 * executable. Users select a platform per folder, or set their own default for
 * a suffix; those choices live in a separate file under .userdata so they
 * survive a pak update.
 *
 * Resolution precedence, highest first:
 *
 *   folder override  ->  user suffix default  ->  bundled suffix default
 */

/* ── Types ────────────────────────────────────────────────────── */

typedef struct {
    const char *id;            /* permanent local identifier */
    const char *name;          /* display name */
    int ss_id;                 /* ScreenScraper platform ID, -1 when none */
    const char *libretro_dir;  /* libretro cht/ directory, NULL when none */
} sg_platform;

typedef enum {
    MAPPING_NONE = 0,     /* no platform selected for this folder */
    MAPPING_BUILTIN,      /* bundled suffix default */
    MAPPING_USER_TAG,     /* user's own default for the suffix */
    MAPPING_USER_FOLDER,  /* user's choice for this folder */
} mapping_source;

typedef struct {
    const sg_platform *platform;  /* NULL means no selected target */
    mapping_source source;
    bool hidden;                  /* independent of platform and source */
} sg_mapping;

/* A saved override, for listing and clearing in Settings. */
typedef struct {
    bool is_folder;          /* false: a suffix default */
    const char *key;         /* folder key relative to Roms/, or the suffix */
    const char *platform_id; /* NULL when the entry only hides a folder */
    bool hidden;
} sg_override;

/* ── Lifecycle ────────────────────────────────────────────────── */

/* Load the catalog and the user's overrides. Returns 0 on success.
 * A missing or invalid catalog fails; missing overrides are normal.
 * On failure nothing is loaded and systems_last_error() explains why,
 * naming the file it tried. */
int systems_init(void);

/* The reason the last systems_init() or mutation failed, or NULL. */
const char *systems_last_error(void);

/* A recoverable problem in the user's override file, or NULL. The message
 * stays until systems_clear_warning() so the UI can show it once. */
const char *systems_warning(void);
void systems_clear_warning(void);

void systems_shutdown(void);

/* ── Resolution ───────────────────────────────────────────────── */

/* Resolve one ROM folder. console_path may be NULL to resolve a suffix alone,
 * in which case folder overrides are not consulted. */
sg_mapping systems_resolve(const char *console_path, const char *tag);

/* The bundled default for a suffix, ignoring every user override. */
const sg_platform *systems_builtin_tag(const char *tag);

/* The folder key a path maps to: its path relative to Roms/, preserving
 * spelling and case. Returns false for a path outside Roms/, an empty key, or
 * one containing a "." or ".." component. */
bool systems_folder_key(const char *console_path, char *buf, size_t buflen);

/* ── Catalog ──────────────────────────────────────────────────── */

int systems_platform_count(void);
const sg_platform *systems_platform_at(int index);
const sg_platform *systems_platform_by_id(const char *id);

/* Reviewed candidates for a suffix. Returns the number written. */
int systems_tag_candidates(const char *tag, const sg_platform **out, int max);

/* Reviewed candidates and simple name/alias matches, best first, deduplicated.
 * `filter` restricts to names, aliases and ids containing it; pass NULL or ""
 * for no filtering. Returns the number written, at most max. */
int systems_suggest(const char *tag, const char *display, const char *filter,
                    const sg_platform **out, int max);

/* ── Overrides ────────────────────────────────────────────────── */

/* Each mutation validates, persists, and only then publishes the new state.
 * A NULL platform_id clears the override at that scope. Returns 0 on success;
 * on failure the previous file and the effective mappings both stand, and
 * systems_last_error() explains why. */
int systems_set_tag(const char *tag, const char *platform_id);
int systems_set_folder_platform(const char *console_path, const char *platform_id);
int systems_set_folder_hidden(const char *console_path, bool hidden);

/* Read-only enumeration of saved overrides, for Settings. Pointers stay valid
 * until the next mutation or systems_shutdown(). */
int systems_override_count(void);
bool systems_override_at(int index, sg_override *out);

#endif /* SYSTEMS_H */

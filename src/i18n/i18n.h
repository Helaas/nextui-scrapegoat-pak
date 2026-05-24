#ifndef __I18N_H__
#define __I18N_H__

#ifdef __cplusplus
extern "C" {
#endif

#define I18N_LANG_CODE_MAX 8
#define I18N_LANG_DIR      "/.system/res/lang"

/* Override the default search paths. Both arguments are optional —
 * NULL keeps the compile-time default. Call BEFORE I18N_init() /
 * I18N_init_from_minuisettings() / I18N_reload() for the override
 * to take effect on the next load.
 *
 *   lang_dir       — directory containing <code>.lang files
 *                    (default: SDCARD_PATH "/.system/res/lang")
 *   settings_file  — absolute path to minuisettings.txt
 *                    (default: SDCARD_PATH "/.userdata/shared/minuisettings.txt")
 */
void        I18N_set_paths(const char *lang_dir, const char *settings_file);

void        I18N_init(const char *lang_code);
void        I18N_init_from_minuisettings(void);
void        I18N_quit(void);
int         I18N_reload(const char *lang_code);
char       *I18N_t(const char *key);
const char *I18N_active_code(void);

#define T(k) I18N_t(k)

#ifdef __cplusplus
}
#endif

#endif

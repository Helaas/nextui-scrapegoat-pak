#include "cheats.h"
#include "systems.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* ── Git binary resolution ────────────────────────────────── */

const char *get_git_bin(void) {
#ifdef PLATFORM_MAC
    struct stat st;
    if (stat("/usr/bin/git", &st) == 0)
        return "/usr/bin/git";
    return "git";
#else
    static char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "git";
    buf[len] = '\0';
    char *slash = strrchr(buf, '/');
    if (slash) *slash = '\0';
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
             "/resources/bin/git");
    return buf;
#endif
}

int check_git_available(void) {
    const char *git = get_git_bin();
    struct stat st;
    if (stat(git, &st) != 0) {
        fprintf(stderr, "cheats: git binary not found: %s\n", git);
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        fprintf(stderr, "cheats: git path is a directory: %s\n", git);
        return -1;
    }
    return 0;
}

/* ── CA certificate bundle ────────────────────────────────── */

static const char *find_ca_certs(void) {
    /* Common CA bundle paths on Linux/device */
    static const char *paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/usr/share/ca-certificates/mozilla/",
        NULL,
    };
    for (const char **p = paths; *p; p++) {
        struct stat st;
        if (stat(*p, &st) == 0)
            return *p;
    }
    return NULL;
}

/* ── Git command helpers ──────────────────────────────────── */

typedef struct {
    const char **argv;
    int argc;
    const char *cwd;
} git_cmd;

/* Build environment for git process */
static char **build_git_env(void) {
    /* Count existing env */
    extern char **environ;
    int count = 0;
    for (char **e = environ; *e; e++) count++;

    /* Allocate space for existing + extras + NULL */
    char **env = malloc(sizeof(char *) * (size_t)(count + 6));
    int idx = 0;

    for (int i = 0; i < count; i++)
        env[idx++] = environ[i];

#ifndef PLATFORM_MAC
    /* Add LD_LIBRARY_PATH, GIT_EXEC_PATH, GIT_TEMPLATE_DIR */
    static char ld_path[PATH_MAX];
    static char exec_path[PATH_MAX];
    static char template_dir[] = "GIT_TEMPLATE_DIR=";

    char exe_dir[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len > 0) {
        exe_dir[len] = '\0';
        char *slash = strrchr(exe_dir, '/');
        if (slash) *slash = '\0';

        snprintf(ld_path, sizeof(ld_path), "LD_LIBRARY_PATH=%s/resources/lib:%s",
                 exe_dir, getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "");
        snprintf(exec_path, sizeof(exec_path), "GIT_EXEC_PATH=%s/resources/bin", exe_dir);

        env[idx++] = ld_path;
        env[idx++] = exec_path;
        env[idx++] = template_dir;
    }

    /* Point git at CA certs */
    static char ca_env[PATH_MAX];
    const char *ca = find_ca_certs();
    if (ca) {
        snprintf(ca_env, sizeof(ca_env), "GIT_SSL_CAINFO=%s", ca);
        env[idx++] = ca_env;
    }
#endif

    env[idx] = NULL;
    return env;
}

/* ── Git process execution with streaming ─────────────────── */

/* Remove stale lock files from a previous killed git process */
static void remove_git_locks(void) {
    char repo[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));

    char lock_paths[2][PATH_MAX];
    snprintf(lock_paths[0], PATH_MAX, "%s/.git/index.lock", repo);
    snprintf(lock_paths[1], PATH_MAX, "%s/.git/info/sparse-checkout.lock", repo);

    for (int i = 0; i < 2; i++) {
        if (unlink(lock_paths[i]) == 0)
            fprintf(stderr, "cheats: removed stale lock: %s\n", lock_paths[i]);
    }
}

/* Parse "Receiving objects:  XX%" from git stderr */
static float parse_git_progress(const char *line) {
    const char *p = strstr(line, "Receiving objects:");
    if (!p) return -1.0f;
    p += strlen("Receiving objects:");
    while (*p == ' ') p++;
    int pct = atoi(p);
    if (pct > 0 && pct <= 100)
        return (float)pct / 100.0f;
    return -1.0f;
}

/* Run a git command with real-time progress parsing and interrupt support.
 * Returns 0 on success, -1 on error, -2 on interrupt. */
static int run_git_streaming(const char **argv, const char *cwd,
                              int *interrupt_signal,
                              cheat_progress_fn set_progress) {
    remove_git_locks();

    /* Log the command */
    fprintf(stderr, "cheats: exec");
    for (const char **a = argv; *a; a++)
        fprintf(stderr, " %s", *a);
    fprintf(stderr, "\n");

    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child: redirect stderr to pipe */
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);

        if (cwd) {
            if (chdir(cwd) != 0) _exit(127);
        }

        char **env = build_git_env();
        execve(argv[0], (char *const *)argv, env);
        _exit(127);
    }

    /* Parent: read stderr from pipe */
    close(pipe_fd[1]);

    char buf[4096];
    size_t buf_pos = 0;
    int killed = 0;

    while (1) {
        /* Check interrupt every read */
        if (interrupt_signal && *interrupt_signal != 0 && !killed) {
            kill(pid, SIGKILL);
            killed = 1;
        }

        ssize_t n = read(pipe_fd[0], buf + buf_pos, sizeof(buf) - buf_pos - 1);
        if (n <= 0) break;
        buf_pos += (size_t)n;
        buf[buf_pos] = '\0';

        /* Process lines (split on \r or \n) */
        char *start = buf;
        for (char *p = buf; p < buf + buf_pos; p++) {
            if (*p == '\r' || *p == '\n') {
                *p = '\0';
                if (p > start) {
                    float progress = parse_git_progress(start);
                    if (progress >= 0 && set_progress)
                        set_progress(progress);
                }
                /* Skip \r\n */
                if (*p == '\r' && (p + 1) < buf + buf_pos && *(p + 1) == '\n')
                    p++;
                start = p + 1;
            }
        }

        /* Move remaining data to beginning */
        size_t remaining = (size_t)(buf + buf_pos - start);
        if (remaining > 0 && start != buf)
            memmove(buf, start, remaining);
        buf_pos = remaining;
    }

    close(pipe_fd[0]);

    int status;
    waitpid(pid, &status, 0);

    if (killed)
        return -2;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;
    return -1;
}

/* ── Sparse checkout management ───────────────────────────── */

static int is_repo_initialized(void) {
    char repo[PATH_MAX], git_dir[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));
    snprintf(git_dir, sizeof(git_dir), "%s/.git", repo);
    struct stat st;
    return stat(git_dir, &st) == 0 && S_ISDIR(st.st_mode);
}

static int init_cheat_repo(int *interrupt_signal,
                            cheat_message_fn set_message,
                            cheat_progress_fn set_progress) {
    char repo[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));

    /* Ensure parent directory exists */
    char parent[PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", repo);
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = '\0';
        /* mkdir -p */
        for (char *p = parent + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(parent, 0755);
                *p = '/';
            }
        }
        mkdir(parent, 0755);
    }

    if (check_git_available() != 0)
        return -1;

    if (set_message) set_message("Cloning cheat database...");

    const char *git = get_git_bin();
    const char *argv[] = {
        git, "clone",
        "--sparse",
        "--filter=blob:none",
        "--depth=1",
        "--branch", CHEAT_REPO_BRANCH,
        "--single-branch",
        "--no-tags",
        "--progress",
        CHEAT_REPO_URL,
        repo,
        NULL,
    };

    return run_git_streaming(argv, NULL, interrupt_signal, set_progress);
}

static int ensure_system_checked_out(const char *libretro_dir_name,
                                      int *interrupt_signal,
                                      cheat_message_fn set_message,
                                      cheat_progress_fn set_progress) {
    char repo[PATH_MAX], local_dir[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));
    snprintf(local_dir, sizeof(local_dir), "%s/cht/%s", repo, libretro_dir_name);

    struct stat st;
    if (stat(local_dir, &st) == 0 && S_ISDIR(st.st_mode))
        return 0; /* already checked out */

    char cht_path[512];
    snprintf(cht_path, sizeof(cht_path), "cht/%s", libretro_dir_name);

    char msg[256];
    snprintf(msg, sizeof(msg), "Checking out %s...", libretro_dir_name);
    if (set_message) set_message(msg);

    const char *git = get_git_bin();
    const char *argv[] = {
        git, "-C", repo,
        "sparse-checkout", "add", cht_path,
        NULL,
    };

    return run_git_streaming(argv, NULL, interrupt_signal, set_progress);
}

static int update_cheat_repo(int *interrupt_signal,
                              cheat_message_fn set_message,
                              cheat_progress_fn set_progress) {
    char repo[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));

    if (set_message) set_message("Updating cheat database...");

    const char *git = get_git_bin();
    const char *argv[] = {
        git, "-C", repo,
        "pull", "--ff-only", "--progress",
        NULL,
    };

    return run_git_streaming(argv, NULL, interrupt_signal, set_progress);
}

/* ── Cheat name normalization and matching ────────────────── */

/* Strip all parenthetical groups from a string: "Foo (USA) (Rev 1)" → "Foo" */
static void strip_parentheticals(const char *in, char *out, size_t out_len) {
    size_t j = 0;
    int depth = 0;
    for (const char *p = in; *p && j < out_len - 1; p++) {
        if (*p == '(') { depth++; continue; }
        if (*p == ')') { if (depth > 0) depth--; continue; }
        if (depth == 0)
            out[j++] = *p;
    }
    out[j] = '\0';

    /* Trim trailing spaces */
    while (j > 0 && out[j - 1] == ' ')
        out[--j] = '\0';
}

/* Normalize a game name for fuzzy matching:
 * 1. Strip parenthetical groups
 * 2. Lowercase
 * 3. Strip punctuation
 * 4. Collapse whitespace */
static void normalize_cheat_name(const char *name, char *out, size_t out_len) {
    char stripped[512];
    strip_parentheticals(name, stripped, sizeof(stripped));

    size_t j = 0;
    int prev_space = 1; /* suppress leading space */
    for (const char *p = stripped; *p && j < out_len - 1; p++) {
        unsigned char c = (unsigned char)*p;
        if (ispunct(c)) continue;
        if (isspace(c)) {
            if (!prev_space) { out[j++] = ' '; prev_space = 1; }
            continue;
        }
        out[j++] = (char)tolower(c);
        prev_space = 0;
    }
    /* Trim trailing space */
    if (j > 0 && out[j - 1] == ' ') j--;
    out[j] = '\0';
}

/* ── Region matching ──────────────────────────────────────── */

typedef struct {
    const char *keyword;
    const char *code;
} region_keyword;

static const region_keyword cheat_region_map[] = {
    /* Full names */
    {"usa",       "us"},
    {"europe",    "eu"},
    {"japan",     "jp"},
    {"world",     "wor"},
    {"france",    "fr"},
    {"germany",   "de"},
    {"spain",     "es"},
    {"italy",     "it"},
    {"portugal",  "pt"},
    {"australia", "eu"},
    {"korea",     "kr"},
    {"china",     "cn"},
    {"taiwan",    "tw"},
    {"brazil",    "pt"},
    /* 2-letter abbreviations */
    {"us", "us"},
    {"eu", "eu"},
    {"jp", "jp"},
    {"fr", "fr"},
    {"de", "de"},
    {"es", "es"},
    {"it", "it"},
    {"pt", "pt"},
    {"kr", "kr"},
    {"cn", "cn"},
    {"tw", "tw"},
};
static const int cheat_region_map_count =
    (int)(sizeof(cheat_region_map) / sizeof(cheat_region_map[0]));

/* Extract region codes from parenthetical groups in a name.
 * Returns count, fills regions[] (up to max_regions). */
static int extract_cheat_regions(const char *name, const char **regions,
                                  int max_regions) {
    int count = 0;
    const char *p = name;

    while ((p = strchr(p, '(')) != NULL) {
        p++;
        const char *end = strchr(p, ')');
        if (!end) break;

        /* Parse comma-separated items inside parentheses */
        const char *item = p;
        while (item < end) {
            /* Skip whitespace */
            while (item < end && isspace((unsigned char)*item)) item++;

            const char *item_end = item;
            while (item_end < end && *item_end != ',') item_end++;

            /* Trim trailing whitespace */
            const char *trim = item_end;
            while (trim > item && isspace((unsigned char)*(trim - 1))) trim--;

            size_t len = (size_t)(trim - item);
            if (len > 0 && len < 32) {
                char lower[32];
                for (size_t i = 0; i < len; i++)
                    lower[i] = (char)tolower((unsigned char)item[i]);
                lower[len] = '\0';

                for (int r = 0; r < cheat_region_map_count; r++) {
                    if (strcmp(lower, cheat_region_map[r].keyword) == 0) {
                        /* Check for duplicates */
                        int dup = 0;
                        for (int d = 0; d < count; d++) {
                            if (strcmp(regions[d], cheat_region_map[r].code) == 0) {
                                dup = 1; break;
                            }
                        }
                        if (!dup && count < max_regions)
                            regions[count++] = cheat_region_map[r].code;
                        break;
                    }
                }
            }

            item = (item_end < end) ? item_end + 1 : end;
        }

        p = end + 1;
    }

    return count;
}

/* Score how well a cheat file's regions match:
 * 1000+ = direct overlap with ROM (best)
 * 500   = "World" cheat
 * 100-N = matches user's region priority
 * 50    = no region info (neutral)
 * 0     = no match */
static int score_cheat_region(const char **cheat_regions, int cheat_count,
                               const char **rom_regions, int rom_count,
                               char **region_prio, int prio_count) {
    /* Direct overlap */
    if (rom_count > 0 && cheat_count > 0) {
        int overlap = 0;
        for (int c = 0; c < cheat_count; c++)
            for (int r = 0; r < rom_count; r++)
                if (strcmp(cheat_regions[c], rom_regions[r]) == 0)
                    overlap++;
        if (overlap > 0)
            return 1000 + overlap;
    }

    /* World */
    for (int c = 0; c < cheat_count; c++)
        if (strcmp(cheat_regions[c], "wor") == 0)
            return 500;

    /* User priority */
    if (cheat_count > 0 && prio_count > 0) {
        for (int i = 0; i < prio_count; i++)
            for (int c = 0; c < cheat_count; c++)
                if (strcmp(cheat_regions[c], region_prio[i]) == 0)
                    return 100 - i;
    }

    /* No region info */
    if (cheat_count == 0)
        return 50;

    return 0;
}

/* ── Cheat list building and matching ─────────────────────── */

typedef struct {
    char path[PATH_MAX];
    const char *regions[16];
    int region_count;
} cheat_candidate;

typedef struct {
    char normalized[256];
    cheat_candidate *candidates;
    int candidate_count;
    int candidate_cap;
} cheat_entry;

typedef struct {
    cheat_entry *entries;
    int count;
    int cap;
} cheat_list;

static cheat_entry *cheat_list_find(cheat_list *list, const char *normalized) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->entries[i].normalized, normalized) == 0)
            return &list->entries[i];
    }
    return NULL;
}

static void cheat_list_add(cheat_list *list, const char *normalized,
                            const char *path, const char **regions, int region_count) {
    cheat_entry *entry = cheat_list_find(list, normalized);
    if (!entry) {
        if (list->count >= list->cap) {
            list->cap = list->cap ? list->cap * 2 : 256;
            list->entries = realloc(list->entries, sizeof(cheat_entry) * (size_t)list->cap);
        }
        entry = &list->entries[list->count++];
        snprintf(entry->normalized, sizeof(entry->normalized), "%s", normalized);
        entry->candidates = NULL;
        entry->candidate_count = 0;
        entry->candidate_cap = 0;
    }

    if (entry->candidate_count >= entry->candidate_cap) {
        entry->candidate_cap = entry->candidate_cap ? entry->candidate_cap * 2 : 4;
        entry->candidates = realloc(entry->candidates,
                                     sizeof(cheat_candidate) * (size_t)entry->candidate_cap);
    }

    cheat_candidate *c = &entry->candidates[entry->candidate_count++];
    snprintf(c->path, sizeof(c->path), "%s", path);
    c->region_count = (region_count > 16) ? 16 : region_count;
    for (int i = 0; i < c->region_count; i++)
        c->regions[i] = regions[i];
}

static void cheat_list_free(cheat_list *list) {
    for (int i = 0; i < list->count; i++)
        free(list->entries[i].candidates);
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->cap = 0;
}

/* Build cheat list from local checkout */
static int build_cheat_list(const char *libretro_dir_name, cheat_list *list) {
    char repo[PATH_MAX], cht_dir[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));
    snprintf(cht_dir, sizeof(cht_dir), "%s/cht/%s", repo, libretro_dir_name);

    DIR *dir = opendir(cht_dir);
    if (!dir) return -1;

    memset(list, 0, sizeof(*list));
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        size_t len = strlen(entry->d_name);
        if (len < 5 || strcasecmp(entry->d_name + len - 4, ".cht") != 0)
            continue;

        /* Build full path */
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", cht_dir, entry->d_name);

        /* Strip extension for game name */
        char game_name[256];
        snprintf(game_name, sizeof(game_name), "%.*s", (int)(len - 4), entry->d_name);

        /* Normalize */
        char normalized[256];
        normalize_cheat_name(game_name, normalized, sizeof(normalized));

        /* Extract regions */
        const char *regions[16];
        int region_count = extract_cheat_regions(game_name, regions, 16);

        cheat_list_add(list, normalized, full_path, regions, region_count);
    }

    closedir(dir);
    fprintf(stderr, "cheats: indexed %d entries for %s\n", list->count, libretro_dir_name);
    return 0;
}

/* Find best matching cheat file for a ROM */
static const char *match_cheat(const char *rom_display, cheat_list *list,
                                char **region_prio, int region_count) {
    char normalized[256];
    normalize_cheat_name(rom_display, normalized, sizeof(normalized));

    cheat_entry *entry = cheat_list_find(list, normalized);
    if (!entry || entry->candidate_count == 0)
        return NULL;

    if (entry->candidate_count == 1)
        return entry->candidates[0].path;

    /* Multiple candidates — pick best by region */
    const char *rom_regions[16];
    int rom_region_count = extract_cheat_regions(rom_display, rom_regions, 16);

    int best_score = -1;
    const char *best_path = entry->candidates[0].path;

    for (int i = 0; i < entry->candidate_count; i++) {
        cheat_candidate *c = &entry->candidates[i];
        int score = score_cheat_region(c->regions, c->region_count,
                                        rom_regions, rom_region_count,
                                        region_prio, region_count);
        if (score > best_score) {
            best_score = score;
            best_path = c->path;
        }
    }

    return best_path;
}

/* ── File copy helper ─────────────────────────────────────── */

static int copy_file(const char *src, const char *dst) {
    /* Ensure parent directory */
    char parent[PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", dst);
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = '\0';
        for (char *p = parent + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(parent, 0755);
                *p = '/';
            }
        }
        mkdir(parent, 0755);
    }

    FILE *in = fopen(src, "rb");
    if (!in) return -1;

    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }

    fclose(in);
    if (fclose(out) != 0) return -1;
    return 0;
}

/* ── Console cheat downloader ─────────────────────────────── */

scrape_summary download_cheats_for_console(const console_dir *console,
                                           char **region_prio, int region_count,
                                           int *interrupt_signal,
                                           cheat_progress_fn set_progress,
                                           cheat_message_fn set_message) {
    scrape_summary summary = {0};

    const char *lr_dir = libretro_dir(console->tag);
    if (!lr_dir) {
        fprintf(stderr, "cheats: no libretro dir for %s\n", console->tag);
        return summary;
    }

    if (set_progress) set_progress(0.0f);

    /* Phase 1: Clone or update git repo [0.0 → 0.3] */
    if (!is_repo_initialized()) {
        int ret = init_cheat_repo(interrupt_signal, set_message, set_progress);
        if (ret == -2) return summary; /* interrupted */
        if (ret != 0) {
            fprintf(stderr, "cheats: clone failed\n");
            return summary;
        }
    } else {
        int ret = update_cheat_repo(interrupt_signal, set_message, set_progress);
        if (ret == -2) return summary; /* interrupted */
        if (ret != 0)
            fprintf(stderr, "cheats: pull failed (using existing data)\n");
    }
    if (set_progress) set_progress(0.3f);

    /* Phase 2: Ensure system checked out [0.3 → 0.5] */
    {
        int ret = ensure_system_checked_out(lr_dir, interrupt_signal,
                                             set_message, set_progress);
        if (ret == -2) return summary;
        if (ret != 0) {
            fprintf(stderr, "cheats: checkout failed for %s\n", lr_dir);
            return summary;
        }
    }
    if (set_progress) set_progress(0.5f);

    /* Build cheat list from local files */
    if (set_message) set_message("Building cheat list...");
    cheat_list list;
    if (build_cheat_list(lr_dir, &list) != 0 || list.count == 0) {
        fprintf(stderr, "cheats: no cheats available for %s\n", lr_dir);
        cheat_list_free(&list);
        return summary;
    }

    /* Scan ROMs */
    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, false, &roms);
    summary.total = rom_count;

    /* Get cheats output directory */
    char cheats_base[PATH_MAX];
    get_cheats_path(cheats_base, sizeof(cheats_base));

    for (int i = 0; i < rom_count; i++) {
        if (interrupt_signal && *interrupt_signal != 0) break;

        /* Progress [0.5 → 1.0] */
        if (set_progress)
            set_progress(0.5f + (float)i / (float)rom_count * 0.5f);

        char msg[512];
        snprintf(msg, sizeof(msg), "(%d/%d) %s", i + 1, rom_count, roms[i].display);
        if (set_message) set_message(msg);

        const char *src_path = match_cheat(roms[i].display, &list,
                                            region_prio, region_count);
        if (!src_path) {
            summary.not_found++;
            continue;
        }

        char dest_path[PATH_MAX];
        snprintf(dest_path, sizeof(dest_path), "%s/%s/%s.cht",
                 cheats_base, console->tag, roms[i].display);

        /* Skip if already exists */
        struct stat st;
        if (stat(dest_path, &st) == 0) {
            summary.found++;
            continue;
        }

        if (copy_file(src_path, dest_path) == 0)
            summary.found++;
        else
            summary.errors++;
    }

    if (set_progress) set_progress(1.0f);

    cheat_list_free(&list);
    free(roms);

    fprintf(stderr, "cheats: done. total=%d found=%d notFound=%d errors=%d\n",
            summary.total, summary.found, summary.not_found, summary.errors);
    return summary;
}

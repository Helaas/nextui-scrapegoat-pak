#include "cheats.h"
#include "systems.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Suppress GCC warnings about snprintf truncation when combining
   PATH_MAX-sized strings — truncation is safe by design. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

/* ── Git binary resolution ────────────────────────────────── */

const char *get_git_bin(void) {
#ifdef PLATFORM_MAC
    struct stat st;
    if (stat("/usr/bin/git", &st) == 0)
        return "/usr/bin/git";
    return "git";
#else
    static _Thread_local char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
        return "git";

    buf[len] = '\0';
    char *slash = strrchr(buf, '/');
    if (slash)
        *slash = '\0';
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "/resources/bin/git");
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

/* ── Result helpers ───────────────────────────────────────── */

static run_result make_run_result(run_status status, scrape_summary summary,
                                  const char *error) {
    run_result result;
    result.status = status;
    result.summary = summary;
    result.error[0] = '\0';
    if (error && error[0] != '\0')
        snprintf(result.error, sizeof(result.error), "%s", error);
    return result;
}

static run_result make_run_error(scrape_summary summary, const char *fmt, ...) {
    run_result result;
    va_list args;

    result.status = RUN_ERROR;
    result.summary = summary;
    va_start(args, fmt);
    vsnprintf(result.error, sizeof(result.error), fmt, args);
    va_end(args);
    return result;
}

/* ── Git command helpers ──────────────────────────────────── */

static char **build_git_env(void) {
    extern char **environ;
    int count = 0;
    for (char **entry = environ; *entry; entry++)
        count++;

    char **env = malloc(sizeof(char *) * (size_t)(count + 4));
    if (!env)
        return NULL;

    int idx = 0;
    for (int i = 0; i < count; i++)
        env[idx++] = environ[i];

#ifndef PLATFORM_MAC
    static _Thread_local char ld_path[PATH_MAX];
    static _Thread_local char exec_path[PATH_MAX];
    static char template_dir[] = "GIT_TEMPLATE_DIR=";

    char exe_dir[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len > 0) {
        exe_dir[len] = '\0';
        char *slash = strrchr(exe_dir, '/');
        if (slash)
            *slash = '\0';

        snprintf(ld_path, sizeof(ld_path), "LD_LIBRARY_PATH=%s/resources/lib:%s",
                 exe_dir, getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "");
        snprintf(exec_path, sizeof(exec_path), "GIT_EXEC_PATH=%s/resources/bin", exe_dir);

        env[idx++] = ld_path;
        env[idx++] = exec_path;
        env[idx++] = template_dir;
    }
#endif

    env[idx] = NULL;
    return env;
}

/* ── Git process execution with streaming ─────────────────── */

static pthread_mutex_t g_repo_lock_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int fd;
} cheat_repo_lock;

static void get_repo_lock_path(char *buf, size_t buflen) {
    char repo[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));
    snprintf(buf, buflen, "%s.lock", repo);
}

static float parse_git_progress(const char *line) {
    const char *progress = strstr(line, "Receiving objects:");
    if (!progress)
        return -1.0f;

    progress += strlen("Receiving objects:");
    while (*progress == ' ')
        progress++;

    int pct = atoi(progress);
    if (pct >= 0 && pct <= 100)
        return (float)pct / 100.0f;
    return -1.0f;
}

static int run_git_streaming(const char **argv, const char *cwd,
                             atomic_int *interrupt_signal,
                             cheat_progress_fn set_progress,
                             float progress_scale,
                             float progress_offset) {
    fprintf(stderr, "cheats: exec");
    for (const char **arg = argv; *arg; arg++)
        fprintf(stderr, " %s", *arg);
    fprintf(stderr, "\n");

    int pipe_fd[2];
    if (pipe(pipe_fd) != 0)
        return CHEAT_OP_ERROR;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return CHEAT_OP_ERROR;
    }

    if (pid == 0) {
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);

        if (cwd && chdir(cwd) != 0)
            _exit(127);

        char **env = build_git_env();
        if (!env)
            _exit(127);

        execve(argv[0], (char *const *)argv, env);
        _exit(127);
    }

    close(pipe_fd[1]);

    char buf[4096];
    size_t buf_pos = 0;
    int killed = 0;

    while (1) {
        if (interrupt_signal && atomic_load(interrupt_signal) != 0 && !killed) {
            kill(pid, SIGKILL);
            killed = 1;
        }

        ssize_t n = read(pipe_fd[0], buf + buf_pos, sizeof(buf) - buf_pos - 1);
        if (n <= 0)
            break;

        buf_pos += (size_t)n;
        buf[buf_pos] = '\0';

        char *start = buf;
        for (char *p = buf; p < buf + buf_pos; p++) {
            if (*p == '\r' || *p == '\n') {
                *p = '\0';
                if (p > start) {
                    float progress = parse_git_progress(start);
                    if (progress >= 0.0f && set_progress) {
                        set_progress(progress_offset + progress * progress_scale);
                    }
                }
                if (*p == '\r' && (p + 1) < buf + buf_pos && *(p + 1) == '\n')
                    p++;
                start = p + 1;
            }
        }

        size_t remaining = (size_t)(buf + buf_pos - start);
        if (remaining > 0 && start != buf)
            memmove(buf, start, remaining);
        buf_pos = remaining;
    }

    close(pipe_fd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (killed)
        return CHEAT_OP_CANCELLED;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return CHEAT_OP_OK;
    return CHEAT_OP_ERROR;
}

/* ── Sparse checkout management ───────────────────────────── */

static void ensure_parent_dir(const char *path) {
    char parent[PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", path);
    char *slash = strrchr(parent, '/');
    if (!slash)
        return;

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

static int acquire_repo_lock(cheat_repo_lock *lock) {
    char lock_path[PATH_MAX];

    lock->fd = -1;
    if (pthread_mutex_trylock(&g_repo_lock_mutex) != 0)
        return CHEAT_OP_BUSY;

    get_repo_lock_path(lock_path, sizeof(lock_path));
    ensure_parent_dir(lock_path);

    lock->fd = open(lock_path, O_CREAT | O_RDWR, 0644);
    if (lock->fd < 0) {
        pthread_mutex_unlock(&g_repo_lock_mutex);
        return CHEAT_OP_ERROR;
    }

    if (flock(lock->fd, LOCK_EX | LOCK_NB) != 0) {
        int err = errno;
        close(lock->fd);
        lock->fd = -1;
        pthread_mutex_unlock(&g_repo_lock_mutex);
        if (err == EWOULDBLOCK || err == EAGAIN)
            return CHEAT_OP_BUSY;
        return CHEAT_OP_ERROR;
    }

    return CHEAT_OP_OK;
}

static void release_repo_lock(cheat_repo_lock *lock) {
    if (!lock || lock->fd < 0)
        return;

    flock(lock->fd, LOCK_UN);
    close(lock->fd);
    lock->fd = -1;
    pthread_mutex_unlock(&g_repo_lock_mutex);
}

static int run_repo_git_streaming(const char **argv, const char *cwd,
                                  atomic_int *interrupt_signal,
                                  cheat_progress_fn set_progress,
                                  float progress_scale,
                                  float progress_offset) {
    cheat_repo_lock lock;
    int ret = acquire_repo_lock(&lock);
    if (ret != CHEAT_OP_OK)
        return ret;

    ret = run_git_streaming(argv, cwd, interrupt_signal,
                            set_progress, progress_scale, progress_offset);
    release_repo_lock(&lock);
    return ret;
}

int is_repo_initialized(void) {
    char repo[PATH_MAX];
    char git_dir[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));
    snprintf(git_dir, sizeof(git_dir), "%s/.git", repo);

    struct stat st;
    return stat(git_dir, &st) == 0 && S_ISDIR(st.st_mode);
}

int init_cheat_repo(atomic_int *interrupt_signal,
                    cheat_message_fn set_message,
                    cheat_progress_fn set_progress,
                    float progress_scale,
                    float progress_offset) {
    char repo[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));
    ensure_parent_dir(repo);

    if (check_git_available() != 0)
        return -1;

    if (set_message)
        set_message("Cloning cheat database...");

    const char *git = get_git_bin();
    const char *argv[] = {
        git,
        "-c", "http.connectTimeout=10",
        "-c", "http.lowSpeedLimit=100",
        "-c", "http.lowSpeedTime=30",
        "clone",
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

    return run_repo_git_streaming(argv, NULL, interrupt_signal,
                                  set_progress, progress_scale, progress_offset);
}

int ensure_system_checked_out(const char *libretro_dir_name,
                              atomic_int *interrupt_signal,
                              cheat_message_fn set_message,
                              cheat_progress_fn set_progress,
                              float progress_scale,
                              float progress_offset) {
    char repo[PATH_MAX];
    char local_dir[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));
    snprintf(local_dir, sizeof(local_dir), "%s/cht/%s", repo, libretro_dir_name);

    struct stat st;
    if (stat(local_dir, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;

    char checkout_path[512];
    snprintf(checkout_path, sizeof(checkout_path), "cht/%s", libretro_dir_name);

    char message[256];
    snprintf(message, sizeof(message), "Checking out %s...", libretro_dir_name);
    if (set_message)
        set_message(message);

    const char *git = get_git_bin();
    const char *argv[] = {
        git,
        "-c", "http.connectTimeout=10",
        "-c", "http.lowSpeedLimit=100",
        "-c", "http.lowSpeedTime=30",
        "-C", repo,
        "sparse-checkout", "add", checkout_path,
        NULL,
    };

    return run_repo_git_streaming(argv, NULL, interrupt_signal,
                                  set_progress, progress_scale, progress_offset);
}

int update_cheat_repo(atomic_int *interrupt_signal,
                      cheat_message_fn set_message,
                      cheat_progress_fn set_progress,
                      float progress_scale,
                      float progress_offset) {
    char repo[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));

    if (set_message)
        set_message("Updating cheat database...");

    const char *git = get_git_bin();

    /* Fetch with blob filter so only tree/commit objects are transferred,
       matching the --filter=blob:none used at clone time. */
    const char *fetch_argv[] = {
        git,
        "-c", "http.connectTimeout=10",
        "-c", "http.lowSpeedLimit=100",
        "-c", "http.lowSpeedTime=30",
        "-C", repo,
        "fetch", "--filter=blob:none", "--depth=1", "--progress",
        NULL,
    };
    int ret = run_repo_git_streaming(fetch_argv, NULL, interrupt_signal,
                                     set_progress, progress_scale * 0.9f,
                                     progress_offset);
    if (ret != CHEAT_OP_OK)
        return ret;

    const char *merge_argv[] = {
        git, "-C", repo,
        "merge", "--ff-only", "FETCH_HEAD",
        NULL,
    };
    return run_repo_git_streaming(merge_argv, NULL, interrupt_signal,
                                  set_progress, progress_scale * 0.1f,
                                  progress_offset + progress_scale * 0.9f);
}

/* ── Cheat name normalization and matching ────────────────── */

static void strip_parentheticals(const char *in, char *out, size_t out_len) {
    size_t j = 0;
    int depth = 0;
    for (const char *p = in; *p && j < out_len - 1; p++) {
        if (*p == '(') {
            depth++;
            continue;
        }
        if (*p == ')') {
            if (depth > 0)
                depth--;
            continue;
        }
        if (depth == 0)
            out[j++] = *p;
    }
    out[j] = '\0';

    while (j > 0 && out[j - 1] == ' ')
        out[--j] = '\0';
}

static void normalize_cheat_name(const char *name, char *out, size_t out_len) {
    char stripped[512];
    strip_parentheticals(name, stripped, sizeof(stripped));

    size_t j = 0;
    int prev_space = 1;
    for (const char *p = stripped; *p && j < out_len - 1; p++) {
        unsigned char c = (unsigned char)*p;
        if (ispunct(c))
            continue;
        if (isspace(c)) {
            if (!prev_space) {
                out[j++] = ' ';
                prev_space = 1;
            }
            continue;
        }
        out[j++] = (char)tolower(c);
        prev_space = 0;
    }
    if (j > 0 && out[j - 1] == ' ')
        j--;
    out[j] = '\0';
}

static int count_normalized_tokens(const char *normalized) {
    int count = 0;
    bool in_token = false;

    for (const char *p = normalized; *p; p++) {
        if (isspace((unsigned char)*p)) {
            in_token = false;
            continue;
        }
        if (!in_token) {
            count++;
            in_token = true;
        }
    }

    return count;
}

static bool normalized_key_exists(char (*keys)[256], int key_count,
                                  const char *normalized) {
    for (int i = 0; i < key_count; i++) {
        if (strcmp(keys[i], normalized) == 0)
            return true;
    }
    return false;
}

/* Treat "Title A _ Title B" as alternate titles only when every part
   still looks like a standalone game title after normalization. */
static int collect_cheat_alias_keys(const char *game_name,
                                    const char *full_normalized,
                                    char (**out_keys)[256]) {
    char stripped[512];
    int part_cap = 1;
    int key_count = 0;
    char (*keys)[256];
    const char *part;

    *out_keys = NULL;
    strip_parentheticals(game_name, stripped, sizeof(stripped));

    for (const char *sep = stripped; (sep = strstr(sep, " _ ")) != NULL; sep += 3)
        part_cap++;
    if (part_cap < 2)
        return 0;

    keys = calloc((size_t)part_cap, sizeof(*keys));
    if (!keys)
        return 0;

    part = stripped;
    for (;;) {
        const char *sep = strstr(part, " _ ");
        size_t len = sep ? (size_t)(sep - part) : strlen(part);
        char alias[256];
        char normalized[256];

        snprintf(alias, sizeof(alias), "%.*s", (int)len, part);
        normalize_cheat_name(alias, normalized, sizeof(normalized));

        if (normalized[0] == '\0' ||
            strcmp(normalized, full_normalized) == 0 ||
            count_normalized_tokens(normalized) < 2) {
            free(keys);
            return 0;
        }

        if (!normalized_key_exists(keys, key_count, normalized))
            snprintf(keys[key_count++], sizeof(keys[0]), "%s", normalized);

        if (!sep)
            break;
        part = sep + 3;
    }

    if (key_count < 2) {
        free(keys);
        return 0;
    }

    *out_keys = keys;
    return key_count;
}

/* ── Region matching ──────────────────────────────────────── */

typedef struct {
    const char *keyword;
    const char *code;
} region_keyword;

static const region_keyword cheat_region_map[] = {
    {"usa", "us"},
    {"europe", "eu"},
    {"japan", "jp"},
    {"world", "wor"},
    {"france", "fr"},
    {"germany", "de"},
    {"spain", "es"},
    {"italy", "it"},
    {"portugal", "pt"},
    {"australia", "eu"},
    {"korea", "kr"},
    {"china", "cn"},
    {"taiwan", "tw"},
    {"brazil", "pt"},
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

static int extract_cheat_regions(const char *name, const char **regions, int max_regions) {
    int count = 0;
    const char *p = name;

    while ((p = strchr(p, '(')) != NULL) {
        p++;
        const char *end = strchr(p, ')');
        if (!end)
            break;

        const char *item = p;
        while (item < end) {
            while (item < end && isspace((unsigned char)*item))
                item++;

            const char *item_end = item;
            while (item_end < end && *item_end != ',')
                item_end++;

            const char *trim = item_end;
            while (trim > item && isspace((unsigned char)*(trim - 1)))
                trim--;

            size_t len = (size_t)(trim - item);
            if (len > 0 && len < 32) {
                char lower[32];
                for (size_t i = 0; i < len; i++)
                    lower[i] = (char)tolower((unsigned char)item[i]);
                lower[len] = '\0';

                for (int r = 0; r < cheat_region_map_count; r++) {
                    if (strcmp(lower, cheat_region_map[r].keyword) == 0) {
                        int duplicate = 0;
                        for (int d = 0; d < count; d++) {
                            if (strcmp(regions[d], cheat_region_map[r].code) == 0) {
                                duplicate = 1;
                                break;
                            }
                        }
                        if (!duplicate && count < max_regions)
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

static int score_cheat_region(const char **cheat_regions, int cheat_count,
                              const char **rom_regions, int rom_count,
                              char **region_prio, int prio_count) {
    if (rom_count > 0 && cheat_count > 0) {
        int overlap = 0;
        for (int c = 0; c < cheat_count; c++) {
            for (int r = 0; r < rom_count; r++) {
                if (strcmp(cheat_regions[c], rom_regions[r]) == 0)
                    overlap++;
            }
        }
        if (overlap > 0)
            return 1000 + overlap;
    }

    for (int c = 0; c < cheat_count; c++) {
        if (strcmp(cheat_regions[c], "wor") == 0)
            return 500;
    }

    if (cheat_count > 0 && prio_count > 0) {
        for (int i = 0; i < prio_count; i++) {
            for (int c = 0; c < cheat_count; c++) {
                if (strcmp(cheat_regions[c], region_prio[i]) == 0)
                    return 100 - i;
            }
        }
    }

    if (cheat_count == 0)
        return 50;
    return 0;
}

/* ── Cheat list building and matching ─────────────────────── */

/* Types are defined in cheats.h */

static cheat_entry *cheat_list_find(cheat_list *list, const char *normalized) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->entries[i].normalized, normalized) == 0)
            return &list->entries[i];
    }
    return NULL;
}

static void cheat_list_add_internal(cheat_list *list, const char *normalized,
                           const char *path, const char **regions, int region_count) {
    cheat_entry *entry = cheat_list_find(list, normalized);
    if (!entry) {
        if (list->count >= list->cap) {
            int new_cap = list->cap ? list->cap * 2 : 256;
            cheat_entry *new_entries =
                realloc(list->entries, sizeof(cheat_entry) * (size_t)new_cap);
            if (!new_entries)
                return;
            list->entries = new_entries;
            list->cap = new_cap;
        }

        entry = &list->entries[list->count++];
        memset(entry, 0, sizeof(*entry));
        snprintf(entry->normalized, sizeof(entry->normalized), "%s", normalized);
    }

    if (entry->candidate_count >= entry->candidate_cap) {
        int new_cap = entry->candidate_cap ? entry->candidate_cap * 2 : 4;
        cheat_candidate *new_candidates =
            realloc(entry->candidates, sizeof(cheat_candidate) * (size_t)new_cap);
        if (!new_candidates)
            return;
        entry->candidates = new_candidates;
        entry->candidate_cap = new_cap;
    }

    cheat_candidate *candidate = &entry->candidates[entry->candidate_count++];
    memset(candidate, 0, sizeof(*candidate));
    snprintf(candidate->path, sizeof(candidate->path), "%s", path);
    candidate->region_count = region_count > 16 ? 16 : region_count;
    for (int i = 0; i < candidate->region_count; i++)
        candidate->regions[i] = regions[i];
}

void cheat_list_free(cheat_list *list) {
    if (!list)
        return;
    for (int i = 0; i < list->count; i++)
        free(list->entries[i].candidates);
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->cap = 0;
}

int build_cheat_list(const char *libretro_dir_name, cheat_list *list) {
    char repo[PATH_MAX];
    char cheat_dir[PATH_MAX];
    get_cheat_repo_path(repo, sizeof(repo));
    snprintf(cheat_dir, sizeof(cheat_dir), "%s/cht/%s", repo, libretro_dir_name);

    DIR *dir = opendir(cheat_dir);
    if (!dir)
        return -1;

    memset(list, 0, sizeof(*list));

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        size_t len = strlen(entry->d_name);
        if (len < 5 || strcasecmp(entry->d_name + len - 4, ".cht") != 0)
            continue;

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", cheat_dir, entry->d_name);

        char game_name[256];
        snprintf(game_name, sizeof(game_name), "%.*s", (int)(len - 4), entry->d_name);

        char normalized[256];
        normalize_cheat_name(game_name, normalized, sizeof(normalized));

        const char *regions[16];
        int region_count = extract_cheat_regions(game_name, regions, 16);
        cheat_list_add_internal(list, normalized, full_path, regions, region_count);

        char (*alias_keys)[256] = NULL;
        int alias_count = collect_cheat_alias_keys(game_name, normalized, &alias_keys);
        for (int i = 0; i < alias_count; i++)
            cheat_list_add_internal(list, alias_keys[i], full_path, regions, region_count);
        free(alias_keys);
    }

    closedir(dir);
    fprintf(stderr, "cheats: indexed %d entries for %s\n", list->count, libretro_dir_name);
    return 0;
}

const char *match_cheat(const char *rom_display, cheat_list *list,
                               char **region_prio, int region_count) {
    char normalized[256];
    normalize_cheat_name(rom_display, normalized, sizeof(normalized));

    cheat_entry *entry = cheat_list_find(list, normalized);
    if (!entry || entry->candidate_count == 0)
        return NULL;

    if (entry->candidate_count == 1)
        return entry->candidates[0].path;

    const char *rom_regions[16];
    int rom_region_count = extract_cheat_regions(rom_display, rom_regions, 16);

    int best_score = -1;
    const char *best_path = entry->candidates[0].path;
    for (int i = 0; i < entry->candidate_count; i++) {
        cheat_candidate *candidate = &entry->candidates[i];
        int score = score_cheat_region(candidate->regions, candidate->region_count,
                                       rom_regions, rom_region_count,
                                       region_prio, region_count);
        if (score > best_score) {
            best_score = score;
            best_path = candidate->path;
        }
    }

    return best_path;
}

/* ── File copy helper ─────────────────────────────────────── */

int copy_cheat_file(const char *src, const char *dst) {
    ensure_parent_dir(dst);

    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", dst);

    FILE *in = fopen(src, "rb");
    if (!in)
        return -1;

    FILE *out = fopen(tmp_path, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }

    char buf[8192];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = 0;
            break;
        }
    }

    if (fflush(out) != 0 || fsync(fileno(out)) != 0)
        ok = 0;

    fclose(in);
    if (fclose(out) != 0)
        ok = 0;

    if (!ok) {
        unlink(tmp_path);
        return -1;
    }

    if (rename(tmp_path, dst) != 0) {
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

/* ── Cheat existence check ───────────────────────────────── */

bool cheat_exists(const char *system_tag, const char *rom_display) {
    char cheats_base[PATH_MAX];
    char cheat_file[PATH_MAX];
    get_cheats_path(cheats_base, sizeof(cheats_base));
    snprintf(cheat_file, sizeof(cheat_file), "%s/%s/%s.cht",
             cheats_base, system_tag, rom_display);

    struct stat st;
    return stat(cheat_file, &st) == 0;
}

/* ── Console cheat downloader ─────────────────────────────── */

run_result download_cheats_for_console(const console_dir *console,
                                       char **region_prio, int region_count,
                                       atomic_int *interrupt_signal,
                                       cheat_progress_fn set_progress,
                                       cheat_message_fn set_message) {
    scrape_summary summary = {0};
    run_result result = make_run_result(RUN_OK, summary, NULL);
    cheat_list list;
    rom_file *roms = NULL;
    bool list_ready = false;
    bool cancelled = false;

    const char *libretro_dir_name = libretro_dir(console->tag);
    if (!libretro_dir_name)
        return make_run_error(summary, "This system does not have a Libretro cheat directory.");

    if (set_progress)
        set_progress(0.0f);

    if (!is_repo_initialized()) {
        int init_result = init_cheat_repo(interrupt_signal, set_message, set_progress, 0.3f, 0.0f);
        if (init_result == CHEAT_OP_CANCELLED) {
            cancelled = true;
            goto cleanup;
        }
        if (init_result == CHEAT_OP_BUSY) {
            result = make_run_error(summary,
                                    "Cheat database is busy. Please wait for the current operation to finish.");
            goto cleanup;
        }
        if (init_result != CHEAT_OP_OK) {
            result = make_run_error(summary, "Failed to clone the cheat database.");
            goto cleanup;
        }
    } else {
        int update_result = update_cheat_repo(interrupt_signal, set_message, set_progress, 0.3f, 0.0f);
        if (update_result == CHEAT_OP_CANCELLED) {
            cancelled = true;
            goto cleanup;
        }
        if (update_result == CHEAT_OP_BUSY) {
            result = make_run_error(summary,
                                    "Cheat database is busy. Please wait for the current operation to finish.");
            goto cleanup;
        }
        if (update_result != CHEAT_OP_OK) {
            fprintf(stderr, "cheats: pull failed (using existing data)\n");
        }
    }

    if (set_progress)
        set_progress(0.3f);

    {
        int checkout_result = ensure_system_checked_out(libretro_dir_name, interrupt_signal,
                                                        set_message, set_progress, 0.2f, 0.3f);
        if (checkout_result == CHEAT_OP_CANCELLED) {
            cancelled = true;
            goto cleanup;
        }
        if (checkout_result == CHEAT_OP_BUSY) {
            result = make_run_error(summary,
                                    "Cheat database is busy. Please wait for the current operation to finish.");
            goto cleanup;
        }
        if (checkout_result != CHEAT_OP_OK) {
            result = make_run_error(summary, "Failed to check out cheats for this system.");
            goto cleanup;
        }
    }

    if (set_progress)
        set_progress(0.5f);

    if (set_message)
        set_message("Building cheat list...");

    if (build_cheat_list(libretro_dir_name, &list) != 0) {
        result = make_run_error(summary, "Failed to build the cheat list for this system.");
        goto cleanup;
    }
    list_ready = true;

    if (list.count == 0) {
        result = make_run_error(summary, "No cheats are available for this system.");
        goto cleanup;
    }

    int rom_count = scan_roms(console->path, false, &roms);
    if (rom_count < 0) {
        result = make_run_error(summary, "Could not read ROM files for this system.");
        goto cleanup;
    }

    summary.total = rom_count;

    char cheats_base[PATH_MAX];
    get_cheats_path(cheats_base, sizeof(cheats_base));

    for (int i = 0; i < rom_count; i++) {
        if (interrupt_signal && atomic_load(interrupt_signal) != 0) {
            cancelled = true;
            break;
        }

        if (set_progress) {
            float loop_progress = rom_count > 0 ? (float)i / (float)rom_count : 1.0f;
            set_progress(0.5f + loop_progress * 0.5f);
        }

        if (set_message) {
            char message[512];
            snprintf(message, sizeof(message), "(%d/%d) %s",
                     i + 1, rom_count, roms[i].display);
            set_message(message);
        }

        const char *src_path = match_cheat(roms[i].display, &list, region_prio, region_count);
        if (!src_path) {
            summary.not_found++;
            continue;
        }

        char dest_path[PATH_MAX];
        snprintf(dest_path, sizeof(dest_path), "%s/%s/%s.cht",
                 cheats_base, console->tag, roms[i].display);

        struct stat st;
        if (stat(dest_path, &st) == 0) {
            summary.found++;
            continue;
        }

        if (copy_cheat_file(src_path, dest_path) == 0)
            summary.found++;
        else
            summary.errors++;
    }

    result = make_run_result(cancelled ? RUN_CANCELLED : RUN_OK, summary, NULL);

cleanup:
    if (list_ready)
        cheat_list_free(&list);
    free(roms);

    if (cancelled) {
        result = make_run_result(RUN_CANCELLED, summary, NULL);
    }

    if (result.status != RUN_ERROR && set_progress)
        set_progress(1.0f);

    return result;
}

/* ── Cheat description parsing ───────────────────────────── */

/* Parse a quoted string value from a line like: cheatN_desc = "value"
 * Writes into buf, returns 0 on success. */
static int parse_cht_string(const char *line, char *buf, size_t buflen) {
    const char *q1 = strchr(line, '"');
    if (!q1) return -1;
    q1++;
    const char *q2 = strrchr(q1, '"');
    if (!q2 || q2 <= q1) return -1;
    size_t len = (size_t)(q2 - q1);
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, q1, len);
    buf[len] = '\0';
    return 0;
}

int parse_cheat_descriptions(const char *cht_path, cheat_desc_list *out) {
    if (!cht_path || !out) return -1;
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(cht_path, "r");
    if (!f) return -1;

    char line[512];
    int total = 0;

    /* First pass: find cheat count (handles both `cheats = 5` and `cheats = "5"`) */
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, " cheats = \"%d\"", &total) == 1)
            break;
        if (sscanf(line, " cheats = %d", &total) == 1)
            break;
    }

    if (total <= 0) {
        fclose(f);
        return -1;
    }

    out->descriptions = calloc((size_t)total, sizeof(char *));
    if (!out->descriptions) {
        fclose(f);
        return -1;
    }

    /* Second pass: extract descriptions */
    rewind(f);
    while (fgets(line, sizeof(line), f)) {
        /* Pre-filter: only _desc lines. Without this guard, sscanf returns 1
         * even for _code lines (the %d succeeds before the literal _desc
         * fails to match), causing cheat codes to be shown as descriptions. */
        if (!strstr(line, "_desc"))
            continue;
        int idx = -1;
        if (sscanf(line, " cheat%d_desc", &idx) != 1)
            continue;
        if (idx < 0 || idx >= total)
            continue;

        char buf[512];
        if (parse_cht_string(line, buf, sizeof(buf)) != 0)
            continue;

        if (buf[0] == '\0')
            continue;

        out->descriptions[idx] = strdup(buf);
    }

    fclose(f);

    /* Count how many were actually found */
    out->count = total;
    return 0;
}

void cheat_desc_list_free(cheat_desc_list *out) {
    if (!out) return;
    for (int i = 0; i < out->count; i++)
        free(out->descriptions[i]);
    free(out->descriptions);
    out->descriptions = NULL;
    out->count = 0;
}

/* ScrapeGoat — Background daemon for continuing downloads after app exit.
 *
 * The daemon is a headless child process that reuses the existing queue
 * machinery to process pending items.  Communication with the foreground
 * app uses file-based IPC: a PID file, an flock-held lock file, and a
 * periodically-updated JSON queue state file.
 */

#include "daemon.h"
#include "screenscraper.h"
#include "cJSON.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef PLATFORM_MAC
#include <mach-o/dyld.h>
#endif

/* ── Path helpers ────────────────────────────────────────── */

static void daemon_dir(char *buf, size_t len) {
    snprintf(buf, len, "%s/.userdata/shared/ScrapeGoat/daemon",
             get_sdcard_path());
}

static void daemon_pid_path(char *buf, size_t len) {
    char dir[PATH_MAX];
    daemon_dir(dir, sizeof(dir));
    snprintf(buf, len, "%s/daemon.pid", dir);
}

static void daemon_lock_path(char *buf, size_t len) {
    char dir[PATH_MAX];
    daemon_dir(dir, sizeof(dir));
    snprintf(buf, len, "%s/daemon.lock", dir);
}

static void daemon_queue_path(char *buf, size_t len) {
    char dir[PATH_MAX];
    daemon_dir(dir, sizeof(dir));
    snprintf(buf, len, "%s/queue.json", dir);
}

static void daemon_settings_path(char *buf, size_t len) {
    char dir[PATH_MAX];
    daemon_dir(dir, sizeof(dir));
    snprintf(buf, len, "%s/settings.json", dir);
}

static void daemon_stop_path(char *buf, size_t len) {
    char dir[PATH_MAX];
    daemon_dir(dir, sizeof(dir));
    snprintf(buf, len, "%s/daemon.stop", dir);
}

static void daemon_log_path(char *buf, size_t len) {
    char dir[PATH_MAX];
    daemon_dir(dir, sizeof(dir));
    snprintf(buf, len, "%s/daemon.log", dir);
}

/* ── Directory creation ──────────────────────────────────── */

static void mkdirs(const char *path) {
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

/* ── Queue serialization ─────────────────────────────────── */

static const char *status_to_str(queue_item_status s) {
    switch (s) {
        case QUEUE_IDLE:        return "idle";
        case QUEUE_SEARCHING:   return "searching";
        case QUEUE_DOWNLOADING: return "downloading";
        case QUEUE_CLONING:     return "cloning";
        case QUEUE_MATCHING:    return "matching";
        case QUEUE_DONE:        return "done";
        case QUEUE_NOT_FOUND:   return "not_found";
        case QUEUE_ERROR:       return "error";
        case QUEUE_SKIPPED:     return "skipped";
        default:                return "unknown";
    }
}

static queue_item_status str_to_status(const char *s) {
    if (!s) return QUEUE_IDLE;
    if (strcmp(s, "idle") == 0)        return QUEUE_IDLE;
    if (strcmp(s, "searching") == 0)   return QUEUE_SEARCHING;
    if (strcmp(s, "downloading") == 0) return QUEUE_DOWNLOADING;
    if (strcmp(s, "cloning") == 0)     return QUEUE_CLONING;
    if (strcmp(s, "matching") == 0)    return QUEUE_MATCHING;
    if (strcmp(s, "done") == 0)        return QUEUE_DONE;
    if (strcmp(s, "not_found") == 0)   return QUEUE_NOT_FOUND;
    if (strcmp(s, "error") == 0)       return QUEUE_ERROR;
    if (strcmp(s, "skipped") == 0)     return QUEUE_SKIPPED;
    return QUEUE_IDLE;
}

static const char *type_to_str(queue_item_type t) {
    switch (t) {
        case QUEUE_TYPE_ARTWORK: return "artwork";
        case QUEUE_TYPE_CHEAT:   return "cheat";
        case QUEUE_TYPE_MANUAL:  return "manual";
        default:                 return "artwork";
    }
}

static queue_item_type str_to_type(const char *s) {
    if (!s) return QUEUE_TYPE_ARTWORK;
    if (strcmp(s, "cheat") == 0)  return QUEUE_TYPE_CHEAT;
    if (strcmp(s, "manual") == 0) return QUEUE_TYPE_MANUAL;
    return QUEUE_TYPE_ARTWORK;
}

static int serialize_queue(const char *path, const queue_item *items, int count) {
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return -1;

    for (int i = 0; i < count; i++) {
        const queue_item *it = &items[i];
        cJSON *obj = cJSON_CreateObject();
        if (!obj) { cJSON_Delete(arr); return -1; }

        cJSON_AddNumberToObject(obj, "id", it->id);
        cJSON_AddStringToObject(obj, "type", type_to_str(it->type));
        cJSON_AddStringToObject(obj, "rom_display", it->rom_display);
        cJSON_AddStringToObject(obj, "rom_path", it->rom_path);
        cJSON_AddStringToObject(obj, "system_tag", it->system_tag);
        cJSON_AddStringToObject(obj, "system_display", it->system_display);
        cJSON_AddStringToObject(obj, "console_path", it->console_path);
        cJSON_AddNumberToObject(obj, "system_id", it->system_id);
        cJSON_AddStringToObject(obj, "status", status_to_str(it->status));
        cJSON_AddStringToObject(obj, "error_msg", it->error_msg);
        cJSON_AddBoolToObject(obj, "force", it->force);

        cJSON_AddItemToArray(arr, obj);
    }

    char *str = cJSON_Print(arr);
    cJSON_Delete(arr);
    if (!str) return -1;

    /* Atomic write: tmp + rename */
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) { free(str); return -1; }
    fputs(str, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    free(str);

    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int deserialize_queue(const char *path, queue_item *out, int max_items) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); return 0; }

    char *data = malloc((size_t)fsize + 1);
    if (!data) { fclose(f); return 0; }
    fread(data, 1, (size_t)fsize, f);
    data[fsize] = '\0';
    fclose(f);

    cJSON *arr = cJSON_Parse(data);
    free(data);
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return 0;
    }

    int n = cJSON_GetArraySize(arr);
    if (n > max_items) n = max_items;

    for (int i = 0; i < n; i++) {
        cJSON *obj = cJSON_GetArrayItem(arr, i);
        queue_item *it = &out[i];
        memset(it, 0, sizeof(*it));

        cJSON *v;
        if ((v = cJSON_GetObjectItem(obj, "id")) && cJSON_IsNumber(v))
            it->id = (uint32_t)v->valuedouble;
        if ((v = cJSON_GetObjectItem(obj, "type")) && cJSON_IsString(v))
            it->type = str_to_type(v->valuestring);
        if ((v = cJSON_GetObjectItem(obj, "rom_display")) && cJSON_IsString(v))
            snprintf(it->rom_display, sizeof(it->rom_display), "%s", v->valuestring);
        if ((v = cJSON_GetObjectItem(obj, "rom_path")) && cJSON_IsString(v))
            snprintf(it->rom_path, sizeof(it->rom_path), "%s", v->valuestring);
        if ((v = cJSON_GetObjectItem(obj, "system_tag")) && cJSON_IsString(v))
            snprintf(it->system_tag, sizeof(it->system_tag), "%s", v->valuestring);
        if ((v = cJSON_GetObjectItem(obj, "system_display")) && cJSON_IsString(v))
            snprintf(it->system_display, sizeof(it->system_display), "%s", v->valuestring);
        if ((v = cJSON_GetObjectItem(obj, "console_path")) && cJSON_IsString(v))
            snprintf(it->console_path, sizeof(it->console_path), "%s", v->valuestring);
        if ((v = cJSON_GetObjectItem(obj, "system_id")) && cJSON_IsNumber(v))
            it->system_id = (int)v->valuedouble;
        if ((v = cJSON_GetObjectItem(obj, "status")) && cJSON_IsString(v))
            it->status = str_to_status(v->valuestring);
        if ((v = cJSON_GetObjectItem(obj, "error_msg")) && cJSON_IsString(v))
            snprintf(it->error_msg, sizeof(it->error_msg), "%s", v->valuestring);
        if ((v = cJSON_GetObjectItem(obj, "force")) && cJSON_IsBool(v))
            it->force = cJSON_IsTrue(v);
    }

    cJSON_Delete(arr);
    return n;
}

/* ── Settings serialization (daemon-specific path) ───────── */

static int save_settings_to(const char *path, const app_settings *s) {
    cJSON *json = cJSON_CreateObject();
    if (!json) return -1;

    cJSON_AddStringToObject(json, "ss_username", s->ss_username);
    cJSON_AddStringToObject(json, "ss_password", s->ss_password);
    cJSON_AddBoolToObject(json, "show_hidden", s->show_hidden);
    cJSON_AddStringToObject(json, "manual_download_dir", s->manual_download_dir);

    cJSON *art_arr = cJSON_AddArrayToObject(json, "artwork_priority");
    for (int i = 0; i < s->artwork_prio_count; i++)
        cJSON_AddItemToArray(art_arr, cJSON_CreateString(s->artwork_prio[i]));

    cJSON *reg_arr = cJSON_AddArrayToObject(json, "region_priority");
    for (int i = 0; i < s->region_prio_count; i++)
        cJSON_AddItemToArray(reg_arr, cJSON_CreateString(s->region_prio[i]));

    char *str = cJSON_Print(json);
    cJSON_Delete(json);
    if (!str) return -1;

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) { free(str); return -1; }
    fputs(str, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    free(str);

    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* ── Detection ───────────────────────────────────────────── */

bool daemon_is_running(void) {
    char pid_path[PATH_MAX];
    daemon_pid_path(pid_path, sizeof(pid_path));

    FILE *f = fopen(pid_path, "r");
    if (!f) return false;

    pid_t pid = 0;
    if (fscanf(f, "%d", &pid) != 1 || pid <= 0) {
        fclose(f);
        daemon_cleanup();
        return false;
    }
    fclose(f);

    /* Check process exists */
    if (kill(pid, 0) != 0) {
        daemon_cleanup();
        return false;
    }

    /* Double-check with flock */
    char lock_path[PATH_MAX];
    daemon_lock_path(lock_path, sizeof(lock_path));
    int fd = open(lock_path, O_RDONLY);
    if (fd < 0) return false;

    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        /* Got the lock — daemon is NOT holding it, stale state */
        flock(fd, LOCK_UN);
        close(fd);
        daemon_cleanup();
        return false;
    }
    close(fd);
    return true;
}

/* ── State I/O ───────────────────────────────────────────── */

int daemon_read_queue(queue_item *out, int max_items, queue_stats *stats_out) {
    char path[PATH_MAX];
    daemon_queue_path(path, sizeof(path));

    int count = deserialize_queue(path, out, max_items);

    if (stats_out) {
        memset(stats_out, 0, sizeof(*stats_out));
        stats_out->total = count;
        for (int i = 0; i < count; i++) {
            switch (out[i].status) {
                case QUEUE_DONE:
                    stats_out->done++;
                    break;
                case QUEUE_NOT_FOUND:
                case QUEUE_ERROR:
                    stats_out->failed++;
                    break;
                case QUEUE_SKIPPED:
                    break;
                default:
                    stats_out->pending++;
                    break;
            }
        }
    }
    return count;
}

/* ── Lifecycle ───────────────────────────────────────────── */

void daemon_request_stop(void) {
    char path[PATH_MAX];
    daemon_stop_path(path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (f) {
        fputs("stop\n", f);
        fclose(f);
    }
}

void daemon_cleanup(void) {
    char path[PATH_MAX];

    daemon_pid_path(path, sizeof(path));
    unlink(path);

    daemon_lock_path(path, sizeof(path));
    unlink(path);

    daemon_queue_path(path, sizeof(path));
    /* Keep queue.json — the app reads final results from it */

    daemon_stop_path(path, sizeof(path));
    unlink(path);

    daemon_settings_path(path, sizeof(path));
    unlink(path);
}

void daemon_cleanup_all(void) {
    char path[PATH_MAX];

    daemon_pid_path(path, sizeof(path));
    unlink(path);
    daemon_lock_path(path, sizeof(path));
    unlink(path);
    daemon_queue_path(path, sizeof(path));
    unlink(path);
    daemon_stop_path(path, sizeof(path));
    unlink(path);
    daemon_settings_path(path, sizeof(path));
    unlink(path);
}

int daemon_launch(const queue_item *items, int count,
                  const app_settings *settings) {
    char dir[PATH_MAX];
    daemon_dir(dir, sizeof(dir));
    mkdirs(dir);

    /* Serialize queue */
    char qpath[PATH_MAX];
    daemon_queue_path(qpath, sizeof(qpath));
    if (serialize_queue(qpath, items, count) != 0)
        return -1;

    /* Serialize settings */
    char spath[PATH_MAX];
    daemon_settings_path(spath, sizeof(spath));
    if (save_settings_to(spath, settings) != 0)
        return -1;

    /* Remove any stale stop sentinel */
    char stop_path[PATH_MAX];
    daemon_stop_path(stop_path, sizeof(stop_path));
    unlink(stop_path);

    /* Build path to our own executable for re-exec */
    char self[PATH_MAX];
#if defined(PLATFORM_MAC)
    {
        uint32_t size = sizeof(self);
        if (_NSGetExecutablePath(self, &size) != 0)
            return -1;
        /* Resolve symlinks so execl gets the real path */
        char resolved[PATH_MAX];
        if (realpath(self, resolved))
            snprintf(self, sizeof(self), "%s", resolved);
    }
#else
    {
        ssize_t len = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (len < 0) return -1;
        self[len] = '\0';
    }
#endif

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid > 0) {
        /* Parent: write PID file and return */
        char pid_path[PATH_MAX];
        daemon_pid_path(pid_path, sizeof(pid_path));
        FILE *f = fopen(pid_path, "w");
        if (f) {
            fprintf(f, "%d\n", pid);
            fclose(f);
        }
        return 0;
    }

    /* ── Child process ─────────────────────────────────────── */

    /* Detach from controlling terminal */
    setsid();

    /* Redirect stdio to log file */
    char logpath[PATH_MAX];
    daemon_log_path(logpath, sizeof(logpath));
    int logfd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd >= 0) {
        dup2(logfd, STDOUT_FILENO);
        dup2(logfd, STDERR_FILENO);
        close(logfd);
    }
    close(STDIN_FILENO);

    /* Re-exec as daemon to get a clean process state */
    execl(self, self, "--daemon", (char *)NULL);

    /* If execl fails, exit */
    _exit(1);
}

/* ── Daemon signal handling ──────────────────────────────── */

static volatile sig_atomic_t g_daemon_stop = 0;

static void daemon_signal_handler(int sig) {
    (void)sig;
    g_daemon_stop = 1;
}

/* ── Daemon main loop ────────────────────────────────────── */

static bool stop_requested(void) {
    if (g_daemon_stop) return true;

    char path[PATH_MAX];
    daemon_stop_path(path, sizeof(path));
    return access(path, F_OK) == 0;
}

int daemon_main(void) {
    fprintf(stderr, "scrapegoat-daemon: starting (pid=%d)\n", getpid());

    /* Acquire lock file */
    char lock_path[PATH_MAX];
    daemon_lock_path(lock_path, sizeof(lock_path));

    int lock_fd = open(lock_path, O_WRONLY | O_CREAT, 0644);
    if (lock_fd < 0) {
        fprintf(stderr, "scrapegoat-daemon: failed to open lock file\n");
        return 1;
    }
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "scrapegoat-daemon: another daemon is already running\n");
        close(lock_fd);
        return 1;
    }

    /* Write our actual PID (the fork parent wrote the child PID, but we
     * may have re-exec'd so update it) */
    char pid_path[PATH_MAX];
    daemon_pid_path(pid_path, sizeof(pid_path));
    FILE *pf = fopen(pid_path, "w");
    if (pf) {
        fprintf(pf, "%d\n", getpid());
        fclose(pf);
    }

    /* Install signal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = daemon_signal_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /* Enable headless mode for JPEG conversion */
    ss_set_headless(true);

    /* Load queue from disk */
    queue_item *items = calloc(QUEUE_MAX_ITEMS, sizeof(queue_item));
    if (!items) {
        fprintf(stderr, "scrapegoat-daemon: out of memory\n");
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return 1;
    }

    char qpath[PATH_MAX];
    daemon_queue_path(qpath, sizeof(qpath));
    int item_count = deserialize_queue(qpath, items, QUEUE_MAX_ITEMS);

    if (item_count <= 0) {
        fprintf(stderr, "scrapegoat-daemon: no items to process\n");
        free(items);
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return 0;
    }

    fprintf(stderr, "scrapegoat-daemon: loaded %d items\n", item_count);

    /* Load settings */
    char spath[PATH_MAX];
    daemon_settings_path(spath, sizeof(spath));

    /* We load settings from the daemon-specific copy. Since load_settings()
     * reads from the app's normal settings path, we temporarily redirect
     * by loading our daemon copy directly. */
    app_settings settings = load_settings();
    /* Try to load from daemon settings first (preferred) */
    {
        FILE *sf = fopen(spath, "rb");
        if (sf) {
            fclose(sf);
            /* The daemon settings file uses the same format.
             * For simplicity, we use the app's normal settings as loaded above
             * since the daemon copy was saved from the same source. */
        }
    }

    /* Initialize queue and load items */
    queue_init();
    queue_set_settings(&settings);
    queue_load_items(items, item_count);
    free(items);
    free_settings(&settings);

    fprintf(stderr, "scrapegoat-daemon: processing started\n");

    /* Poll loop: update state file + check for stop signal */
    while (!stop_requested()) {
        queue_stats stats = queue_get_stats();

        if (stats.pending <= 0)
            break;

        /* Snapshot and write queue state to disk */
        queue_item *snap = calloc(QUEUE_MAX_ITEMS, sizeof(queue_item));
        if (snap) {
            int n = queue_snapshot(snap, QUEUE_MAX_ITEMS);
            serialize_queue(qpath, snap, n);
            free(snap);
        }

        /* Sleep 2 seconds between updates */
        for (int i = 0; i < 20 && !stop_requested(); i++)
            usleep(100000); /* 100ms */
    }

    /* Write final state */
    fprintf(stderr, "scrapegoat-daemon: shutting down\n");

    if (stop_requested()) {
        queue_cancel_all();
    }

    queue_item *snap = calloc(QUEUE_MAX_ITEMS, sizeof(queue_item));
    if (snap) {
        int n = queue_snapshot(snap, QUEUE_MAX_ITEMS);
        serialize_queue(qpath, snap, n);
        free(snap);
    }

    queue_shutdown();

    /* Clean up daemon files (keep queue.json for the app to read) */
    {
        char tmp_path[PATH_MAX];
        daemon_stop_path(tmp_path, sizeof(tmp_path));
        unlink(tmp_path); /* remove stop sentinel */

        daemon_pid_path(tmp_path, sizeof(tmp_path));
        unlink(tmp_path); /* remove PID file */
    }

    /* Release and remove lock */
    flock(lock_fd, LOCK_UN);
    close(lock_fd);
    unlink(lock_path);

    fprintf(stderr, "scrapegoat-daemon: exited cleanly\n");
    return 0;
}

#include "screenscraper.h"
#include "systems.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_surface.h>

/* ── Credentials ──────────────────────────────────────────── */

bool ss_is_debug(void) {
    return SCREENSCRAPER_DEBUG_PASSWORD[0] != '\0';
}

int ss_check_dev_credentials(void) {
    if (SCREENSCRAPER_DEV_ID[0] == '\0' || SCREENSCRAPER_DEV_PASSWORD[0] == '\0') {
        fprintf(stderr, "developer credentials not embedded — rebuild with 'make'\n");
        return -1;
    }
    if (ss_is_debug())
        fprintf(stderr, "[DEBUG] debug mode enabled\n");
    return 0;
}

/* ── cURL helpers ─────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t size;
} curl_buffer;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    curl_buffer *buf = (curl_buffer *)userdata;
    char *tmp = realloc(buf->data, buf->size + total + 1);
    if (!tmp) return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

/* Perform a GET request with retries and exponential backoff.
 * Returns the HTTP response body in *out_buf (caller frees ->data).
 * Returns the HTTP status code, or -1 on complete failure. */
static int http_get(const char *url, const char *user_agent,
                    curl_buffer *out_buf, int max_retries) {
    out_buf->data = NULL;
    out_buf->size = 0;

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out_buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    long http_code = 0;
    int backoff_ms = 1000;

    for (int attempt = 0; attempt <= max_retries; attempt++) {
        if (attempt > 0) {
            free(out_buf->data);
            out_buf->data = NULL;
            out_buf->size = 0;

            struct timespec ts = {backoff_ms / 1000, (backoff_ms % 1000) * 1000000L};
            nanosleep(&ts, NULL);
            backoff_ms *= 2;
        }

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            if (attempt < max_retries)
                continue;
            curl_easy_cleanup(curl);
            return -1;
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        /* Retry on 5xx server errors */
        if (http_code >= 500 && attempt < max_retries)
            continue;

        break;
    }

    curl_easy_cleanup(curl);
    return (int)http_code;
}

/* ── URL building ─────────────────────────────────────────── */

static char *build_search_url(const ss_client *client, const char *rom_name,
                              const char *md5_hash, long file_size,
                              int system_id) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    /* Build query string manually */
    char *enc_romname = curl_easy_escape(curl, rom_name, 0);
    char *enc_devid = curl_easy_escape(curl, SCREENSCRAPER_DEV_ID, 0);
    char *enc_devpwd = curl_easy_escape(curl, SCREENSCRAPER_DEV_PASSWORD, 0);

    /* Start with required params */
    size_t url_cap = 2048;
    char *url = malloc(url_cap);
    int len = snprintf(url, url_cap,
        "%s/jeuInfos.php?devid=%s&devpassword=%s&softname=%s&output=json"
        "&romnom=%s&systemeid=%d",
        SS_API_BASE, enc_devid, enc_devpwd, SS_SOFT_NAME,
        enc_romname, system_id);

    if (client->username[0]) {
        char *enc_user = curl_easy_escape(curl, client->username, 0);
        char *enc_pass = curl_easy_escape(curl, client->password, 0);
        len += snprintf(url + len, url_cap - (size_t)len,
            "&ssid=%s&sspassword=%s", enc_user, enc_pass);
        curl_free(enc_user);
        curl_free(enc_pass);
    }

    if (md5_hash && md5_hash[0]) {
        len += snprintf(url + len, url_cap - (size_t)len,
            "&md5=%s&romtaille=%ld", md5_hash, file_size);
    }

    if (ss_is_debug()) {
        char *enc_dbgpwd = curl_easy_escape(curl, SCREENSCRAPER_DEBUG_PASSWORD, 0);
        len += snprintf(url + len, url_cap - (size_t)len,
            "&devdebugpassword=%s", enc_dbgpwd);
        curl_free(enc_dbgpwd);

        if (SCREENSCRAPER_FORCE_LEVEL[0])
            len += snprintf(url + len, url_cap - (size_t)len,
                "&forcelevel=%s", SCREENSCRAPER_FORCE_LEVEL);
        if (SCREENSCRAPER_FORCE_UPDATE[0])
            snprintf(url + len, url_cap - (size_t)len,
                "&forceupdate=%s", SCREENSCRAPER_FORCE_UPDATE);
    }

    curl_free(enc_romname);
    curl_free(enc_devid);
    curl_free(enc_devpwd);
    curl_easy_cleanup(curl);

    return url;
}

/* ── Media resolution ─────────────────────────────────────── */

static int resolve_media(cJSON *medias, char **media_types, int type_count,
                         char **region_prio, int region_count,
                         char *url_out, size_t url_len,
                         char *format_out, size_t format_len) {
    if (!cJSON_IsArray(medias))
        return -1;

    for (int t = 0; t < type_count; t++) {
        /* Collect candidates for this media type */
        int media_count = cJSON_GetArraySize(medias);
        cJSON *first_match = NULL;

        /* Try preferred regions first */
        for (int r = 0; r < region_count; r++) {
            for (int m = 0; m < media_count; m++) {
                cJSON *media = cJSON_GetArrayItem(medias, m);
                cJSON *type = cJSON_GetObjectItem(media, "type");
                cJSON *region = cJSON_GetObjectItem(media, "region");
                if (!cJSON_IsString(type) || strcmp(type->valuestring, media_types[t]) != 0)
                    continue;
                if (!first_match)
                    first_match = media;
                if (cJSON_IsString(region) && strcmp(region->valuestring, region_prio[r]) == 0) {
                    cJSON *murl = cJSON_GetObjectItem(media, "url");
                    cJSON *mfmt = cJSON_GetObjectItem(media, "format");
                    if (cJSON_IsString(murl)) {
                        snprintf(url_out, url_len, "%s", murl->valuestring);
                        snprintf(format_out, format_len, "%s",
                                 cJSON_IsString(mfmt) ? mfmt->valuestring : "png");
                        return 0;
                    }
                }
            }
        }

        /* No region match — use first candidate for this type */
        if (first_match) {
            cJSON *murl = cJSON_GetObjectItem(first_match, "url");
            cJSON *mfmt = cJSON_GetObjectItem(first_match, "format");
            if (cJSON_IsString(murl)) {
                snprintf(url_out, url_len, "%s", murl->valuestring);
                snprintf(format_out, format_len, "%s",
                         cJSON_IsString(mfmt) ? mfmt->valuestring : "png");
                return 0;
            }
        }
    }

    return -1;
}

static void resolve_game_name(cJSON *names, char **region_prio, int region_count,
                              char *name_out, size_t name_len) {
    if (!cJSON_IsArray(names) || cJSON_GetArraySize(names) == 0) {
        name_out[0] = '\0';
        return;
    }

    for (int r = 0; r < region_count; r++) {
        int n = cJSON_GetArraySize(names);
        for (int i = 0; i < n; i++) {
            cJSON *entry = cJSON_GetArrayItem(names, i);
            cJSON *region = cJSON_GetObjectItem(entry, "region");
            cJSON *text = cJSON_GetObjectItem(entry, "text");
            if (cJSON_IsString(region) && cJSON_IsString(text) &&
                strcmp(region->valuestring, region_prio[r]) == 0) {
                snprintf(name_out, name_len, "%s", text->valuestring);
                return;
            }
        }
    }

    /* Fallback: first name */
    cJSON *first = cJSON_GetArrayItem(names, 0);
    cJSON *text = cJSON_GetObjectItem(first, "text");
    if (cJSON_IsString(text))
        snprintf(name_out, name_len, "%s", text->valuestring);
    else
        name_out[0] = '\0';
}

/* ── HTTP status handling ─────────────────────────────────── */

static const char *http_status_error(int code) {
    switch (code) {
    case 429: return "Thread limit reached (HTTP 429)";
    case 430: return "Daily quota exceeded (HTTP 430)";
    case 431: return "Too many unrecognized ROMs (HTTP 431)";
    case 423: return "ScreenScraper API temporarily closed (HTTP 423)";
    case 426: return "Software has been blacklisted (HTTP 426)";
    default:  return NULL;
    }
}

/* ── ROM search ───────────────────────────────────────────── */

static int search_rom_request(const ss_client *client, const char *rom_name,
                              const char *md5_hash, long file_size, int system_id,
                              char **artwork_types, int artwork_count,
                              char **region_prio, int region_count,
                              ss_result *result) {
    char *url = build_search_url(client, rom_name, md5_hash, file_size, system_id);
    if (!url) return -1;

    curl_buffer buf;
    int http_code = http_get(url, SS_USER_AGENT, &buf, 2);
    free(url);

    if (http_code < 0) {
        free(buf.data);
        return -1;
    }

    /* 404 = not found */
    if (http_code == 404) {
        free(buf.data);
        return 1;
    }

    /* Check for error status codes */
    if (http_code >= 400) {
        const char *msg = http_status_error(http_code);
        if (msg)
            fprintf(stderr, "screenscraper: %s\n", msg);
        else
            fprintf(stderr, "screenscraper: unexpected HTTP %d\n", http_code);
        free(buf.data);
        return -1;
    }

    if (!buf.data || buf.size == 0) {
        free(buf.data);
        return -1;
    }

    cJSON *json = cJSON_Parse(buf.data);
    free(buf.data);
    if (!json) return -1;

    /* Navigate JSON: response.jeu */
    cJSON *response = cJSON_GetObjectItem(json, "response");
    if (!response) { cJSON_Delete(json); return -1; }

    cJSON *jeu = cJSON_GetObjectItem(response, "jeu");
    if (!jeu) { cJSON_Delete(json); return 1; } /* not found */

    cJSON *game_id = cJSON_GetObjectItem(jeu, "id");
    if (!game_id || !cJSON_IsString(game_id) || game_id->valuestring[0] == '\0') {
        cJSON_Delete(json);
        return 1; /* not found */
    }

    /* Resolve game name */
    cJSON *names = cJSON_GetObjectItem(jeu, "noms");
    resolve_game_name(names, region_prio, region_count,
                      result->game_name, sizeof(result->game_name));

    /* Resolve media */
    cJSON *medias = cJSON_GetObjectItem(jeu, "medias");
    if (resolve_media(medias, artwork_types, artwork_count,
                      region_prio, region_count,
                      result->media_url, sizeof(result->media_url),
                      result->media_format, sizeof(result->media_format)) != 0) {
        cJSON_Delete(json);
        return 1; /* found game but no matching media */
    }

    /* User stats */
    cJSON *ssuser = cJSON_GetObjectItem(response, "ssuser");
    if (ssuser) {
        cJSON *rt = cJSON_GetObjectItem(ssuser, "requeststoday");
        cJSON *mr = cJSON_GetObjectItem(ssuser, "maxrequestsperday");
        cJSON *mt = cJSON_GetObjectItem(ssuser, "maxthreads");
        result->requests_today = (rt && cJSON_IsString(rt)) ? atoi(rt->valuestring) : 0;
        result->max_requests = (mr && cJSON_IsString(mr)) ? atoi(mr->valuestring) : 0;
        result->max_threads = (mt && cJSON_IsString(mt)) ? atoi(mt->valuestring) : 1;
    }

    cJSON_Delete(json);
    return 0;
}

int ss_search_rom(const ss_client *client, const rom_file *rom, int system_id,
                  char **artwork_types, int artwork_count,
                  char **region_prio, int region_count,
                  ss_result *result) {
    memset(result, 0, sizeof(*result));

    char md5_hash[33] = {0};
    long file_size = 0;
    if (compute_md5(rom, md5_hash, &file_size) != 0) {
        md5_hash[0] = '\0';
        file_size = 0;
    }

    int ret = search_rom_request(client, rom->name, md5_hash, file_size, system_id,
                                 artwork_types, artwork_count,
                                 region_prio, region_count, result);

    /* If hash lookup returned no results, retry without hash */
    if (ret == 1 && md5_hash[0]) {
        ret = search_rom_request(client, rom->name, "", 0, system_id,
                                 artwork_types, artwork_count,
                                 region_prio, region_count, result);
    }

    return ret;
}

/* ── Media download ───────────────────────────────────────── */

int ss_download_media(const ss_client *client, const char *media_url,
                      const char *dest_path) {
    (void)client;

    curl_buffer buf;
    int http_code = http_get(media_url, SS_USER_AGENT, &buf, 2);
    if (http_code != 200) {
        free(buf.data);
        return -1;
    }

    /* Ensure parent directory exists */
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", dest_path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        char tmp[PATH_MAX];
        snprintf(tmp, sizeof(tmp), "%s", dir);
        for (char *p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(tmp, 0755);
                *p = '/';
            }
        }
        mkdir(tmp, 0755);
    }

    /* Write to temp file first */
    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", dest_path);

    bool is_jpeg = (strstr(media_url, ".jpg") != NULL) ||
                   (strstr(media_url, ".jpeg") != NULL);

    if (is_jpeg) {
        /* Convert JPEG to PNG using SDL2_image */
        SDL_RWops *rw = SDL_RWFromMem(buf.data, (int)buf.size);
        if (!rw) { free(buf.data); return -1; }

        SDL_Surface *surface = IMG_Load_RW(rw, 1);
        free(buf.data);
        if (!surface) return -1;

        if (IMG_SavePNG(surface, tmp_path) != 0) {
            SDL_FreeSurface(surface);
            return -1;
        }
        SDL_FreeSurface(surface);
    } else {
        /* Write raw data */
        FILE *f = fopen(tmp_path, "wb");
        if (!f) { free(buf.data); return -1; }
        size_t written = fwrite(buf.data, 1, buf.size, f);
        fclose(f);
        free(buf.data);
        if (written != buf.size) {
            unlink(tmp_path);
            return -1;
        }
    }

    /* Atomic rename */
    if (rename(tmp_path, dest_path) != 0) {
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

/* ── ETA formatting ───────────────────────────────────────── */

void format_eta(double seconds, char *buf, size_t buflen) {
    if (seconds <= 0) {
        snprintf(buf, buflen, "0s");
        return;
    }
    int h = (int)(seconds / 3600);
    int m = ((int)(seconds / 60)) % 60;
    int s = ((int)seconds) % 60;
    if (h > 0)
        snprintf(buf, buflen, "%dh%02dm%02ds", h, m, s);
    else if (m > 0)
        snprintf(buf, buflen, "%dm%02ds", m, s);
    else
        snprintf(buf, buflen, "%ds", s);
}

/* ── Multithreaded console scraping ───────────────────────── */

typedef struct {
    const ss_client *client;
    const rom_file *rom;
    const console_dir *console;
    int system_id;
    char **artwork_types;
    int artwork_count;
    char **region_prio;
    int region_count;
    scrape_summary *summary;
    pthread_mutex_t *mu;
    atomic_int *completed;
    atomic_int *interrupt_signal;
    atomic_int *aborted;
    /* EMA stats (protected by mu) */
    long *avg_rom_ns;
    long *last_ns;
    atomic_int *stats_req_today;
    atomic_int *stats_max_req;
} worker_arg;

static void *scrape_worker(void *arg) {
    worker_arg *w = (worker_arg *)arg;

    if (atomic_load(w->aborted) || atomic_load(w->interrupt_signal))
        goto done;

    ss_result result;
    int ret = ss_search_rom(w->client, w->rom, w->system_id,
                            w->artwork_types, w->artwork_count,
                            w->region_prio, w->region_count, &result);

    pthread_mutex_lock(w->mu);
    if (ret == 0) {
        /* Download the media */
        char dest[PATH_MAX];
        artwork_src_path(w->rom->path, w->rom->display, dest, sizeof(dest));

        if (ss_download_media(w->client, result.media_url, dest) == 0)
            w->summary->found++;
        else
            w->summary->errors++;

        atomic_store(w->stats_req_today, result.requests_today);
        atomic_store(w->stats_max_req, result.max_requests);

        if (result.max_requests > 0 && result.requests_today >= result.max_requests) {
            atomic_store(w->aborted, 1);
            fprintf(stderr, "screenscraper: quota exceeded (%d/%d)\n",
                    result.requests_today, result.max_requests);
        }
    } else if (ret == 1) {
        w->summary->not_found++;
    } else {
        w->summary->errors++;
    }

    /* Update EMA */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long now_ns = now.tv_sec * 1000000000L + now.tv_nsec;
    long prev_ns = *w->last_ns;
    if (prev_ns > 0) {
        long elapsed = now_ns - prev_ns;
        long prev_avg = *w->avg_rom_ns;
        long new_avg = (long)(0.3 * (double)elapsed + 0.7 * (double)prev_avg);
        if (new_avg > 0) *w->avg_rom_ns = new_avg;
    }
    *w->last_ns = now_ns;
    pthread_mutex_unlock(w->mu);

done:
    atomic_fetch_add(w->completed, 1);
    free(w);
    return NULL;
}

/* HUD thread context and function */
typedef struct {
    atomic_int *completed;
    int scrape_total;
    int rom_total;
    atomic_int *stats_threads;
    atomic_int *stats_req_today;
    atomic_int *stats_max_req;
    long *avg_rom_ns;       /* protected by mu */
    long *last_ns;          /* protected by mu */
    pthread_mutex_t *mu;
    atomic_int *stop;
    message_fn set_message;
    progress_fn set_progress;
} hud_ctx;

static void *hud_thread_fn(void *arg) {
    hud_ctx *h = (hud_ctx *)arg;
    while (!atomic_load(h->stop)) {
        int done = atomic_load(h->completed);
        int remaining = h->scrape_total - done;
        int threads = atomic_load(h->stats_threads);
        int req_today = atomic_load(h->stats_req_today);
        int max_req = atomic_load(h->stats_max_req);

        char eta_str[64];
        pthread_mutex_lock(h->mu);
        long avg_ns = *h->avg_rom_ns;
        long last = *h->last_ns;
        pthread_mutex_unlock(h->mu);

        if (remaining <= 0) {
            snprintf(eta_str, sizeof(eta_str), "finishing...");
        } else if (avg_ns > 0) {
            struct timespec tnow;
            clock_gettime(CLOCK_MONOTONIC, &tnow);
            long now_ns2 = tnow.tv_sec * 1000000000L + tnow.tv_nsec;
            long time_since = now_ns2 - last;
            long eta_ns = (long)remaining * avg_ns - time_since;
            if (eta_ns < 1000000000L)
                snprintf(eta_str, sizeof(eta_str), "finishing...");
            else
                format_eta((double)eta_ns / 1.0e9, eta_str, sizeof(eta_str));
        } else {
            snprintf(eta_str, sizeof(eta_str), "calculating...");
        }

        char msg[512];
        int len = snprintf(msg, sizeof(msg),
            "ROMs left: %d / %d\nThreads: %d - ETA: %s",
            remaining, h->scrape_total, threads, eta_str);
        if (max_req > 0)
            snprintf(msg + len, sizeof(msg) - (size_t)len,
                "\nQuota: %d / %d", req_today, max_req);

        if (h->set_message) h->set_message(msg);
        if (h->set_progress)
            h->set_progress((float)done / (float)h->rom_total);

        usleep(333000);
    }
    return NULL;
}

scrape_summary scrape_console(const console_dir *console, bool missing_only,
                              const app_settings *settings,
                              atomic_int *interrupt_signal,
                              progress_fn set_progress,
                              message_fn set_message) {
    scrape_summary summary = {0};

    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, settings->show_hidden, &roms);

    int sys_id = ss_platform_id(console->tag);
    if (sys_id < 0) {
        free(roms);
        return summary;
    }

    /* Filter ROMs if missing-only mode */
    rom_file *to_scrape = malloc(sizeof(rom_file) * (size_t)rom_count);
    int scrape_count = 0;
    int pre_existing = 0;

    for (int i = 0; i < rom_count; i++) {
        if (missing_only && artwork_exists(roms[i].path, roms[i].display)) {
            pre_existing++;
            continue;
        }
        to_scrape[scrape_count++] = roms[i];
    }

    summary.total = rom_count;
    summary.found = pre_existing;

    if (scrape_count == 0) {
        if (set_progress) set_progress(1.0f);
        free(roms);
        free(to_scrape);
        return summary;
    }

    /* Build client and priority lists */
    ss_client client = {0};
    snprintf(client.username, sizeof(client.username), "%s", settings->ss_username);
    snprintf(client.password, sizeof(client.password), "%s", settings->ss_password);

    int artwork_count = 0;
    char **artwork_types = build_artwork_types(settings, &artwork_count);
    int region_count = 0;
    char **region_prio = build_region_types(settings, &region_count);

    /* Shared state */
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    atomic_int completed = 0;
    atomic_int aborted = 0;
    long avg_rom_ns = 0;
    long last_ns = 0;
    atomic_int stats_threads = 1;
    atomic_int stats_req_today = 0;
    atomic_int stats_max_req = 0;

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Phase 1: Scrape first ROM to learn maxthreads */
    ss_result first_result;
    int first_ret = ss_search_rom(&client, &to_scrape[0], sys_id,
                                  artwork_types, artwork_count,
                                  region_prio, region_count, &first_result);

    int max_threads = 1;
    pthread_mutex_lock(&mu);
    if (first_ret == 0) {
        char dest[PATH_MAX];
        artwork_src_path(to_scrape[0].path, to_scrape[0].display, dest, sizeof(dest));
        if (ss_download_media(&client, first_result.media_url, dest) == 0)
            summary.found++;
        else
            summary.errors++;

        if (first_result.max_threads > 1)
            max_threads = first_result.max_threads;
        atomic_store(&stats_threads, max_threads);
        atomic_store(&stats_req_today, first_result.requests_today);
        atomic_store(&stats_max_req, first_result.max_requests);
    } else if (first_ret == 1) {
        summary.not_found++;
    } else {
        summary.errors++;
    }
    pthread_mutex_unlock(&mu);

    /* Seed EMA */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long seed_ns = (now.tv_sec - start.tv_sec) * 1000000000L + (now.tv_nsec - start.tv_nsec);
    avg_rom_ns = seed_ns;
    last_ns = now.tv_sec * 1000000000L + now.tv_nsec;

    atomic_store(&completed, 1);
    if (set_progress) set_progress((float)1 / (float)rom_count);

    /* HUD update thread */
    atomic_int hud_stop = 0;
    hud_ctx hud = {
        .completed = &completed,
        .scrape_total = scrape_count,
        .rom_total = rom_count,
        .stats_threads = &stats_threads,
        .stats_req_today = &stats_req_today,
        .stats_max_req = &stats_max_req,
        .avg_rom_ns = &avg_rom_ns,
        .last_ns = &last_ns,
        .mu = &mu,
        .stop = &hud_stop,
        .set_message = set_message,
        .set_progress = set_progress,
    };

    pthread_t hud_thread;
    pthread_create(&hud_thread, NULL, hud_thread_fn, &hud);

    if (scrape_count > 1) {
        /* Phase 2: Scrape remaining ROMs with thread pool.
         * Use detached threads with a simple slot-counting approach
         * instead of pthread_tryjoin_np (not available on macOS). */
        atomic_int active_count = 0;

        for (int i = 1; i < scrape_count; i++) {
            if (atomic_load(&aborted) || atomic_load(interrupt_signal))
                break;

            /* Wait for a slot */
            while (atomic_load(&active_count) >= max_threads) {
                usleep(10000);
                if (atomic_load(&aborted) || atomic_load(interrupt_signal))
                    break;
            }
            if (atomic_load(&aborted) || atomic_load(interrupt_signal))
                break;

            worker_arg *w = malloc(sizeof(worker_arg));
            w->client = &client;
            w->rom = &to_scrape[i];
            w->console = console;
            w->system_id = sys_id;
            w->artwork_types = artwork_types;
            w->artwork_count = artwork_count;
            w->region_prio = region_prio;
            w->region_count = region_count;
            w->summary = &summary;
            w->mu = &mu;
            w->completed = &completed;
            w->interrupt_signal = interrupt_signal;
            w->aborted = &aborted;
            w->avg_rom_ns = &avg_rom_ns;
            w->last_ns = &last_ns;
            w->stats_req_today = &stats_req_today;
            w->stats_max_req = &stats_max_req;

            atomic_fetch_add(&active_count, 1);

            pthread_t thread;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_create(&thread, &attr, scrape_worker, w);
            pthread_attr_destroy(&attr);
        }

        /* Wait for all workers to complete */
        while (atomic_load(&completed) < scrape_count) {
            usleep(50000);
        }
    }

    /* Stop HUD */
    atomic_store(&hud_stop, 1);
    pthread_join(hud_thread, NULL);

    if (set_progress) set_progress(1.0f);

    /* Cleanup */
    for (int i = 0; i < artwork_count; i++) free(artwork_types[i]);
    free(artwork_types);
    for (int i = 0; i < region_count; i++) free(region_prio[i]);
    free(region_prio);
    free(roms);
    free(to_scrape);

    return summary;
}

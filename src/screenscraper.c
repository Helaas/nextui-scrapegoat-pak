#include "screenscraper.h"
#include "systems.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <pthread.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_surface.h>

/* Suppress GCC warnings about snprintf truncation when combining
   PATH_MAX-sized strings — truncation is safe by design. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

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

/* ── Error helpers ────────────────────────────────────────── */

static _Thread_local char g_ss_last_error[256];

static void ss_clear_last_error(void) {
    g_ss_last_error[0] = '\0';
}

static void ss_set_last_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_ss_last_error, sizeof(g_ss_last_error), fmt, args);
    va_end(args);
}

static const char *ss_last_error_message(void) {
    return g_ss_last_error[0] ? g_ss_last_error : NULL;
}

/* ── cURL helpers ─────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t size;
} curl_buffer;

static pthread_once_t g_curl_once = PTHREAD_ONCE_INIT;
static CURLcode g_curl_global_result = CURLE_OK;

static void curl_global_init_once(void) {
    g_curl_global_result = curl_global_init(CURL_GLOBAL_DEFAULT);
}

static bool is_cancel_requested(atomic_int *interrupt_signal) {
    return interrupt_signal && atomic_load(interrupt_signal) != 0;
}

static long monotonic_now_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000000000L + now.tv_nsec;
}

static int sleep_with_cancel(int backoff_ms, atomic_int *interrupt_signal) {
    int remaining = backoff_ms;
    while (remaining > 0) {
        int chunk_ms = remaining > 50 ? 50 : remaining;
        if (is_cancel_requested(interrupt_signal))
            return -1;
        usleep((useconds_t)chunk_ms * 1000U);
        remaining -= chunk_ms;
    }
    return 0;
}

static const char *ssl_cert_file(void) {
    const char *ca = getenv("SSL_CERT_FILE");
    return (ca && ca[0] != '\0') ? ca : NULL;
}

static int curl_xferinfo_cb(void *clientp,
                            curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;

    return is_cancel_requested((atomic_int *)clientp) ? 1 : 0;
}

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    curl_buffer *buf = (curl_buffer *)userdata;
    char *tmp = realloc(buf->data, buf->size + total + 1);
    if (!tmp)
        return 0;

    buf->data = tmp;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

/* Perform a GET request with retries and exponential backoff.
 * Returns the HTTP response code, -1 on error, and -2 when cancelled. */
static int http_get(const ss_client *client, const char *url, const char *user_agent,
                    curl_buffer *out_buf, int max_retries,
                    char *content_type_out, size_t content_type_len) {
    atomic_int *interrupt_signal = client ? client->interrupt_signal : NULL;
    const char *ca = ssl_cert_file();

    out_buf->data = NULL;
    out_buf->size = 0;
    if (content_type_out && content_type_len > 0)
        content_type_out[0] = '\0';

    pthread_once(&g_curl_once, curl_global_init_once);
    if (g_curl_global_result != CURLE_OK) {
        ss_set_last_error("curl global init failed: %s",
                          curl_easy_strerror(g_curl_global_result));
        return -1;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        ss_set_last_error("curl init failed");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out_buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_xferinfo_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, interrupt_signal);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    if (ca)
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca);

    long http_code = 0;
    int backoff_ms = 1000;

    for (int attempt = 0; attempt <= max_retries; attempt++) {
        if (attempt > 0) {
            free(out_buf->data);
            out_buf->data = NULL;
            out_buf->size = 0;
            if (content_type_out && content_type_len > 0)
                content_type_out[0] = '\0';

            if (sleep_with_cancel(backoff_ms, interrupt_signal) != 0) {
                curl_easy_cleanup(curl);
                return -2;
            }
            backoff_ms *= 2;
        }

        if (is_cancel_requested(interrupt_signal)) {
            curl_easy_cleanup(curl);
            return -2;
        }

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_ABORTED_BY_CALLBACK || is_cancel_requested(interrupt_signal)) {
            curl_easy_cleanup(curl);
            return -2;
        }
        if (res != CURLE_OK) {
            if (attempt < max_retries)
                continue;
            ss_set_last_error("request failed: %s (ca=%s)",
                              curl_easy_strerror(res), ca ? ca : "(unset)");
            curl_easy_cleanup(curl);
            return -1;
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (content_type_out && content_type_len > 0) {
            char *content_type = NULL;
            if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type) == CURLE_OK &&
                content_type != NULL) {
                snprintf(content_type_out, content_type_len, "%s", content_type);
            }
        }

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
    if (!curl) {
        ss_set_last_error("curl init failed while building request");
        return NULL;
    }

    char *enc_romname = curl_easy_escape(curl, rom_name, 0);
    char *enc_devid = curl_easy_escape(curl, SCREENSCRAPER_DEV_ID, 0);
    char *enc_devpwd = curl_easy_escape(curl, SCREENSCRAPER_DEV_PASSWORD, 0);
    if (!enc_romname || !enc_devid || !enc_devpwd) {
        ss_set_last_error("failed to encode ScreenScraper request");
        if (enc_romname) curl_free(enc_romname);
        if (enc_devid) curl_free(enc_devid);
        if (enc_devpwd) curl_free(enc_devpwd);
        curl_easy_cleanup(curl);
        return NULL;
    }

    size_t url_cap = 2048;
    char *url = malloc(url_cap);
    if (!url) {
        ss_set_last_error("out of memory while building request");
        curl_free(enc_romname);
        curl_free(enc_devid);
        curl_free(enc_devpwd);
        curl_easy_cleanup(curl);
        return NULL;
    }

    int len = snprintf(url, url_cap,
        "%s/jeuInfos.php?devid=%s&devpassword=%s&softname=%s&output=json"
        "&romnom=%s&systemeid=%d",
        SS_API_BASE, enc_devid, enc_devpwd, SS_SOFT_NAME,
        enc_romname, system_id);

    if (client->username[0]) {
        char *enc_user = curl_easy_escape(curl, client->username, 0);
        char *enc_pass = curl_easy_escape(curl, client->password, 0);
        if (!enc_user || !enc_pass) {
            ss_set_last_error("failed to encode ScreenScraper credentials");
            if (enc_user) curl_free(enc_user);
            if (enc_pass) curl_free(enc_pass);
            free(url);
            curl_free(enc_romname);
            curl_free(enc_devid);
            curl_free(enc_devpwd);
            curl_easy_cleanup(curl);
            return NULL;
        }
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
        if (!enc_dbgpwd) {
            ss_set_last_error("failed to encode ScreenScraper debug credentials");
            free(url);
            curl_free(enc_romname);
            curl_free(enc_devid);
            curl_free(enc_devpwd);
            curl_easy_cleanup(curl);
            return NULL;
        }

        len += snprintf(url + len, url_cap - (size_t)len,
            "&devdebugpassword=%s", enc_dbgpwd);
        curl_free(enc_dbgpwd);

        if (SCREENSCRAPER_FORCE_LEVEL[0]) {
            len += snprintf(url + len, url_cap - (size_t)len,
                "&forcelevel=%s", SCREENSCRAPER_FORCE_LEVEL);
        }
        if (SCREENSCRAPER_FORCE_UPDATE[0]) {
            snprintf(url + len, url_cap - (size_t)len,
                "&forceupdate=%s", SCREENSCRAPER_FORCE_UPDATE);
        }
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
        int media_count = cJSON_GetArraySize(medias);
        cJSON *first_match = NULL;

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
        int count = cJSON_GetArraySize(names);
        for (int i = 0; i < count; i++) {
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

static bool is_fatal_search_error(const char *message) {
    if (!message)
        return false;
    return strstr(message, "Thread limit reached") != NULL ||
           strstr(message, "Daily quota exceeded") != NULL ||
           strstr(message, "Too many unrecognized ROMs") != NULL ||
           strstr(message, "temporarily closed") != NULL ||
           strstr(message, "blacklisted") != NULL;
}

/* ── ROM search ───────────────────────────────────────────── */

static int search_rom_request(const ss_client *client, const char *rom_name,
                              const char *md5_hash, long file_size, int system_id,
                              char **artwork_types, int artwork_count,
                              char **region_prio, int region_count,
                              ss_result *result) {
    char *url = build_search_url(client, rom_name, md5_hash, file_size, system_id);
    if (!url)
        return -1;

    curl_buffer buf;
    int http_code = http_get(client, url, SS_USER_AGENT, &buf, 2, NULL, 0);
    free(url);

    if (http_code == -2)
        return -2;
    if (http_code < 0) {
        if (!ss_last_error_message())
            ss_set_last_error("ScreenScraper request failed");
        free(buf.data);
        return -1;
    }

    if (http_code == 404) {
        free(buf.data);
        return 1;
    }

    if (http_code >= 400) {
        const char *msg = http_status_error(http_code);
        if (msg)
            ss_set_last_error("%s", msg);
        else
            ss_set_last_error("ScreenScraper returned HTTP %d", http_code);
        free(buf.data);
        return -1;
    }

    if (!buf.data || buf.size == 0) {
        ss_set_last_error("ScreenScraper returned an empty response");
        free(buf.data);
        return -1;
    }

    cJSON *json = cJSON_Parse(buf.data);
    free(buf.data);
    if (!json) {
        ss_set_last_error("Failed to parse ScreenScraper response");
        return -1;
    }

    cJSON *response = cJSON_GetObjectItem(json, "response");
    if (!response) {
        cJSON_Delete(json);
        ss_set_last_error("ScreenScraper response is missing 'response'");
        return -1;
    }

    cJSON *jeu = cJSON_GetObjectItem(response, "jeu");
    if (!jeu) {
        cJSON_Delete(json);
        return 1;
    }

    cJSON *game_id = cJSON_GetObjectItem(jeu, "id");
    if (!game_id || !cJSON_IsString(game_id) || game_id->valuestring[0] == '\0') {
        cJSON_Delete(json);
        return 1;
    }

    cJSON *names = cJSON_GetObjectItem(jeu, "noms");
    resolve_game_name(names, region_prio, region_count,
                      result->game_name, sizeof(result->game_name));

    cJSON *medias = cJSON_GetObjectItem(jeu, "medias");
    if (resolve_media(medias, artwork_types, artwork_count,
                      region_prio, region_count,
                      result->media_url, sizeof(result->media_url),
                      result->media_format, sizeof(result->media_format)) != 0) {
        cJSON_Delete(json);
        return 1;
    }

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
    ss_clear_last_error();
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

    if (ret == 1 && md5_hash[0] != '\0') {
        ret = search_rom_request(client, rom->name, "", 0, system_id,
                                 artwork_types, artwork_count,
                                 region_prio, region_count, result);
    }

    return ret;
}

/* ── Media download ───────────────────────────────────────── */

static void ensure_parent_dir(const char *path) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash)
        return;

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

static bool url_has_jpeg_suffix(const char *media_url) {
    const char *last_dot = strrchr(media_url, '.');
    if (!last_dot)
        return false;
    return strcasecmp(last_dot, ".jpg") == 0 || strcasecmp(last_dot, ".jpeg") == 0;
}

int ss_download_media(const ss_client *client, const char *media_url,
                      const char *dest_path) {
    ss_clear_last_error();

    curl_buffer buf;
    char content_type[128];
    int http_code = http_get(client, media_url, SS_USER_AGENT, &buf, 2,
                             content_type, sizeof(content_type));
    if (http_code == -2)
        return -2;
    if (http_code != 200) {
        free(buf.data);
        if (http_code < 0 && ss_last_error_message())
            return -1;
        ss_set_last_error("Media download returned HTTP %d", http_code);
        return -1;
    }

    ensure_parent_dir(dest_path);

    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", dest_path);

    bool is_jpeg = strstr(content_type, "jpeg") != NULL || url_has_jpeg_suffix(media_url);

    if (is_jpeg) {
        SDL_RWops *rw = SDL_RWFromMem(buf.data, (int)buf.size);
        if (!rw) {
            free(buf.data);
            ss_set_last_error("Failed to create SDL buffer for JPEG conversion");
            return -1;
        }

        SDL_Surface *surface = IMG_Load_RW(rw, 1);
        free(buf.data);
        if (!surface) {
            ss_set_last_error("JPEG decode failed: %s", IMG_GetError());
            return -1;
        }

        if (IMG_SavePNG(surface, tmp_path) != 0) {
            SDL_FreeSurface(surface);
            ss_set_last_error("PNG conversion failed: %s", IMG_GetError());
            unlink(tmp_path);
            return -1;
        }

        SDL_FreeSurface(surface);
    } else {
        FILE *f = fopen(tmp_path, "wb");
        if (!f) {
            free(buf.data);
            ss_set_last_error("Failed to create temporary media file");
            return -1;
        }

        size_t written = fwrite(buf.data, 1, buf.size, f);
        free(buf.data);
        int write_ok = (written == buf.size);
        int flush_ok = (fflush(f) == 0);
        int sync_ok = flush_ok && (fsync(fileno(f)) == 0);
        int close_ok = (fclose(f) == 0);
        if (!write_ok || !flush_ok || !sync_ok || !close_ok) {
            unlink(tmp_path);
            ss_set_last_error("Failed to write media file");
            return -1;
        }
    }

    if (rename(tmp_path, dest_path) != 0) {
        unlink(tmp_path);
        ss_set_last_error("Failed to finalize media file");
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
    const console_dir *console;
    const rom_file *roms;
    int rom_count;
    const ss_client *client;
    int system_id;
    char **artwork_types;
    int artwork_count;
    char **region_prio;
    int region_count;
    atomic_int *interrupt_signal;

    scrape_summary summary;
    pthread_mutex_t mu;
    atomic_int next_index;
    atomic_int completed;
    atomic_int abort_requested;
    atomic_int stats_threads;
    atomic_int stats_req_today;
    atomic_int stats_max_req;
    long avg_rom_ns;
    long last_ns;
    char fatal_message[256];
} scrape_state;

typedef struct {
    scrape_state *state;
} worker_arg;

typedef struct {
    atomic_int *completed;
    int scrape_total;
    int rom_total;
    atomic_int *stats_threads;
    atomic_int *stats_req_today;
    atomic_int *stats_max_req;
    long *avg_rom_ns;
    long *last_ns;
    pthread_mutex_t *mu;
    atomic_int *stop;
    message_fn set_message;
    progress_fn set_progress;
} hud_ctx;

static void set_fatal_message_unlocked(scrape_state *state, const char *message) {
    if (message && message[0] != '\0' && state->fatal_message[0] == '\0') {
        snprintf(state->fatal_message, sizeof(state->fatal_message), "%s", message);
    }
    atomic_store(&state->abort_requested, 1);
}

static void set_fatal_message_locked(scrape_state *state, const char *message) {
    pthread_mutex_lock(&state->mu);
    set_fatal_message_unlocked(state, message);
    pthread_mutex_unlock(&state->mu);
}

static void copy_fatal_message(scrape_state *state, char *buf, size_t buflen) {
    pthread_mutex_lock(&state->mu);
    snprintf(buf, buflen, "%s", state->fatal_message);
    pthread_mutex_unlock(&state->mu);
}

static void record_completion_locked(scrape_state *state, int found_delta,
                                     int not_found_delta, int error_delta,
                                     const ss_result *result,
                                     const char *fatal_message) {
    long now_ns = monotonic_now_ns();

    pthread_mutex_lock(&state->mu);
    state->summary.found += found_delta;
    state->summary.not_found += not_found_delta;
    state->summary.errors += error_delta;

    if (result) {
        atomic_store(&state->stats_req_today, result->requests_today);
        atomic_store(&state->stats_max_req, result->max_requests);
        if (result->max_requests > 0 && result->requests_today >= result->max_requests) {
            char quota_message[256];
            snprintf(quota_message, sizeof(quota_message),
                     "Daily quota exceeded (%d/%d).",
                     result->requests_today, result->max_requests);
            set_fatal_message_unlocked(state, quota_message);
        }
    }

    if (fatal_message && fatal_message[0] != '\0')
        set_fatal_message_unlocked(state, fatal_message);

    if (state->last_ns > 0) {
        long elapsed = now_ns - state->last_ns;
        if (elapsed > 0) {
            if (state->avg_rom_ns > 0) {
                state->avg_rom_ns =
                    (long)(0.3 * (double)elapsed + 0.7 * (double)state->avg_rom_ns);
            } else {
                state->avg_rom_ns = elapsed;
            }
        }
    }
    state->last_ns = now_ns;
    pthread_mutex_unlock(&state->mu);

    atomic_fetch_add(&state->completed, 1);
}

/* Returns 0 to continue, -1 for fatal error, -2 for cancellation. */
static int process_one_rom(scrape_state *state, const rom_file *rom, bool first_phase,
                           int *max_threads_out) {
    int found_delta = 0;
    int not_found_delta = 0;
    int error_delta = 0;
    char fatal_message[256] = "";
    bool cancelled = false;
    bool have_result = false;
    ss_result result;

    int search_ret = ss_search_rom(state->client, rom, state->system_id,
                                   state->artwork_types, state->artwork_count,
                                   state->region_prio, state->region_count, &result);
    if (search_ret == 0) {
        char dest[PATH_MAX];
        have_result = true;
        if (max_threads_out && result.max_threads > 1)
            *max_threads_out = result.max_threads;

        artwork_src_path(rom->path, rom->display, dest, sizeof(dest));
        int download_ret = ss_download_media(state->client, result.media_url, dest);
        if (download_ret == 0) {
            found_delta = 1;
        } else if (download_ret == -2) {
            cancelled = true;
            have_result = false;
        } else {
            error_delta = 1;
            have_result = false;
            if (first_phase) {
                const char *message = ss_last_error_message();
                snprintf(fatal_message, sizeof(fatal_message),
                         "Failed to download artwork for %s: %s",
                         rom->display,
                         message ? message : "unknown error");
            }
        }
    } else if (search_ret == 1) {
        not_found_delta = 1;
    } else if (search_ret == -2) {
        cancelled = true;
    } else {
        const char *message = ss_last_error_message();
        error_delta = 1;
        if (first_phase || is_fatal_search_error(message)) {
            snprintf(fatal_message, sizeof(fatal_message), "%s",
                     message ? message : "ScreenScraper request failed");
        }
    }

    record_completion_locked(state, found_delta, not_found_delta, error_delta,
                             have_result ? &result : NULL, fatal_message);

    if (fatal_message[0] != '\0' || (have_result && result.max_requests > 0 &&
        result.requests_today >= result.max_requests)) {
        return -1;
    }
    if (cancelled)
        return -2;
    return 0;
}

static void *scrape_worker(void *arg) {
    worker_arg *worker = (worker_arg *)arg;
    scrape_state *state = worker->state;

    for (;;) {
        if (atomic_load(&state->abort_requested) || is_cancel_requested(state->interrupt_signal))
            break;

        int index = atomic_fetch_add(&state->next_index, 1);
        if (index >= state->rom_count)
            break;

        int result = process_one_rom(state, &state->roms[index], false, NULL);
        if (result == -1 || result == -2)
            continue;
    }

    return NULL;
}

static void *hud_thread_fn(void *arg) {
    hud_ctx *hud = (hud_ctx *)arg;
    while (!atomic_load(hud->stop)) {
        int done = atomic_load(hud->completed);
        int remaining = hud->scrape_total - done;
        int threads = atomic_load(hud->stats_threads);
        int req_today = atomic_load(hud->stats_req_today);
        int max_req = atomic_load(hud->stats_max_req);

        char eta_str[64];
        pthread_mutex_lock(hud->mu);
        long avg_ns = *hud->avg_rom_ns;
        long last_ns = *hud->last_ns;
        pthread_mutex_unlock(hud->mu);

        if (remaining <= 0) {
            snprintf(eta_str, sizeof(eta_str), "finishing...");
        } else if (avg_ns > 0) {
            long eta_ns = (long)remaining * avg_ns - (monotonic_now_ns() - last_ns);
            if (eta_ns < 1000000000L)
                snprintf(eta_str, sizeof(eta_str), "finishing...");
            else
                format_eta((double)eta_ns / 1.0e9, eta_str, sizeof(eta_str));
        } else {
            snprintf(eta_str, sizeof(eta_str), "calculating...");
        }

        if (hud->set_message) {
            char message[512];
            int len = snprintf(message, sizeof(message),
                               "ROMs left: %d / %d\nThreads: %d - ETA: %s",
                               remaining, hud->scrape_total, threads, eta_str);
            if (max_req > 0) {
                snprintf(message + len, sizeof(message) - (size_t)len,
                         "\nQuota: %d / %d", req_today, max_req);
            }
            hud->set_message(message);
        }

        if (hud->set_progress) {
            hud->set_progress((float)done / (float)hud->rom_total);
        }

        usleep(333000);
    }
    return NULL;
}

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

run_result scrape_console(const console_dir *console, bool missing_only,
                          const app_settings *settings,
                          atomic_int *interrupt_signal,
                          progress_fn set_progress,
                          message_fn set_message) {
    rom_file *roms = NULL;
    int rom_count = scan_roms(console->path, settings->show_hidden, &roms);
    if (rom_count < 0) {
        return make_run_result(RUN_ERROR, (scrape_summary){0},
                               "Could not read ROM files for this system.");
    }

    int system_id = ss_platform_id(console->tag);
    if (system_id < 0) {
        free(roms);
        return make_run_result(RUN_ERROR, (scrape_summary){0},
                               "This system does not have a ScreenScraper mapping.");
    }

    rom_file *to_scrape = malloc(sizeof(rom_file) * (size_t)rom_count);
    if (!to_scrape) {
        free(roms);
        return make_run_result(RUN_ERROR, (scrape_summary){0},
                               "Out of memory while preparing the scrape.");
    }

    int scrape_count = 0;
    int pre_existing = 0;
    for (int i = 0; i < rom_count; i++) {
        if (missing_only && artwork_exists(roms[i].path, roms[i].display)) {
            pre_existing++;
            continue;
        }
        to_scrape[scrape_count++] = roms[i];
    }

    scrape_state state;
    memset(&state, 0, sizeof(state));
    state.console = console;
    state.roms = to_scrape;
    state.rom_count = scrape_count;
    state.system_id = system_id;
    state.interrupt_signal = interrupt_signal;
    state.summary.total = rom_count;
    state.summary.found = pre_existing;
    pthread_mutex_init(&state.mu, NULL);
    atomic_init(&state.next_index, 1);
    atomic_init(&state.completed, 0);
    atomic_init(&state.abort_requested, 0);
    atomic_init(&state.stats_threads, 1);
    atomic_init(&state.stats_req_today, 0);
    atomic_init(&state.stats_max_req, 0);
    state.last_ns = monotonic_now_ns();

    ss_client client = {0};
    snprintf(client.username, sizeof(client.username), "%s", settings->ss_username);
    snprintf(client.password, sizeof(client.password), "%s", settings->ss_password);
    client.interrupt_signal = interrupt_signal;
    state.client = &client;

    state.artwork_types = build_artwork_types(settings, &state.artwork_count);
    state.region_prio = build_region_types(settings, &state.region_count);
    if (!state.artwork_types || !state.region_prio) {
        for (int i = 0; i < state.artwork_count; i++) free(state.artwork_types[i]);
        free(state.artwork_types);
        for (int i = 0; i < state.region_count; i++) free(state.region_prio[i]);
        free(state.region_prio);
        free(to_scrape);
        free(roms);
        pthread_mutex_destroy(&state.mu);
        return make_run_result(RUN_ERROR, state.summary,
                               "Out of memory while preparing scrape settings.");
    }

    if (scrape_count == 0) {
        if (set_progress)
            set_progress(1.0f);
        run_result result = make_run_result(RUN_OK, state.summary, NULL);
        for (int i = 0; i < state.artwork_count; i++) free(state.artwork_types[i]);
        free(state.artwork_types);
        for (int i = 0; i < state.region_count; i++) free(state.region_prio[i]);
        free(state.region_prio);
        free(to_scrape);
        free(roms);
        pthread_mutex_destroy(&state.mu);
        return result;
    }

    int max_threads = 1;
    int first_result = process_one_rom(&state, &to_scrape[0], true, &max_threads);
    atomic_store(&state.stats_threads, max_threads);

    if (set_progress)
        set_progress((float)atomic_load(&state.completed) / (float)rom_count);

    char fatal_message[256];
    copy_fatal_message(&state, fatal_message, sizeof(fatal_message));

    if (fatal_message[0] != '\0') {
        run_result result = make_run_result(RUN_ERROR, state.summary, fatal_message);
        for (int i = 0; i < state.artwork_count; i++) free(state.artwork_types[i]);
        free(state.artwork_types);
        for (int i = 0; i < state.region_count; i++) free(state.region_prio[i]);
        free(state.region_prio);
        free(to_scrape);
        free(roms);
        pthread_mutex_destroy(&state.mu);
        return result;
    }

    if (first_result == -2 || is_cancel_requested(interrupt_signal)) {
        run_result result = make_run_result(RUN_CANCELLED, state.summary, NULL);
        for (int i = 0; i < state.artwork_count; i++) free(state.artwork_types[i]);
        free(state.artwork_types);
        for (int i = 0; i < state.region_count; i++) free(state.region_prio[i]);
        free(state.region_prio);
        free(to_scrape);
        free(roms);
        pthread_mutex_destroy(&state.mu);
        return result;
    }

    if (scrape_count > 1) {
        atomic_int hud_stop;
        atomic_init(&hud_stop, 0);
        hud_ctx hud = {
            .completed = &state.completed,
            .scrape_total = scrape_count,
            .rom_total = rom_count,
            .stats_threads = &state.stats_threads,
            .stats_req_today = &state.stats_req_today,
            .stats_max_req = &state.stats_max_req,
            .avg_rom_ns = &state.avg_rom_ns,
            .last_ns = &state.last_ns,
            .mu = &state.mu,
            .stop = &hud_stop,
            .set_message = set_message,
            .set_progress = set_progress,
        };

        pthread_t hud_thread;
        int hud_started = (pthread_create(&hud_thread, NULL, hud_thread_fn, &hud) == 0);
        if (!hud_started)
            set_fatal_message_locked(&state, "Failed to start scrape progress updates.");

        pthread_t *workers = calloc((size_t)max_threads, sizeof(pthread_t));
        worker_arg worker = {.state = &state};

        if (!workers) {
            if (hud_started) {
                atomic_store(&hud_stop, 1);
                pthread_join(hud_thread, NULL);
            }
            set_fatal_message_locked(&state, "Out of memory while starting scrape workers.");
        } else {
            int worker_count = 0;
            for (int i = 0; i < max_threads; i++) {
                if (pthread_create(&workers[i], NULL, scrape_worker, &worker) == 0) {
                    worker_count++;
                } else {
                    set_fatal_message_locked(&state, "Failed to start scrape worker threads.");
                    break;
                }
            }

            for (int i = 0; i < worker_count; i++)
                pthread_join(workers[i], NULL);

            free(workers);
            if (hud_started) {
                atomic_store(&hud_stop, 1);
                pthread_join(hud_thread, NULL);
            }
        }
    }

    copy_fatal_message(&state, fatal_message, sizeof(fatal_message));
    if (set_progress && fatal_message[0] == '\0')
        set_progress(1.0f);

    run_result result;
    if (fatal_message[0] != '\0') {
        result = make_run_result(RUN_ERROR, state.summary, fatal_message);
    } else if (is_cancel_requested(interrupt_signal)) {
        result = make_run_result(RUN_CANCELLED, state.summary, NULL);
    } else {
        result = make_run_result(RUN_OK, state.summary, NULL);
    }

    for (int i = 0; i < state.artwork_count; i++) free(state.artwork_types[i]);
    free(state.artwork_types);
    for (int i = 0; i < state.region_count; i++) free(state.region_prio[i]);
    free(state.region_prio);
    free(to_scrape);
    free(roms);
    pthread_mutex_destroy(&state.mu);
    return result;
}

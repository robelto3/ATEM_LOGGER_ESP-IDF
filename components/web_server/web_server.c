#include "web_server.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "app_state.h"
#include "cut_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net_config.h"
#include "logger_session.h"
#include "logger_events.h"
#include "net_eth.h"
#include "rtc.h"
#include "sd_storage.h"
#include "show_config.h"

#define WEB_FILE_NAME_MAX_LEN 64
#define WEB_FILE_PATH_MAX_LEN 128
#define WEB_READ_BUFFER_LEN   256
#define WEB_MAX_FILE_LIST     300
#define WEB_FILES_PER_PAGE     20
#define WEB_MAX_SELECTED_DELETE 20
#define WEB_FORM_BODY_MAX_LEN   2048
#define WEB_ARCHIVE_DIR_NAME    "archive"
#define WEB_ARCHIVE_DIR_PATH    SD_STORAGE_MOUNT_POINT "/" WEB_ARCHIVE_DIR_NAME
#define WEB_TRASH_DIR_NAME      "trash"
#define WEB_TRASH_DIR_PATH      SD_STORAGE_MOUNT_POINT "/" WEB_TRASH_DIR_NAME

// Web server je pomalá/obslužná část projektu.
// HTTP task držíme na Core 0, mimo rychlou ATEM/LTC část na Core 1.
#define WEB_SERVER_TASK_CORE    0
#define WEB_SERVER_TASK_STACK   8192

static const char *TAG = "WEB";
static httpd_handle_t s_server = NULL;

static esp_err_t web_get_query_value(httpd_req_t *req, const char *key, char *value, size_t value_len);

// Režim výpisu souborů držíme jen v RAM.
// Po resetu je výchozí filtr: soubory se střihy + aktuální soubor.
typedef enum {
    WEB_FILES_MODE_WITH_CUTS = 0,
    WEB_FILES_MODE_ALL,
    WEB_FILES_MODE_EMPTY,
    WEB_FILES_MODE_ARCHIVE,
    WEB_FILES_MODE_TRASH
} web_files_mode_t;

static web_files_mode_t s_files_mode = WEB_FILES_MODE_WITH_CUTS;

static const char *web_files_mode_query_value(web_files_mode_t mode)
{
    switch (mode) {
    case WEB_FILES_MODE_TRASH:
        return "trash";
    case WEB_FILES_MODE_ARCHIVE:
        return "archive";
    case WEB_FILES_MODE_ALL:
        return "all";
    case WEB_FILES_MODE_EMPTY:
        return "empty";
    case WEB_FILES_MODE_WITH_CUTS:
    default:
        return "cuts";
    }
}

static const char *web_files_mode_label(web_files_mode_t mode)
{
    switch (mode) {
    case WEB_FILES_MODE_TRASH:
        return "koš";
    case WEB_FILES_MODE_ARCHIVE:
        return "archiv";
    case WEB_FILES_MODE_ALL:
        return "všechny soubory";
    case WEB_FILES_MODE_EMPTY:
        return "soubory se střihy = 0 + aktuální";
    case WEB_FILES_MODE_WITH_CUTS:
    default:
        return "soubory se střihy + aktuální";
    }
}

static bool web_files_mode_should_show(web_files_mode_t mode, int cut_count, bool is_current_file)
{
    if (mode == WEB_FILES_MODE_ARCHIVE || mode == WEB_FILES_MODE_TRASH) {
        return true;
    }

    if (is_current_file) {
        return true;
    }

    switch (mode) {
    case WEB_FILES_MODE_ALL:
        return true;
    case WEB_FILES_MODE_EMPTY:
        return cut_count == 0;
    case WEB_FILES_MODE_WITH_CUTS:
    default:
        return cut_count > 0;
    }
}

static void web_send_chunk(httpd_req_t *req, const char *text)
{
    if (text) {
        httpd_resp_sendstr_chunk(req, text);
    }
}

static esp_err_t web_redirect_to_files(httpd_req_t *req)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/files");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "Redirecting to /files");
    return ESP_OK;
}

static esp_err_t web_redirect_to_files_location(httpd_req_t *req, bool archived, bool trashed)
{
    if (trashed) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/files?mode=trash");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_sendstr(req, "Redirecting to trash");
        return ESP_OK;
    }

    if (!archived) {
        return web_redirect_to_files(req);
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/files?mode=archive");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "Redirecting to archive");
    return ESP_OK;
}

static void web_send_html_escaped(httpd_req_t *req, const char *text)
{
    if (!text) {
        return;
    }

    for (const char *p = text; *p; p++) {
        switch (*p) {
        case '&': web_send_chunk(req, "&amp;"); break;
        case '<': web_send_chunk(req, "&lt;"); break;
        case '>': web_send_chunk(req, "&gt;"); break;
        case '"': web_send_chunk(req, "&quot;"); break;
        case '\'': web_send_chunk(req, "&#39;"); break;
        default: {
            char c[2] = {*p, '\0'};
            web_send_chunk(req, c);
            break;
        }
        }
    }
}


static void web_send_json_escaped(httpd_req_t *req, const char *text)
{
    if (!text) {
        return;
    }

    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (*p) {
        case '\\': web_send_chunk(req, "\\\\"); break;
        case '"': web_send_chunk(req, "\\\""); break;
        case '\b': web_send_chunk(req, "\\b"); break;
        case '\f': web_send_chunk(req, "\\f"); break;
        case '\n': web_send_chunk(req, "\\n"); break;
        case '\r': web_send_chunk(req, "\\r"); break;
        case '\t': web_send_chunk(req, "\\t"); break;
        default:
            if (c < 0x20U) {
                char esc[7];
                snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)c);
                web_send_chunk(req, esc);
            } else {
                char one[2] = {*p, '\0'};
                web_send_chunk(req, one);
            }
            break;
        }
    }
}

static void web_format_uint_spaces(uint32_t value, char *out, size_t out_size)
{
    char raw[16];
    char formatted[24];
    size_t raw_len;
    size_t out_pos = 0;

    if (!out || out_size == 0U) {
        return;
    }

    snprintf(raw, sizeof(raw), "%lu", (unsigned long)value);
    raw_len = strlen(raw);

    for (size_t i = 0; i < raw_len && out_pos + 1U < sizeof(formatted); i++) {
        size_t remaining = raw_len - i;
        if (i > 0U && (remaining % 3U) == 0U) {
            formatted[out_pos++] = ' ';
            if (out_pos + 1U >= sizeof(formatted)) {
                break;
            }
        }
        formatted[out_pos++] = raw[i];
    }

    formatted[out_pos] = '\0';
    snprintf(out, out_size, "%s", formatted);
}

static void web_format_storage_size(uint64_t bytes, char *out, size_t out_size)
{
    uint64_t unit_bytes = 1;
    const char *unit = "B";

    if (!out || out_size == 0U) {
        return;
    }

    if (bytes >= 1000000000ULL) {
        unit_bytes = 1000000000ULL;
        unit = "GB";
    } else if (bytes >= 1000000ULL) {
        unit_bytes = 1000000ULL;
        unit = "MB";
    } else if (bytes >= 1000ULL) {
        unit_bytes = 1000ULL;
        unit = "kB";
    }

    if (unit_bytes == 1U) {
        snprintf(out, out_size, "%llu %s", (unsigned long long)bytes, unit);
        return;
    }

    uint64_t whole = bytes / unit_bytes;
    uint64_t fraction = ((bytes % unit_bytes) * 10ULL) / unit_bytes;
    snprintf(out, out_size, "%llu.%llu %s", (unsigned long long)whole, (unsigned long long)fraction, unit);
}

static void web_send_html_header(httpd_req_t *req, const char *title)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    web_send_chunk(req, "<!doctype html><html><head><meta charset='utf-8'>");
    web_send_chunk(req, "<meta name='viewport' content='width=device-width,initial-scale=1'>");
    web_send_chunk(req, "<title>");
    web_send_html_escaped(req, title ? title : "ATEM Logger");
    web_send_chunk(req, "</title>");
    web_send_chunk(req,
        "<style>"
        "body{font-family:Arial,sans-serif;margin:24px;background:#111;color:#eee;}"
        "a{color:#7cc7ff;text-decoration:none;}"
        "a:hover{text-decoration:underline;}"
        ".card{background:#1b1b1b;border:1px solid #333;border-radius:10px;padding:16px;margin:0 0 16px 0;}"
        ".ok{color:#81f781;font-weight:bold;}"
        ".bad{color:#ff8a8a;font-weight:bold;}"
        ".rtc-link{text-decoration:none;cursor:pointer;}"
        ".rtc-link:hover{color:#7cc7ff;text-decoration:none;}"
        ".rtc-time{display:inline-block;margin-left:28px;}"
        ".active-show{color:#81f781;font-weight:bold;}"
        ".home-title{color:#eee;text-decoration:none;}"
        ".home-title:hover{color:#7cc7ff;text-decoration:none;}"
        ".home-top{display:inline-block;max-width:100%;}"
        ".home-head{display:flex;align-items:baseline;justify-content:space-between;gap:42px;flex-wrap:wrap;margin:0 0 12px 0;}"
        ".home-head h2{margin:0;}"
        ".home-head .rtc-link{color:#eee;font-size:21px;}"
        ".home-head .rtc-link:hover{text-decoration:underline;}"
        ".home-head .rtc-link:hover .rtc-time{text-decoration:underline;}"
        ".home-top .btn:last-child{margin-right:0;}"
        ".home-pgm-label{color:#ff8a8a;font-weight:bold;}"
        ".home-pvw-label{color:#81f781;font-weight:bold;}"
        ".settings-note{display:block;margin-top:2px;margin-bottom:12px;}"
        ".settings-note-ltc{margin-bottom:20px;}"
        ".settings-ip-save{margin-top:0;}"
        ".settings-section-spaced{margin-top:28px;}"
        ".selectcheck{accent-color:crimson;min-width:0;width:auto;margin:0;}"
        ".table-wrap{overflow-x:auto;max-width:100%;}"
        ".select-cell{text-align:center;width:32px;padding-left:4px;padding-right:4px;}"
        ".file-cell{white-space:nowrap;width:120px;padding-right:6px;}"
        ".program-cell{white-space:nowrap;padding-left:20px;padding-right:20px;}"
        ".cuts-cell{text-align:right;white-space:nowrap;width:55px;padding-left:12px;padding-right:12px;}"
        ".size-cell{text-align:right;white-space:nowrap;width:95px;padding-left:12px;padding-right:12px;}"
        ".view-cell{text-align:center;white-space:nowrap;width:127px;padding-left:20px;padding-right:20px;}"
        ".download-cell{text-align:center;white-space:nowrap;width:85px;padding-left:8px;padding-right:8px;}"
        ".download-cell a{color:#7fe08a;}"
        ".download-cell a:hover{color:#a6f5ad;}"
        ".archive-cell{text-align:center;white-space:nowrap;width:80px;padding-left:12px;padding-right:12px;}"
        ".delete-cell{text-align:center;white-space:nowrap;padding-left:12px;padding-right:12px;}"
        ".current-row{height:56px;}"
        ".files-table td{border-bottom:0;}"
        ".files-table tr+tr td{border-top:0;}"
        ".files-table tr:nth-child(even) td{background:#202020;}"
        ".files-table tr:nth-child(odd) td{background:#171717;}"
        ".files-table tr.current-row td{background:#262626;}"
        ".files-table tr:hover td{background:#26313a;}"
        ".files-table tr.current-row:hover td{background:#2b3741;}"
        ".disabled-delete{color:#aaa;text-decoration:line-through;}"
        ".cut-count{color:#7cc7ff;}"
        ".current-file{text-decoration:none;}"
        ".current-file:hover{text-decoration:underline;}"
        ".copy-title{cursor:pointer;}"
        ".copy-title:hover{color:#fff;}"
        ".copy-title.copied{color:#7fe08a;}"
        "table{border-collapse:collapse;width:auto;}"
        "th,td{border-bottom:1px solid #333;padding:8px;text-align:left;}"
        "pre{background:#050505;border:1px solid #333;border-radius:10px;padding:14px;overflow:auto;white-space:pre-wrap;}"
        ".btn{display:inline-block;background:#2c2c2c;border:1px solid #555;border-radius:8px;padding:8px 12px;margin:4px 8px 4px 0;}"
        "button.btn{appearance:none;-webkit-appearance:none;cursor:pointer;color:#eee;font-family:inherit;font-size:14px;line-height:normal;}button.btn:disabled{opacity:.45;cursor:not-allowed;}button.btn.link-like{color:#7cc7ff;}"
        "button.btn.settings-btn,a.btn.settings-btn{height:39px;padding:0 12px;line-height:37px;}"
        "button.btn.rtc-sync-btn{color:#7cc7ff;font-weight:bold;font-size:16px;font-family:inherit;line-height:normal;}.home-card{font-size:18px;line-height:1.35;}.home-card h2{font-size:26px;}.home-card .btn{font-size:16px;line-height:normal;}"
        ".btn-active{background:#3a101a;border-color:crimson;color:#fff;font-weight:bold;}.btn-active:hover{background:#4a1420;text-decoration:none;}.del{color:#ff7777;}.danger{background:#3a1b1b;border-color:#884444;color:#ffb0b0;}"
        ".muted{color:#aaa;}input{background:#050505;color:#eee;border:1px solid #555;border-radius:8px;padding:8px;margin:4px 0 10px 0;min-width:160px;}input.filecheck{min-width:0;width:auto;margin:0;}input.radio{accent-color:crimson;min-width:0;width:auto;margin:0;}input.setting-check{accent-color:crimson;min-width:0;width:auto;margin:0;}input.settings-field,button.btn.settings-field,a.btn.settings-field{box-sizing:border-box;height:39px;margin:4px 0 10px 0;vertical-align:top;}label{display:block;margin-top:8px;}small{color:#aaa;}.settings-check-row{display:flex;align-items:center;gap:8px;}.settings-check-text{display:inline-block;width:128px;}.settings-card,.settings-card label,.settings-card input{font-size:18px;}.settings-card button,.settings-card a.btn{font-size:16px;}.settings-card small{font-size:15px;}"
        ".show-row{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin:8px 0 10px 0;}"
        ".show-label{display:inline-block;min-width:120px;margin:0;}"
        ".show-name{min-width:260px;width:360px;max-width:100%;margin:0;}"
        ".show-active{display:inline-flex;align-items:center;gap:6px;margin:0;white-space:nowrap;}"
        ".show-active-selected{color:crimson;font-weight:bold;}"
        ".show-active-muted{color:#aaa;}"
        ".about-list{line-height:1.65;}"
        ".about-badge{display:inline-block;border:1px solid #555;border-radius:999px;padding:4px 9px;margin:3px 5px 3px 0;background:#222;color:#ddd;}"
        "</style></head><body>"
    );
}

static void web_send_html_footer(httpd_req_t *req)
{
    web_send_chunk(req,
        "<script>"
        "(function(){"
        "function fb(t,done){var a=document.createElement('textarea');a.value=t;document.body.appendChild(a);a.select();try{document.execCommand('copy');}catch(e){}document.body.removeChild(a);done();}"
        "document.querySelectorAll('.copy-title').forEach(function(el){"
        "el.addEventListener('click',function(){"
        "var t=el.getAttribute('data-copy')||el.textContent;"
        "var old=el.getAttribute('title')||'Zkopírovat';"
        "function done(){el.classList.add('copied');el.setAttribute('title','Zkopírováno');setTimeout(function(){el.classList.remove('copied');el.setAttribute('title',old);},1200);}"
        "if(navigator.clipboard&&navigator.clipboard.writeText){navigator.clipboard.writeText(t).then(done).catch(function(){fb(t,done);});}else{fb(t,done);}"
        "});"
        "});"
        "})();"
        "</script>"
    );
    web_send_chunk(req, "</body></html>");
    httpd_resp_send_chunk(req, NULL, 0);
}

static bool web_is_safe_filename(const char *filename)
{
    if (!filename || filename[0] == '\0') {
        return false;
    }

    if (strlen(filename) >= WEB_FILE_NAME_MAX_LEN) {
        return false;
    }

    if (strstr(filename, "..") != NULL) {
        return false;
    }

    for (const char *p = filename; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }

    return true;
}

static esp_err_t web_make_file_path_for_location(const char *filename, bool archived, char *path, size_t path_len)
{
    if (!web_is_safe_filename(filename) || !path || path_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(
        path,
        path_len,
        "%s/%s",
        archived ? WEB_ARCHIVE_DIR_PATH : SD_STORAGE_MOUNT_POINT,
        filename
    );
    if (written <= 0 || written >= (int)path_len) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t web_make_file_path(const char *filename, char *path, size_t path_len)
{
    return web_make_file_path_for_location(filename, false, path, path_len);
}

static esp_err_t web_make_archive_file_path(const char *filename, char *path, size_t path_len)
{
    return web_make_file_path_for_location(filename, true, path, path_len);
}

static esp_err_t web_make_trash_file_path(const char *filename, char *path, size_t path_len)
{
    if (!web_is_safe_filename(filename) || !path || path_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(path, path_len, "%s/%s", WEB_TRASH_DIR_PATH, filename);
    if (written <= 0 || written >= (int)path_len) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t web_make_trash_filename_with_index(const char *filename, unsigned index, char *out, size_t out_len)
{
    if (!web_is_safe_filename(filename) || !out || out_len == 0U || index > 999U) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *dot = strrchr(filename, '.');
    size_t base_len = dot ? (size_t)(dot - filename) : strlen(filename);
    if (base_len == 0U || base_len + 5U > out_len) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(out, filename, base_len);
    snprintf(out + base_len, out_len - base_len, ".%03u", index);
    return ESP_OK;
}

static esp_err_t web_find_trash_target_path(const char *filename, char *trash_filename, size_t trash_filename_len, char *path, size_t path_len)
{
    if (!trash_filename || trash_filename_len == 0U || !path || path_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    char original_path[WEB_FILE_PATH_MAX_LEN] = {0};
    if (web_make_trash_file_path(filename, original_path, sizeof(original_path)) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(original_path, &st) != 0) {
        snprintf(trash_filename, trash_filename_len, "%s", filename);
        snprintf(path, path_len, "%s", original_path);
        return ESP_OK;
    }

    for (unsigned i = 0; i <= 999U; i++) {
        char candidate[WEB_FILE_NAME_MAX_LEN] = {0};
        if (web_make_trash_filename_with_index(filename, i, candidate, sizeof(candidate)) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }

        char candidate_path[WEB_FILE_PATH_MAX_LEN] = {0};
        if (web_make_trash_file_path(candidate, candidate_path, sizeof(candidate_path)) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }

        if (stat(candidate_path, &st) != 0) {
            snprintf(trash_filename, trash_filename_len, "%s", candidate);
            snprintf(path, path_len, "%s", candidate_path);
            return ESP_OK;
        }
    }

    return ESP_ERR_NO_MEM;
}

static esp_err_t web_find_archive_target_path(const char *filename, char *archive_filename, size_t archive_filename_len, char *path, size_t path_len)
{
    if (!archive_filename || archive_filename_len == 0U || !path || path_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    char original_path[WEB_FILE_PATH_MAX_LEN] = {0};
    if (web_make_archive_file_path(filename, original_path, sizeof(original_path)) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(original_path, &st) != 0) {
        snprintf(archive_filename, archive_filename_len, "%s", filename);
        snprintf(path, path_len, "%s", original_path);
        return ESP_OK;
    }

    for (unsigned i = 0; i <= 999U; i++) {
        char candidate[WEB_FILE_NAME_MAX_LEN] = {0};
        if (web_make_trash_filename_with_index(filename, i, candidate, sizeof(candidate)) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }

        char candidate_path[WEB_FILE_PATH_MAX_LEN] = {0};
        if (web_make_archive_file_path(candidate, candidate_path, sizeof(candidate_path)) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }

        if (stat(candidate_path, &st) != 0) {
            snprintf(archive_filename, archive_filename_len, "%s", candidate);
            snprintf(path, path_len, "%s", candidate_path);
            return ESP_OK;
        }
    }

    return ESP_ERR_NO_MEM;
}

static esp_err_t web_find_restore_target_path(const char *filename, char *restore_filename, size_t restore_filename_len, char *path, size_t path_len)
{
    if (!restore_filename || restore_filename_len == 0U || !path || path_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    char original_path[WEB_FILE_PATH_MAX_LEN] = {0};
    if (web_make_file_path(filename, original_path, sizeof(original_path)) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(original_path, &st) != 0) {
        snprintf(restore_filename, restore_filename_len, "%s", filename);
        snprintf(path, path_len, "%s", original_path);
        return ESP_OK;
    }

    for (unsigned i = 0; i <= 999U; i++) {
        char candidate[WEB_FILE_NAME_MAX_LEN] = {0};
        if (web_make_trash_filename_with_index(filename, i, candidate, sizeof(candidate)) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }

        char candidate_path[WEB_FILE_PATH_MAX_LEN] = {0};
        if (web_make_file_path(candidate, candidate_path, sizeof(candidate_path)) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }

        if (stat(candidate_path, &st) != 0) {
            snprintf(restore_filename, restore_filename_len, "%s", candidate);
            snprintf(path, path_len, "%s", candidate_path);
            return ESP_OK;
        }
    }

    return ESP_ERR_NO_MEM;
}

static esp_err_t web_make_file_path_for_query_location(const char *filename, bool archived, bool trashed, char *path, size_t path_len)
{
    if (trashed) {
        return web_make_trash_file_path(filename, path, path_len);
    }

    return web_make_file_path_for_location(filename, archived, path, path_len);
}

static bool web_query_has_archive(httpd_req_t *req)
{
    if (!req) {
        return false;
    }

    char query[128] = {0};
    char value[8] = {0};
    size_t query_len = httpd_req_get_url_query_len(req) + 1;

    if (query_len <= 1 || query_len > sizeof(query)) {
        return false;
    }

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }

    return (httpd_query_key_value(query, "archive", value, sizeof(value)) == ESP_OK &&
            strcmp(value, "1") == 0);
}

static bool web_query_has_trash(httpd_req_t *req)
{
    if (!req) {
        return false;
    }

    char query[128] = {0};
    char value[8] = {0};
    size_t query_len = httpd_req_get_url_query_len(req) + 1;

    if (query_len <= 1 || query_len > sizeof(query)) {
        return false;
    }

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }

    return (httpd_query_key_value(query, "trash", value, sizeof(value)) == ESP_OK &&
            strcmp(value, "1") == 0);
}

static esp_err_t web_ensure_archive_dir(void)
{
    if (mkdir(WEB_ARCHIVE_DIR_PATH, 0775) == 0) {
        return ESP_OK;
    }

    if (errno == EEXIST) {
        struct stat st;
        if (stat(WEB_ARCHIVE_DIR_PATH, &st) == 0 && S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

static esp_err_t web_ensure_trash_dir(void)
{
    if (mkdir(WEB_TRASH_DIR_PATH, 0775) == 0) {
        return ESP_OK;
    }

    if (errno == EEXIST) {
        struct stat st;
        if (stat(WEB_TRASH_DIR_PATH, &st) == 0 && S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

static esp_err_t web_get_query_filename(httpd_req_t *req, char *filename, size_t filename_len)
{
    if (!req || !filename || filename_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char query[128] = {0};
    size_t query_len = httpd_req_get_url_query_len(req) + 1;

    if (query_len <= 1 || query_len > sizeof(query)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = httpd_req_get_url_query_str(req, query, sizeof(query));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = httpd_query_key_value(query, "file", filename, filename_len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!web_is_safe_filename(filename)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}



static bool web_is_current_file(const char *filename)
{
    if (!filename) {
        return false;
    }

    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    if (!web_is_safe_filename(state.current_filename)) {
        return false;
    }

    return strcmp(filename, state.current_filename) == 0;
}

typedef struct {
    char name[WEB_FILE_NAME_MAX_LEN];
    long size;
    bool has_edl_sort_key;
    uint32_t edl_sort_key;
    bool is_current_file;
    int cut_count;
    char edl_title[SHOW_CONFIG_TITLE_MAX_LEN];
} web_file_item_t;

typedef struct {
    char name[WEB_FILE_NAME_MAX_LEN];
} web_selected_file_t;

static bool web_selected_contains(const web_selected_file_t *selected, unsigned count, const char *name)
{
    if (!selected || !name) {
        return false;
    }

    for (unsigned i = 0; i < count; i++) {
        if (strcmp(selected[i].name, name) == 0) {
            return true;
        }
    }

    return false;
}

static bool web_make_index_key(char *out, size_t out_len, unsigned index)
{
    if (!out || out_len < 3U) {
        return false;
    }

    char digits[10];
    unsigned digit_count = 0;
    unsigned value = index;

    do {
        if (digit_count >= sizeof(digits)) {
            return false;
        }

        digits[digit_count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value > 0U);

    if ((digit_count + 2U) > out_len) {
        return false;
    }

    out[0] = 'f';
    for (unsigned i = 0; i < digit_count; i++) {
        out[1U + i] = digits[digit_count - 1U - i];
    }
    out[1U + digit_count] = '\0';

    return true;
}

static bool web_file_exists_regular(const char *filename)
{
    char path[WEB_FILE_PATH_MAX_LEN] = {0};
    if (web_make_file_path(filename, path, sizeof(path)) != ESP_OK) {
        return false;
    }

    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static unsigned web_get_selected_files_from_query(
    httpd_req_t *req,
    web_selected_file_t *selected,
    unsigned max_selected,
    unsigned max_keys
)
{
    if (!req || !selected || max_selected == 0U) {
        return 0;
    }

    char query[1024] = {0};
    size_t query_len = httpd_req_get_url_query_len(req) + 1U;

    if (query_len <= 1U || query_len > sizeof(query)) {
        return 0;
    }

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return 0;
    }

    unsigned count = 0;
    for (unsigned i = 0; i < max_keys && count < max_selected; i++) {
        char key[16];
        char filename[WEB_FILE_NAME_MAX_LEN] = {0};

        if (!web_make_index_key(key, sizeof(key), i)) {
            continue;
        }

        if (httpd_query_key_value(query, key, filename, sizeof(filename)) != ESP_OK) {
            continue;
        }

        if (!web_is_safe_filename(filename)) {
            continue;
        }

        if (web_selected_contains(selected, count, filename)) {
            continue;
        }

        if (!web_file_exists_regular(filename)) {
            continue;
        }

        // Aktuální otevřený soubor se nesmí dostat do hromadného mazání.
        // Mazání aktuálního souboru se řeší pouze samostatnou akcí /delete.
        if (web_is_current_file(filename)) {
            continue;
        }

        size_t name_len = strlen(filename);
        if (name_len >= sizeof(selected[count].name)) {
            continue;
        }
        memcpy(selected[count].name, filename, name_len + 1U);
        count++;
    }

    return count;
}

static int web_char_to_lower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

static int web_filename_compare_az(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = web_char_to_lower((unsigned char)*a);
        int cb = web_char_to_lower((unsigned char)*b);

        if (ca != cb) {
            return ca - cb;
        }

        a++;
        b++;
    }

    return web_char_to_lower((unsigned char)*a) - web_char_to_lower((unsigned char)*b);
}


static bool web_filename_has_edl_extension(const char *name)
{
    if (!name) {
        return false;
    }

    size_t len = strlen(name);
    if (len < 5) {
        return false;
    }

    const char *ext = name + len - 4;
    return ext[0] == '.' &&
           web_char_to_lower((unsigned char)ext[1]) == 'e' &&
           web_char_to_lower((unsigned char)ext[2]) == 'd' &&
           web_char_to_lower((unsigned char)ext[3]) == 'l';
}

static bool web_filename_has_numeric_trash_extension(const char *name)
{
    if (!name) {
        return false;
    }

    size_t len = strlen(name);
    if (len < 5) {
        return false;
    }

    const char *ext = name + len - 4;
    return ext[0] == '.' &&
           isdigit((unsigned char)ext[1]) &&
           isdigit((unsigned char)ext[2]) &&
           isdigit((unsigned char)ext[3]);
}

static bool web_filename_has_readable_edl_extension(const char *name)
{
    return web_filename_has_edl_extension(name) || web_filename_has_numeric_trash_extension(name);
}

static bool web_line_starts_with_edl_event(const char *line)
{
    if (!line) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        if (!isdigit((unsigned char)line[i])) {
            return false;
        }
    }

    return isspace((unsigned char)line[6]);
}

static bool web_title_has_trailing_show_number(const char *title, size_t len, size_t *new_len)
{
    if (!title || !new_len || len < 6U) {
        return false;
    }

    // Starší formát: "Název pořadu (004)".
    if (len >= 6U && title[len - 1U] == ')' &&
        isdigit((unsigned char)title[len - 2U]) &&
        isdigit((unsigned char)title[len - 3U]) &&
        isdigit((unsigned char)title[len - 4U]) &&
        title[len - 5U] == '(' &&
        isspace((unsigned char)title[len - 6U])) {
        *new_len = len - 6U;
        return true;
    }

    // Ještě starší mezikrok: "Název pořadu 004".
    if (len >= 4U &&
        isdigit((unsigned char)title[len - 1U]) &&
        isdigit((unsigned char)title[len - 2U]) &&
        isdigit((unsigned char)title[len - 3U]) &&
        isspace((unsigned char)title[len - 4U])) {
        *new_len = len - 4U;
        return true;
    }

    return false;
}

static void web_copy_edl_title_from_line(const char *line, char *out, size_t out_size)
{
    if (!line || !out || out_size == 0) {
        return;
    }

    const char prefix[] = "TITLE:";
    size_t prefix_len = strlen(prefix);
    if (strncmp(line, prefix, prefix_len) != 0) {
        return;
    }

    const char *title = line + prefix_len;
    while (*title && isspace((unsigned char)*title)) {
        title++;
    }

    size_t len = strcspn(title, "\r\n");
    while (len > 0 && isspace((unsigned char)title[len - 1])) {
        len--;
    }

    size_t stripped_len = len;
    if (web_title_has_trailing_show_number(title, len, &stripped_len)) {
        len = stripped_len;
        while (len > 0 && isspace((unsigned char)title[len - 1])) {
            len--;
        }
    }

    if (len >= out_size) {
        len = out_size - 1;
    }

    memcpy(out, title, len);
    out[len] = '\0';
}

static int web_read_edl_info_for_file_location(const char *filename, bool archived, bool trashed, char *title_out, size_t title_out_size)
{
    if (title_out && title_out_size > 0) {
        title_out[0] = '\0';
    }

    if (!web_filename_has_readable_edl_extension(filename)) {
        return -1;
    }

    char path[WEB_FILE_PATH_MAX_LEN];
    if (web_make_file_path_for_query_location(filename, archived, trashed, path, sizeof(path)) != ESP_OK) {
        return -1;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }

    int cuts = 0;
    char line[WEB_READ_BUFFER_LEN];
    while (fgets(line, sizeof(line), f) != NULL) {
        if (web_line_starts_with_edl_event(line)) {
            cuts++;
        }

        if (title_out && title_out_size > 0 && title_out[0] == '\0') {
            web_copy_edl_title_from_line(line, title_out, title_out_size);
        }
    }

    fclose(f);
    return cuts;
}

static void web_make_download_filename_from_title(const char *title, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }

    out[0] = '\0';

    if (!title) {
        return;
    }

    const char *src = title;
    while (*src && isspace((unsigned char)*src)) {
        src++;
    }

    size_t pos = 0;
    bool last_was_space = false;

    while (*src && pos + 1 < out_size) {
        unsigned char c = (unsigned char)*src;

        if (c == '\r' || c == '\n') {
            break;
        }

        if (c < 32 || c == 127 ||
            c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            if (pos > 0 && !last_was_space && pos + 1 < out_size) {
                out[pos++] = ' ';
                last_was_space = true;
            }
            src++;
            continue;
        }

        if (isspace(c)) {
            if (pos > 0 && !last_was_space) {
                out[pos++] = ' ';
                last_was_space = true;
            }
            src++;
            continue;
        }

        out[pos++] = (char)c;
        last_was_space = false;
        src++;
    }

    while (pos > 0 && isspace((unsigned char)out[pos - 1])) {
        pos--;
    }

    out[pos] = '\0';

    if (out[0] == '\0') {
        return;
    }

    size_t len = strlen(out);
    if (len < 4 || strcasecmp(out + len - 4, ".edl") != 0) {
        strncat(out, ".edl", out_size - strlen(out) - 1);
    }
}

static bool web_parse_edl_filename_sort_key(const char *name, uint32_t *sort_key)
{
    if (!name || !sort_key) {
        return false;
    }

    // Format: DDMMRRNN.edl, for example 04052601.edl
    if (strlen(name) != 12) {
        return false;
    }

    for (int i = 0; i < 8; i++) {
        if (!isdigit((unsigned char)name[i])) {
            return false;
        }
    }

    if (name[8] != '.' ||
        (!web_filename_has_edl_extension(name) && !web_filename_has_numeric_trash_extension(name))) {
        return false;
    }

    int dd = (name[0] - '0') * 10 + (name[1] - '0');
    int mm = (name[2] - '0') * 10 + (name[3] - '0');
    int rr = (name[4] - '0') * 10 + (name[5] - '0');
    int nn = (name[6] - '0') * 10 + (name[7] - '0');

    if (dd < 1 || dd > 31 || mm < 1 || mm > 12 || nn < 1 || nn > 99) {
        return false;
    }

    // Sort by real date order from filename: RR -> MM -> DD -> sequence.
    *sort_key = (uint32_t)rr * 1000000U + (uint32_t)mm * 10000U + (uint32_t)dd * 100U + (uint32_t)nn;
    return true;
}

static int web_file_item_compare(const void *pa, const void *pb)
{
    const web_file_item_t *a = (const web_file_item_t *)pa;
    const web_file_item_t *b = (const web_file_item_t *)pb;

    if (a->is_current_file != b->is_current_file) {
        return a->is_current_file ? -1 : 1;
    }

    if (a->has_edl_sort_key && b->has_edl_sort_key) {
        if (a->edl_sort_key < b->edl_sort_key) {
            return 1;
        }
        if (a->edl_sort_key > b->edl_sort_key) {
            return -1;
        }
        return web_filename_compare_az(a->name, b->name);
    }

    if (a->has_edl_sort_key != b->has_edl_sort_key) {
        return a->has_edl_sort_key ? -1 : 1;
    }

    return web_filename_compare_az(a->name, b->name);
}


static unsigned web_get_query_page(httpd_req_t *req)
{
    if (!req) {
        return 1;
    }

    char query[96] = {0};
    size_t query_len = httpd_req_get_url_query_len(req) + 1;

    if (query_len <= 1 || query_len > sizeof(query)) {
        return 1;
    }

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return 1;
    }

    char page_text[16] = {0};
    if (httpd_query_key_value(query, "page", page_text, sizeof(page_text)) != ESP_OK) {
        return 1;
    }

    char *end = NULL;
    unsigned long value = strtoul(page_text, &end, 10);
    if (end == page_text || value < 1UL) {
        return 1;
    }

    if (value > 9999UL) {
        return 9999;
    }

    return (unsigned)value;
}

static void web_send_files_pagination(httpd_req_t *req, unsigned page, unsigned total_pages, web_files_mode_t mode)
{
    if (total_pages <= 1) {
        return;
    }

    char line[256];

    web_send_chunk(req, "<p>");

    char mode_suffix[32];
    snprintf(mode_suffix, sizeof(mode_suffix), "&amp;mode=%s", web_files_mode_query_value(mode));

    if (page > 1) {
        snprintf(line, sizeof(line), "<a class='btn' href='/files?page=%u%s'>Předchozí</a>", page - 1, mode_suffix);
        web_send_chunk(req, line);
    } else {
        web_send_chunk(req, "<span class='btn muted'>Předchozí</span>");
    }

    snprintf(line, sizeof(line), " <span class='muted'>Stránka %u / %u</span> ", page, total_pages);
    web_send_chunk(req, line);

    if (page < total_pages) {
        snprintf(line, sizeof(line), "<a class='btn' href='/files?page=%u%s'>Další</a>", page + 1, mode_suffix);
        web_send_chunk(req, line);
    } else {
        web_send_chunk(req, "<span class='btn muted'>Další</span>");
    }

    web_send_chunk(req, "</p>");
}

static void web_send_status_card(httpd_req_t *req)
{
    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    net_eth_status_t eth;
    net_eth_get_status(&eth);

    char line[256];

    web_send_chunk(req, "<div class='card home-card'>");
    web_send_chunk(req, "<div class='home-top'>");
    web_send_chunk(req, "<div class='home-head'>");
    web_send_chunk(req, "<h2>Home</h2>");

    if (state.rtc_valid) {
        web_send_chunk(req, "<a id='home-rtc-link' class='rtc-link ok' href='#' title='Synchronizovat datum a čas' onclick='return syncRtcFromBrowserConfirm();'><b>");

        snprintf(
            line,
            sizeof(line),
            "<span id='home-rtc-date' title='Synchronizovat datum a čas'>%02u.%02u.%04u</span>",
            state.rtc.date,
            state.rtc.month,
            state.rtc.year
        );
        web_send_chunk(req, line);

        snprintf(
            line,
            sizeof(line),
            "<span id='home-rtc-time' class='rtc-time' title='Synchronizovat datum a čas'>%02u:%02u:%02u</span>",
            state.rtc.hours,
            state.rtc.minutes,
            state.rtc.seconds
        );
        web_send_chunk(req, line);

        web_send_chunk(req, "</b></a>");
    } else {
        web_send_chunk(req, "<a id='home-rtc-link' class='rtc-link bad' href='#' title='Synchronizovat datum a čas' onclick='return syncRtcFromBrowserConfirm();'><b>");
        web_send_chunk(req, "<span id='home-rtc-date' title='Synchronizovat datum a čas'>---</span><span id='home-rtc-time' class='rtc-time' title='Synchronizovat datum a čas'></span>");
        web_send_chunk(req, "</b></a>");
    }
    web_send_chunk(req, "</div>");

    web_send_chunk(req, "<p>");
    web_send_chunk(req, "<a class='btn' href='/files'>Soubory na SD kartě</a> ");
    web_send_chunk(req, "<a class='btn' href='/shows'>Názvy pořadů</a> ");
    web_send_chunk(req, "<a class='btn' href='/network'>Nastavení</a> ");
    web_send_chunk(req, "<a class='btn' href='/about'>About</a>");
    web_send_chunk(req, "</p>");
    web_send_chunk(req, "</div>");

    char atem_ip[NET_CONFIG_IP_STR_LEN] = {0};
    net_config_get_atem_ip_string(atem_ip, sizeof(atem_ip));

    snprintf(
        line,
        sizeof(line),
        "<p>Ethernet: <span class='%s'>%s</span> &nbsp; IP: <b>%s</b></p>",
        eth.link_up ? "ok" : "bad",
        eth.link_up ? "OK" : "---",
        eth.ip
    );
    web_send_chunk(req, line);

    snprintf(
        line,
        sizeof(line),
        "<p>ATEM: <span id='home-atem' class='%s'>%s</span> &nbsp; IP: <b>",
        state.atem_connected ? "ok" : "bad",
        state.atem_connected ? "OK" : "---"
    );
    web_send_chunk(req, line);
    web_send_html_escaped(req, atem_ip);
    web_send_chunk(req, "</b></p>");

    snprintf(
        line,
        sizeof(line),
        "<p>LTC: <span id='home-ltc' class='%s'>%s</span> &nbsp; <b id='home-tc'>%02u:%02u:%02u:%02u</b></p>",
        state.ltc_valid ? "ok" : "bad",
        state.ltc_valid ? "OK" : "---",
        state.tc.hours,
        state.tc.minutes,
        state.tc.seconds,
        state.tc.frames
    );
    web_send_chunk(req, line);

    uint64_t sd_total_bytes = 0;
    uint64_t sd_free_bytes = 0;
    bool sd_mounted = sd_storage_is_mounted();
    bool sd_space_ok = sd_mounted && sd_storage_get_space(&sd_total_bytes, &sd_free_bytes) == ESP_OK;
    if (sd_space_ok) {
        char sd_free_text[24];
        web_format_storage_size(sd_free_bytes, sd_free_text, sizeof(sd_free_text));
        snprintf(
            line,
            sizeof(line),
            "<p>SD karta: <span class='ok'>OK</span> &nbsp; Free: <b>%s</b></p>",
            sd_free_text
        );
    } else {
        snprintf(
            line,
            sizeof(line),
            "<p>SD karta: <span class='%s'>%s</span></p>",
            sd_mounted ? "ok" : "bad",
            sd_mounted ? "OK" : "---"
        );
    }
    web_send_chunk(req, line);

    bool program_tally_enabled = net_config_get_program_tally_enabled();
    bool preview_tally_enabled = net_config_get_preview_tally_enabled();
    snprintf(
        line,
        sizeof(line),
        "<p><span class='home-pgm-label'>PGM:</span> <b id='home-pgm'>%u</b> &nbsp; Tally: <a id='home-pgm-tally' class='%s' href='/save_program_tally?%sback=home' title='%s'><b>%s</b></a></p>",
        state.program_input,
        program_tally_enabled ? "ok" : "bad",
        program_tally_enabled ? "" : "enabled=1&amp;",
        program_tally_enabled ? "Vypnout" : "Zapnout",
        program_tally_enabled ? "ON" : "OFF"
    );
    web_send_chunk(req, line);

    snprintf(
        line,
        sizeof(line),
        "<p><span class='home-pvw-label'>PVW:</span> <b id='home-pvw'>%u</b> &nbsp; Tally: <a id='home-pvw-tally' class='%s' href='/save_preview_tally?%sback=home' title='%s'><b>%s</b></a></p>",
        state.preview_input,
        preview_tally_enabled ? "ok" : "bad",
        preview_tally_enabled ? "" : "enabled=1&amp;",
        preview_tally_enabled ? "Vypnout" : "Zapnout",
        preview_tally_enabled ? "ON" : "OFF"
    );
    web_send_chunk(req, line);

    char cut_count_text[24];
    web_format_uint_spaces(cut_event_get_cut_count(), cut_count_text, sizeof(cut_count_text));

    snprintf(
        line,
        sizeof(line),
        "<p>Počet střihů: <b id='home-cut-count' class='cut-count'>%s</b></p>",
        cut_count_text
    );
    web_send_chunk(req, line);

    char active_show[SHOW_CONFIG_NAME_MAX_LEN] = {0};
    show_config_get_active_name(active_show, sizeof(active_show));

    web_send_chunk(req, "<p>Pořad: <a class='active-show' href='/shows' title='Změnit název pořadu'>");
    web_send_html_escaped(req, active_show);
    web_send_chunk(req, "</a></p>");

    web_send_chunk(req, "<p>Soubor: <b><a id='home-file' class='current-file' href='/new_file' title='Uzavřít a vytvořit nový' onclick=\"return confirm('Opravdu uzavřít aktuální EDL soubor a vytvořit nový?');\">");
    web_send_html_escaped(req, state.current_filename);
    web_send_chunk(req, "</a></b></p>");

    web_send_chunk(req, "</div>");
}


static esp_err_t web_api_state_handler(httpd_req_t *req)
{
    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");

    char line[192];

    web_send_chunk(req, "{");

    bool program_tally_enabled = net_config_get_program_tally_enabled();
    bool preview_tally_enabled = net_config_get_preview_tally_enabled();

    snprintf(
        line,
        sizeof(line),
        "\"atem\":%s,\"ltc\":%s,\"pgm\":%u,\"pvw\":%u,\"pgm_tally\":%s,\"pvw_tally\":%s,",
        state.atem_connected ? "true" : "false",
        state.ltc_valid ? "true" : "false",
        state.program_input,
        state.preview_input,
        program_tally_enabled ? "true" : "false",
        preview_tally_enabled ? "true" : "false"
    );
    web_send_chunk(req, line);

    snprintf(
        line,
        sizeof(line),
        "\"tc\":\"%02u:%02u:%02u:%02u\",",
        state.tc.hours,
        state.tc.minutes,
        state.tc.seconds,
        state.tc.frames
    );
    web_send_chunk(req, line);

    if (state.rtc_valid) {
        snprintf(
            line,
            sizeof(line),
            "\"rtc_valid\":true,\"rtc\":\"%02u.%02u.%04u %02u:%02u:%02u\",",
            state.rtc.date,
            state.rtc.month,
            state.rtc.year,
            state.rtc.hours,
            state.rtc.minutes,
            state.rtc.seconds
        );
    } else {
        snprintf(line, sizeof(line), "\"rtc_valid\":false,\"rtc\":\"---\",");
    }
    web_send_chunk(req, line);

    snprintf(
        line,
        sizeof(line),
        "\"cuts\":%lu,",
        (unsigned long)cut_event_get_cut_count()
    );
    web_send_chunk(req, line);

    web_send_chunk(req, "\"file\":\"");
    web_send_json_escaped(req, state.current_filename);
    web_send_chunk(req, "\"");

    web_send_chunk(req, "}");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t web_about_handler(httpd_req_t *req)
{
    web_send_html_header(req, "ATEM Logger - About");
    web_send_chunk(req, "<h1>About</h1>");
    web_send_chunk(req, "<p><a class='btn' href='/'>Home</a></p>");

    web_send_chunk(req, "<div class='card'>");
    web_send_chunk(req, "<h2>ATEM_LOGGER_ESP-IDF</h2>");
    web_send_chunk(req, "<p><b>ESP32-P4 ATEM logger</b> je zařízení pro záznam střihů z ATEM switcheru do EDL souborů na SD kartu.</p>");
    web_send_chunk(req, "<p>Projekt běží na desce ESP32-P4-ETH v prostředí ESP-IDF a je stavěný modulárně po komponentách.</p>");
    web_send_chunk(req, "<p><span class='about-badge'>ATEM</span><span class='about-badge'>LTC 25 fps</span><span class='about-badge'>TCx2</span><span class='about-badge'>CMX EDL</span><span class='about-badge'>OLED</span><span class='about-badge'>Web</span><span class='about-badge'>Archiv</span><span class='about-badge'>Koš</span><span class='about-badge'>Tally</span></p>");
    web_send_chunk(req, "</div>");

    web_send_chunk(req, "<div class='card'>");
    web_send_chunk(req, "<h2>Hlavní funkce</h2>");
    web_send_chunk(req, "<ul class='about-list'>");
    web_send_chunk(req, "<li>čtení ATEM Program / Preview přes UDP parser <b>PrgI</b> a <b>PrvI</b></li>");
    web_send_chunk(req, "<li>zápis změn Program busu do EDL souboru ve formátu <b>CMX / NON-DROP FRAME</b></li>");
    web_send_chunk(req, "<li>LTC vstup 25 fps s nastavitelnou korekcí a převod na <b>TCx2</b> pro EDL i OLED</li>");
    web_send_chunk(req, "<li>vytváření EDL souborů na SD kartě s názvem <b>DDMMRRNN.edl</b></li>");
    web_send_chunk(req, "<li>uložené názvy pořadů a automatický <b>TITLE: Název pořadu</b></li>");
    web_send_chunk(req, "<li>webové zobrazení, stažení, archivace a přesun EDL souborů do koše</li>");
    web_send_chunk(req, "<li>samostatné stránky pro Archiv a Koš s obnovou souborů a definitivním mazáním v koši</li>");
    web_send_chunk(req, "<li>RTC synchronizace z času prohlížeče</li>");
    web_send_chunk(req, "<li>OLED úvodní IP obrazovka a stav: ATEM, LTC, PGM, PVW, TCx2 a počet střihů</li>");
    web_send_chunk(req, "<li>samostatně vypínatelné Program / Preview tally výstupy</li>");
    web_send_chunk(req, "<li>nastavení IP loggeru a ATEM switcheru s pohodlným rebootem a přechodem na novou IP</li>");
    web_send_chunk(req, "<li>fake cut test přes GPIO46 bez nutnosti připojeného ATEMu</li>");
    web_send_chunk(req, "<li>rozdělení úloh mezi dvě jádra ESP32-P4 přes FreeRTOS tasky</li>");
    web_send_chunk(req, "</ul>");
    web_send_chunk(req, "</div>");

    web_send_chunk(req, "<div class='card'>");
    web_send_chunk(req, "<h2>Rozdělení jader</h2>");
    web_send_chunk(req, "<p><b>Core 1</b>: rychlá část – ATEM, LTC snapshot, tlačítka a vkládání událostí do logger fronty.</p>");
    web_send_chunk(req, "<p><b>Core 0</b>: pomalá/obslužná část – logger, SD zápis, web, OLED, RTC, UART a tally výstupy.</p>");
    web_send_chunk(req, "<p>Události jdou přes <b>logger_events queue</b>, aby ATEM/LTC část nečekala na pomalý zápis na SD kartu.</p>");
    web_send_chunk(req, "</div>");

    web_send_chunk(req, "<div class='card'>");
    web_send_chunk(req, "<p>Na tomto programu se mnou spolupracovali Astra (ChatGPT) a Codík (Codex), moji AI asistenti od OpenAI.</p>");
    web_send_chunk(req, "<p>K tomuto programu neuplatňuji žádná autorská práva. Je volně použitelný, upravitelný a šiřitelný bez omezení.</p>");
    web_send_chunk(req, "<p>Zdrojový kód je dostupný na GitHubu: <a href='https://github.com/robelto3/ATEM_LOGGER_ESP-IDF'>robelto3/ATEM_LOGGER_ESP-IDF</a></p>");
    web_send_chunk(req, "</div>");

    web_send_chunk(req, "<p><a class='btn' href='/'>Home</a></p>");
    web_send_html_footer(req);
    return ESP_OK;
}


static bool web_new_file_should_return_to_files(httpd_req_t *req)
{
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0 || query_len >= 64) {
        return false;
    }

    char query[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }

    return strstr(query, "back=files") != NULL;
}

static esp_err_t web_new_file_handler(httpd_req_t *req)
{
    bool return_to_files = web_new_file_should_return_to_files(req);
    esp_err_t ret = logger_events_submit_new_file();
    if (ret != ESP_OK) {
        web_send_html_header(req, "Nový EDL soubor");
        web_send_chunk(req, "<h1>Nový EDL soubor</h1>");
        web_send_chunk(req, "<div class='card'>");
        web_send_chunk(req, "<p class='bad'>Požadavek na nový EDL soubor se nepodařilo vložit do logger fronty.</p>");
        web_send_chunk(req, "<p>Chyba: <b>");
        web_send_html_escaped(req, esp_err_to_name(ret));
        web_send_chunk(req, "</b></p>");
        if (return_to_files) {
            web_send_chunk(req, "<p><a class='btn' href='/files'>Soubory na SD kartě</a></p>");
        } else {
            web_send_chunk(req, "<p><a class='btn' href='/'>Home</a></p>");
        }
        web_send_chunk(req, "</div>");
        web_send_html_footer(req);
        return ESP_OK;
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", return_to_files ? "/files" : "/");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, return_to_files ? "Redirecting to files" : "Redirecting to Home");
    return ESP_OK;
}

static esp_err_t web_home_handler(httpd_req_t *req)
{
    char reboot_to_ip[NET_CONFIG_IP_STR_LEN] = {0};
    bool reboot_redirect = false;

    if (web_get_query_value(req, "reboot_to", reboot_to_ip, sizeof(reboot_to_ip)) == ESP_OK) {
        net_config_ip4_t parsed = {0};
        reboot_redirect = net_config_parse_ip4(reboot_to_ip, &parsed);
    }

    web_send_html_header(req, "ATEM Logger");
    web_send_chunk(req, "<h1><a class='home-title' href='/' title='Refresh'>ATEM Logger</a></h1>");
    web_send_status_card(req);

    if (reboot_redirect) {
        web_send_chunk(req, "<script>setTimeout(function(){window.location.href='http://");
        web_send_html_escaped(req, reboot_to_ip);
        web_send_chunk(req, "/';},3500);</script>");
    }

    web_send_chunk(req,
        "<script>"
        "function pad2(n){return String(n).padStart(2,'0');}"
        "function formatBrowserDateTime(d){return pad2(d.getDate())+'.'+pad2(d.getMonth()+1)+'.'+d.getFullYear()+' '+pad2(d.getHours())+':'+pad2(d.getMinutes())+':'+pad2(d.getSeconds());}"
        "function makeRtcSyncUrl(d){return '/rtc_sync'"
        "+'?y='+d.getFullYear()"
        "+'&mo='+(d.getMonth()+1)"
        "+'&d='+d.getDate()"
        "+'&h='+d.getHours()"
        "+'&mi='+d.getMinutes()"
        "+'&s='+d.getSeconds()"
        "+'&dow='+(d.getDay()+1);}"
        "function syncRtcFromBrowserConfirm(){"
        "var d=new Date();"
        "var dt=formatBrowserDateTime(d);"
        "if(!confirm('Opravdu synchronizovat RTC z času prohlížeče?\\n\\nNový čas: '+dt)){return false;}"
        "window.location.href=makeRtcSyncUrl(new Date());"
        "return false;"
        "}"
        "function setText(id,text){var e=document.getElementById(id);if(e){e.textContent=text;}}"
        "function setOkBad(id,ok){var e=document.getElementById(id);if(e){e.textContent=ok?'OK':'---';e.className=ok?'ok':'bad';}}"
        "function setOnOff(id,on){var e=document.getElementById(id);if(e){e.textContent=on?'ON':'OFF';e.className=on?'ok':'bad';}}"
        "function setTally(id,on,url){var e=document.getElementById(id);if(e){e.textContent=on?'ON':'OFF';e.className=on?'ok':'bad';e.href=on?url+'?back=home':url+'?enabled=1&back=home';e.title=on?'Vypnout':'Zapnout';}}"
        "function formatNumberSpaces(n){return String(n).replace(/\\B(?=(\\d{3})+(?!\\d))/g,' ');}"
        "function updateHomeState(){"
        "fetch('/api/state',{cache:'no-store'}).then(function(r){return r.json();}).then(function(s){"
        "setOkBad('home-atem',!!s.atem);"
        "setOkBad('home-ltc',!!s.ltc);"
        "setText('home-pgm',s.pgm);"
        "setText('home-pvw',s.pvw);"
        "setText('home-tc',s.tc);"
        "setTally('home-pgm-tally',!!s.pgm_tally,'/save_program_tally');"
        "setTally('home-pvw-tally',!!s.pvw_tally,'/save_preview_tally');"
        "setText('home-cut-count',formatNumberSpaces(s.cuts));"
        "setText('home-file',s.file);"
        "var rtcLink=document.getElementById('home-rtc-link');"
        "var rtcDate=document.getElementById('home-rtc-date');"
        "var rtcTime=document.getElementById('home-rtc-time');"
        "if(rtcLink){rtcLink.className=s.rtc_valid?'rtc-link ok':'rtc-link bad';}"
        "if(rtcDate&&rtcTime){if(s.rtc_valid){var parts=String(s.rtc).split(' ');rtcDate.textContent=parts[0]||s.rtc;rtcTime.textContent=parts[1]||'';}else{rtcDate.textContent='---';rtcTime.textContent='';}}"
        "}).catch(function(){});"
        "}"
        "setInterval(updateHomeState,250);"
        "updateHomeState();"
        "</script>"
    );
    web_send_html_footer(req);
    return ESP_OK;
}


static esp_err_t web_get_query_value(httpd_req_t *req, const char *key, char *value, size_t value_len)
{
    if (!req || !key || !value || value_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    char query[160] = {0};
    size_t query_len = httpd_req_get_url_query_len(req) + 1U;

    if (query_len <= 1U || query_len > sizeof(query)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = httpd_req_get_url_query_str(req, query, sizeof(query));
    if (ret != ESP_OK) {
        return ret;
    }

    return httpd_query_key_value(query, key, value, value_len);
}


static bool web_parse_query_uint(httpd_req_t *req,
                                 const char *key,
                                 unsigned min_value,
                                 unsigned max_value,
                                 unsigned *out)
{
    if (!req || !key || !out || min_value > max_value) {
        return false;
    }

    char text[16] = {0};
    if (web_get_query_value(req, key, text, sizeof(text)) != ESP_OK) {
        return false;
    }

    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value < min_value || value > max_value) {
        return false;
    }

    *out = (unsigned)value;
    return true;
}

static void web_send_ip_input(httpd_req_t *req, const char *label, const char *name, const char *value)
{
    web_send_chunk(req, "<label>");
    web_send_html_escaped(req, label);
    web_send_chunk(req, "</label><input class='settings-field' name='");
    web_send_html_escaped(req, name);
    web_send_chunk(req, "' value='");
    web_send_html_escaped(req, value);
    web_send_chunk(req, "' maxlength='15' pattern='[0-9.]*'>");
}

static esp_err_t web_network_handler(httpd_req_t *req)
{
    char server_ip[NET_CONFIG_IP_STR_LEN] = {0};
    char atem_ip[NET_CONFIG_IP_STR_LEN] = {0};
    char netmask[NET_CONFIG_IP_STR_LEN] = {0};
    char ltc_correction_text[16] = {0};

    net_config_get_server_ip_string(server_ip, sizeof(server_ip));
    net_config_get_atem_ip_string(atem_ip, sizeof(atem_ip));
    net_config_get_netmask_string(netmask, sizeof(netmask));
    bool program_tally_enabled = net_config_get_program_tally_enabled();
    bool preview_tally_enabled = net_config_get_preview_tally_enabled();
    int ltc_correction = net_config_get_ltc_frame_correction();
    snprintf(ltc_correction_text, sizeof(ltc_correction_text), "%d", ltc_correction);

    web_send_html_header(req, "ATEM Logger - nastavení");
    web_send_chunk(req, "<h1>Nastavení</h1>");
    web_send_chunk(req, "<p><a class='btn' href='/'>Home</a></p>");

    web_send_chunk(req, "<div class='card settings-card'>");
    web_send_chunk(req, "<form action='/save_program_tally' method='get'>");
    web_send_chunk(req, "<label class='settings-check-row'><span class='settings-check-text'>Program Tally:</span><input class='setting-check' type='checkbox' name='enabled' value='1' onchange='this.form.submit()'");
    if (program_tally_enabled) {
        web_send_chunk(req, " checked");
    }
    web_send_chunk(req, "></label><small class='settings-note'>Zaškrtnuto = červené Program tally výstupy jsou aktivní. Odškrtnuto = PGM výstupy jsou zhasnuté. Změna se uloží hned.</small>");
    web_send_chunk(req, "</form>");

    web_send_chunk(req, "<form action='/save_preview_tally' method='get'>");
    web_send_chunk(req, "<label class='settings-check-row'><span class='settings-check-text'>Preview Tally:</span><input class='setting-check' type='checkbox' name='enabled' value='1' onchange='this.form.submit()'");
    if (preview_tally_enabled) {
        web_send_chunk(req, " checked");
    }
    web_send_chunk(req, "></label><small class='settings-note'>Zaškrtnuto = zelené Preview tally výstupy jsou aktivní. Odškrtnuto = PVW výstupy jsou zhasnuté. Změna se uloží hned.</small>");
    web_send_chunk(req, "</form>");

    web_send_chunk(req, "<form class='settings-section-spaced' action='/save_ltc_correction' method='get'>");
    web_send_chunk(req, "<label>Korekce LTC</label><input class='settings-field' name='correction' type='number' min='");
    snprintf(ltc_correction_text, sizeof(ltc_correction_text), "%d", NET_CONFIG_LTC_CORRECTION_MIN);
    web_send_html_escaped(req, ltc_correction_text);
    web_send_chunk(req, "' max='");
    snprintf(ltc_correction_text, sizeof(ltc_correction_text), "%d", NET_CONFIG_LTC_CORRECTION_MAX);
    web_send_html_escaped(req, ltc_correction_text);
    web_send_chunk(req, "' step='1' value='");
    snprintf(ltc_correction_text, sizeof(ltc_correction_text), "%d", ltc_correction);
    web_send_html_escaped(req, ltc_correction_text);
    web_send_chunk(req, "' style='width:65px;min-width:65px;text-align:center'> <button class='btn settings-btn settings-field' type='submit'>Uložit korekci LTC</button><small class='settings-note settings-note-ltc'>Zadává se ve framech vstupního LTC 25 fps. Záporná hodnota posune logovaný TC zpět.</small>");
    web_send_chunk(req, "</form>");

    web_send_chunk(req, "<form class='settings-section-spaced' action='/save_network' method='get'>");
    web_send_ip_input(req, "IP loggeru/web serveru", "server_ip", server_ip);
    web_send_ip_input(req, "IP ATEM switcheru", "atem_ip", atem_ip);
    web_send_chunk(req, "<p class='settings-ip-save'><button class='btn settings-btn settings-field' type='submit'>Uložit nastavení IP</button> <a class='btn settings-btn settings-field' href='/reboot?target=");
    web_send_html_escaped(req, server_ip);
    web_send_chunk(req, "' onclick=\"return confirm('Opravdu restartovat logger?')\">Reboot</a></p>");
    web_send_chunk(req, "</form>");
    web_send_chunk(req, "<p><small>Maska je pevně ");
    web_send_html_escaped(req, netmask);
    web_send_chunk(req, ".</small></p>");
    web_send_chunk(req, "<p><small>Po změně IP je nejčistší logger restartovat, aby se znovu rozběhl Ethernet, web i ATEM spojení.</small></p>");
    web_send_chunk(req, "</div>");

    web_send_html_footer(req);
    return ESP_OK;
}

static esp_err_t web_save_program_tally_handler(httpd_req_t *req)
{
    char enabled_value[8] = {0};
    char back_value[8] = {0};
    bool enabled = (web_get_query_value(req, "enabled", enabled_value, sizeof(enabled_value)) == ESP_OK);
    bool back_home = (web_get_query_value(req, "back", back_value, sizeof(back_value)) == ESP_OK &&
                      strcmp(back_value, "home") == 0);

    esp_err_t ret = net_config_set_program_tally_enabled(enabled);
    if (ret != ESP_OK) {
        web_send_html_header(req, "ATEM Logger - nastavení");
        web_send_chunk(req, "<h1>Nastavení</h1>");
        web_send_chunk(req, "<div class='card'><p><span class='bad'>Program Tally nastavení nešlo uložit.</span></p>");
        web_send_chunk(req, "<p><a class='btn' href='/network'>Zpět na nastavení</a></p></div>");
        web_send_html_footer(req);
        return ESP_OK;
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", back_home ? "/" : "/network");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, back_home ? "Redirecting to Home" : "Redirecting to settings");
    return ESP_OK;
}

static esp_err_t web_save_preview_tally_handler(httpd_req_t *req)
{
    char enabled_value[8] = {0};
    char back_value[8] = {0};
    bool enabled = (web_get_query_value(req, "enabled", enabled_value, sizeof(enabled_value)) == ESP_OK);
    bool back_home = (web_get_query_value(req, "back", back_value, sizeof(back_value)) == ESP_OK &&
                      strcmp(back_value, "home") == 0);

    esp_err_t ret = net_config_set_preview_tally_enabled(enabled);
    if (ret != ESP_OK) {
        web_send_html_header(req, "ATEM Logger - nastavení");
        web_send_chunk(req, "<h1>Nastavení</h1>");
        web_send_chunk(req, "<div class='card'><p><span class='bad'>Preview Tally nastavení nešlo uložit.</span></p>");
        web_send_chunk(req, "<p><a class='btn' href='/network'>Zpět na nastavení</a></p></div>");
        web_send_html_footer(req);
        return ESP_OK;
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", back_home ? "/" : "/network");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, back_home ? "Redirecting to Home" : "Redirecting to settings");
    return ESP_OK;
}

static esp_err_t web_save_ltc_correction_handler(httpd_req_t *req)
{
    char correction_text[16] = {0};
    char *end = NULL;
    long correction = 0;

    bool has_correction = (web_get_query_value(req, "correction", correction_text, sizeof(correction_text)) == ESP_OK);
    if (has_correction) {
        correction = strtol(correction_text, &end, 10);
        if (end == correction_text || *end != '\0' ||
            correction < NET_CONFIG_LTC_CORRECTION_MIN ||
            correction > NET_CONFIG_LTC_CORRECTION_MAX) {
            has_correction = false;
        }
    }

    esp_err_t ret = has_correction
        ? net_config_set_ltc_frame_correction((int)correction)
        : ESP_ERR_INVALID_ARG;

    if (ret != ESP_OK) {
        web_send_html_header(req, "ATEM Logger - nastavení");
        web_send_chunk(req, "<h1>Nastavení</h1>");
        web_send_chunk(req, "<div class='card'><p><span class='bad'>Korekci LTC se nepodařilo uložit.</span></p>");
        web_send_chunk(req, "<p>Povolený rozsah je ");
        char range_text[48];
        snprintf(
            range_text,
            sizeof(range_text),
            "%d až %+d framů.",
            NET_CONFIG_LTC_CORRECTION_MIN,
            NET_CONFIG_LTC_CORRECTION_MAX
        );
        web_send_html_escaped(req, range_text);
        web_send_chunk(req, "</p><p><a class='btn' href='/network'>Zpět na nastavení</a></p></div>");
        web_send_html_footer(req);
        return ESP_OK;
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/network");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "Redirecting to settings");
    return ESP_OK;
}

static esp_err_t web_save_network_handler(httpd_req_t *req)
{
    char server_ip[NET_CONFIG_IP_STR_LEN] = {0};
    char atem_ip[NET_CONFIG_IP_STR_LEN] = {0};

    bool server_param = (web_get_query_value(req, "server_ip", server_ip, sizeof(server_ip)) == ESP_OK);
    bool atem_param = (web_get_query_value(req, "atem_ip", atem_ip, sizeof(atem_ip)) == ESP_OK);

    esp_err_t server_ret = ESP_OK;
    esp_err_t atem_ret = ESP_OK;

    if (server_param) {
        server_ret = net_config_set_server_ip_string(server_ip);
    }

    if (atem_param) {
        atem_ret = net_config_set_atem_ip_string(atem_ip);
    }

    char saved_server_ip[NET_CONFIG_IP_STR_LEN] = {0};
    char saved_atem_ip[NET_CONFIG_IP_STR_LEN] = {0};
    net_config_get_server_ip_string(saved_server_ip, sizeof(saved_server_ip));
    net_config_get_atem_ip_string(saved_atem_ip, sizeof(saved_atem_ip));

    web_send_html_header(req, "ATEM Logger - nastavení uloženo");
    web_send_chunk(req, "<h1>Nastavení</h1>");

    web_send_chunk(req, "<div class='card'>");

    if (!server_param || server_ret != ESP_OK || !atem_param || atem_ret != ESP_OK) {
        web_send_chunk(req, "<p><span class='bad'>Některé nastavení se nepodařilo uložit.</span></p>");
    } else {
        web_send_chunk(req, "<p><span class='ok'>Nastavení bylo uloženo do NVS.</span></p>");
    }

    web_send_chunk(req, "<p>ESP / web server: <b>");
    web_send_html_escaped(req, saved_server_ip);
    web_send_chunk(req, "</b></p>");

    web_send_chunk(req, "<p>ATEM switcher: <b>");
    web_send_html_escaped(req, saved_atem_ip);
    web_send_chunk(req, "</b></p>");

    if (!server_param || server_ret != ESP_OK) {
        web_send_chunk(req, "<p><span class='bad'>ESP IP nebyla platná nebo nešla uložit.</span></p>");
    }

    if (!atem_param || atem_ret != ESP_OK) {
        web_send_chunk(req, "<p><span class='bad'>ATEM IP nebyla platná nebo nešla uložit.</span></p>");
    }

    web_send_chunk(req, "<p><a class='btn danger' href='/reboot?target=");
    web_send_html_escaped(req, saved_server_ip);
    web_send_chunk(req, "'>Restartovat logger</a> ");
    web_send_chunk(req, "<a class='btn' href='/network'>Zpět na nastavení</a> ");
    web_send_chunk(req, "<a class='btn' href='/'>Home</a></p>");
    web_send_chunk(req, "<p><small>Nová IP ESP/web serveru se projeví až po restartu. Po restartu otevři web na nové adrese.</small></p>");
    web_send_chunk(req, "</div>");

    web_send_html_footer(req);
    return ESP_OK;
}


static int web_hex_to_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    return -1;
}

static void web_url_decode_copy(const char *src, char *dst, size_t dst_len)
{
    if (!dst || dst_len == 0U) {
        return;
    }

    dst[0] = '\0';
    if (!src) {
        return;
    }

    size_t out = 0;
    for (size_t i = 0; src[i] != '\0' && out + 1U < dst_len; i++) {
        char c = src[i];
        if (c == '+') {
            dst[out++] = ' ';
        } else if (c == '%' && src[i + 1U] != '\0' && src[i + 2U] != '\0') {
            int hi = web_hex_to_nibble(src[i + 1U]);
            int lo = web_hex_to_nibble(src[i + 2U]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2U;
            } else {
                dst[out++] = c;
            }
        } else {
            dst[out++] = c;
        }
    }
    dst[out] = '\0';
}

static esp_err_t web_receive_form_body(httpd_req_t *req, char *body, size_t body_len)
{
    if (!req || !body || body_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    body[0] = '\0';

    if (req->content_len == 0 || req->content_len >= body_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }

    body[received] = '\0';
    return ESP_OK;
}

static esp_err_t web_get_form_value_decoded(const char *body,
                                            const char *key,
                                            char *value,
                                            size_t value_len)
{
    if (!body || !key || !value || value_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    char raw[768] = {0};
    esp_err_t ret = httpd_query_key_value(body, key, raw, sizeof(raw));
    if (ret != ESP_OK) {
        return ret;
    }

    web_url_decode_copy(raw, value, value_len);
    return ESP_OK;
}

static void web_send_show_settings_form(httpd_req_t *req)
{
    uint8_t active = show_config_get_active_index();
    char line[256];

    web_send_chunk(req, "<div class='card'>");
    web_send_chunk(req, "<form action='/save_shows' method='post'>");
    web_send_chunk(req, "<p class='muted'>Uloženo je až 5 názvů pořadů. Do EDL TITLE se použije aktivní název bez číslování. Pořadové číslo zůstává jen v názvu souboru.</p>");

    for (uint8_t i = 0; i < SHOW_CONFIG_SLOT_COUNT; i++) {
        char name[SHOW_CONFIG_NAME_MAX_LEN] = {0};
        show_config_get_name(i, name, sizeof(name));

        web_send_chunk(req, "<div class='show-row'>");

        snprintf(line, sizeof(line), "<label class='show-label' for='show_name_%u'>Název pořadu %u</label>",
                 (unsigned)i,
                 (unsigned)(i + 1U));
        web_send_chunk(req, line);

        snprintf(line, sizeof(line), "<input class='show-name' id='show_name_%u' name='name%u' maxlength='%u' value='",
                 (unsigned)i,
                 (unsigned)i,
                 (unsigned)(SHOW_CONFIG_NAME_MAX_LEN - 1U));
        web_send_chunk(req, line);
        web_send_html_escaped(req, name);
        web_send_chunk(req, "'>");

        snprintf(line, sizeof(line), "<label class='show-active %s'><input class='radio' type='radio' name='active' value='%u'%s> aktivní</label>",
                 (i == active) ? "show-active-selected" : "show-active-muted",
                 (unsigned)i,
                 (i == active) ? " checked" : "");
        web_send_chunk(req, line);

        web_send_chunk(req, "</div>");
    }

    web_send_chunk(req, "<p><button class='btn link-like' type='submit'>Uložit a použít vybraný</button></p>");
    web_send_chunk(req, "</form>");
    web_send_chunk(req, "<p><small>Slot 1 nesmí být prázdný; při prázdné hodnotě se použije ATEM LOGGER. Při změně aktivního pořadu se automaticky založí nový EDL soubor.</small></p>");
    web_send_chunk(req, "</div>");
}

static esp_err_t web_shows_handler(httpd_req_t *req)
{
    web_send_html_header(req, "ATEM Logger - názvy pořadů");
    web_send_chunk(req, "<h1>Názvy pořadů</h1>");
    web_send_chunk(req, "<p><a class='btn' href='/'>Home</a></p>");
    web_send_show_settings_form(req);
    web_send_html_footer(req);
    return ESP_OK;
}

static esp_err_t web_save_shows_handler(httpd_req_t *req)
{
    char body[WEB_FORM_BODY_MAX_LEN];
    esp_err_t body_ret = web_receive_form_body(req, body, sizeof(body));

    uint8_t old_active_index = show_config_get_active_index();
    char old_active_name[SHOW_CONFIG_NAME_MAX_LEN] = {0};
    show_config_get_active_name(old_active_name, sizeof(old_active_name));

    bool ok = (body_ret == ESP_OK);
    bool active_show_changed = false;
    bool new_file_requested = false;
    esp_err_t ret = ESP_OK;
    esp_err_t new_file_ret = ESP_OK;

    if (ok) {
        for (uint8_t i = 0; i < SHOW_CONFIG_SLOT_COUNT; i++) {
            char key[16];
            char name[SHOW_CONFIG_NAME_MAX_LEN] = {0};
            snprintf(key, sizeof(key), "name%u", (unsigned)i);

            if (web_get_form_value_decoded(body, key, name, sizeof(name)) == ESP_OK) {
                ret = show_config_set_name(i, name);
                if (ret != ESP_OK) {
                    ok = false;
                    break;
                }
            }
        }
    }

    if (ok) {
        char active_text[16] = {0};
        if (web_get_form_value_decoded(body, "active", active_text, sizeof(active_text)) == ESP_OK) {
            char *end = NULL;
            unsigned long value = strtoul(active_text, &end, 10);
            if (end != active_text && *end == '\0' && value < SHOW_CONFIG_SLOT_COUNT) {
                char selected_name[SHOW_CONFIG_NAME_MAX_LEN] = {0};
                show_config_get_name((uint8_t)value, selected_name, sizeof(selected_name));
                if (selected_name[0] == '\0') {
                    value = 0;
                }

                ret = show_config_set_active_index((uint8_t)value);
                if (ret != ESP_OK) {
                    ok = false;
                }
            } else {
                ok = false;
            }
        } else {
            ok = false;
        }
    }

    if (ok) {
        uint8_t new_active_index = show_config_get_active_index();
        char new_active_name[SHOW_CONFIG_NAME_MAX_LEN] = {0};
        show_config_get_active_name(new_active_name, sizeof(new_active_name));

        active_show_changed =
            (old_active_index != new_active_index) ||
            (strcmp(old_active_name, new_active_name) != 0);

        // Když se změní aktivní pořad, rovnou přes logger frontu založíme nový EDL soubor.
        // Díky frontě se nejdřív zpracují případné starší CUT eventy a nový soubor
        // už dostane nový TITLE podle nově vybraného pořadu.
        if (active_show_changed) {
            new_file_ret = logger_events_submit_new_file();
            new_file_requested = (new_file_ret == ESP_OK);
        }
    }

    web_send_html_header(req, "ATEM Logger - názvy pořadů uloženy");
    web_send_chunk(req, "<h1>Názvy pořadů</h1>");
    web_send_chunk(req, "<div class='card'>");
    if (ok) {
        char active_name[SHOW_CONFIG_NAME_MAX_LEN] = {0};
        show_config_get_active_name(active_name, sizeof(active_name));
        web_send_chunk(req, "<p><span class='ok'>Názvy pořadů byly uloženy do NVS.</span></p>");
        web_send_chunk(req, "<p>Aktivní pořad: <span class='active-show'>");
        web_send_html_escaped(req, active_name);
        web_send_chunk(req, "</span></p>");

        if (active_show_changed && new_file_requested) {
            web_send_chunk(req, "<p><span class='ok'>Aktivní pořad se změnil, proto byl přes logger frontu založen nový EDL soubor.</span></p>");
        } else if (active_show_changed) {
            web_send_chunk(req, "<p><span class='bad'>Aktivní pořad se změnil, ale požadavek na nový EDL soubor se nepodařilo vložit do fronty: ");
            web_send_html_escaped(req, esp_err_to_name(new_file_ret));
            web_send_chunk(req, "</span></p>");
        } else {
            web_send_chunk(req, "<p><span class='muted'>Aktivní pořad se nezměnil, aktuální EDL soubor zůstává beze změny.</span></p>");
        }
    } else {
        web_send_chunk(req, "<p><span class='bad'>Nastavení názvů pořadů se nepodařilo uložit.</span></p>");
    }
    web_send_chunk(req, "<p><a class='btn' href='/shows'>Zpět na názvy pořadů</a> <a class='btn' href='/'>Home</a></p>");
    web_send_chunk(req, "</div>");
    web_send_html_footer(req);
    return ESP_OK;
}


static esp_err_t web_rtc_sync_handler(httpd_req_t *req)
{
    unsigned year = 0;
    unsigned month = 0;
    unsigned date = 0;
    unsigned hours = 0;
    unsigned minutes = 0;
    unsigned seconds = 0;
    unsigned day = 0;

    bool ok = true;
    ok = ok && web_parse_query_uint(req, "y", 2020, 2099, &year);
    ok = ok && web_parse_query_uint(req, "mo", 1, 12, &month);
    ok = ok && web_parse_query_uint(req, "d", 1, 31, &date);
    ok = ok && web_parse_query_uint(req, "h", 0, 23, &hours);
    ok = ok && web_parse_query_uint(req, "mi", 0, 59, &minutes);
    ok = ok && web_parse_query_uint(req, "s", 0, 59, &seconds);
    ok = ok && web_parse_query_uint(req, "dow", 1, 7, &day);

    rtc_datetime_t dt = {
        .seconds = (uint8_t)seconds,
        .minutes = (uint8_t)minutes,
        .hours = (uint8_t)hours,
        .day = (uint8_t)day,
        .date = (uint8_t)date,
        .month = (uint8_t)month,
        .year = (uint16_t)year,
    };

    esp_err_t set_ret = ESP_ERR_INVALID_ARG;
    if (ok) {
        set_ret = rtc_set_datetime(&dt);
        if (set_ret == ESP_OK) {
            app_state_update_rtc(&dt, true);
        }
    }

    web_send_html_header(req, "ATEM Logger - RTC synchro");
    web_send_chunk(req, "<h1>RTC synchro</h1>");
    web_send_chunk(req, "<div class='card'>");

    if (ok && set_ret == ESP_OK) {
        char line[160];
        snprintf(
            line,
            sizeof(line),
            "<p><span class='ok'>RTC bylo nastaveno z času prohlížeče.</span></p>"
            "<p>Nový RTC čas: <b>%02u.%02u.%04u %02u:%02u:%02u</b></p>",
            dt.date,
            dt.month,
            dt.year,
            dt.hours,
            dt.minutes,
            dt.seconds
        );
        web_send_chunk(req, line);
    } else {
        web_send_chunk(req, "<p><span class='bad'>RTC se nepodařilo nastavit.</span></p>");
        if (!ok) {
            web_send_chunk(req, "<p>Chybí některý parametr času z prohlížeče nebo má neplatnou hodnotu.</p>");
        } else {
            web_send_chunk(req, "<p>RTC driver vrátil chybu při zápisu do DS3231.</p>");
        }
    }

    web_send_chunk(req, "<p><a class='btn' href='/'>Home</a></p>");
    web_send_chunk(req, "</div>");
    web_send_html_footer(req);
    return ESP_OK;
}

static void web_reboot_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(900));
    esp_restart();
}

static esp_err_t web_reboot_handler(httpd_req_t *req)
{
    char target_ip[NET_CONFIG_IP_STR_LEN] = {0};
    char location[64] = "/";

    if (web_get_query_value(req, "target", target_ip, sizeof(target_ip)) == ESP_OK) {
        net_config_ip4_t parsed = {0};
        if (net_config_parse_ip4(target_ip, &parsed)) {
            snprintf(location, sizeof(location), "/?reboot_to=%s", target_ip);
        }
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "Redirecting to Home");

    if (xTaskCreate(web_reboot_task, "web_reboot", 2048, NULL, 5, NULL) != pdPASS) {
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    }

    return ESP_OK;
}

static esp_err_t web_archive_handler(httpd_req_t *req)
{
    char filename[WEB_FILE_NAME_MAX_LEN] = {0};
    char action[16] = {0};
    char src[WEB_FILE_PATH_MAX_LEN] = {0};
    char dst[WEB_FILE_PATH_MAX_LEN] = {0};
    char target_filename[WEB_FILE_NAME_MAX_LEN] = {0};
    bool restore = false;

    if (!sd_storage_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card is not mounted");
        return ESP_OK;
    }

    if (web_get_query_filename(req, filename, sizeof(filename)) != ESP_OK ||
        web_get_query_value(req, "action", action, sizeof(action)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid file/action parameter");
        return ESP_OK;
    }

    if (strcmp(action, "archive") == 0) {
        restore = false;
        if (web_is_current_file(filename)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Current file cannot be archived");
            return ESP_OK;
        }
    } else if (strcmp(action, "restore") == 0) {
        restore = true;
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid archive action");
        return ESP_OK;
    }

    if (web_ensure_archive_dir() != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Archive directory cannot be created");
        return ESP_OK;
    }

    if ((restore ? web_make_archive_file_path(filename, src, sizeof(src)) : web_make_file_path(filename, src, sizeof(src))) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid file path");
        return ESP_OK;
    }

    struct stat st;
    if (stat(src, &st) != 0 || !S_ISREG(st.st_mode)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Source file not found");
        return ESP_OK;
    }

    esp_err_t target_ret = restore
        ? web_find_restore_target_path(filename, target_filename, sizeof(target_filename), dst, sizeof(dst))
        : web_find_archive_target_path(filename, target_filename, sizeof(target_filename), dst, sizeof(dst));
    if (target_ret == ESP_ERR_NO_MEM) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, restore ? "No free restore filename" : "Archive is full for this file");
        return ESP_OK;
    }
    if (target_ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid target path");
        return ESP_OK;
    }

    if (rename(src, dst) != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Moving file failed");
        return ESP_OK;
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", restore ? "/files?mode=archive" : "/files");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, restore ? "Restored from archive" : "Archived");
    return ESP_OK;
}

static esp_err_t web_trash_handler(httpd_req_t *req)
{
    char filename[WEB_FILE_NAME_MAX_LEN] = {0};
    char action[16] = {0};
    char src[WEB_FILE_PATH_MAX_LEN] = {0};
    char dst[WEB_FILE_PATH_MAX_LEN] = {0};
    char restore_filename[WEB_FILE_NAME_MAX_LEN] = {0};

    if (!sd_storage_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card is not mounted");
        return ESP_OK;
    }

    if (web_get_query_value(req, "action", action, sizeof(action)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid action parameter");
        return ESP_OK;
    }

    if (strcmp(action, "empty") == 0) {
        if (web_ensure_trash_dir() != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Trash directory cannot be created");
            return ESP_OK;
        }

        DIR *dir = opendir(WEB_TRASH_DIR_PATH);
        if (!dir) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Trash directory cannot be opened");
            return ESP_OK;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.' || !web_is_safe_filename(entry->d_name)) {
                continue;
            }

            char path[WEB_FILE_PATH_MAX_LEN] = {0};
            if (web_make_trash_file_path(entry->d_name, path, sizeof(path)) != ESP_OK) {
                continue;
            }

            struct stat st;
            if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
                (void)remove(path);
            }
        }

        closedir(dir);
        return web_redirect_to_files_location(req, false, true);
    }

    if (web_get_query_filename(req, filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid file parameter");
        return ESP_OK;
    }

    if (strcmp(action, "restore") != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid trash action");
        return ESP_OK;
    }

    if (web_make_trash_file_path(filename, src, sizeof(src)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid file path");
        return ESP_OK;
    }

    struct stat st;
    if (stat(src, &st) != 0 || !S_ISREG(st.st_mode)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Trash file not found");
        return ESP_OK;
    }

    esp_err_t target_ret = web_find_restore_target_path(filename, restore_filename, sizeof(restore_filename), dst, sizeof(dst));
    if (target_ret == ESP_ERR_NO_MEM) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No free restore filename");
        return ESP_OK;
    }
    if (target_ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid restore path");
        return ESP_OK;
    }

    if (rename(src, dst) != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Restoring file failed");
        return ESP_OK;
    }

    return web_redirect_to_files_location(req, false, true);
}

static esp_err_t web_move_file_to_trash(const char *filename, bool archived)
{
    char src[WEB_FILE_PATH_MAX_LEN] = {0};
    char dst[WEB_FILE_PATH_MAX_LEN] = {0};
    char trash_filename[WEB_FILE_NAME_MAX_LEN] = {0};

    if (web_ensure_trash_dir() != ESP_OK) {
        return ESP_FAIL;
    }

    if (web_make_file_path_for_location(filename, archived, src, sizeof(src)) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t target_ret = web_find_trash_target_path(filename, trash_filename, sizeof(trash_filename), dst, sizeof(dst));
    if (target_ret != ESP_OK) {
        return target_ret;
    }

    struct stat st;
    if (stat(src, &st) != 0 || !S_ISREG(st.st_mode)) {
        return ESP_ERR_NOT_FOUND;
    }

    if (rename(src, dst) != 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t web_files_handler(httpd_req_t *req)
{
    unsigned requested_page = web_get_query_page(req);

    char mode_value[16] = {0};
    if (web_get_query_value(req, "mode", mode_value, sizeof(mode_value)) == ESP_OK) {
        if (strcmp(mode_value, "trash") == 0) {
            s_files_mode = WEB_FILES_MODE_TRASH;
        } else if (strcmp(mode_value, "archive") == 0) {
            s_files_mode = WEB_FILES_MODE_ARCHIVE;
        } else if (strcmp(mode_value, "all") == 0) {
            s_files_mode = WEB_FILES_MODE_ALL;
        } else if (strcmp(mode_value, "empty") == 0) {
            s_files_mode = WEB_FILES_MODE_EMPTY;
        } else if (strcmp(mode_value, "cuts") == 0) {
            s_files_mode = WEB_FILES_MODE_WITH_CUTS;
        }
    } else {
        // Zpětná kompatibilita se staršími odkazy /files?all=1 a /files?all=0.
        char all_value[8] = {0};
        if (web_get_query_value(req, "all", all_value, sizeof(all_value)) == ESP_OK) {
            if (strcmp(all_value, "1") == 0) {
                s_files_mode = WEB_FILES_MODE_ALL;
            } else if (strcmp(all_value, "0") == 0) {
                s_files_mode = WEB_FILES_MODE_WITH_CUTS;
            }
        }
    }

    web_files_mode_t files_mode = s_files_mode;
    bool archive_mode = (files_mode == WEB_FILES_MODE_ARCHIVE);
    bool trash_mode = (files_mode == WEB_FILES_MODE_TRASH);
    const char *list_dir = trash_mode ? WEB_TRASH_DIR_PATH : (archive_mode ? WEB_ARCHIVE_DIR_PATH : SD_STORAGE_MOUNT_POINT);

    web_send_html_header(req, "ATEM Logger - soubory");
    web_send_chunk(req, "<h1>Soubory na SD kartě</h1>");
    web_send_chunk(req, "<p><a class='btn' href='/'>Home</a> <a class='btn' href='/files'>Refresh</a> ");
    web_send_chunk(req, (files_mode == WEB_FILES_MODE_WITH_CUTS)
                        ? "<a class='btn btn-active' href='/files?mode=cuts'>Soubory se střihy</a> "
                        : "<a class='btn' href='/files?mode=cuts'>Soubory se střihy</a> ");
    web_send_chunk(req, (files_mode == WEB_FILES_MODE_EMPTY)
                        ? "<a class='btn btn-active' href='/files?mode=empty'>Prázdné soubory</a> "
                        : "<a class='btn' href='/files?mode=empty'>Prázdné soubory</a> ");
    web_send_chunk(req, (files_mode == WEB_FILES_MODE_ALL)
                        ? "<a class='btn btn-active' href='/files?mode=all'>Všechny soubory</a> "
                        : "<a class='btn' href='/files?mode=all'>Všechny soubory</a> ");
    web_send_chunk(req, (files_mode == WEB_FILES_MODE_ARCHIVE)
                        ? "<a class='btn btn-active' href='/files?mode=archive'>Archiv</a> "
                        : "<a class='btn' href='/files?mode=archive'>Archiv</a> ");
    if (!archive_mode && !trash_mode) {
        web_send_chunk(req, "<a class='btn' href='/new_file?back=files' onclick=\"return confirm('Opravdu uzavřít aktuální EDL soubor a vytvořit nový?');\">Uzavřít aktuální a vytvořit nový</a> ");
    }
    web_send_chunk(req, (files_mode == WEB_FILES_MODE_TRASH)
                        ? "<a class='btn btn-active' href='/files?mode=trash'>Koš</a> "
                        : "<a class='btn' href='/files?mode=trash'>Koš</a> ");
    if (trash_mode) {
        web_send_chunk(req, "<a class='btn danger' href='/trash?action=empty' title='Smaže navždy obsah celého koše' onclick=\"return confirm('Opravdu definitivně smazat celý koš?');\">Vyprázdnit koš</a> ");
    }
    web_send_chunk(req, "</p>");

    if (!sd_storage_is_mounted()) {
        web_send_chunk(req, "<div class='card'><span class='bad'>SD karta není připojená.</span></div>");
        web_send_html_footer(req);
        return ESP_OK;
    }

    web_file_item_t *files = calloc(WEB_MAX_FILE_LIST, sizeof(web_file_item_t));
    if (!files) {
        web_send_chunk(req, "<div class='card'><span class='bad'>Nelze vyhradit paměť pro seznam souborů.</span></div>");
        web_send_html_footer(req);
        return ESP_OK;
    }

    if (archive_mode) {
        (void)web_ensure_archive_dir();
    } else if (trash_mode) {
        (void)web_ensure_trash_dir();
    }

    DIR *dir = opendir(list_dir);
    if (!dir) {
        free(files);
        web_send_chunk(req, "<div class='card'><span class='bad'>Nejde otevřít adresář se soubory.</span></div>");
        web_send_html_footer(req);
        return ESP_OK;
    }

    unsigned count = 0;
    unsigned skipped = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        if (!web_is_safe_filename(entry->d_name)) {
            continue;
        }

        char path[WEB_FILE_PATH_MAX_LEN];
        if (web_make_file_path_for_query_location(entry->d_name, archive_mode, trash_mode, path, sizeof(path)) != ESP_OK) {
            continue;
        }

        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        if (count >= WEB_MAX_FILE_LIST) {
            skipped++;
            continue;
        }

        size_t name_len = strlen(entry->d_name);
        if (name_len >= sizeof(files[count].name)) {
            continue;
        }
        memcpy(files[count].name, entry->d_name, name_len + 1);

        files[count].size = (long)st.st_size;
        files[count].has_edl_sort_key = web_parse_edl_filename_sort_key(files[count].name, &files[count].edl_sort_key);
        files[count].is_current_file = !archive_mode && !trash_mode && web_is_current_file(files[count].name);
        count++;
    }

    closedir(dir);

    if (count > 1) {
        qsort(files, count, sizeof(files[0]), web_file_item_compare);
    }

    unsigned total_loaded = count;
    unsigned filtered_count = 0;

    for (unsigned i = 0; i < total_loaded; i++) {
        files[i].cut_count = web_read_edl_info_for_file_location(files[i].name, archive_mode, trash_mode, files[i].edl_title, sizeof(files[i].edl_title));
        if (web_files_mode_should_show(files_mode, files[i].cut_count, files[i].is_current_file)) {
            if (filtered_count != i) {
                files[filtered_count] = files[i];
            }
            filtered_count++;
        }
    }

    count = filtered_count;

    unsigned total_pages = 1;
    if (count > 0) {
        total_pages = (count + WEB_FILES_PER_PAGE - 1U) / WEB_FILES_PER_PAGE;
    }

    unsigned page = requested_page;
    if (page > total_pages) {
        page = total_pages;
    }

    unsigned start_index = 0;
    unsigned end_index = 0;
    if (count > 0) {
        start_index = (page - 1U) * WEB_FILES_PER_PAGE;
        end_index = start_index + WEB_FILES_PER_PAGE;
        if (end_index > count) {
            end_index = count;
        }
    }

    char info[256];
    snprintf(
        info,
        sizeof(info),
        "<p class='muted'>Režim: %s. Řazení: EDL soubory podle data v názvu sestupně, ostatní soubory podle názvu. Zobrazeno %u z %u načtených souborů, na stránku %u.</p>",
        web_files_mode_label(files_mode),
        count,
        total_loaded,
        (unsigned)WEB_FILES_PER_PAGE
    );
    web_send_chunk(req, info);

    web_send_files_pagination(req, page, total_pages, files_mode);

    web_send_chunk(req, "<form action='/delete_selected' method='get' id='deleteSelectedForm'>");
    web_send_chunk(req, "<div class='table-wrap'>");
    if (trash_mode) {
        web_send_chunk(req, "<table class='files-table'><tr><th class='file-cell'>Soubor</th><th class='program-cell'>Název pořadu</th><th class='cuts-cell'>Střihy</th><th class='size-cell'>Velikost</th><th class='archive-cell'>Vrátit</th><th class='delete-cell'>Smazat navždy</th></tr>");
    } else {
        web_send_chunk(req, "<table class='files-table'><tr><th class='select-cell'><input class='selectcheck' type='checkbox' id='selectVisibleFiles' title='Vybrat všechny zobrazené mazatelné soubory'></th><th class='file-cell'>Soubor</th><th class='program-cell'>Název pořadu</th><th class='cuts-cell'>Střihy</th><th class='size-cell'>Velikost</th><th class='view-cell'>Zobrazit</th><th class='download-cell'>Stáhnout</th><th class='archive-cell'>Archiv</th><th class='delete-cell'>Odstranit do koše</th></tr>");
    }

    for (unsigned i = start_index; i < end_index; i++) {
        char size_text[32];
        snprintf(size_text, sizeof(size_text), "%ld B", files[i].size);
        bool is_current_file = files[i].is_current_file;
        const char *edl_title = files[i].edl_title;
        int cut_count = files[i].cut_count;
        unsigned checkbox_index = i - start_index;
        char checkbox_name[16];
        bool has_checkbox_name = web_make_index_key(checkbox_name, sizeof(checkbox_name), checkbox_index);

        if (is_current_file) {
            web_send_chunk(req, "<tr class='current-row' data-file='");
        } else {
            web_send_chunk(req, "<tr data-file='");
        }
        web_send_html_escaped(req, files[i].name);
        web_send_chunk(req, "' data-current='");
        web_send_chunk(req, is_current_file ? "1" : "0");
        web_send_chunk(req, "' data-archive='");
        web_send_chunk(req, archive_mode ? "1" : "0");
        web_send_chunk(req, "' data-trash='");
        web_send_chunk(req, trash_mode ? "1" : "0");
        web_send_chunk(req, "'>");

        if (!trash_mode) {
            web_send_chunk(req, "<td class='select-cell'>");
        }

        if (trash_mode) {
            // Koš nemá hromadný výběr; maže se jednotlivě nebo tlačítkem Smazat vše.
        } else if (archive_mode) {
            web_send_chunk(req, "<input class='filecheck' type='checkbox' disabled title='Archivované soubory se mažou samostatně'>");
        } else if (is_current_file) {
            web_send_chunk(req, "<input class='filecheck' type='checkbox' disabled title='Aktuální soubor se maže jen samostatně'>");
        } else if (has_checkbox_name) {
            web_send_chunk(req, "<input class='filecheck' type='checkbox' name='");
            web_send_html_escaped(req, checkbox_name);
            web_send_chunk(req, "' value='");
            web_send_html_escaped(req, files[i].name);
            web_send_chunk(req, "'");
            web_send_chunk(req, ">");
        }

        if (!trash_mode) {
            web_send_chunk(req, "</td>");
        }

        web_send_chunk(req, "<td class='file-cell'>");
        if (trash_mode) {
            web_send_html_escaped(req, files[i].name);
        } else {
            web_send_chunk(req, "<a href='/view?file=");
            web_send_html_escaped(req, files[i].name);
            if (archive_mode) {
                web_send_chunk(req, "&amp;archive=1");
            }
            web_send_chunk(req, "' title='Zobrazit obsah'>");
            web_send_html_escaped(req, files[i].name);
            web_send_chunk(req, "</a>");
        }

        web_send_chunk(req, "</td><td class='program-cell'>");
        if (edl_title[0] != '\0') {
            if (trash_mode || is_current_file) {
                web_send_html_escaped(req, edl_title);
            } else {
                web_send_chunk(req, "<a href='/download?file=");
                web_send_html_escaped(req, files[i].name);
                if (archive_mode) {
                    web_send_chunk(req, "&amp;archive=1");
                }
                web_send_chunk(req, "' title='Stáhnout: ");
                web_send_html_escaped(req, edl_title);
                web_send_chunk(req, ".edl'>");
                web_send_html_escaped(req, edl_title);
                web_send_chunk(req, "</a>");
            }
        } else {
            web_send_chunk(req, "<span class='muted'>—</span>");
        }

        web_send_chunk(req, "</td><td class='cuts-cell'>");
        if (cut_count >= 0) {
            char cut_text[16];
            snprintf(cut_text, sizeof(cut_text), "%d", cut_count);
            web_send_html_escaped(req, cut_text);
        } else {
            web_send_chunk(req, "<span class='muted'>—</span>");
        }

        web_send_chunk(req, "</td><td class='size-cell'>");
        web_send_html_escaped(req, size_text);

        if (!trash_mode) {
            web_send_chunk(req, "</td><td class='view-cell'><a href='/view?file=");
            web_send_html_escaped(req, files[i].name);
            if (archive_mode) {
                web_send_chunk(req, "&amp;archive=1");
            }
            web_send_chunk(req, "' title='Zobrazit obsah'>zobrazit</a>");

            web_send_chunk(req, "</td><td class='download-cell'>");
            if (is_current_file) {
                web_send_chunk(req, "<span class='muted' title='Aktuální soubor nelze stáhnout, nejprve ho uzavřete tlačítkem Uzavřít aktuální a vytvořit nový'>stáhnout</span>");
            } else {
                web_send_chunk(req, "<a href='/download?file=");
                web_send_html_escaped(req, files[i].name);
                if (archive_mode) {
                    web_send_chunk(req, "&amp;archive=1");
                }
                if (edl_title[0] != '\0') {
                    web_send_chunk(req, "' title='Stáhnout: ");
                    web_send_html_escaped(req, edl_title);
                    web_send_chunk(req, ".edl'>stáhnout</a>");
                } else {
                    web_send_chunk(req, "' title='Stáhnout soubor s názvem pořadu'>stáhnout</a>");
                }
            }
        }

        web_send_chunk(req, "</td><td class='archive-cell'>");
        if (trash_mode) {
            web_send_chunk(req, "<a href='/trash?file=");
            web_send_html_escaped(req, files[i].name);
            web_send_chunk(req, "&amp;action=restore' onclick=\"return confirm('Vrátit soubor z koše?');\">vrátit</a>");
        } else if (archive_mode) {
            web_send_chunk(req, "<a href='/archive?file=");
            web_send_html_escaped(req, files[i].name);
            web_send_chunk(req, "&amp;action=restore' onclick=\"return confirm('Vrátit soubor z archivu?');\">vrátit</a>");
        } else if (is_current_file) {
            web_send_chunk(req, "<span class='muted'>aktuální soubor</span>");
        } else {
            web_send_chunk(req, "<a href='/archive?file=");
            web_send_html_escaped(req, files[i].name);
            web_send_chunk(req, "&amp;action=archive' title='Přesunout soubor do složky Archiv' onclick=\"return confirm('Archivovat soubor?');\">archivovat</a>");
        }

        web_send_chunk(req, "</td><td class='delete-cell' data-delete-cell='1'>");
        if (is_current_file) {
            web_send_chunk(req, "<span class='muted'>aktuální soubor</span><br>");
            web_send_chunk(req, "<a class='del' href='/delete?file=");
            web_send_html_escaped(req, files[i].name);
            web_send_chunk(req, "' title='Uzavřít, přesunout do koše a vytvořit nový'>odstranit aktuální</a>");
        } else {
            web_send_chunk(req, "<a class='del' href='/delete?file=");
            web_send_html_escaped(req, files[i].name);
            if (archive_mode) {
                web_send_chunk(req, "&amp;archive=1");
            } else if (trash_mode) {
                web_send_chunk(req, "&amp;trash=1");
            }
            web_send_chunk(req, trash_mode ? "'>odstranit</a>" : "' title='Přesunout soubor do koše'>odstranit</a>");
        }

        web_send_chunk(req, "</td></tr>");
    }

    if (count == 0) {
        web_send_chunk(req, trash_mode
            ? "<tr><td colspan='6' class='muted'>Koš je prázdný.</td></tr>"
            : "<tr><td colspan='9' class='muted'>V tomto režimu nejsou žádné zobrazitelné soubory.</td></tr>");
    }

    web_send_chunk(req, "</table></div>");

    if (count > 0 && !trash_mode) {
        web_send_chunk(req, "<p><button class='btn danger' type='submit' id='deleteSelectedBtn' disabled>Přesunout vybrané do koše</button> ");
        web_send_chunk(req, "<span class='muted' id='selectedInfo'>Vybráno 0 / 20</span></p>");
        web_send_chunk(req,
            "<script>"
            "(function(){"
            "const max=20;"
            "const selectAll=document.getElementById('selectVisibleFiles');"
            "const btn=document.getElementById('deleteSelectedBtn');"
            "const info=document.getElementById('selectedInfo');"
            "function boxes(){return Array.from(document.querySelectorAll('input.filecheck:not(:disabled)'));}"
            "function upd(changed){"
            "const bs=boxes();"
            "let n=bs.filter(b=>b.checked).length;"
            "if(n>max && changed){changed.checked=false;n=bs.filter(b=>b.checked).length;alert('Najednou lze smazat nejvýše 20 souborů.');}"
            "if(btn)btn.disabled=(n===0);"
            "if(info)info.textContent='Vybráno '+n+' / '+max;"
            "if(selectAll){selectAll.disabled=(bs.length===0);selectAll.checked=(bs.length>0&&n===bs.length);selectAll.indeterminate=(n>0&&n<bs.length);}"
            "}"
            "function wireFileChecks(){document.querySelectorAll('input.filecheck').forEach(function(b){if(!b.dataset.wired){b.dataset.wired='1';b.addEventListener('change',function(){upd(this);});}});}"
            "if(selectAll){selectAll.addEventListener('change',function(){boxes().forEach(b=>b.checked=selectAll.checked);upd(null);});}"
            "wireFileChecks();"
            "upd(null);"
            "})();"
            "</script>"
        );
    }

    web_send_chunk(req, "</form>");

    web_send_files_pagination(req, page, total_pages, files_mode);

    if (skipped > 0) {
        char skipped_text[160];
        snprintf(skipped_text, sizeof(skipped_text), "<p class='muted'>Poznámka: %u souborů se nezobrazilo kvůli limitu seznamu %u.</p>", skipped, (unsigned)WEB_MAX_FILE_LIST);
        web_send_chunk(req, skipped_text);
    }

    free(files);
    web_send_html_footer(req);
    return ESP_OK;
}

static esp_err_t web_view_handler(httpd_req_t *req)
{
    char filename[WEB_FILE_NAME_MAX_LEN] = {0};
    char path[WEB_FILE_PATH_MAX_LEN] = {0};
    bool archived = web_query_has_archive(req);
    bool trashed = web_query_has_trash(req);

    if (!sd_storage_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card is not mounted");
        return ESP_OK;
    }

    if (web_get_query_filename(req, filename, sizeof(filename)) != ESP_OK ||
        web_make_file_path_for_query_location(filename, archived, trashed, path, sizeof(path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid file parameter");
        return ESP_OK;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_OK;
    }

    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");

    bool is_current_file = !archived && !trashed && web_is_current_file(filename);
    web_send_html_header(req, filename);
    web_send_chunk(req, "<p><a class='btn' href='");
    web_send_chunk(req, trashed ? "/files?mode=trash" : (archived ? "/files?mode=archive" : "/files"));
    web_send_chunk(req, "'>Zpět na soubory</a> ");
    if (is_current_file) {
        web_send_chunk(req, "<a class='btn' href='/view?file=");
        web_send_html_escaped(req, filename);
        web_send_chunk(req, "'>Refresh</a> ");
    }
    web_send_chunk(req, "<a class='btn' href='/download?file=");
    web_send_html_escaped(req, filename);
    if (archived) {
        web_send_chunk(req, "&amp;archive=1");
    } else if (trashed) {
        web_send_chunk(req, "&amp;trash=1");
    }
    web_send_chunk(req, "'>Stáhnout</a>");
    web_send_chunk(req, " <a class='btn danger' href='/delete?file=");
    web_send_html_escaped(req, filename);
    if (archived) {
        web_send_chunk(req, "&amp;archive=1");
    } else if (trashed) {
        web_send_chunk(req, "&amp;trash=1");
    }
    if (is_current_file) {
        web_send_chunk(req, "' title='Smaže aktuální soubor a vytvoří nový se stejným názvem'>Smazat aktuální a vytvořit nový</a>");
    } else {
        web_send_chunk(req, "'>Smazat</a>");
    }
    web_send_chunk(req, "</p>");
    web_send_chunk(req, "<h1>");
    web_send_html_escaped(req, filename);
    web_send_chunk(req, "</h1><pre>");

    char buffer[WEB_READ_BUFFER_LEN];
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        web_send_html_escaped(req, buffer);
    }

    fclose(file);

    web_send_chunk(req, "</pre>");
    web_send_html_footer(req);
    return ESP_OK;
}


static esp_err_t web_delete_selected_confirm_handler(httpd_req_t *req)
{
    if (!sd_storage_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card is not mounted");
        return ESP_OK;
    }

    web_selected_file_t selected[WEB_MAX_SELECTED_DELETE] = {0};
    unsigned selected_count = web_get_selected_files_from_query(
        req,
        selected,
        WEB_MAX_SELECTED_DELETE,
        WEB_FILES_PER_PAGE
    );

    web_send_html_header(req, "ATEM Logger - přesunout vybrané do koše");
    web_send_chunk(req, "<h1>Přesunout vybrané soubory do koše</h1>");

    if (selected_count == 0) {
        web_send_chunk(req, "<div class='card'><p><span class='bad'>Není vybraný žádný platný soubor ke smazání.</span></p></div>");
        web_send_chunk(req, "<p><a class='btn' href='/files'>Zpět na soubory</a></p>");
        web_send_html_footer(req);
        return ESP_OK;
    }

    char line[160];
    snprintf(line, sizeof(line), "<div class='card'><p>Opravdu přesunout vybrané soubory do koše? Počet: <b>%u</b></p><ul>", selected_count);
    web_send_chunk(req, line);

    for (unsigned i = 0; i < selected_count; i++) {
        web_send_chunk(req, "<li>");
        web_send_html_escaped(req, selected[i].name);
        web_send_chunk(req, "</li>");
    }

    web_send_chunk(req, "</ul><form action='/delete_selected_do' method='get'>");
    for (unsigned i = 0; i < selected_count; i++) {
        char name_attr[16];
        if (!web_make_index_key(name_attr, sizeof(name_attr), i)) {
            continue;
        }
        web_send_chunk(req, "<input type='hidden' name='");
        web_send_html_escaped(req, name_attr);
        web_send_chunk(req, "' value='");
        web_send_html_escaped(req, selected[i].name);
        web_send_chunk(req, "'>");
    }
    web_send_chunk(req, "<p><button class='btn danger' type='submit'>Ano, přesunout vybrané</button> <a class='btn' href='/files'>Ne, zpět</a></p></form></div>");

    web_send_html_footer(req);
    return ESP_OK;
}

static esp_err_t web_delete_selected_do_handler(httpd_req_t *req)
{
    if (!sd_storage_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card is not mounted");
        return ESP_OK;
    }

    web_selected_file_t selected[WEB_MAX_SELECTED_DELETE] = {0};
    unsigned selected_count = web_get_selected_files_from_query(
        req,
        selected,
        WEB_MAX_SELECTED_DELETE,
        WEB_MAX_SELECTED_DELETE
    );

    web_send_html_header(req, "ATEM Logger - přesun vybraných do koše");
    web_send_chunk(req, "<h1>Přesun vybraných souborů do koše</h1>");

    if (selected_count == 0) {
        web_send_chunk(req, "<div class='card'><p><span class='bad'>Není vybraný žádný platný soubor ke smazání.</span></p></div>");
        web_send_chunk(req, "<p><a class='btn' href='/files'>Zpět na soubory</a></p>");
        web_send_html_footer(req);
        return ESP_OK;
    }

    unsigned moved = 0;
    unsigned skipped = 0;
    unsigned failed = 0;

    web_send_chunk(req, "<table><tr><th>Soubor</th><th>Výsledek</th></tr>");

    for (unsigned i = 0; i < selected_count; i++) {
        char path[WEB_FILE_PATH_MAX_LEN] = {0};
        const char *result = NULL;
        const char *class_name = NULL;

        if (web_is_current_file(selected[i].name)) {
            result = "aktuální soubor přeskočen";
            class_name = "muted";
            skipped++;
        } else if (web_make_file_path(selected[i].name, path, sizeof(path)) != ESP_OK) {
            result = "neplatná cesta";
            class_name = "bad";
            failed++;
        } else {
            struct stat st;
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
                result = "soubor neexistuje";
                class_name = "bad";
                failed++;
            } else {
                esp_err_t move_ret = web_move_file_to_trash(selected[i].name, false);
                if (move_ret == ESP_OK) {
                    result = "přesunuto do koše";
                    class_name = "ok";
                    moved++;
                } else if (move_ret == ESP_ERR_NO_MEM) {
                    result = "koš je plný pro tento soubor";
                    class_name = "bad";
                    failed++;
                } else {
                    result = "nepodařilo se přesunout";
                    class_name = "bad";
                    failed++;
                }
            }
        }

        web_send_chunk(req, "<tr><td>");
        web_send_html_escaped(req, selected[i].name);
        web_send_chunk(req, "</td><td><span class='");
        web_send_html_escaped(req, class_name);
        web_send_chunk(req, "'>");
        web_send_html_escaped(req, result);
        web_send_chunk(req, "</span></td></tr>");
    }

    web_send_chunk(req, "</table>");

    char summary[192];
    snprintf(summary, sizeof(summary), "<p>Přesunuto do koše: <b>%u</b>, přeskočeno: <b>%u</b>, chyba: <b>%u</b>.</p>", moved, skipped, failed);
    web_send_chunk(req, summary);
    web_send_chunk(req, "<p><a class='btn' href='/files'>Zpět na soubory</a></p>");

    web_send_html_footer(req);
    return ESP_OK;
}

static esp_err_t web_delete_confirm_handler(httpd_req_t *req)
{
    char filename[WEB_FILE_NAME_MAX_LEN] = {0};
    bool archived = web_query_has_archive(req);
    bool trashed = web_query_has_trash(req);

    if (!sd_storage_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card is not mounted");
        return ESP_OK;
    }

    if (web_get_query_filename(req, filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid file parameter");
        return ESP_OK;
    }

    web_send_html_header(req, "ATEM Logger - smazat soubor");
    web_send_chunk(req, "<h1>Smazat soubor</h1>");

    if (!archived && !trashed && web_is_current_file(filename)) {
        web_send_chunk(req, "<div class='card'><p><span class='bad'>Toto je aktuální otevřený EDL soubor.</span></p>");
        web_send_chunk(req, "<p>Přesunout ho lze pouze samostatně. Před přesunem do koše se uzavře aktuální session a hned se vytvoří nový EDL soubor.</p><p><b>");
        web_send_html_escaped(req, filename);
        web_send_chunk(req, "</b></p>");
        web_send_chunk(req, "<p><a class='btn danger' href='/delete_do?file=");
        web_send_html_escaped(req, filename);
        web_send_chunk(req, "'>Ano, přesunout do koše a vytvořit nový</a> <a class='btn' href='/files'>Ne, zpět</a></p></div>");
        web_send_html_footer(req);
        return ESP_OK;
    }

    web_send_chunk(req, trashed
        ? "<div class='card'><p>Opravdu definitivně smazat soubor?</p><p><b>"
        : "<div class='card'><p>Opravdu přesunout soubor do koše?</p><p><b>");
    web_send_html_escaped(req, filename);
    web_send_chunk(req, "</b></p>");
    web_send_chunk(req, "<p><a class='btn danger' href='/delete_do?file=");
    web_send_html_escaped(req, filename);
    if (archived) {
        web_send_chunk(req, "&amp;archive=1");
    } else if (trashed) {
        web_send_chunk(req, "&amp;trash=1");
    }
    web_send_chunk(req, trashed ? "'>Ano, smazat definitivně</a> <a class='btn' href='" : "'>Ano, přesunout do koše</a> <a class='btn' href='");
    web_send_chunk(req, trashed ? "/files?mode=trash" : (archived ? "/files?mode=archive" : "/files"));
    web_send_chunk(req, "'>Ne, zpět</a></p></div>");

    web_send_html_footer(req);
    return ESP_OK;
}

static esp_err_t web_delete_do_handler(httpd_req_t *req)
{
    char filename[WEB_FILE_NAME_MAX_LEN] = {0};
    char path[WEB_FILE_PATH_MAX_LEN] = {0};
    bool archived = web_query_has_archive(req);
    bool trashed = web_query_has_trash(req);

    if (!sd_storage_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card is not mounted");
        return ESP_OK;
    }

    if (web_get_query_filename(req, filename, sizeof(filename)) != ESP_OK ||
        web_make_file_path_for_query_location(filename, archived, trashed, path, sizeof(path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid file parameter");
        return ESP_OK;
    }

    bool is_current_file = !archived && !trashed && web_is_current_file(filename);

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        web_send_html_header(req, "ATEM Logger - chyba mazání");
        web_send_chunk(req, "<h1>Mazání souboru</h1>");
        web_send_chunk(req, "<div class='card'><p><span class='bad'>Soubor neexistuje nebo to není běžný soubor.</span></p><p><b>");
        web_send_html_escaped(req, filename);
        web_send_chunk(req, "</b></p></div>");
        web_send_chunk(req, "<p><a class='btn' href='");
        web_send_chunk(req, trashed ? "/files?mode=trash" : (archived ? "/files?mode=archive" : "/files"));
        web_send_chunk(req, "'>Zpět na soubory</a></p>");
        web_send_html_footer(req);
        return ESP_OK;
    }

    if (is_current_file) {
        rtc_datetime_t rtc_now = {0};
        esp_err_t rtc_ret = rtc_read_datetime(&rtc_now);
        bool rtc_valid = (rtc_ret == ESP_OK);

        app_state_update_rtc(&rtc_now, rtc_valid);

        char trash_filename[WEB_FILE_NAME_MAX_LEN] = {0};
        char trash_path[WEB_FILE_PATH_MAX_LEN] = {0};
        esp_err_t trash_ret = web_ensure_trash_dir();
        if (trash_ret == ESP_OK) {
            trash_ret = web_find_trash_target_path(filename, trash_filename, sizeof(trash_filename), trash_path, sizeof(trash_path));
        }

        if (trash_ret != ESP_OK && trash_ret != ESP_ERR_NO_MEM) {
            web_send_html_header(req, "ATEM Logger - chyba koše");
            web_send_chunk(req, "<h1>Mazání souboru</h1>");
            web_send_chunk(req, "<div class='card'><p><span class='bad'>Koš se nepodařilo připravit.</span></p></div>");
            web_send_chunk(req, "<p><a class='btn' href='/files'>Zpět na soubory</a></p>");
            web_send_html_footer(req);
            return ESP_OK;
        }

        if (trash_ret == ESP_ERR_NO_MEM) {
            web_send_html_header(req, "ATEM Logger - koš je plný");
            web_send_chunk(req, "<h1>Mazání souboru</h1>");
            web_send_chunk(req, "<div class='card'><p><span class='bad'>Koš je plný pro tento soubor.</span></p><p><b>");
            web_send_html_escaped(req, filename);
            web_send_chunk(req, "</b></p><p>V koši jsou už obsazené přípony .000 až .999.</p></div>");
            web_send_chunk(req, "<p><a class='btn' href='/files'>Zpět na soubory</a> <a class='btn' href='/files?mode=trash'>Koš</a></p>");
            web_send_html_footer(req);
            return ESP_OK;
        }

        // Aktuální soubor se přesouvá do koše jen samostatně:
        // 1) případný rozpracovaný segment se uzavře do starého souboru,
        // 2) RAM session se vynuluje,
        // 3) starý aktuální soubor se přesune do koše,
        // 4) až potom se založí nový EDL soubor.
        //
        // Důležité: nový název se hledá až po přesunu starého aktuálního souboru,
        // aby se při mazání posledního souboru dne nezapočítával soubor, který právě mažeme.
        (void)cut_event_close_active_segment_from_state();
        cut_event_reset_session();

        bool deleted_ok = (web_move_file_to_trash(filename, false) == ESP_OK);
        esp_err_t session_ret = ESP_FAIL;

        if (deleted_ok) {
            session_ret = logger_session_start_new_from_rtc(&rtc_now, rtc_valid);
        }

        if (deleted_ok && session_ret == ESP_OK) {
            return web_redirect_to_files(req);
        }

        web_send_html_header(req, "ATEM Logger - chyba mazání");
        web_send_chunk(req, "<h1>Mazání souboru</h1>");
        web_send_chunk(req, "<div class='card'><p><span class='bad'>Mazání aktuálního souboru nebo vytvoření náhradního souboru se nepovedlo úplně správně.</span></p><p>Původní: <b>");
        web_send_html_escaped(req, filename);
        web_send_chunk(req, "</b></p><p>Nový aktuální soubor: <b>");
        web_send_html_escaped(req, logger_session_get_filename());
        web_send_chunk(req, "</b></p></div>");
    } else {
        esp_err_t move_ret = trashed ? (remove(path) == 0 ? ESP_OK : ESP_FAIL) : web_move_file_to_trash(filename, archived);
        if (move_ret == ESP_OK) {
            return web_redirect_to_files_location(req, archived, trashed);
        }

        web_send_html_header(req, move_ret == ESP_ERR_NO_MEM ? "ATEM Logger - koš je plný" : "ATEM Logger - chyba mazání");
        web_send_chunk(req, "<h1>Mazání souboru</h1>");
        if (move_ret == ESP_ERR_NO_MEM) {
            web_send_chunk(req, "<div class='card'><p><span class='bad'>Koš je plný pro tento soubor.</span></p><p><b>");
            web_send_html_escaped(req, filename);
            web_send_chunk(req, "</b></p><p>V koši jsou už obsazené přípony .000 až .999.</p></div>");
        } else {
            web_send_chunk(req, "<div class='card'><p><span class='bad'>Soubor se nepodařilo smazat.</span></p><p><b>");
            web_send_html_escaped(req, filename);
            web_send_chunk(req, "</b></p></div>");
        }
    }

    web_send_chunk(req, "<p><a class='btn' href='");
    web_send_chunk(req, trashed ? "/files?mode=trash" : (archived ? "/files?mode=archive" : "/files"));
    web_send_chunk(req, "'>Zpět na soubory</a></p>");
    web_send_html_footer(req);
    return ESP_OK;
}

static esp_err_t web_download_handler(httpd_req_t *req)
{
    char filename[WEB_FILE_NAME_MAX_LEN] = {0};
    char path[WEB_FILE_PATH_MAX_LEN] = {0};
    bool archived = web_query_has_archive(req);
    bool trashed = web_query_has_trash(req);

    if (!sd_storage_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card is not mounted");
        return ESP_OK;
    }

    if (web_get_query_filename(req, filename, sizeof(filename)) != ESP_OK ||
        web_make_file_path_for_query_location(filename, archived, trashed, path, sizeof(path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid file parameter");
        return ESP_OK;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/plain");

    char download_filename[128] = {0};
    char edl_title[SHOW_CONFIG_TITLE_MAX_LEN] = {0};
    (void)web_read_edl_info_for_file_location(filename, archived, trashed, edl_title, sizeof(edl_title));
    web_make_download_filename_from_title(edl_title, download_filename, sizeof(download_filename));
    if (download_filename[0] == '\0') {
        snprintf(download_filename, sizeof(download_filename), "%s", filename);
    }

    char disposition[192];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", download_filename);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);

    char buffer[WEB_READ_BUFFER_LEN];
    size_t read_len;
    while ((read_len = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, read_len) != ESP_OK) {
            fclose(file);
            return ESP_FAIL;
        }
    }

    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 22;
    config.stack_size = WEB_SERVER_TASK_STACK;
    config.core_id = WEB_SERVER_TASK_CORE;
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        s_server = NULL;
        return ret;
    }

    httpd_uri_t home_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = web_home_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t new_file_uri = {
        .uri = "/new_file",
        .method = HTTP_GET,
        .handler = web_new_file_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t about_uri = {
        .uri = "/about",
        .method = HTTP_GET,
        .handler = web_about_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t api_state_uri = {
        .uri = "/api/state",
        .method = HTTP_GET,
        .handler = web_api_state_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t network_uri = {
        .uri = "/network",
        .method = HTTP_GET,
        .handler = web_network_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t save_network_uri = {
        .uri = "/save_network",
        .method = HTTP_GET,
        .handler = web_save_network_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t save_program_tally_uri = {
        .uri = "/save_program_tally",
        .method = HTTP_GET,
        .handler = web_save_program_tally_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t save_preview_tally_uri = {
        .uri = "/save_preview_tally",
        .method = HTTP_GET,
        .handler = web_save_preview_tally_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t save_ltc_correction_uri = {
        .uri = "/save_ltc_correction",
        .method = HTTP_GET,
        .handler = web_save_ltc_correction_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t shows_uri = {
        .uri = "/shows",
        .method = HTTP_GET,
        .handler = web_shows_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t save_shows_uri = {
        .uri = "/save_shows",
        .method = HTTP_POST,
        .handler = web_save_shows_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t rtc_sync_uri = {
        .uri = "/rtc_sync",
        .method = HTTP_GET,
        .handler = web_rtc_sync_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t reboot_uri = {
        .uri = "/reboot",
        .method = HTTP_GET,
        .handler = web_reboot_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t files_uri = {
        .uri = "/files",
        .method = HTTP_GET,
        .handler = web_files_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t archive_uri = {
        .uri = "/archive",
        .method = HTTP_GET,
        .handler = web_archive_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t trash_uri = {
        .uri = "/trash",
        .method = HTTP_GET,
        .handler = web_trash_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t view_uri = {
        .uri = "/view",
        .method = HTTP_GET,
        .handler = web_view_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t download_uri = {
        .uri = "/download",
        .method = HTTP_GET,
        .handler = web_download_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t delete_uri = {
        .uri = "/delete",
        .method = HTTP_GET,
        .handler = web_delete_confirm_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t delete_do_uri = {
        .uri = "/delete_do",
        .method = HTTP_GET,
        .handler = web_delete_do_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t delete_selected_uri = {
        .uri = "/delete_selected",
        .method = HTTP_GET,
        .handler = web_delete_selected_confirm_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t delete_selected_do_uri = {
        .uri = "/delete_selected_do",
        .method = HTTP_GET,
        .handler = web_delete_selected_do_handler,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &home_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &new_file_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &about_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &api_state_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &network_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &save_network_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &save_program_tally_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &save_preview_tally_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &save_ltc_correction_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &shows_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &save_shows_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &rtc_sync_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &reboot_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &files_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &archive_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &trash_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &view_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &download_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &delete_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &delete_do_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &delete_selected_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &delete_selected_do_uri));

    net_eth_status_t eth_status = {0};
    net_eth_get_status(&eth_status);
    ESP_LOGI(
        TAG,
        "web server started at http://%s/ core=%d stack=%u",
        eth_status.ip,
        WEB_SERVER_TASK_CORE,
        (unsigned)WEB_SERVER_TASK_STACK
    );
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server == NULL) {
        return;
    }

    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "web server stopped");
}

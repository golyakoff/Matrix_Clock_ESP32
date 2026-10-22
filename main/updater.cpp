#include <string.h>
#include <stdio.h>
#include <time.h>
#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <mbedtls/sha256.h>

#include "updater.h"
#include "ota.h"
#include "wifi.h"

static const char* TAG = "UPDATER";

#define UPDATER_MANIFEST_MAX_SIZE   (4096U)
#define UPDATER_MAX_REDIRECTS       (5U)
#define UPDATER_HTTP_TIMEOUT_MS     (15000U)
#define UPDATER_HTTP_BUFFER_SIZE    (4096U)  // GitHub redirect headers are long
#define UPDATER_CHUNK_SIZE          (512U)   // ota_write() takes up to 512 bytes, as a BLE chunk
#define UPDATER_OTA_WRITE_WAIT_MS   (5000U)  // the flash writer erases sectors as it goes
#define UPDATER_PROGRESS_STEP       (4096U)  // OTA status notifications while downloading

static update_info_t _info = {};
static updater_info_cb_t _info_cb = nullptr;
static updater_ota_status_cb_t _ota_status_cb = nullptr;
static updater_ota_begin_cb_t _ota_begin_cb = nullptr;
static volatile bool _check_requested = false;
static volatile bool _install_requested = false;

// the latest manifest data for this build
static char _url[512];
static uint32_t _size = 0;
static uint8_t _sha[32];
static bool _sha_valid = false;

static void set_state(update_state_t s)
{
    _info.state = s;
    if (_info_cb)
        _info_cb();
}

static void report(uint8_t state, uint8_t error, uint32_t received = 0, uint32_t total = 0)
{
    if (_ota_status_cb)
        _ota_status_cb(state, error, received, total);
}

// "v1.2.3" -> comparable number, 0 if not parsed
static uint32_t version_num(const char *v)
{
    unsigned major = 0, minor = 0, patch = 0;
    if (v[0] == 'v')
        v++;
    if (sscanf(v, "%u.%u.%u", &major, &minor, &patch) < 2)
        return 0;
    return (major << 20) | ((minor & 0x3ff) << 10) | (patch & 0x3ff);
}

static bool parse_hex(const char *s, uint8_t *out, size_t len)
{
    if (!s || strlen(s) != len * 2)
        return false;
    for (size_t i = 0; i < len; i++)
        if (sscanf(s + i * 2, "%2hhx", &out[i]) != 1)
            return false;
    return true;
}

// Opens a GET request following redirects (GitHub release assets are redirected twice)
static esp_err_t http_open(esp_http_client_handle_t client, int64_t *content_length)
{
    for (unsigned i = 0; i < UPDATER_MAX_REDIRECTS; i++)
    {
        // TLS needs a few tens of KB of heap, partly in one block
        ESP_LOGI(TAG, "Free heap %u, largest block %u, minimum ever %u",
                heap_caps_get_free_size(MALLOC_CAP_8BIT),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
        esp_err_t res = esp_http_client_open(client, 0);
        if (res != ESP_OK)
        {
            ESP_LOGW(TAG, "Could not connect: %d (%s)", res, esp_err_to_name(res));
            return res;
        }
        *content_length = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status == 200)
            return ESP_OK;
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
        {
            esp_http_client_set_redirection(client);
            esp_http_client_flush_response(client, nullptr);
            esp_http_client_close(client);
            continue;
        }
        ESP_LOGW(TAG, "HTTP status %d", status);
        esp_http_client_close(client);
        return ESP_FAIL;
    }
    esp_http_client_close(client);
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_http_client_handle_t http_client(const char *url)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = UPDATER_HTTP_TIMEOUT_MS;
    cfg.buffer_size = UPDATER_HTTP_BUFFER_SIZE;
    cfg.buffer_size_tx = UPDATER_HTTP_BUFFER_SIZE;
    return esp_http_client_init(&cfg);
}

// report: also send OTA status notifications (the app asked to install)
static esp_err_t check(bool report_status)
{
    set_state(update_checking);
    if (report_status)
        report(updater_ota_checking, updater_ota_err_none);

    esp_http_client_handle_t client = http_client(UPDATER_MANIFEST_URL);
    if (!client)
        return ESP_ERR_NO_MEM;

    char *buf = (char *)calloc(1, UPDATER_MANIFEST_MAX_SIZE);
    cJSON *json = nullptr;
    esp_err_t res = ESP_FAIL;
    int64_t len = 0;
    const char *version = nullptr;
    cJSON *build = nullptr;
    const char *build_url = nullptr;
    cJSON *build_size = nullptr;
    const char *current = nullptr;
    bool newer = false;
    int total = 0, n;

    if (!buf)
    {
        res = ESP_ERR_NO_MEM;
        goto exit;
    }
    if ((res = http_open(client, &len)) != ESP_OK)
        goto exit;

    while (total < (int)UPDATER_MANIFEST_MAX_SIZE - 1 &&
           (n = esp_http_client_read(client, buf + total, UPDATER_MANIFEST_MAX_SIZE - 1 - total)) > 0)
        total += n;
    esp_http_client_close(client);

    res = ESP_ERR_INVALID_RESPONSE;
    json = cJSON_Parse(buf);
    version = cJSON_GetStringValue(cJSON_GetObjectItem(json, "version"));
    if (!version || !version_num(version))
    {
        ESP_LOGW(TAG, "Invalid manifest");
        goto exit;
    }
    strncpy(_info.version, version, sizeof(_info.version) - 1);
    _info.checked_at = time(nullptr);

    build = cJSON_GetObjectItem(cJSON_GetObjectItem(json, "builds"), CONFIG_MC_BUILD_VARIANT);
    if (!build)
    {
        // a custom build: official releases are not for this clock
        ESP_LOGI(TAG, "Release %s has no build " CONFIG_MC_BUILD_VARIANT, version);
        set_state(update_up_to_date);
        res = ESP_OK;
        goto exit;
    }
    build_url = cJSON_GetStringValue(cJSON_GetObjectItem(build, "url"));
    build_size = cJSON_GetObjectItem(build, "size");
    if (!build_url || strlen(build_url) >= sizeof(_url) || strncmp(build_url, "https://", 8)
            || !cJSON_IsNumber(build_size) || cJSON_GetNumberValue(build_size) < 1)
    {
        ESP_LOGW(TAG, "Invalid build " CONFIG_MC_BUILD_VARIANT " in manifest");
        goto exit;
    }

    strcpy(_url, build_url);
    _size = (uint32_t)cJSON_GetNumberValue(build_size);
    _sha_valid = parse_hex(cJSON_GetStringValue(cJSON_GetObjectItem(build, "sha256")), _sha, sizeof(_sha));

    current = esp_app_get_description()->version;
    newer = version_num(version) > version_num(current);
    ESP_LOGI(TAG, "Current %s, released %s%s", current, version, newer ? ": update available" : "");
    set_state(newer ? update_available : update_up_to_date);
    res = ESP_OK;

exit:
    if (res != ESP_OK)
    {
        ESP_LOGW(TAG, "Check failed: %d (%s)", res, esp_err_to_name(res));
        set_state(update_check_failed);
    }
    cJSON_Delete(json);
    free(buf);
    esp_http_client_cleanup(client);
    return res;
}

static esp_err_t install()
{
    ESP_LOGI(TAG, "Installing %s from %s", _info.version, _url);

    esp_http_client_handle_t client = http_client(_url);
    if (!client)
    {
        report(updater_ota_error, updater_ota_err_download);
        return ESP_ERR_NO_MEM;
    }

    int64_t len = 0;
    esp_err_t res = http_open(client, &len);
    if (res != ESP_OK)
    {
        report(updater_ota_error, updater_ota_err_download);
        esp_http_client_cleanup(client);
        return res;
    }

    // From here on the clock is in the OTA mode: any failure ends in an ota_abort(), after which
    // main.cpp reboots the clock (as after a failed BLE upload)
    if (!_ota_begin_cb || !_ota_begin_cb(_size))
    {
        report(updater_ota_error, updater_ota_err_busy);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_STATE;
    }

    static uint8_t chunk[UPDATER_CHUNK_SIZE];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    uint32_t received = 0, reported = 0;
    uint8_t error = updater_ota_err_none;
    report(updater_ota_receiving, updater_ota_err_none, 0, _size);
    while (received < _size)
    {
        uint32_t want = _size - received;
        int n = esp_http_client_read(client, (char *)chunk, want < sizeof(chunk) ? want : sizeof(chunk));
        if (n <= 0)
        {
            ESP_LOGE(TAG, "Download interrupted at %lu of %lu bytes", (unsigned long)received, (unsigned long)_size);
            error = updater_ota_err_download;
            break;
        }
        mbedtls_sha256_update(&sha, chunk, n);
        if (!ota_write(chunk, n, UPDATER_OTA_WRITE_WAIT_MS))
        {
            error = updater_ota_err_flash;
            break;
        }
        received += n;
        if (received - reported >= UPDATER_PROGRESS_STEP)
        {
            reported = received;
            report(updater_ota_receiving, updater_ota_err_none, received, _size);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    if (error == updater_ota_err_none && _sha_valid && memcmp(digest, _sha, sizeof(digest)))
    {
        ESP_LOGE(TAG, "SHA-256 mismatch");
        error = updater_ota_err_sha;
    }

    if (error != updater_ota_err_none)
    {
        // ota_write() aborts by itself on a flash error
        if (error != updater_ota_err_flash)
            ota_abort();
        report(updater_ota_error, error, received, _size);
        return ESP_FAIL;
    }

    report(updater_ota_success, updater_ota_err_none, received, _size);
    // verifies the image, switches the boot partition and reboots
    if (!ota_end())
    {
        report(updater_ota_error, updater_ota_err_flash, received, _size);
        return ESP_FAIL;
    }
    return ESP_OK;
}

////////////////////////////////////////////////////////////////////////////////

void updater_init(updater_info_cb_t info_cb, updater_ota_status_cb_t ota_status_cb, updater_ota_begin_cb_t ota_begin_cb)
{
    _info_cb = info_cb;
    _ota_status_cb = ota_status_cb;
    _ota_begin_cb = ota_begin_cb;
}

void updater_get_info(update_info_t *info)
{
    *info = _info;
}

bool updater_request_check()
{
    _check_requested = true;
    if (wifi_request_session())
        return true;
    // WiFi is off or not configured: answer right away
    updater_fail_pending();
    return false;
}

bool updater_request_install()
{
    if (ota_is_in_progress())
    {
        report(updater_ota_error, updater_ota_err_busy);
        return false;
    }
    _install_requested = true;
    if (wifi_request_session())
        return true;
    updater_fail_pending();
    return false;
}

void updater_fail_pending()
{
    bool install_pending = _install_requested;
    if (!_check_requested && !install_pending)
        return;
    _check_requested = false;
    _install_requested = false;
    ESP_LOGW(TAG, "No WiFi connection");
    set_state(update_check_failed);
    if (install_pending)
        report(updater_ota_error, updater_ota_err_download);
}

void updater_run(bool regular_check)
{
    bool do_install = _install_requested;
    bool do_check = regular_check || _check_requested || do_install;
    _check_requested = false;
    _install_requested = false;

    if (!do_check)
        return;
    if (check(do_install) != ESP_OK)
    {
        if (do_install)
            report(updater_ota_error, updater_ota_err_download);
        return;
    }
    if (!do_install)
        return;
    if (_info.state != update_available)
    {
        report(updater_ota_no_update, updater_ota_err_none);
        return;
    }
    if (install() != ESP_OK)
        ESP_LOGE(TAG, "Installation failed");
}

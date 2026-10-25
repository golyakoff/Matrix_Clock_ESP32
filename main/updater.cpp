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
#include "net_settings.h"
#include "ota.h"
#include "wifi.h"

static const char* TAG = "UPDATER";

#define UPDATER_MANIFEST_MAX_SIZE   (4096U)
#define UPDATER_MAX_REDIRECTS       (5U)
#define UPDATER_HTTP_TIMEOUT_MS     (15000U)
#define UPDATER_HTTP_BUFFER_SIZE    (4096U)  // GitHub redirect headers are long
#define UPDATER_CHUNK_SIZE          (512U)   // ota_write() takes up to 512 bytes, as a BLE chunk
// The flash writer erases as it goes, a 64 KB block at a time (Update), which can take a while:
// the download just waits for it, as FireLamp_ESP32 does (TCP holds the data meanwhile)
#define UPDATER_OTA_WRITE_WAIT_MS   (30000U)
#define UPDATER_PROGRESS_STEP       (4096U)  // OTA status notifications while downloading

static update_info_t _info = {};
static updater_info_cb_t _info_cb = nullptr;
static updater_ota_status_cb_t _ota_status_cb = nullptr;
static updater_ota_begin_cb_t _ota_begin_cb = nullptr;
static volatile bool _check_requested = false;
static volatile bool _install_requested = false;

// A release as a manifest describes it, with the data of this build
typedef struct {
    char version[sizeof(update_info_t::version)];
    bool has_build;         // the manifest has CONFIG_MC_BUILD_VARIANT
    char url[512];
    uint32_t size;
    uint8_t sha[32];
    bool sha_valid;
} release_t;

// the releases of the last check: the stable one and the beta (beta channel only)
static release_t _releases[2];
// the release to install: one of _releases, nullptr if none has this build
static const release_t *_release = nullptr;
// the last check found a newer release without a build of this clock's variant
static bool _newer_without_build = false;

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

// "v1.2.3" -> comparable number, 0 if not parsed. A beta comes before its release:
// v1.2.3 < v1.2.4-beta1 < v1.2.4-beta2 < v1.2.4. Other suffixes are ignored.
static uint64_t version_num(const char *v)
{
    unsigned major = 0, minor = 0, patch = 0, beta = 0;
    int n = 0;
    if (v[0] == 'v')
        v++;
    if (sscanf(v, "%u.%u%n", &major, &minor, &n) < 2)
        return 0;
    v += n;
    n = 0;
    if (sscanf(v, ".%u%n", &patch, &n) == 1)
        v += n;
    uint64_t pre = 0xffff; // a release
    if (sscanf(v, "-beta%u", &beta) == 1 && beta > 0 && beta < 0xffff)
        pre = beta;
    return ((uint64_t)(major & 0xffff) << 48) | ((uint64_t)(minor & 0xffff) << 32) |
           ((uint64_t)(patch & 0xffff) << 16) | pre;
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

// The build of this clock in a manifest's "builds". A debug build takes the release one of its flash
// size when the release has no debug build (releases never do; the same partitions), as an update over
// BLE from the phone does
static cJSON *find_build(cJSON *builds, const char *version)
{
    cJSON *build = cJSON_GetObjectItem(builds, CONFIG_MC_BUILD_VARIANT);
    static const char debug[] = "debug_";
    if (build || strncmp(CONFIG_MC_BUILD_VARIANT, debug, sizeof(debug) - 1))
        return build;
    char release[sizeof(CONFIG_MC_BUILD_VARIANT) + 2];
    snprintf(release, sizeof(release), "release_%s", CONFIG_MC_BUILD_VARIANT + sizeof(debug) - 1);
    build = cJSON_GetObjectItem(builds, release);
    if (build)
        ESP_LOGI(TAG, "Release %s has no build " CONFIG_MC_BUILD_VARIANT ", taking %s", version, release);
    return build;
}

// Downloads and parses the manifest at url into out
static esp_err_t read_manifest(const char *url, release_t *out)
{
    memset(out, 0, sizeof(*out));
    esp_http_client_handle_t client = http_client(url);
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
    int total = 0, n;

    if (!buf)
    {
        res = ESP_ERR_NO_MEM;
        goto exit;
    }
    ESP_LOGI(TAG, "Reading %s", url);
    if ((res = http_open(client, &len)) != ESP_OK)
        goto exit;

    while (total < (int)UPDATER_MANIFEST_MAX_SIZE - 1 &&
           (n = esp_http_client_read(client, buf + total, UPDATER_MANIFEST_MAX_SIZE - 1 - total)) > 0)
        total += n;
    esp_http_client_close(client);

    res = ESP_ERR_INVALID_RESPONSE;
    json = cJSON_Parse(buf);
    version = cJSON_GetStringValue(cJSON_GetObjectItem(json, "version"));
    if (!version || !version_num(version) || strlen(version) >= sizeof(out->version))
    {
        ESP_LOGW(TAG, "Invalid manifest");
        goto exit;
    }
    strcpy(out->version, version);

    build = find_build(cJSON_GetObjectItem(json, "builds"), version);
    if (!build)
    {
        // a custom build: official releases are not for this clock
        ESP_LOGI(TAG, "Release %s has no build " CONFIG_MC_BUILD_VARIANT, version);
        res = ESP_OK;
        goto exit;
    }
    build_url = cJSON_GetStringValue(cJSON_GetObjectItem(build, "url"));
    build_size = cJSON_GetObjectItem(build, "size");
    if (!build_url || strlen(build_url) >= sizeof(out->url) || strncmp(build_url, "https://", 8)
            || !cJSON_IsNumber(build_size) || cJSON_GetNumberValue(build_size) < 1)
    {
        ESP_LOGW(TAG, "Invalid build for " CONFIG_MC_BUILD_VARIANT " in manifest");
        goto exit;
    }

    strcpy(out->url, build_url);
    out->size = (uint32_t)cJSON_GetNumberValue(build_size);
    out->sha_valid = parse_hex(cJSON_GetStringValue(cJSON_GetObjectItem(build, "sha256")), out->sha, sizeof(out->sha));
    out->has_build = true;
    res = ESP_OK;

exit:
    if (res != ESP_OK)
        ESP_LOGW(TAG, "Reading the manifest failed: %d (%s)", res, esp_err_to_name(res));
    cJSON_Delete(json);
    free(buf);
    esp_http_client_cleanup(client);
    return res;
}

// report: also send OTA status notifications (the app asked to install)
static esp_err_t check(bool report_status)
{
    set_state(update_checking);
    if (report_status)
        report(updater_ota_checking, updater_ota_err_none);

    net_settings_t settings;
    net_settings_get(&settings);
    const char *urls[] = { UPDATER_MANIFEST_URL, UPDATER_BETA_MANIFEST_URL };
    size_t count = settings.update_channel == net_update_beta ? 2 : 1;

    // The highest version with a build for this clock; the highest version at all if none has one.
    // A manifest that could not be read is skipped (there may be no beta yet, or no stable release
    // with a manifest): the check fails only when none could be.
    const release_t *latest = nullptr;
    _release = nullptr;
    for (size_t i = 0; i < count; i++)
    {
        if (read_manifest(urls[i], &_releases[i]) != ESP_OK)
            continue;
        uint64_t v = version_num(_releases[i].version);
        if (!latest || v > version_num(latest->version))
            latest = &_releases[i];
        if (_releases[i].has_build && (!_release || v > version_num(_release->version)))
            _release = &_releases[i];
    }
    if (!latest)
    {
        ESP_LOGW(TAG, "Check failed");
        set_state(update_check_failed);
        return ESP_FAIL;
    }

    const release_t *shown = _release ? _release : latest;
    strcpy(_info.version, shown->version);
    _info.checked_at = time(nullptr);

    const char *current = esp_app_get_description()->version;
    bool newer = _release && version_num(_release->version) > version_num(current);
    _newer_without_build = !newer && version_num(latest->version) > version_num(current);
    if (_newer_without_build)
        ESP_LOGW(TAG, "Release %s has no build " CONFIG_MC_BUILD_VARIANT ", update over BLE", latest->version);
    ESP_LOGI(TAG, "Current %s, released %s%s", current, shown->version, newer ? ": update available" : "");
    set_state(newer ? update_available : update_up_to_date);
    return ESP_OK;
}

static esp_err_t install()
{
    const release_t *r = _release;
    ESP_LOGI(TAG, "Installing %s from %s", r->version, r->url);

    esp_http_client_handle_t client = http_client(r->url);
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
    if (!_ota_begin_cb || !_ota_begin_cb(r->size))
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
    report(updater_ota_receiving, updater_ota_err_none, 0, r->size);
    while (received < r->size)
    {
        uint32_t want = r->size - received;
        int n = esp_http_client_read(client, (char *)chunk, want < sizeof(chunk) ? want : sizeof(chunk));
        if (n <= 0)
        {
            ESP_LOGE(TAG, "Download interrupted at %lu of %lu bytes", (unsigned long)received, (unsigned long)r->size);
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
            report(updater_ota_receiving, updater_ota_err_none, received, r->size);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    if (error == updater_ota_err_none && r->sha_valid && memcmp(digest, r->sha, sizeof(digest)))
    {
        ESP_LOGE(TAG, "SHA-256 mismatch");
        error = updater_ota_err_sha;
    }

    if (error != updater_ota_err_none)
    {
        // ota_write() aborts by itself on a flash error
        if (error != updater_ota_err_flash)
            ota_abort();
        report(updater_ota_error, error, received, r->size);
        return ESP_FAIL;
    }

    report(updater_ota_success, updater_ota_err_none, received, r->size);
    // verifies the image, switches the boot partition and reboots
    if (!ota_end())
    {
        report(updater_ota_error, updater_ota_err_flash, received, r->size);
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
        report(updater_ota_no_update, _newer_without_build ? updater_ota_err_no_build : updater_ota_err_none);
        return;
    }
    if (install() != ESP_OK)
        ESP_LOGE(TAG, "Installation failed");
}

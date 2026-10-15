/*
--------------------------------------------------------------------------------
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.
You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the
Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
Boston, MA  02110-1301, USA.
--------------------------------------------------------------------------------
*/

// Copyright (c) 2016 John Seamons, ZL4VO/KF6VO

#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "mem.h"
#include "misc.h"
#include "str.h"
#include "timer.h"
#include "web.h"
#include "cfg.h"
#include "coroutines.h"
#include "net.h"
#include "rx.h"
#include "rx_util.h"
#include "services.h"
#include "peri.h"
#include "sha256.h"
#include "jsmn.h"
#ifdef NATIVE_HARNESS
#include "system.h"
#endif

#include <types.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <set>
#include <string>

static bool update_pending = false, update_task_running = false, update_in_progress = false;
static bool update_restart_required = false;
static time_t last_update_check;
static u4_t next_update_check;
static int pending_maj = -1, pending_min = -1;
static std::string pending_date, pending_filename, pending_changes;
static std::string requested_release_date;

static bool file_auto_download_check = false;
static bool file_auto_download_oneshot = false;

static const long update_download_timeout_s = 120;
static const u4_t update_check_interval_s = 24 * 60 * 60;

static const char* update_root()
{
#ifdef NATIVE_HARNESS
    const char* root = getenv("WEBSDR_UPDATE_ROOT");
    if (root != NULL && root[0] == '/')
        return root;
#endif
    return "/media/mmcblk0p1";
}

static std::string update_dir()
{
    return std::string(update_root()) + "/update";
}

static const char* update_api_base()
{
#ifdef NATIVE_HARNESS
    const char* base = getenv("WEBSDR_UPDATE_API_BASE");
    if (base != NULL && strncmp(base, "http://127.0.0.1:", 17) == 0)
        return base;
#endif
    return "https://www.rx-888.com/api";
}

static bool is_valid_release_url(const std::string& url)
{
#ifdef NATIVE_HARNESS
    const char* base = update_api_base();
    if (strncmp(base, "http://127.0.0.1:", 17) == 0)
        return url.compare(0, strlen(base), base) == 0;
#endif
    return url.compare(0, 8, "https://") == 0;
}

enum fail_reason_e {
    FAIL_NONE = 0,
    FAIL_FS_FULL = 1,
    FAIL_NO_INET = 2,
    FAIL_NO_GITHUB = 3,
    FAIL_GIT = 4,
    FAIL_VERSION = 5,
    FAIL_DOWNLOAD = 6,
    FAIL_CHECKSUM = 7,
    FAIL_INSTALL = 8
};
fail_reason_e fail_reason;

static bool sha256_file(const char* filename, char actual[65]);

static void update_restart_required_refresh()
{
    char running_digest[65], installed_digest[65];
    update_restart_required = sha256_file("/proc/self/exe", running_digest) &&
                              sha256_file((std::string(update_root()) + "/websdr.bin").c_str(), installed_digest) &&
                              strcmp(running_digest, installed_digest) != 0;
}

static void report_result(conn_t* conn) {
    // let admin interface know result
    assert(conn != NULL);
    char* date_m = kiwi_str_encode((char*)__DATE__);
    char* time_m = kiwi_str_encode((char*)__TIME__);
    char* release_date_m = kiwi_str_encode((char*)pending_date.c_str());
    char* release_filename_m = kiwi_str_encode((char*)pending_filename.c_str());
    char* release_changes_m = kiwi_str_encode((char*)pending_changes.c_str());
    send_msg(conn, false, "MSG update_cb="
                          "{\"f\":%d,\"p\":%d,\"i\":%d,\"r\":%d,\"g\":%d,\"rr\":%d,"
                          "\"v1\":%d,\"v2\":%d,\"p1\":%d,\"p2\":%d,\"d\":\"%s\",\"t\":\"%s\","
                          "\"rd\":\"%s\",\"rf\":\"%s\",\"rc\":\"%s\",\"lc\":%lld}",
             fail_reason, update_pending, update_in_progress, rx_chans, gps_chans,
             update_restart_required, version_maj, version_min, pending_maj, pending_min, date_m, time_m,
             release_date_m, release_filename_m, release_changes_m, (long long) last_update_check);
    kiwi_ifree(date_m, "date_m");
    kiwi_ifree(time_m, "time_m");
    kiwi_ifree(release_date_m, "release_date_m");
    kiwi_ifree(release_filename_m, "release_filename_m");
    kiwi_ifree(release_changes_m, "release_changes_m");
}

static void report_progress(conn_t* conn, const char* msg) {
    // let admin interface know result
    assert(conn != NULL);
    char* msg_m = kiwi_str_encode((char*)msg);
    send_msg(conn, false, "MSG update_cb={\"msg\":\"%s\"}\n", msg_m);

    kiwi_ifree(msg_m, "msg_m");
}

typedef struct {
    std::string date, filename, sha256, link, mirror, changes;
    int maj, min;
} release_t;

static int json_skip(const jsmntok_t* tok, int index)
{
    int next = index + 1;
    for (int i = 0; i < tok[index].size; i++)
        next = json_skip(tok, next);
    return next;
}

static bool json_token_eq(const char* json, const jsmntok_t* tok, const char* value)
{
    int len = tok->end - tok->start;
    return tok->type == JSMN_STRING && (int) strlen(value) == len &&
        strncmp(&json[tok->start], value, len) == 0;
}

static int json_object_value(const char* json, const jsmntok_t* tok, int object, const char* key)
{
    if (tok[object].type != JSMN_OBJECT)
        return -1;

    int pos = object + 1;
    for (int i = 0; i < tok[object].size; i++) {
        if (json_token_eq(json, &tok[pos], key))
            return pos + 1;
        pos = json_skip(tok, pos + 1);
    }
    return -1;
}

static bool json_string_value(const char* json, const jsmntok_t* tok, int index, std::string* value)
{
    if (index < 0 || tok[index].type != JSMN_STRING)
        return false;
    value->assign(&json[tok[index].start], tok[index].end - tok[index].start);
    return true;
}

static bool release_date_to_version(const std::string& date, int* maj, int* min)
{
    int year, month, day;
    if (date.size() != 10 || sscanf(date.c_str(), "%4d-%2d-%2d", &year, &month, &day) != 3 ||
        date[4] != '-' || date[7] != '-' || month < 1 || month > 12 || day < 1 || day > 31)
        return false;

    *maj = year;
    *min = month * 100 + day;
    return true;
}

static bool is_hex_sha256(const std::string& sha256)
{
    if (sha256.size() != 64)
        return false;
    for (size_t i = 0; i < sha256.size(); i++) {
        if (!isxdigit((unsigned char) sha256[i]))
            return false;
    }
    return true;
}

static bool is_safe_release_filename(const std::string& filename)
{
    if (filename.size() < 5 || filename.substr(filename.size() - 4) != ".zip")
        return false;
    for (size_t i = 0; i < filename.size(); i++) {
        unsigned char ch = filename[i];
        if (!isalnum(ch) && ch != '.' && ch != '_' && ch != '-')
            return false;
    }
    return true;
}

static bool is_sd_image_release_filename(const std::string& filename)
{
    static const char* prefix = "web-888-alpine-";
    const size_t prefix_len = strlen(prefix);
    const size_t date_len = 8;
    const size_t suffix_len = 4;
    if (filename.compare(0, prefix_len, prefix) != 0 ||
        filename.size() < prefix_len + date_len + suffix_len ||
        filename.compare(filename.size() - suffix_len, suffix_len, ".zip") != 0)
        return false;
    size_t date_start = filename.size() - suffix_len - date_len;
    for (size_t i = date_start; i < date_start + date_len; i++) {
        if (!isdigit((unsigned char) filename[i]))
            return false;
    }
    return true;
}

static bool release_parse(const char* json, release_t* release)
{
    jsmn_parser parser;
    jsmntok_t tok[128];
    jsmn_init(&parser);
    int ntok = jsmn_parse(&parser, json, strlen(json), tok, ARRAY_LEN(tok));
    if (ntok < 1 || tok[0].type != JSMN_OBJECT)
        return false;

    int release_object = json_object_value(json, tok, 0, "release");
    if (release_object < 0)
        release_object = 0;

    int downloads = json_object_value(json, tok, release_object, "downloads");
    if (!json_string_value(json, tok, json_object_value(json, tok, release_object, "date"), &release->date) ||
        downloads < 0 || tok[downloads].type != JSMN_ARRAY || tok[downloads].size < 1)
        return false;

    int download = downloads + 1;
    if (tok[download].type != JSMN_OBJECT ||
        !json_string_value(json, tok, json_object_value(json, tok, download, "filename"), &release->filename) ||
        !json_string_value(json, tok, json_object_value(json, tok, download, "sha256"), &release->sha256) ||
        !json_string_value(json, tok, json_object_value(json, tok, download, "link"), &release->link) ||
        !release_date_to_version(release->date, &release->maj, &release->min) ||
        !is_hex_sha256(release->sha256) ||
        !is_safe_release_filename(release->filename) ||
        !is_valid_release_url(release->link))
        return false;

    int mirror = json_object_value(json, tok, download, "mirror");
    if (mirror >= 0 && (!json_string_value(json, tok, mirror, &release->mirror) ||
                       !is_valid_release_url(release->mirror)))
        return false;

    int changes = json_object_value(json, tok, release_object, "changes");
    if (changes >= 0 && tok[changes].type == JSMN_ARRAY) {
        int pos = changes + 1;
        for (int i = 0; i < tok[changes].size; i++) {
            std::string change;
            if (!json_string_value(json, tok, pos, &change))
                return false;
            if (!release->changes.empty())
                release->changes += "\n";
            release->changes += change;
            pos = json_skip(tok, pos);
        }
    }

    return true;
}

static bool sha256_file(const char* filename, char actual[65])
{
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL)
        return false;

    SHA256_CTX ctx;
    sha256_init(&ctx);
    u1_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) != 0)
        sha256_update(&ctx, buf, n);
    bool ok = !ferror(fp);
    fclose(fp);
    if (!ok)
        return false;

    u1_t digest[32];
    sha256_final(&ctx, digest);
    for (int i = 0; i < 32; i++)
        snprintf(&actual[i * 2], 3, "%02x", digest[i]);
    actual[64] = '\0';
    return true;
}

static bool sha256_file_matches(const char* filename, const std::string& expected)
{
    char actual[65];
    if (!sha256_file(filename, actual))
        return false;
    return strcasecmp(actual, expected.c_str()) == 0;
}

static bool archive_paths_safe(const std::string& archive)
{
    std::string command = "unzip -Z1 " + archive;
    FILE* fp = popen(command.c_str(), "r");
    if (fp == NULL)
        return false;

    char path[PATH_MAX];
    bool ok = true;
    while (fgets(path, sizeof(path), fp) != NULL) {
        path[strcspn(path, "\r\n")] = '\0';
        if (path[0] == '\0' || path[0] == '/' || strstr(path, "\\") != NULL) {
            ok = false;
            break;
        }

        const char* part = path;
        while (*part != '\0') {
            const char* slash = strchr(part, '/');
            size_t len = slash? (size_t) (slash - part) : strlen(part);
            if (len == 0 || (len == 2 && strncmp(part, "..", 2) == 0)) {
                ok = false;
                break;
            }
            if (slash == NULL)
                break;
            part = slash + 1;
        }
        if (!ok)
            break;
    }

    if (pclose(fp) != 0)
        ok = false;
    return ok;
}

static bool download_release(conn_t* conn, bool report, const release_t& release)
{
    std::string archive = update_dir() + "/" + release.filename;
    struct stat st;
    if (stat(archive.c_str(), &st) == 0 && sha256_file_matches(archive.c_str(), release.sha256)) {
        if (report) report_progress(conn, "Using previously downloaded verified release package");
        return true;
    }
    if (stat(archive.c_str(), &st) == 0 || errno != ENOENT)
        unlink(archive.c_str());

    const std::string urls[] = { release.link, release.mirror };
    for (int source = 0; source < ARRAY_LEN(urls); source++) {
        if (urls[source].empty())
            continue;
        for (int attempt = 1; attempt <= 3; attempt++) {
            if (report) {
                char* msg;
                asprintf(&msg, "Downloading %s (%s attempt %d of 3)%s",
                         release.filename.c_str(), source == 0? "primary" : "mirror", attempt,
                         source == 0? "" : " after primary download failures");
                report_progress(conn, msg);
                kiwi_asfree(msg);
            }

            int status = curl_get_file_resume(urls[source].c_str(), archive.c_str(), update_download_timeout_s);
            if (status == 0) {
                if (sha256_file_matches(archive.c_str(), release.sha256))
                    return true;
                lprintf("UPDATE: downloaded ZIP checksum did not match on %s attempt %d\n",
                        source == 0? "primary" : "mirror", attempt);
                unlink(archive.c_str());
            }
            else if (status == -2) {
                lprintf("UPDATE: stored partial ZIP cannot be resumed; restarting transfer\n");
                unlink(archive.c_str());
            }
        }
    }
    return false;
}

static bool update_dir_create()
{
    std::string dir = update_dir();
    if (mkdir(dir.c_str(), 0755) == 0 || errno == EEXIST)
        return true;

    lprintf("UPDATE: unable to create %s: %s\n", dir.c_str(), strerror(errno));
    fail_reason = FAIL_INSTALL;
    return false;
}

static int update_build(conn_t* conn, bool report, const release_t& release)
{
    sd_enable(true);
    int status = EXIT_FAILURE;
    std::string archive = update_dir() + "/" + release.filename;
    std::string sd_image_archive = std::string(update_root()) + "/" + release.filename;
    std::string verify_command, install_command;
    struct stat st;
    bool use_sd_image = is_sd_image_release_filename(release.filename) &&
        stat(sd_image_archive.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
        sha256_file_matches(sd_image_archive.c_str(), release.sha256);
    if (use_sd_image) {
        archive = sd_image_archive;
        if (report) report_progress(conn, "Using verified release package from SD card");
    } else {
        if (!update_dir_create())
            goto exit;

        if (!download_release(conn, report, release)) {
            fail_reason = FAIL_DOWNLOAD;
            goto exit;
        }
    }

    verify_command = "unzip -tq " + archive + " >/dev/null";
    install_command = "unzip -oq " + archive + " -d " + update_root() + " -x 'config/*'";
    if (report) report_progress(conn, "Verifying release archive");
    if (!archive_paths_safe(archive) || system(verify_command.c_str()) != 0) {
        lprintf("UPDATE: release archive is invalid or contains unsafe paths\n");
        fail_reason = FAIL_INSTALL;
        goto exit;
    }
    if (report) report_progress(conn, "Installing release to SD card (preserving config)");
    if (report) report_progress(conn, "Installing release to SD card (preserving config)");
    if (system(install_command.c_str()) != 0) {
        lprintf("UPDATE: unable to extract release archive\n");
        fail_reason = FAIL_INSTALL;
        goto exit;
    }

    update_restart_required_refresh();
    status = EXIT_SUCCESS;
exit:
    sd_enable(false);
    return status;
}

static void _update_task(void* param) {
    conn_t* conn = (conn_t*)FROM_VOID_PARAM(param);
    bool force_check = (conn && conn->update_check == FORCE_CHECK);
    bool force_release = (conn && (conn->update_check == FORCE_RELEASE_BUILD ||
                                   conn->update_check == FORCE_RELEASE_BUILD_REBOOT));
    bool force_build = (conn && (conn->update_check == FORCE_BUILD || conn->update_check == FORCE_BUILD_REBOOT ||
                                 force_release));
    bool force_build_reboot = (conn && (conn->update_check == FORCE_BUILD_REBOOT ||
                                        conn->update_check == FORCE_RELEASE_BUILD_REBOOT));
    bool report = (force_check || force_build);
    bool release_changed, update_download, update_install;
    int status;
    kstr_t* metadata = NULL;
    release_t release;
    std::string metadata_url;
    fail_reason = FAIL_NONE;

    update_in_progress = true;
    last_update_check = time(NULL);

    bool err;
    bool alpha = admcfg_bool("update_channel", &err, CFG_OPTIONAL);
    if (err) alpha = true;

    lprintf("UPDATE: checking for updates\n");
    if (force_check) update_pending = false; // don't let pending status override version reporting when a forced check

    if (report) report_progress(conn, "Getting latest release information");
    if (force_release)
        metadata_url = std::string(update_api_base()) + "/releases/" + requested_release_date;
    else
        metadata_url = std::string(update_api_base()) + "/releases/latest" +
                       (alpha? "" : "?stable=true");
    metadata = curl_get(metadata_url.c_str(), 15, &status);

    if (metadata == NULL || status != 0 || !release_parse(kstr_sp(metadata), &release)) {
        lprintf("UPDATE: failed to get valid latest release information from server (curl status %d)\n", status);
        if (metadata) kstr_free(metadata);
        fail_reason = FAIL_VERSION;
        if (report) report_result(conn);
        goto common_return;
    }

    pending_maj = release.maj;
    pending_min = release.min;
    pending_date = release.date;
    pending_filename = release.filename;
    pending_changes = release.changes;
    release_changed = (pending_maj > version_maj || (pending_maj == version_maj && pending_min > version_min));
    update_download = (admcfg_bool("update_check", NULL, CFG_REQUIRED) == true);
    update_install = (admcfg_bool("update_install", NULL, CFG_REQUIRED) == true);
    kstr_free(metadata);

    {
        if (kiwi_file_exists("/root/config/force_update")) {
            release_changed = true;
            update_install = true;
        }
    }

    if (force_check) {
        update_in_progress = false;
        if (release_changed)
            lprintf("UPDATE: release changed (current %d.%d, new %d.%d), but check only\n",
                    version_maj, version_min, pending_maj, pending_min);
        else
            lprintf("UPDATE: running the most current release\n");

        if (report) report_result(conn);
        goto common_return;
    }
    else

        if (release_changed && !update_install && !force_build) {
        lprintf("UPDATE: release changed (current %d.%d, new %d.%d), automatic install not enabled\n",
                version_maj, version_min, pending_maj, pending_min);

        if (update_download) {
            lprintf("UPDATE: downloading release package for later installation\n");
            sd_enable(true);
            bool downloaded = update_dir_create() && download_release(NULL, false, release);
            sd_enable(false);
            if (!downloaded) {
                if (fail_reason == FAIL_NONE)
                    fail_reason = FAIL_DOWNLOAD;
                lprintf("UPDATE: automatic release download failed\n");
            }
        }
    }
    else

        if (release_changed || force_build) {
        lprintf("UPDATE: installing release, current %d.%d, new %d.%d\n",
                version_maj, version_min, pending_maj, pending_min);

        u4_t build_time = timer_sec();
        status = update_build(conn, report, release);

        if (status) {
            lprintf("UPDATE: installation failed\n");
            if (force_build && report) report_result(conn);
            goto common_return;
        }

        lprintf("UPDATE: build took %d secs\n", timer_sec() - build_time);
        bool reboot = force_build? force_build_reboot : admcfg_bool("update_reboot", NULL, CFG_REQUIRED);
        if (reboot) {
            lprintf("UPDATE: rebooting after successful installation\n");
#ifdef NATIVE_HARNESS
            system_reboot();
#else
            system("sleep 3; reboot");
#endif
        } else {
            lprintf("UPDATE: update installed; reboot is required to activate it\n");
            if (report) {
                update_in_progress = false;
                report_result(conn);
                report_progress(conn, "Update installed. Reboot the receiver to activate it.");
            }
        }
    }
    else {
        lprintf("UPDATE: release %d.%d is current\n", version_maj, version_min);
    }

common_return:
    if (file_auto_download_oneshot) {
        file_auto_download_oneshot = false;
        // printf("file_GET: update check normal\n");
        file_GET(TO_VOID_PARAM(FILE_DOWNLOAD_DIFF_RESTART));
    }

    if (conn) conn->update_check = WAIT_UNTIL_NO_USERS; // restore default
    requested_release_date.clear();
    update_pending = update_task_running = update_in_progress = false;
}

// called at update check TOD, on each user logout in case update is pending or on demand by admin UI
void check_for_update(update_check_e type, conn_t* conn) {
#ifdef NATIVE_HARNESS
    const char* automatic = getenv("WEBSDR_UPDATE_AUTOMATIC");
    if (type == WAIT_UNTIL_NO_USERS && (automatic == NULL || strcmp(automatic, "1") != 0))
        return;
#endif

    bool force = (type != WAIT_UNTIL_NO_USERS);

    if (force) {
        lprintf("UPDATE: force %s by admin\n", (type == FORCE_CHECK) ? "update check" : "build");
        assert(conn != NULL);
        if (update_task_running) {
            lprintf("UPDATE: update task already running\n");
            report_result(conn);
            return;
        }
        else {
            conn->update_check = type;
        }
    }

    if (file_auto_download_check) {
        file_auto_download_oneshot = true;
        file_auto_download_check = false;
    }

    if ((force || (update_pending && rx_count_server_conns(EXTERNAL_ONLY) == 0)) && !update_task_running) {
        update_task_running = true;
        CreateTask(_update_task, TO_VOID_PARAM(conn), ADMIN_PRIORITY);
    }
}

void update_init()
{
    update_restart_required_refresh();
}

void update_start()
{
    next_update_check = timer_sec() + update_check_interval_s;
    update_pending = true;
    check_for_update(WAIT_UNTIL_NO_USERS, NULL);
}

void update_send_status(conn_t* conn)
{
    assert(conn != NULL);
    report_result(conn);
}

void update_send_release_list(conn_t* conn)
{
    assert(conn != NULL);
    int status;
    std::string releases_url = std::string(update_api_base()) + "/releases";
    kstr_t* releases = curl_get(releases_url.c_str(), 20, &status);
    if (releases == NULL || status != 0) {
        if (releases) kstr_free(releases);
        send_msg_encoded(conn, "MSG", "release_list_cb", "%s",
                         "{\"error\":\"Unable to retrieve release list\"}");
        return;
    }

    const char* json = kstr_sp(releases);
    if (json[0] != '{') {
        kstr_free(releases);
        send_msg_encoded(conn, "MSG", "release_list_cb", "%s",
                         "{\"error\":\"Invalid release list response\"}");
        return;
    }

    std::set<std::string> local_releases;
    std::string dir = update_dir();
    DIR* update_dp = opendir(dir.c_str());
    if (update_dp != NULL) {
        struct dirent* entry;
        while ((entry = readdir(update_dp)) != NULL) {
            std::string filename = entry->d_name;
            if (!is_safe_release_filename(filename))
                continue;
            std::string path = dir + "/" + filename;
            struct stat st;
            if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
                continue;
            local_releases.insert(filename);
        }
        closedir(update_dp);
    }

    DIR* sd_dp = opendir(update_root());
    if (sd_dp != NULL) {
        struct dirent* entry;
        while ((entry = readdir(sd_dp)) != NULL) {
            std::string filename = entry->d_name;
            if (!is_sd_image_release_filename(filename))
                continue;
            std::string path = std::string(update_root()) + "/" + filename;
            struct stat st;
            if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
                continue;
            local_releases.insert(filename);
        }
        closedir(sd_dp);
    }

    std::string response = "{\"local\":[";
    bool first = true;
    for (std::set<std::string>::const_iterator it = local_releases.begin();
            it != local_releases.end(); ++it) {
        if (!first)
            response += ",";
        response += "\"" + *it + "\"";
        first = false;
    }
    response += "],";
    response += json + 1;

    send_msg_encoded(conn, "MSG", "release_list_cb", "%s", response.c_str());
    kstr_free(releases);
}

void update_install_release(const char* date, bool reboot, conn_t* conn)
{
    assert(conn != NULL);
    if (strlen(date) != 8) {
        lprintf("UPDATE: invalid requested release date\n");
        report_progress(conn, "Invalid release selection.");
        return;
    }
    for (const char* p = date; *p != '\0'; p++) {
        if (!isdigit((unsigned char) *p)) {
            lprintf("UPDATE: invalid requested release date\n");
            report_progress(conn, "Invalid release selection.");
            return;
        }
    }

    requested_release_date = date;
    check_for_update(reboot? FORCE_RELEASE_BUILD_REBOOT : FORCE_RELEASE_BUILD, conn);
}

// called at the top of each minute
void schedule_update(int min) {
    (void) min;
    u4_t now = timer_sec();
    if (next_update_check == 0 || now < next_update_check)
        return;

    lprintf("UPDATE: 24-hour check scheduled\n");
    next_update_check = now + update_check_interval_s;
    update_pending = true;
    check_for_update(WAIT_UNTIL_NO_USERS, NULL);
}

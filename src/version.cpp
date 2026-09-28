

#include "mod.hpp"
#include "print.hpp"

#include "mods/service.hpp"
#include "mods/svc/http.h"

#include <cstdio>
#include <cstdlib>
#include <string>

IMPORT_OPTIONAL_SERVICE(HttpService, svc_http);

#ifndef COOP_MOD_VERSION
#define COOP_MOD_VERSION "0.0.0"
#endif

namespace {

const char* const kLatestUrl =
    "https://api.github.com/repos/remiafterdark/crests-of-courage/releases/latest";
const uint32_t kCheckAfterTicks = 300;
const uint32_t kRemindGapTicks = 30 * 20;

uint32_t s_tick = 0;
bool s_asked = false;
bool s_outdated = false;
std::string s_latest;
uint32_t s_lastReminder = 0;
bool s_remindedOnce = false;

void parse_version(const std::string& text, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    size_t i = 0;
    while (i < text.size() && (text[i] < '0' || text[i] > '9')) ++i;
    for (int part = 0; part < 3 && i < text.size(); ++part) {
        out[part] = std::atoi(text.c_str() + i);
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
        if (i < text.size() && text[i] == '.') ++i;
        else break;
    }
}

bool newer(const std::string& theirs, const std::string& ours) {
    int a[3], b[3];
    parse_version(theirs, a);
    parse_version(ours, b);
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

std::string tag_name(const char* body, size_t size) {
    const std::string json(body, size);
    const size_t key = json.find("\"tag_name\"");
    if (key == std::string::npos) return {};
    const size_t open = json.find('"', json.find(':', key) + 1);
    if (open == std::string::npos) return {};
    const size_t close = json.find('"', open + 1);
    if (close == std::string::npos) return {};
    return json.substr(open + 1, close - open - 1);
}

std::string versions_line() {
    return "v" + std::string(s_latest[0] == 'v' ? s_latest.substr(1) : s_latest) + " is out - you have v" +
           COOP_MOD_VERSION + ".";
}

void on_latest(ModContext*, HttpRequestHandle, const HttpResult* result, void*) {
    if (result == nullptr || result->error != HTTP_ERROR_NONE || result->status_code != 200 ||
        result->body == nullptr) {
        coop_log::info("coop_mod: [VERSION] could not check for a newer version (error {} status {})",
            result != nullptr ? static_cast<int>(result->error) : -1,
            result != nullptr ? result->status_code : 0);
        return;
    }
    const std::string tag = tag_name(static_cast<const char*>(result->body), result->body_size);
    if (tag.empty()) return;
    s_latest = tag;
    s_outdated = newer(tag, COOP_MOD_VERSION);
    coop_log::info("coop_mod: [VERSION] this is v{}, the latest release is {}{}", COOP_MOD_VERSION,
        tag, s_outdated ? " - OUTDATED" : "");
    if (s_outdated) {
        coop_notify_c(kNotifyOther, "Crests of Courage update available", versions_line().c_str());
    }
}

void ask() {
    if (svc_http == nullptr) {
        coop_log::info("coop_mod: [VERSION] no HTTP service - not checking for updates");
        return;
    }

    static const HttpHeader kHeaders[] = {
        {"Accept", "application/vnd.github+json"},
    };
    HttpRequestDesc desc = HTTP_REQUEST_DESC_INIT;
    desc.url = kLatestUrl;
    desc.headers = kHeaders;
    desc.header_count = 1;
    desc.total_timeout_ms = 15000;
    desc.max_body_bytes = 256 * 1024;
    HttpRequestHandle handle = 0;
    const ModResult started = svc_http->request(mod_ctx, &desc, on_latest, nullptr, &handle);
    if (started != MOD_OK) {
        coop_log::warn("coop_mod: [VERSION] could not start the update check ({})",
            static_cast<int>(started));
    }
}

}

void version_update() {
    ++s_tick;
    if (!s_asked && s_tick >= kCheckAfterTicks) {
        s_asked = true;
        ask();
    }
}

void version_remind() {
    if (!s_outdated) return;
    if (s_remindedOnce && s_tick - s_lastReminder < kRemindGapTicks) return;
    s_remindedOnce = true;
    s_lastReminder = s_tick;
    coop_notify_c(kNotifyOther, "Crests of Courage is outdated", versions_line().c_str());
}

bool version_outdated() { return s_outdated; }

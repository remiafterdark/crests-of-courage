

#include "mod.hpp"
#include "net/messages.hpp"
#include "print.hpp"
#include "util.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/host.h"
#include "mods/svc/http.h"
#include "mods/svc/ui.h"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif
#if defined(__APPLE__) || defined(__linux__)
#include <sys/utsname.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

extern const ConfigService* svc_config;
extern const HostService* svc_host;

IMPORT_OPTIONAL_SERVICE(HttpService, svc_http);
extern const UiService* svc_ui;

namespace {

const char* const kReportUrl = "https://crests-rooms.crestsofcourage.workers.dev/report";

std::mutex s_logMutex;
std::deque<std::string> s_lines;
size_t s_lineBytes = 0;
const size_t kKeepBytes = 400u * 1024u;
std::string s_unwritten;
const auto s_start = std::chrono::steady_clock::now();

std::filesystem::path log_dir() {
    const char* dir = nullptr;
    if (svc_host == nullptr || svc_host->data_dir(mod_ctx, &dir) != MOD_OK || dir == nullptr) return {};
    return std::filesystem::path(dir);
}

std::string current_log() {
    std::lock_guard lock(s_logMutex);
    std::string out;
    out.reserve(s_lineBytes);
    for (const std::string& line : s_lines) out += line;
    return out;
}

std::string previous_log(int back = 1, size_t keep = kKeepBytes) {
    std::ifstream in(log_dir() / ("coop-log-" + std::to_string(back) + ".txt"), std::ios::binary);
    if (!in) return "(no log from that run)\n";
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (all.size() > keep) all.erase(0, all.size() - keep);
    return all;
}

std::string crash_trail_tail(size_t keep = 16u * 1024u) {
    std::ifstream in("coop-crash-trail.txt", std::ios::binary);
    if (!in) return "(no crash trail)\n";
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (all.size() > keep) {
        all.erase(0, all.size() - keep);
        const size_t nl = all.find('\n');
        if (nl != std::string::npos) all.erase(0, nl + 1);
    }
    return all;
}

std::string engine_log_before(size_t keep = 48u * 1024u) {
    const std::filesystem::path data = log_dir();
    if (data.empty()) return "(no engine log)\n";
    std::error_code ec;
    const std::filesystem::path logs = data.parent_path().parent_path() / "logs";
    std::vector<std::filesystem::path> runs;
    for (const auto& e : std::filesystem::directory_iterator(logs, ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("dusklight-", 0) == 0 && e.path().extension() == ".log") runs.push_back(e.path());
    }
    if (runs.size() < 2) return "(no engine log from that run)\n";
    std::sort(runs.begin(), runs.end());
    std::ifstream in(runs[runs.size() - 2], std::ios::binary);
    if (!in) return "(no engine log from that run)\n";
    std::string out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("Loading Resource") != std::string::npos) continue;
        out += line;
        out += '\n';
        if (out.size() > 4 * keep) out.erase(0, out.size() - keep);
    }
    if (out.size() > keep) out.erase(0, out.size() - keep);
    return out;
}

std::ofstream s_file;
size_t s_fileBytes = 0;
const size_t kFileCap = 2u * 1024u * 1024u;

void flush_to_disk() {
    std::string chunk;
    {
        std::lock_guard lock(s_logMutex);
        if (s_unwritten.empty()) return;
        chunk.swap(s_unwritten);
    }
    const std::filesystem::path dir = log_dir();
    if (dir.empty()) return;
    if (s_file.is_open() && s_fileBytes > kFileCap) s_file.close();
    if (!s_file.is_open()) {
        const bool first = s_fileBytes == 0;
        s_file.open(dir / "coop-log.txt", std::ios::binary | std::ios::trunc);
        if (!s_file) return;
        s_fileBytes = 0;
        if (!first) chunk = current_log();
    }
    s_file << chunk;
    s_file.flush();
    s_fileBytes += chunk.size();
}

std::string platform_name() {
#if defined(_WIN32)
    return "windows";
#elif defined(__ANDROID__)
    return "android";
#elif defined(__APPLE__) && TARGET_OS_IOS
    return "ios";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

std::string device_name() {
#if defined(__ANDROID__)
    char maker[PROP_VALUE_MAX] = {}, model[PROP_VALUE_MAX] = {}, release[PROP_VALUE_MAX] = {};
    __system_property_get("ro.product.manufacturer", maker);
    __system_property_get("ro.product.model", model);
    __system_property_get("ro.build.version.release", release);
    return std::string(maker) + " " + model + ", Android " + release;
#elif defined(__APPLE__) || defined(__linux__)
    struct utsname u {};
    if (uname(&u) != 0) return "";
    std::string out = std::string(u.machine) + ", " + u.sysname + " " + u.release;
#if defined(__linux__)
    std::ifstream os("/etc/os-release");
    std::string line;
    while (std::getline(os, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            std::string name = line.substr(12);
            if (name.size() >= 2 && name.front() == '"') name = name.substr(1, name.size() - 2);
            out += " (" + name + ")";
            break;
        }
    }
#endif
    return out;
#else
    return "";
#endif
}

std::string session_summary() {
    std::string s;
    s += "role: ";
    s += !coop_net_connected() ? "not connected" : coop_net_is_host() ? "host" : "joiner";
    s += "\nplayer id: " + std::to_string(coop_net_local_id());
    const std::string code = online_room_code();
    if (!code.empty()) s += "\nroom code: yes";
    const char* stage = dComIfGp_getStartStageName();
    s += "\nstage: ";
    s += (stage != nullptr && stage[0] != '\0') ? stage : "(none)";
    if (daAlink_getAlinkActorClass() != nullptr) {
        s += " room " + std::to_string(dComIfGp_roomControl_getStayNo());
    }
    s += "\nrandomizer: ";
    s += rando_active() ? "yes" : "no";
    s += "\nmemory: " + coop_mem_status();

    s += "\nmodels:";
    bool anyModel = false;
    for (int slot = 0; slot < kSkinChoiceCount; ++slot) {
        const std::string chosen = skins_local_slot(slot);
        if (chosen.empty()) continue;
        s += std::string(anyModel ? ", " : " ") + skins_slot_label(slot) + "=" + chosen;
        anyModel = true;
    }
    if (!anyModel) s += " (all the game's own)";
    s += "\nplayers:";
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const uint8_t id = static_cast<uint8_t>(i);
        if (id == coop_net_local_id() || !coop_net_player_present(id)) continue;
        const CoopPeer& p = features_peer_of(id);
        char line[160];
        std::snprintf(line, sizeof(line), "\n  %d '%s' stage=%.8s inGame=%d%s", i, p.name.c_str(),
            p.stage, p.inGame ? 1 : 0, coop_player_unheard(id) ? " NO SIGNAL" : "");
        s += line;
    }
    return s;
}

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + in.size() / 16 + 2);
    for (unsigned char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

std::string scrub_addresses(std::string text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {

        size_t j = i;
        int parts = 0;
        while (parts < 4) {
            size_t k = j;
            while (k < text.size() && k - j < 4 && text[k] >= '0' && text[k] <= '9') ++k;
            if (k == j || k - j > 3) break;
            ++parts;
            j = k;
            if (parts < 4) {
                if (j < text.size() && text[j] == '.') ++j;
                else break;
            }
        }
        const bool boundary = i == 0 || !(text[i - 1] >= '0' && text[i - 1] <= '9');
        if (parts == 4 && boundary) {
            out += "x.x.x.x";
            i = j;
        } else {
            out.push_back(text[i++]);
        }
    }
    return out;
}

std::string build_body(const char* kind, const std::string& code, const std::string& text,
    const std::string& when, const std::string& log) {
    std::string b = "{";
    b += "\"kind\":\"" + std::string(kind) + "\"";
    if (!code.empty()) b += ",\"code\":\"" + json_escape(code) + "\"";
    b += ",\"player\":\"" + json_escape(features_local_name()) + "\"";
    b += ",\"platform\":\"" + platform_name() + "\"";
    b += ",\"device\":\"" + json_escape(device_name()) + "\"";
    b += ",\"mod\":\"" COOP_MOD_VERSION "\"";
    b += ",\"when\":\"" + json_escape(when) + "\"";
    b += ",\"text\":\"" + json_escape(text) + "\"";
    b += ",\"session\":\"" + json_escape(scrub_addresses(session_summary())) + "\"";
    b += ",\"log\":\"" + json_escape(scrub_addresses(log)) + "\"";
    b += "}";
    return b;
}

std::string s_text;
int s_when = 0;
bool s_sending = false;
std::string s_status = "";
uint32_t s_sentAtTick = 0;
uint32_t s_tick = 0;
const char* const kWhen[] = {"Just now", "Earlier this session", "The game crashed or closed last time"};

std::string s_answered[8];
int s_answeredNext = 0;

std::string code_from(const HttpResult* r) {
    if (r == nullptr || r->body == nullptr) return "";
    const std::string body(static_cast<const char*>(r->body), r->body_size);
    const size_t at = body.find("\"code\":\"");
    if (at == std::string::npos) return "";
    const size_t start = at + 8;
    const size_t end = body.find('"', start);
    if (end == std::string::npos || end - start > 11) return "";
    return body.substr(start, end - start);
}

bool post(const std::string& body, HttpCompleteFn done) {
    if (svc_http == nullptr) return false;
    static const HttpHeader kHeaders[] = {{"Content-Type", "application/json"}};
    HttpRequestDesc desc = HTTP_REQUEST_DESC_INIT;
    desc.method = HTTP_METHOD_POST;
    desc.url = kReportUrl;
    desc.headers = kHeaders;
    desc.header_count = 1;
    desc.body = body.data();
    desc.body_size = body.size();
    desc.total_timeout_ms = 60000;
    desc.max_body_bytes = 64 * 1024;
    HttpRequestHandle handle = 0;
    return svc_http->request(mod_ctx, &desc, done, nullptr, &handle) == MOD_OK;
}

void on_report_sent(ModContext*, HttpRequestHandle, const HttpResult* r, void*) {
    s_sending = false;
    const std::string code = code_from(r);
    if (r == nullptr || r->error != HTTP_ERROR_NONE || r->status_code != 200 || code.empty()) {
        const int status = r != nullptr ? r->status_code : 0;
        coop_log::warn("coop_mod: [REPORT] not sent (error {}, status {})",
            r != nullptr ? static_cast<int>(r->error) : -1, status);
        s_status = status == 429 ? "Too many reports - try again in a few minutes."
                                 : "Couldn't send it. Check your connection and try again.";
        return;
    }
    coop_log::info("coop_mod: [REPORT] sent as {}", code);

    const bool copied = svc_ui != nullptr && SERVICE_HAS(svc_ui, UiService, set_clipboard_text) &&
                        svc_ui->set_clipboard_text != nullptr &&
                        svc_ui->set_clipboard_text(mod_ctx, code.c_str()) == MOD_OK;
    s_status = "Sent! Your report code is " + code +
               (copied ? " (copied) - paste it in the Discord thread."
                       : " - mention it in the Discord thread.");
    s_text.clear();
    coop_notify_c(kNotifyOther, "Bug report sent",
        ("Code " + code + (copied ? " is copied - paste it in the Discord thread."
                                  : " - mention it in the Discord thread.")).c_str());

    if (coop_net_connected()) {
        MsgLogRequest req{};
        std::strncpy(req.code, code.c_str(), sizeof(req.code) - 1);
        coop_net_send(kMsgLogRequest, &req, sizeof(req));
    }
}

void on_log_sent(ModContext*, HttpRequestHandle, const HttpResult* r, void*) {
    if (r == nullptr || r->error != HTTP_ERROR_NONE || r->status_code != 200) {
        coop_log::warn("coop_mod: [REPORT] log for peer report not sent");
    }
}

void send_report() {
    if (s_sending || s_text.empty()) return;
    flush_to_disk();
    const bool lastRun = s_when == 2;

    const std::string thisRun = current_log();
    const std::string lastRunLog = previous_log();
    const std::string before = "=== the run before this one ===\n" + lastRunLog;
    const std::string now = "=== this run ===\n" + thisRun;
    std::string log = lastRun ? before + "\n" + now : now + "\n" + before;

    if (lastRun) log += "\n=== two runs before this one ===\n" + previous_log(2, 250u * 1024u);

    log += "\n=== crash trail, last lines ===\n" + crash_trail_tail();
    log += "\n=== dusklight's own log, the run before ===\n" +
           (lastRun ? engine_log_before() : engine_log_before(16u * 1024u));
    const std::string body = build_body("report", "", s_text, kWhen[s_when], log);
    if (!post(body, on_report_sent)) {
        s_status = "Couldn't send it - this build has no network access.";
        return;
    }
    s_sending = true;
    s_sentAtTick = s_tick;
    s_status = "Sending...";
    coop_log::info("coop_mod: [REPORT] sending ({} KB)", body.size() / 1024);
}

UiElementHandle s_statusElem = 0;
std::string s_statusShown;

}

void coop_log_capture(char level, const std::string& message) {
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_start).count();
    char head[24];
    std::snprintf(head, sizeof(head), "[%8.1f] %c ", secs, level);
    std::string line = head;
    line += message;
    line += '\n';
    std::lock_guard lock(s_logMutex);
    s_lineBytes += line.size();
    s_unwritten += line;
    s_lines.push_back(std::move(line));
    while (s_lineBytes > kKeepBytes && !s_lines.empty()) {
        s_lineBytes -= s_lines.front().size();
        s_lines.pop_front();
    }
    if (s_unwritten.size() > kKeepBytes) s_unwritten.erase(0, s_unwritten.size() - kKeepBytes);
}

const int kKeepRuns = 3;

void report_init() {

    const std::filesystem::path dir = log_dir();
    if (dir.empty()) return;
    std::error_code ec;
    const auto run = [&](int n) { return dir / ("coop-log-" + std::to_string(n) + ".txt"); };
    std::filesystem::remove(run(kKeepRuns), ec);
    for (int n = kKeepRuns - 1; n >= 1; --n) std::filesystem::rename(run(n), run(n + 1), ec);
    std::filesystem::rename(dir / "coop-log.txt", run(1), ec);
    std::filesystem::remove(dir / "coop-log-previous.txt", ec);
}

void report_update() {
    ++s_tick;

    static char s_stage[8] = {};
    const char* stage = dComIfGp_getStartStageName();
    const bool newStage = stage != nullptr && std::strncmp(stage, s_stage, sizeof(s_stage)) != 0;
    if (newStage) std::strncpy(s_stage, stage, sizeof(s_stage));
    if (newStage || s_tick % 300 == 0) flush_to_disk();
}

void report_on_message(const uint8_t* payload, size_t size, uint8_t from) {
    if (size < sizeof(MsgLogRequest) || from == coop_net_local_id()) return;
    MsgLogRequest req;
    std::memcpy(&req, payload, sizeof(req));
    req.code[sizeof(req.code) - 1] = '\0';
    const std::string code(req.code);
    if (code.size() < 3) return;
    for (const std::string& done : s_answered) {
        if (done == code) return;
    }
    s_answered[s_answeredNext] = code;
    s_answeredNext = (s_answeredNext + 1) % 8;
    flush_to_disk();
    const std::string asker = features_peer_of(from).name;
    const std::string body = build_body("log", code, "log for " + asker + "'s report", "", current_log());
    if (post(body, on_log_sent)) {
        coop_log::info("coop_mod: [REPORT] sending log for {} report {}", asker, code);
    }
}

void report_build_tab(UiElementHandle left, UiElementHandle right) {
    s_statusElem = 0;
    s_statusShown.clear();
    svc_ui->pane_add_section(mod_ctx, left, "Report a bug");

    UiControlDesc text = UI_CONTROL_DESC_INIT;
    text.kind = UI_CONTROL_STRING;
    text.label = "What happened";
    text.binding = UI_BINDING_CALLBACKS;
    text.max_length = 1500;
    text.string_set_mode = UI_STRING_SET_ON_CHANGE;
    text.get = [](ModContext*, void*, UiControlValue* out) { out->string_value = s_text.c_str(); };
    text.set = [](ModContext*, void*, const UiControlValue* v) {
        s_text = v->string_value != nullptr ? v->string_value : "";
    };
    text.help_rml = "What you were doing, what you expected, and what happened instead.";
    svc_ui->pane_add_control(mod_ctx, left, &text, nullptr);

    UiControlDesc when = UI_CONTROL_DESC_INIT;
    when.kind = UI_CONTROL_SELECT;
    when.label = "When";
    when.binding = UI_BINDING_CALLBACKS;
    when.options = kWhen;
    when.option_count = 3;
    when.get = [](ModContext*, void*, UiControlValue* out) { out->int_value = s_when; };
    when.set = [](ModContext*, void*, const UiControlValue* v) {
        s_when = static_cast<int>(v->int_value >= 0 && v->int_value < 3 ? v->int_value : 0);
    };
    when.help_rml = "If the game crashed, pick the last one - it sends the log from before the crash.";
    svc_ui->pane_add_control(mod_ctx, left, &when, nullptr);

    UiControlDesc send = UI_CONTROL_DESC_INIT;
    send.kind = UI_CONTROL_BUTTON;
    send.label = "Send report";
    send.on_pressed = [](ModContext*, void*) { send_report(); };
    send.is_disabled = [](ModContext*, void*) { return s_sending || s_text.empty(); };
    svc_ui->pane_add_control(mod_ctx, left, &send, nullptr);

    svc_ui->pane_add_text(mod_ctx, left, s_status.c_str(), &s_statusElem);
    s_statusShown = s_status;

    svc_ui->pane_add_text(mod_ctx, right,
        "Sent with your report:\n"
        "- your device and versions\n"
        "- who you're playing with and where you are\n"
        "- the co-op mod's log\n\n"
        "Everyone you're connected to sends their co-op log too, so we can see both sides.\n\n"
        "Network addresses are removed.",
        nullptr);
}

void report_update_tab() {
    if (s_statusElem == 0 || s_statusShown == s_status) return;
    s_statusShown = s_status;
    svc_ui->elem_set_text(mod_ctx, s_statusElem, s_status.c_str());
}

ConfigVarHandle s_hintVar = 0;
bool s_hintArmed = false;
uint32_t s_hintTicks = 0;

void report_hint_arm() {
    s_hintArmed = true;
}

void report_register_vars() {
    if (svc_config == nullptr) return;
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = "report_hint_shown";
    desc.type = CONFIG_VAR_BOOL;
    desc.default_bool = false;
    if (svc_config->register_var(mod_ctx, &desc, &s_hintVar) != MOD_OK) s_hintVar = 0;
}

void report_hint_update() {
    if (!s_hintArmed || s_hintVar == 0 || cfg_bool(s_hintVar, false)) return;
    if (++s_hintTicks < 120) return;
    coop_notify_c(kNotifyOther, "Found a bug?", "Report bugs in CO-OP > Report Bug");
    svc_config->set_bool(mod_ctx, s_hintVar, true);
}



#include "mod.hpp"
#include "print.hpp"

#include "mods/svc/net.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

const char* const kSsdpEndpoint = "udp://239.255.255.250:1900";
const int kSearchTicks = 180;
const int kHttpTicks = 300;
const int kLeaseSeconds = 3600;

const int kRenewTicks = 30 * 60 * 45;

enum class Stage {
    Idle,
    Searching,
    Describing,
    MappingTcp,
    MappingUdp,
    Asking,
    Ready,
    Failed,
};

Stage s_stage = Stage::Idle;
std::string s_status;
std::string s_external;
std::string s_controlUrl;
std::string s_serviceType;
std::string s_descUrl;
std::string s_rxBuffer;
int s_port = 0;
int s_ticks = 0;
int s_renewTicks = 0;
mods::net::Socket s_ssdp;
mods::net::Socket s_http;

void fail(const std::string& why) {
    s_stage = Stage::Failed;
    s_status = why;
    s_ssdp.close();
    s_http.close();
    coop_log::info("coop_mod: [UPNP] {}", why);
}

bool header_value(const std::string& text, const char* name, std::string* out) {
    const size_t nameLen = std::strlen(name);
    for (size_t i = 0; i + nameLen < text.size(); ++i) {
        if (i != 0 && text[i - 1] != '\n') continue;
        bool match = true;
        for (size_t j = 0; j < nameLen; ++j) {
            const char a = static_cast<char>(std::tolower(text[i + j]));
            const char b = static_cast<char>(std::tolower(name[j]));
            if (a != b) { match = false; break; }
        }
        if (!match) continue;
        size_t v = i + nameLen;
        while (v < text.size() && (text[v] == ' ' || text[v] == ':')) ++v;
        size_t end = v;
        while (end < text.size() && text[end] != '\r' && text[end] != '\n') ++end;
        *out = text.substr(v, end - v);
        return true;
    }
    return false;
}

bool split_url(const std::string& url, std::string* hostPort, std::string* path) {
    const std::string prefix = "http://";
    if (url.compare(0, prefix.size(), prefix) != 0) return false;
    const size_t slash = url.find('/', prefix.size());
    if (slash == std::string::npos) {
        *hostPort = url.substr(prefix.size());
        *path = "/";
    } else {
        *hostPort = url.substr(prefix.size(), slash - prefix.size());
        *path = url.substr(slash);
    }
    if (hostPort->find(':') == std::string::npos) *hostPort += ":80";
    return !hostPort->empty();
}

bool xml_value(const std::string& xml, const char* tag, std::string* out, size_t from = 0) {
    const std::string open = std::string("<") + tag + ">";
    const std::string close = std::string("</") + tag + ">";
    const size_t a = xml.find(open, from);
    if (a == std::string::npos) return false;
    const size_t b = xml.find(close, a);
    if (b == std::string::npos) return false;
    *out = xml.substr(a + open.size(), b - a - open.size());
    return true;
}

bool send_http(const std::string& url, const std::string& soapAction, const std::string& body) {
    std::string hostPort;
    std::string path;
    if (!split_url(url, &hostPort, &path)) {
        fail("the router gave an address we could not read");
        return false;
    }
    s_http.close();
    s_rxBuffer.clear();
    s_http = mods::net::connect("tcp://" + hostPort);
    if (!s_http) {
        fail("could not reach the router");
        return false;
    }

    std::string request;
    if (soapAction.empty()) {
        request = "GET " + path + " HTTP/1.1\r\n";
    } else {
        request = "POST " + path + " HTTP/1.1\r\n";
    }
    request += "HOST: " + hostPort + "\r\n";
    request += "CONNECTION: close\r\n";
    if (!soapAction.empty()) {
        request += "CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n";
        request += "SOAPACTION: \"" + soapAction + "\"\r\n";
        request += "CONTENT-LENGTH: " + std::to_string(body.size()) + "\r\n";
    }
    request += "\r\n";
    request += body;
    s_http.send({reinterpret_cast<const std::byte*>(request.data()), request.size()});
    s_ticks = kHttpTicks;
    return true;
}

std::string soap_envelope(const std::string& action, const std::string& args) {
    return "<?xml version=\"1.0\"?>"
           "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
           "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
           "<u:" + action + " xmlns:u=\"" + s_serviceType + "\">" + args +
           "</u:" + action + "></s:Body></s:Envelope>";
}

void ask_for_mapping(const char* protocol) {
    const std::string port = std::to_string(s_port);
    const std::string args =
        "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>" + port + "</NewExternalPort>"
        "<NewProtocol>" + protocol + "</NewProtocol>"
        "<NewInternalPort>" + port + "</NewInternalPort>"
        "<NewInternalClient></NewInternalClient>"
        "<NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>Crests of Courage</NewPortMappingDescription>"
        "<NewLeaseDuration>" + std::to_string(kLeaseSeconds) + "</NewLeaseDuration>";
    s_stage = (std::strcmp(protocol, "TCP") == 0) ? Stage::MappingTcp : Stage::MappingUdp;
    coop_log::info("coop_mod: [UPNP] asking for {} port {}", protocol, s_port);
    send_http(s_controlUrl, s_serviceType + "#AddPortMapping",
              soap_envelope("AddPortMapping", args));
}

}

void upnp_begin(int port) {
    if (s_stage == Stage::Searching || s_stage == Stage::Describing) return;
    s_port = port;
    s_external.clear();
    s_controlUrl.clear();
    s_stage = Stage::Searching;
    s_status = "Asking your router to open the port...";

    s_ssdp = mods::net::open_datagram("udp://0.0.0.0:0");
    if (!s_ssdp) {
        fail("could not start looking for your router");
        return;
    }

    const std::string search =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";
    s_ssdp.send_to(kSsdpEndpoint,
                   {reinterpret_cast<const std::byte*>(search.data()), search.size()});
    s_ticks = kSearchTicks;
    coop_log::info("coop_mod: [UPNP] looking for a gateway on the network");
}

void upnp_release() {

    if (s_stage == Stage::Ready && !s_controlUrl.empty()) {
        const std::string args =
            "<NewRemoteHost></NewRemoteHost>"
            "<NewExternalPort>" + std::to_string(s_port) + "</NewExternalPort>"
            "<NewProtocol>TCP</NewProtocol>";
        send_http(s_controlUrl, s_serviceType + "#DeletePortMapping",
                  soap_envelope("DeletePortMapping", args));
    }
    s_ssdp.close();
    s_stage = Stage::Idle;
    s_status.clear();
    s_external.clear();
}

bool upnp_ready() {
    return s_stage == Stage::Ready;
}

std::string upnp_external_address() {
    if (s_external.empty()) return "";
    return s_external + ":" + std::to_string(s_port);
}

std::string upnp_status() {
    return s_status;
}

void upnp_update() {
    if (s_renewTicks > 0 && --s_renewTicks == 0 && s_stage == Stage::Ready) {
        coop_log::info("coop_mod: [UPNP] renewing the mapping before its lease runs out");
        ask_for_mapping("TCP");
    }
    if (s_ticks > 0 && --s_ticks == 0) {
        switch (s_stage) {
        case Stage::Searching:
            fail("no router here answered. Open the port yourself, or use Tailscale.");
            break;
        case Stage::Describing:
        case Stage::MappingTcp:
        case Stage::MappingUdp:
        case Stage::Asking:
            fail("your router stopped answering partway through.");
            break;
        default:
            break;
        }
    }
}

bool upnp_owns(NetHandle handle) {
    return (s_ssdp && handle == s_ssdp.handle()) || (s_http && handle == s_http.handle());
}

void upnp_on_net_event(const mods::net::Event& event) {
    if (s_ssdp && event.handle == s_ssdp.handle()) {
        if (event.type != NET_EVENT_DATAGRAM || s_stage != Stage::Searching) return;
        const std::string reply(reinterpret_cast<const char*>(event.data.data()), event.data.size());
        std::string location;
        if (!header_value(reply, "location", &location)) return;

        s_ssdp.close();
        s_descUrl = location;
        s_stage = Stage::Describing;
        coop_log::info("coop_mod: [UPNP] a gateway answered: {}", location);
        send_http(location, "", "");
        return;
    }

    if (!s_http || event.handle != s_http.handle()) return;

    if (event.type == NET_EVENT_STREAM_DATA) {
        s_rxBuffer.append(reinterpret_cast<const char*>(event.data.data()), event.data.size());
        return;
    }
    if (event.type != NET_EVENT_CLOSED && event.type != NET_EVENT_DROPPED) return;

    const std::string body = s_rxBuffer;
    s_http.close();
    s_rxBuffer.clear();

    switch (s_stage) {
    case Stage::Describing: {

        static const char* const kWanted[] = {
            "urn:schemas-upnp-org:service:WANIPConnection:1",
            "urn:schemas-upnp-org:service:WANIPConnection:2",
            "urn:schemas-upnp-org:service:WANPPPConnection:1",
        };
        std::string control;
        for (const char* want : kWanted) {
            const size_t at = body.find(want);
            if (at == std::string::npos) continue;
            if (!xml_value(body, "controlURL", &control, at)) continue;
            s_serviceType = want;
            break;
        }
        if (control.empty()) {
            fail("your router does not offer to open ports.");
            return;
        }
        if (control.compare(0, 7, "http://") != 0) {
            std::string hostPort;
            std::string path;
            split_url(s_descUrl, &hostPort, &path);
            if (!control.empty() && control[0] != '/') control = "/" + control;
            control = "http://" + hostPort + control;
        }
        s_controlUrl = control;

        ask_for_mapping("TCP");
        return;
    }
    case Stage::MappingTcp: {
        if (body.find("AddPortMappingResponse") == std::string::npos &&
            body.find("200 OK") == std::string::npos) {
            fail("your router refused to open the port.");
            return;
        }
        ask_for_mapping("UDP");
        return;
    }
    case Stage::MappingUdp: {
        if (body.find("AddPortMappingResponse") == std::string::npos &&
            body.find("200 OK") == std::string::npos) {
            fail("your router opened one port but not the other.");
            return;
        }
        s_stage = Stage::Asking;
        send_http(s_controlUrl, s_serviceType + "#GetExternalIPAddress",
                  soap_envelope("GetExternalIPAddress", ""));
        return;
    }
    case Stage::Asking: {
        std::string ip;
        if (!xml_value(body, "NewExternalIPAddress", &ip) || ip.empty()) {
            fail("the port is open, but your router would not say what your address is.");
            return;
        }
        s_external = ip;
        s_stage = Stage::Ready;
        s_renewTicks = kRenewTicks;
        s_status = "Your router opened the port.";
        coop_log::info("coop_mod: [UPNP] port {} open, external address {}", s_port, ip);
        return;
    }
    default:
        return;
    }
}

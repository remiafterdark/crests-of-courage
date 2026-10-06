
#include <cstdint>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

uint32_t route_source(uint32_t towards) {
#if defined(_WIN32)
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;
#else
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return 0;
#endif
    uint32_t out = 0;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = htonl(towards);
    if (connect(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) == 0) {
        sockaddr_in me{};
#if defined(_WIN32)
        int len = sizeof(me);
#else
        socklen_t len = sizeof(me);
#endif
        if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0) out = ntohl(me.sin_addr.s_addr);
    }
#if defined(_WIN32)
    closesocket(s);
#else
    close(s);
#endif
    return out;
}

std::string dotted(uint32_t ip) {
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF);
}

}

std::string coop_lan_address() {
    const uint32_t ip = route_source(0x08080808u);
    const uint32_t a = ip >> 24;
    const uint32_t b = (ip >> 16) & 0xFF;

    if (a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31)) return dotted(ip);
    return "";
}

std::string coop_tailscale_address() {
    const uint32_t ip = route_source(0x64646464u);
    if ((ip >> 24) == 100 && ((ip >> 16) & 0xC0) == 64) return dotted(ip);
    return "";
}


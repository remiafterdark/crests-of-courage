
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

std::string coop_lan_address() {
#if defined(_WIN32)
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return "";
#else
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return "";
#endif
    std::string out;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = htonl(0x08080808u);
    if (connect(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) == 0) {
        sockaddr_in me{};
#if defined(_WIN32)
        int len = sizeof(me);
#else
        socklen_t len = sizeof(me);
#endif
        if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0) {
            const uint32_t ip = ntohl(me.sin_addr.s_addr);
            const uint32_t a = ip >> 24;
            const uint32_t b = (ip >> 16) & 0xFF;

            if (a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31)) {
                out = std::to_string(a) + "." + std::to_string(b) + "." +
                      std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF);
            }
        }
    }
#if defined(_WIN32)
    closesocket(s);
#else
    close(s);
#endif
    return out;
}


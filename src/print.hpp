#pragma once

#include "mods/svc/log.hpp"

#include <utility>

#include <string>

bool coop_dev_logging();

void coop_log_capture(char level, const std::string& message);

namespace coop_log {

template <typename... Args>
void info(fmt::format_string<Args...> formatString, Args&&... args) {
    const std::string line = fmt::format(formatString, std::forward<Args>(args)...);
    coop_log_capture('I', line);
    mods::log::info("{}", line);
}

template <typename... Args>
void warn(fmt::format_string<Args...> formatString, Args&&... args) {
    const std::string line = fmt::format(formatString, std::forward<Args>(args)...);
    coop_log_capture('W', line);
    mods::log::warn("{}", line);
}

template <typename... Args>
void trace(fmt::format_string<Args...> formatString, Args&&... args) {
    if (!coop_dev_logging()) return;
    const std::string line = fmt::format(formatString, std::forward<Args>(args)...);
    coop_log_capture('T', line);
    mods::log::info("{}", line);
}

template <typename... Args>
void error(fmt::format_string<Args...> formatString, Args&&... args) {
    const std::string line = fmt::format(formatString, std::forward<Args>(args)...);
    coop_log_capture('E', line);
    mods::log::error("{}", line);
}

}

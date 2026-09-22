#pragma once

#include "mods/svc/log.hpp"

#include <utility>

bool coop_dev_logging();

namespace coop_log {

template <typename... Args>
void info(fmt::format_string<Args...> formatString, Args&&... args) {
    mods::log::info(formatString, std::forward<Args>(args)...);
}

template <typename... Args>
void warn(fmt::format_string<Args...> formatString, Args&&... args) {
    mods::log::warn(formatString, std::forward<Args>(args)...);
}

template <typename... Args>
void trace(fmt::format_string<Args...> formatString, Args&&... args) {
    if (coop_dev_logging()) mods::log::info(formatString, std::forward<Args>(args)...);
}

template <typename... Args>
void error(fmt::format_string<Args...> formatString, Args&&... args) {
    mods::log::error(formatString, std::forward<Args>(args)...);
}

}

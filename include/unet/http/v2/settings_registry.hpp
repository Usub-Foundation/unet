#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>

#include "unet/http/v2/wire/settings.hpp"


namespace usub::unet::http::v2 {

    class Connection;

    using SettingHandler = void (*)(Connection &, std::uint32_t value);

    struct SettingsRegistry {
        std::unordered_map<std::uint16_t, SettingHandler> handlers{};

        void registerHandler(std::uint16_t id, SettingHandler fn) { handlers[id] = fn; }
        void registerHandler(SETTINGS id, SettingHandler fn) { registerHandler(std::to_underlying(id), fn); }

        bool dispatch(Connection &c, std::uint16_t id, std::uint32_t value) const {
            auto it = handlers.find(id);
            if (it == handlers.end()) return false;
            it->second(c, value);
            return true;
        }
    };

}// namespace usub::unet::http::v2

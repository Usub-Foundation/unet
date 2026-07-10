#pragma once

#include <cctype>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>


namespace usub::unet::header {

    struct KeepAliveHint {
        std::optional<std::chrono::seconds> timeout;
        std::optional<std::uint32_t> max;
    };

    // Tolerant: unknown params ignored, malformed values skipped. Mirrors what
    // nginx / Apache accept.
    inline KeepAliveHint parseKeepAlive(std::string_view value) noexcept {
        KeepAliveHint out;

        auto skip_ws = [&](std::size_t &i) {
            while (i < value.size() && (value[i] == ' ' || value[i] == '\t')) ++i;
        };
        auto eq_iequal = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                unsigned char ca = static_cast<unsigned char>(a[i]);
                unsigned char cb = static_cast<unsigned char>(b[i]);
                if (std::tolower(ca) != std::tolower(cb)) return false;
            }
            return true;
        };

        std::size_t i = 0;
        while (i < value.size()) {
            skip_ws(i);
            std::size_t name_start = i;
            while (i < value.size() && value[i] != '=' && value[i] != ',') ++i;
            std::size_t name_end = i;
            // Right-trim the name
            while (name_end > name_start && (value[name_end - 1] == ' ' || value[name_end - 1] == '\t')) --name_end;
            std::string_view name = value.substr(name_start, name_end - name_start);

            std::string_view raw_value{};
            if (i < value.size() && value[i] == '=') {
                ++i;
                skip_ws(i);
                std::size_t v_start = i;
                if (i < value.size() && value[i] == '"') {
                    ++v_start;
                    ++i;
                    while (i < value.size() && value[i] != '"') ++i;
                    raw_value = value.substr(v_start, i - v_start);
                    if (i < value.size()) ++i;// closing quote
                } else {
                    while (i < value.size() && value[i] != ',') ++i;
                    std::size_t v_end = i;
                    while (v_end > v_start && (value[v_end - 1] == ' ' || value[v_end - 1] == '\t')) --v_end;
                    raw_value = value.substr(v_start, v_end - v_start);
                }
            }

            auto parse_uint = [&](std::string_view s) -> std::optional<std::uint64_t> {
                if (s.empty()) return std::nullopt;
                std::uint64_t acc = 0;
                for (char c: s) {
                    if (c < '0' || c > '9') return std::nullopt;
                    std::uint64_t next = acc * 10 + static_cast<std::uint64_t>(c - '0');
                    if (next < acc) return std::nullopt;// overflow
                    acc = next;
                }
                return acc;
            };

            if (eq_iequal(name, "timeout")) {
                if (auto n = parse_uint(raw_value); n.has_value()) { out.timeout = std::chrono::seconds{*n}; }
            } else if (eq_iequal(name, "max")) {
                if (auto n = parse_uint(raw_value); n.has_value()) {
                    if (*n > 0xffffffffull) *n = 0xffffffffull;
                    out.max = static_cast<std::uint32_t>(*n);
                }
            }
            // Unknown params silently ignored.

            skip_ws(i);
            if (i < value.size() && value[i] == ',') ++i;
        }

        return out;
    }

}// namespace usub::unet::header

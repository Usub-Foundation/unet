#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace usub::unet::core {

    class Config {
    public:
        struct Value;// forward declare

        using Object = std::unordered_map<std::string, Value>;
        using Array = std::vector<Value>;

        struct Value {
            using Data = std::variant<std::nullptr_t, bool, double, std::uint64_t, std::string, Object, Array>;

            Data data{};
        };

        Object root{};

        const Object *getObject(std::string_view path) const {
            if (const Value *v = this->find(path)) {
                if (const auto *obj = std::get_if<Object>(&v->data)) { return obj; }
            }

            // Support top-level keys that include dots literally, e.g. "HTTP.PlainTextStream".
            auto it = this->root.find(std::string(path));
            if (it == this->root.end()) { return nullptr; }
            return std::get_if<Object>(&it->second.data);
        }

        const Value *find(std::string_view path) const {
            const Object *obj = &this->root;
            const Value *current = nullptr;

            while (true) {
                const std::size_t dot = path.find('.');
                const std::string_view key = (dot == std::string_view::npos) ? path : path.substr(0, dot);
                auto it = obj->find(std::string(key));
                if (it == obj->end()) { return nullptr; }

                current = &it->second;
                if (dot == std::string_view::npos) { return current; }

                const auto *next = std::get_if<Object>(&current->data);
                if (!next) { return nullptr; }
                obj = next;
                path.remove_prefix(dot + 1);
            }
        }

        Value *find(std::string_view path) {
            Object *obj = &this->root;
            Value *current = nullptr;

            while (true) {
                const std::size_t dot = path.find('.');
                const std::string_view key = (dot == std::string_view::npos) ? path : path.substr(0, dot);
                auto it = obj->find(std::string(key));
                if (it == obj->end()) { return nullptr; }

                current = &it->second;
                if (dot == std::string_view::npos) { return current; }

                auto *next = std::get_if<Object>(&current->data);
                if (!next) { return nullptr; }
                obj = next;
                path.remove_prefix(dot + 1);
            }
        }

        std::string getString(std::string_view path, std::string_view fallback = {}) const {
            const auto *value = this->find(path);
            if (!value) { return std::string(fallback); }
            if (const auto *v = std::get_if<std::string>(&value->data)) { return *v; }
            if (const auto *v = std::get_if<std::uint64_t>(&value->data)) { return std::to_string(*v); }
            if (const auto *v = std::get_if<double>(&value->data)) { return std::to_string(*v); }
            if (const auto *v = std::get_if<bool>(&value->data)) { return *v ? "true" : "false"; }
            return std::string(fallback);
        }

        std::string getString(const Object &obj, std::string_view key, std::string_view fallback = {}) const {
            auto it = obj.find(std::string(key));
            if (it == obj.end()) { return std::string(fallback); }
            const auto &data = it->second.data;
            if (const auto *v = std::get_if<std::string>(&data)) { return *v; }
            if (const auto *v = std::get_if<std::uint64_t>(&data)) { return std::to_string(*v); }
            if (const auto *v = std::get_if<double>(&data)) { return std::to_string(*v); }
            if (const auto *v = std::get_if<bool>(&data)) { return *v ? "true" : "false"; }
            return std::string(fallback);
        }

        std::uint64_t getUInt(std::string_view path, std::uint64_t fallback = 0) const {
            const auto *value = this->find(path);
            if (!value) { return fallback; }

            if (const auto *v = std::get_if<std::uint64_t>(&value->data)) { return *v; }
            if (const auto *v = std::get_if<double>(&value->data)) {
                if (!std::isfinite(*v) || *v < 0.0) { return fallback; }
                if (*v > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) { return fallback; }
                return static_cast<std::uint64_t>(*v);
            }
            if (const auto *v = std::get_if<std::string>(&value->data)) {
                std::uint64_t parsed = 0;
                const char *begin = v->data();
                const char *end = begin + v->size();
                auto [ptr, ec] = std::from_chars(begin, end, parsed);
                if (ec != std::errc() || ptr != end) { return fallback; }
                return parsed;
            }
            return fallback;
        }

        std::uint64_t getUInt(const Object &obj, std::string_view key, std::uint64_t fallback = 0) const {
            auto it = obj.find(std::string(key));
            if (it == obj.end()) { return fallback; }
            const auto &data = it->second.data;

            if (const auto *v = std::get_if<std::uint64_t>(&data)) { return *v; }
            if (const auto *v = std::get_if<double>(&data)) {
                if (!std::isfinite(*v) || *v < 0.0) { return fallback; }
                if (*v > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) { return fallback; }
                return static_cast<std::uint64_t>(*v);
            }
            if (const auto *v = std::get_if<std::string>(&data)) {
                std::uint64_t parsed = 0;
                const char *begin = v->data();
                const char *end = begin + v->size();
                auto [ptr, ec] = std::from_chars(begin, end, parsed);
                if (ec != std::errc() || ptr != end) { return fallback; }
                return parsed;
            }
            return fallback;
        }

        std::int64_t getInt(std::string_view path, std::int64_t fallback = 0) const {
            const auto *value = this->find(path);
            if (!value) { return fallback; }

            if (const auto *v = std::get_if<std::uint64_t>(&value->data)) {
                if (*v > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) { return fallback; }
                return static_cast<std::int64_t>(*v);
            }
            if (const auto *v = std::get_if<double>(&value->data)) {
                if (!std::isfinite(*v)) { return fallback; }
                if (*v < static_cast<double>(std::numeric_limits<std::int64_t>::min())) { return fallback; }
                if (*v > static_cast<double>(std::numeric_limits<std::int64_t>::max())) { return fallback; }
                return static_cast<std::int64_t>(*v);
            }
            if (const auto *v = std::get_if<std::string>(&value->data)) {
                std::int64_t parsed = 0;
                const char *begin = v->data();
                const char *end = begin + v->size();
                auto [ptr, ec] = std::from_chars(begin, end, parsed);
                if (ec != std::errc() || ptr != end) { return fallback; }
                return parsed;
            }
            if (const auto *v = std::get_if<bool>(&value->data)) { return *v ? 1 : 0; }
            return fallback;
        }

        std::int64_t getInt(const Object &obj, std::string_view key, std::int64_t fallback = 0) const {
            auto it = obj.find(std::string(key));
            if (it == obj.end()) { return fallback; }
            const auto &data = it->second.data;

            if (const auto *v = std::get_if<std::uint64_t>(&data)) {
                if (*v > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) { return fallback; }
                return static_cast<std::int64_t>(*v);
            }
            if (const auto *v = std::get_if<double>(&data)) {
                if (!std::isfinite(*v)) { return fallback; }
                if (*v < static_cast<double>(std::numeric_limits<std::int64_t>::min())) { return fallback; }
                if (*v > static_cast<double>(std::numeric_limits<std::int64_t>::max())) { return fallback; }
                return static_cast<std::int64_t>(*v);
            }
            if (const auto *v = std::get_if<std::string>(&data)) {
                std::int64_t parsed = 0;
                const char *begin = v->data();
                const char *end = begin + v->size();
                auto [ptr, ec] = std::from_chars(begin, end, parsed);
                if (ec != std::errc() || ptr != end) { return fallback; }
                return parsed;
            }
            if (const auto *v = std::get_if<bool>(&data)) { return *v ? 1 : 0; }
            return fallback;
        }
    };

}// namespace usub::unet::core

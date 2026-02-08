#pragma once

#include <algorithm>
#include <cctype>
#include <expected>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "unet/header/enum.hpp"
#include "unet/utils/error.hpp"

#include "unet/utils/string_utils.h"

namespace usub::unet::header {

    struct Generic;

    struct Header {
        std::string key;
        std::string value;
    };

    class Headers {
    public:
        Headers() = default;
        ~Headers() = default;

        void addHeader(std::string_view key, std::string_view value) {// Make into std::expected, we would need to validate value
            std::string key_lower = normalizeKey(key);
            std::string value_trimmed = std::string(value);
            usub::utils::trim(value_trimmed);
            header_list_.emplace_back(Header{key_lower, value_trimmed});
            header_keys_.insert(std::move(key_lower));
        }

        void addHeader(std::string &&key, std::string &&value) {
            normalizeKey(key);
            usub::utils::trim(value);
            header_list_.emplace_back(Header{std::move(key), std::move(value)});
            header_keys_.insert(header_list_.back().key);
        }

        bool contains(std::string_view key) const {
            return header_keys_.contains(normalizeKey(key));
        }

        std::size_t size() const noexcept { return header_list_.size(); }
        bool empty() const noexcept { return header_list_.empty(); }
        void clear() noexcept {
            header_list_.clear();
            header_keys_.clear();
        }

        const std::vector<Header> &all() const noexcept { return header_list_; }

        const Header *first(std::string_view key) const {
            std::string key_lower = normalizeKey(key);
            for (const auto &header: header_list_) {
                if (header.key == key_lower) {
                    return &header;
                }
            }
            return nullptr;
        }

        std::vector<Header> all(std::string_view key) const {
            std::vector<Header> result;
            std::string key_lower = normalizeKey(key);
            for (const auto &header: header_list_) {
                if (header.key == key_lower) {
                    result.push_back(header);
                }
            }
            return result;
        }

        std::optional<std::string_view> value(std::string_view key) const {
            const Header *header = first(key);
            if (!header) {
                return std::nullopt;
            }
            return std::string_view(header->value);
        }

        Header &at(std::size_t index) { return header_list_.at(index); }
        const Header &at(std::size_t index) const { return header_list_.at(index); }

        Header &operator[](std::size_t index) { return header_list_[index]; }
        const Header &operator[](std::size_t index) const { return header_list_[index]; }

        auto begin() noexcept { return header_list_.begin(); }
        auto end() noexcept { return header_list_.end(); }
        auto begin() const noexcept { return header_list_.begin(); }
        auto end() const noexcept { return header_list_.end(); }
        auto cbegin() const noexcept { return header_list_.cbegin(); }
        auto cend() const noexcept { return header_list_.cend(); }

        void emplace(std::string_view key, std::string_view value) { addHeader(key, value); }
        void emplace_back(std::string_view key, std::string_view value) { addHeader(key, value); }
        void emplace(std::string &&key, std::string &&value) { addHeader(std::move(key), std::move(value)); }
        void emplace_back(std::string &&key, std::string &&value) { addHeader(std::move(key), std::move(value)); }

        // template<class Type, ENUM Header>
        // std::expected<void, usub::unet::utils::UnetError> validate(std::string_view key, std::string_view value);

    private:
        static std::string normalizeKey(std::string_view key) {
            std::string key_lower;
            key_lower.resize(key.size());
            std::transform(key.begin(), key.end(), key_lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return key_lower;
        }

        static void normalizeKey(std::string &key) {
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }

        std::vector<Header> header_list_;
        std::set<std::string> header_keys_;// We can use this to quickly check for existence of headers for cases
                                           // where we don't allow duplicate headers. "Host", "Content-Length", etc.
    };

}// namespace usub::unet::header

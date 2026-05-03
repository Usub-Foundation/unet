#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <expected>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
        using iterator = std::vector<Header>::iterator;
        using const_iterator = std::vector<Header>::const_iterator;

        Headers() = default;
        ~Headers() = default;

        //COMPAT
        using HeaderValues = std::vector<std::string>;
        using HeaderPair = std::pair<std::string, HeaderValues>;

        //COMPAT
        class CompatIterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = HeaderPair;
            using difference_type = std::ptrdiff_t;
            using pointer = const value_type *;
            using reference = const value_type &;

            CompatIterator() = default;
            CompatIterator(const Headers *owner, std::string key, std::size_t first_index)
                : owner_(owner), key_(std::move(key)), first_index_(first_index), found_(owner != nullptr) {
                if (this->found_) {
                    this->entry_.first = this->key_;
                    this->entry_.second = this->owner_->valuesForNormalizedKey(this->key_);
                }
            }

            reference operator*() const { return this->entry_; }
            pointer operator->() const { return &this->entry_; }

            CompatIterator &operator++() {
                this->found_ = false;
                return *this;
            }

            CompatIterator operator++(int) {
                CompatIterator copy = *this;
                ++(*this);
                return copy;
            }

            bool operator==(const CompatIterator &other) const {
                if (!this->found_ && !other.found_) {
                    return true;
                }
                return this->owner_ == other.owner_ && this->first_index_ == other.first_index_ && this->found_ == other.found_;
            }
            bool operator!=(const CompatIterator &other) const { return !(*this == other); }

            bool operator==(const iterator &) const { return this->is_end(); }
            bool operator!=(const iterator &) const { return !this->is_end(); }
            bool operator==(const const_iterator &) const { return this->is_end(); }
            bool operator!=(const const_iterator &) const { return !this->is_end(); }

            friend bool operator==(const iterator &, const CompatIterator &compat) { return compat.is_end(); }
            friend bool operator!=(const iterator &, const CompatIterator &compat) { return !compat.is_end(); }
            friend bool operator==(const const_iterator &, const CompatIterator &compat) { return compat.is_end(); }
            friend bool operator!=(const const_iterator &, const CompatIterator &compat) { return !compat.is_end(); }

        private:
            bool is_end() const { return !this->found_; }

            const Headers *owner_{};
            std::string key_{};
            std::size_t first_index_{};
            bool found_{false};
            value_type entry_{};
        };

        void addHeader(std::string_view key, std::string_view value) {// Make into std::expected, we would need to validate value
            std::string key_lower = normalizeKey(key);
            std::string value_trimmed = std::string(value);
            usub::utils::trim(value_trimmed);
            header_list_.emplace_back(Header{key_lower, value_trimmed});
            header_keys_.insert(std::move(key_lower));
        }

        //COMPAT
        void addHeader(const char *key, const char *value) {
            addHeader(std::string_view{key}, std::string_view{value});
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

        //COMPAT
        HeaderValues at(std::string_view key) const {
            std::string key_lower = normalizeKey(key);
            if (!this->header_keys_.contains(key_lower)) {
                throw std::out_of_range("Header not found");
            }
            return valuesForNormalizedKey(key_lower);
        }

        //COMPAT
        HeaderValues at(const char *key) const {
            return at(std::string_view{key});
        }

        //COMPAT
        CompatIterator find(std::string_view key) const {
            std::string key_lower = normalizeKey(key);
            for (std::size_t i = 0; i < header_list_.size(); ++i) {
                if (header_list_[i].key == key_lower) {
                    return CompatIterator{this, std::move(key_lower), i};
                }
            }
            return CompatIterator{};
        }

        //COMPAT
        CompatIterator find(const char *key) const {
            return find(std::string_view{key});
        }

        Header &operator[](std::size_t index) { return header_list_[index]; }
        const Header &operator[](std::size_t index) const { return header_list_[index]; }

        iterator begin() noexcept { return header_list_.begin(); }
        iterator end() noexcept { return header_list_.end(); }
        const_iterator begin() const noexcept { return header_list_.begin(); }
        const_iterator end() const noexcept { return header_list_.end(); }
        const_iterator cbegin() const noexcept { return header_list_.cbegin(); }
        const_iterator cend() const noexcept { return header_list_.cend(); }

        void emplace(std::string_view key, std::string_view value) { addHeader(key, value); }
        void emplace_back(std::string_view key, std::string_view value) { addHeader(key, value); }
        //COMPAT
        void emplace(const char *key, const char *value) { addHeader(key, value); }
        //COMPAT
        void emplace_back(const char *key, const char *value) { addHeader(key, value); }
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

        HeaderValues valuesForNormalizedKey(std::string_view key) const {
            HeaderValues values;
            for (const auto &header: header_list_) {
                if (header.key == key) {
                    values.push_back(header.value);
                }
            }
            return values;
        }

        std::vector<Header> header_list_;
        std::set<std::string> header_keys_;// We can use this to quickly check for existence of headers for cases
                                           // where we don't allow duplicate headers. "Host", "Content-Length", etc.
    };

}// namespace usub::unet::header

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "unet/header/generic.hpp"

namespace usub::unet::header {

    //COMPAT
    class HeaderCompatView {
    public:
        using HeaderValues = std::vector<std::string>;
        using HeaderPair = std::pair<std::string, HeaderValues>;

        class Iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = HeaderPair;
            using difference_type = std::ptrdiff_t;
            using pointer = const value_type *;
            using reference = const value_type &;

            Iterator() = default;

            Iterator(const HeaderCompatView *owner, std::size_t index)
                : owner_(owner), index_(index), found_(owner != nullptr && index < owner->entries_.size()) {}

            explicit Iterator(HeaderPair entry) : found_(true), entry_(std::move(entry)) {}

            reference operator*() const {
                refreshEntry();
                return this->entry_;
            }

            pointer operator->() const {
                refreshEntry();
                return &this->entry_;
            }

            Iterator &operator++() {
                if (this->owner_) {
                    ++this->index_;
                    this->found_ = this->index_ < this->owner_->entries_.size();
                    this->entry_dirty_ = true;
                } else {
                    this->found_ = false;
                }
                return *this;
            }

            Iterator operator++(int) {
                Iterator copy = *this;
                ++(*this);
                return copy;
            }

            bool operator==(const Iterator &other) const {
                if (!this->found_ && !other.found_) {
                    return true;
                }
                return this->owner_ == other.owner_ && this->index_ == other.index_ && this->found_ == other.found_;
            }

            bool operator!=(const Iterator &other) const { return !(*this == other); }

        private:
            void refreshEntry() const {
                if (!this->owner_ || !this->found_ || !this->entry_dirty_) {
                    return;
                }

                this->entry_ = this->owner_->entries_[this->index_];
                this->entry_dirty_ = false;
            }

            const HeaderCompatView *owner_{};
            std::size_t index_{};
            bool found_{false};
            mutable bool entry_dirty_{true};
            mutable value_type entry_{};
        };

        HeaderCompatView() = default;
        explicit HeaderCompatView(const Headers &headers) { rebuild(headers); }

        void rebuild(const Headers &headers) {
            entries_.clear();

            for (const auto &header: headers.all()) {
                auto it = std::find_if(entries_.begin(), entries_.end(),
                                       [&](const HeaderPair &entry) { return entry.first == header.key; });

                if (it == entries_.end()) {
                    entries_.push_back(HeaderPair{header.key, HeaderValues{header.value}});
                } else {
                    it->second.push_back(header.value);
                }
            }
        }

        //COMPAT
        [[deprecated("compatability function: use Headers::all(key) or Headers::value(key) on the public headers field")]]
        const HeaderValues &at(std::string_view key) const {
            return atImpl(key);
        }

        //COMPAT
        [[deprecated("compatability function: use Headers::all(key) or Headers::value(key) on the public headers field")]]
        const HeaderValues &at(const char *key) const { return atImpl(std::string_view{key}); }

        //COMPAT
        [[deprecated("compatability function: use Headers::contains(key) on the public headers field")]]
        bool contains(std::string_view key) const { return containsImpl(key); }
        //COMPAT
        [[deprecated("compatability function: use Headers::contains(key) on the public headers field")]]
        bool contains(const char *key) const { return containsImpl(std::string_view{key}); }

        //COMPAT
        [[deprecated("compatability function: use Headers::empty() on the public headers field")]]
        bool empty() const noexcept { return entries_.empty(); }
        //COMPAT
        [[deprecated("compatability function: use Headers::size() on the public headers field")]]
        std::size_t size() const noexcept { return entries_.size(); }

        //COMPAT
        [[deprecated("compatability function: use Headers::first(key), Headers::all(key), or Headers::value(key) on the public headers field")]]
        Iterator find(std::string_view key) const {
            return findImpl(key);
        }

        //COMPAT
        [[deprecated("compatability function: use Headers::first(key), Headers::all(key), or Headers::value(key) on the public headers field")]]
        Iterator find(const char *key) const { return findImpl(std::string_view{key}); }

        //COMPAT
        [[deprecated("compatability function: iterate the public headers field or Headers::all() directly")]]
        Iterator begin() const { return beginImpl(); }
        //COMPAT
        [[deprecated("compatability function: iterate the public headers field or Headers::all() directly")]]
        Iterator end() const { return endImpl(); }
        //COMPAT
        [[deprecated("compatability function: iterate the public headers field or Headers::all() directly")]]
        Iterator cbegin() const { return beginImpl(); }
        //COMPAT
        [[deprecated("compatability function: iterate the public headers field or Headers::all() directly")]]
        Iterator cend() const { return endImpl(); }

    private:
        const HeaderValues &atImpl(std::string_view key) const {
            const auto *entry = findEntry(key);
            if (!entry) {
                throw std::out_of_range("Header not found");
            }
            return entry->second;
        }

        bool containsImpl(std::string_view key) const { return findEntry(key) != nullptr; }

        Iterator findImpl(std::string_view key) const {
            const auto *entry = findEntry(key);
            if (!entry) {
                return endImpl();
            }
            return Iterator{*entry};
        }

        Iterator beginImpl() const { return Iterator{this, 0}; }
        Iterator endImpl() const { return Iterator{}; }

        static std::string normalizeKey(std::string_view key) {
            std::string key_lower;
            key_lower.resize(key.size());
            std::transform(key.begin(), key.end(), key_lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return key_lower;
        }

        const HeaderPair *findEntry(std::string_view key) const {
            const std::string key_lower = normalizeKey(key);
            auto it = std::find_if(entries_.begin(), entries_.end(),
                                   [&](const HeaderPair &entry) { return entry.first == key_lower; });
            return it == entries_.end() ? nullptr : &(*it);
        }

        std::vector<HeaderPair> entries_;
    };

}// namespace usub::unet::header

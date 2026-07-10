#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <uvent/tasks/Awaitable.h>

#include "unet/vless/core/request.hpp"

namespace usub::unet::vless {

    using usub::uvent::task::Awaitable;

    using UUID = std::array<std::byte, 16>;

    struct User {
        UUID uuid{};
        std::string email{};
    };

    class MemoryAuthenticationProvider {
    public:
        MemoryAuthenticationProvider() = default;

        explicit MemoryAuthenticationProvider(const std::vector<User> &seed) {
            users_.reserve(seed.size());
            for (const auto &u: seed) users_.emplace(u.uuid, u);
        }

        void add(User user) {
            std::unique_lock lock{mutex_};
            users_[user.uuid] = std::move(user);
        }

        bool remove(const UUID &uuid) {
            std::unique_lock lock{mutex_};
            return users_.erase(uuid) > 0;
        }

        [[nodiscard]] Awaitable<std::optional<User>> lookup(const Request &request) const {
            std::shared_lock lock{mutex_};
            if (const auto it = users_.find(request.uuid); it != users_.end()) co_return it->second;
            co_return std::nullopt;
        }

        [[nodiscard]] std::size_t size() const {
            std::shared_lock lock{mutex_};
            return users_.size();
        }

    private:
        struct UUIDHash {
            std::size_t operator()(const UUID &u) const noexcept {
                std::uint64_t h = 0xcbf29ce484222325ULL;
                for (const auto b: u) {
                    h ^= std::to_integer<std::uint64_t>(b);
                    h *= 0x100000001b3ULL;
                }
                return static_cast<std::size_t>(h);
            }
        };

        mutable std::shared_mutex mutex_;
        std::unordered_map<UUID, User, UUIDHash> users_;
    };

}// namespace usub::unet::vless

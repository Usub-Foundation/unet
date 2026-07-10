#pragma once

#include <any>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>

#include <uvent/Uvent.h>

#include "unet/core/transport/transport.hpp"
#include "unet/ws/core/frame.hpp"

namespace usub::unet::ws {

    struct WriteState {

        std::shared_ptr<usub::unet::core::transport::Transport> transport{};

        std::queue<std::string> pending{};
        bool draining{false};
        std::mutex mu{};

        std::uint64_t id{0};

        std::any user_data{};
    };

    class ServerWriter {
    public:
        ServerWriter() = default;
        explicit ServerWriter(std::shared_ptr<WriteState> state);

        [[nodiscard]] bool expired() const noexcept;

        [[nodiscard]] std::uint64_t id() const noexcept { return this->state_ ? this->state_->id : 0; }

        [[nodiscard]] std::any &user_data() noexcept { return this->state_->user_data; }
        [[nodiscard]] const std::any &user_data() const noexcept { return this->state_->user_data; }

        usub::uvent::task::Awaitable<void> sendText(std::string_view payload);

        usub::uvent::task::Awaitable<void> sendBinary(std::string_view payload);

        usub::uvent::task::Awaitable<void> sendFrame(const Frame &frame);

        usub::uvent::task::Awaitable<void> ping(std::string_view payload = {});

        usub::uvent::task::Awaitable<void> pong(std::string_view payload = {});

        usub::uvent::task::Awaitable<void> close(CLOSE_CODE code = CLOSE_CODE::NORMAL, std::string_view reason = {});

    private:
        usub::uvent::task::Awaitable<void> enqueueAndDrain(std::string bytes);

        std::shared_ptr<WriteState> state_;
    };

}// namespace usub::unet::ws

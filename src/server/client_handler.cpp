#include "client_handler.h"

usub::server::ClientHandler::ClientHandler(usub::uvent::net::Socket *socket, const std::shared_ptr<usub::server::protocols::http::HTTPEndpointHandler> &endpoint_handler) : IEIRAwaitableVoid(nullptr), socket_(socket), http1_(endpoint_handler) {
    auto awaitable = run();
    this->set_handle(awaitable.get_handle());
    this->setRemovable();
}

bool usub::server::ClientHandler::await_ready() noexcept {
    return this->is_ready_;
}

bool usub::server::ClientHandler::await_suspend(std::coroutine_handle<> handle) noexcept {
    //    auto h = std::coroutine_handle<>::from_address(handle.address());
    //    this->set_handle(h);
    return true;
}

void usub::server::ClientHandler::await_resume() {
    if (!this->handle_.done() && this->handle_.promise().exception_)
        this->is_done_ = true;
    this->is_ready_ = true;
}

IEIRAwaitableVoid usub::server::ClientHandler::run() {
    spdlog::info("Awaitable client2: {}", static_cast<void *>(this));
    uint8_t arr[16384];
    std::string request{"test"};
    //    auto i = 0;
    while (!this->done()) {
        //        i++;
        //        if (i == 10000) exit(0);
        if (this->done()) break;
        if (this->socket_ == nullptr) {
            this->is_done_ = true;
            break;
        }
        ssize_t rdsz = co_await *this->socket_->async_read(arr, sizeof(arr));
        if (rdsz <= 0) {
            this->is_done_ = true;
            break;
        }
        this->socket_->set_timeout_ms(usub::uvent::settings::timeout_duration_ms);
        // request = std::string(arr, arr + rdsz);
        auto a = this->http1_.readCallback(request, this->socket_);
        co_await a;
        while (this->http1_.getResponse().getState() == usub::server::protocols::http::SEND_STATE::SENDING || this->http1_.getResponse().getState() == usub::server::protocols::http::SEND_STATE::NOT_SENT) {
            //            resStr = this->http1_.getResponse().string();
            //            auto buf = std::make_unique<uint8_t[]>(resStr.size());
            //            std::memcpy(buf.get(), resStr.data(), resStr.size());
            const auto &responseString = this->http1_.getResponse().string();

            ssize_t wrsz = co_await *this->socket_->async_write((uint8_t *) responseString.data(), responseString.size());
            if (wrsz <= 0) {
                this->is_done_ = true;
                break;
            }
            this->http1_.getResponse().setSent();
#ifdef UVENT_DEBUG
            spdlog::info("Write size: {}", wrsz);
#endif
        }

        this->http1_.getResponse().clear();
        memset(arr, '\0', sizeof(arr));
    }
}

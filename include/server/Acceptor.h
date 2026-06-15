#pragma once

#include <coroutine>
#include <memory>

#include <utils/configuration/ConfigReader.h>
#include <uvent/Uvent.h>
#include <uvent/net/Socket.h>
#include <uvent/tasks/Awaitable.h>
#include <uvent/tasks/AwaitableFrame.h>

#include "Protocols/HTTP/EndpointHandler.h"
#include "Protocols/HTTP/HTTP1.h"
#include "Protocols/HTTP/Message.h"

static constexpr std::size_t MAX_READ_SIZE = 64 * 1024;


namespace usub::server {


    template<class TRouter>
    class PlainHTTPStreamHandler {
    public:
        using RouterType = TRouter;

        PlainHTTPStreamHandler(std::shared_ptr<RouterType> router)
            : endpoint_handler_(std::move(router)) {}

        usub::uvent::task::Awaitable<void>
        clientCoroutine(usub::uvent::net::TCPClientSocket socket)//co_spawn(handle(socket))
        {
#ifdef UVENT_DEBUG
            spdlog::info("Entered {}: {}", __PRETTY_FUNCTION__, static_cast<void *>(this));
#endif
            protocols::http::HTTP1 http1{endpoint_handler_};

            auto &request = http1.getRequest();
            auto &response = http1.getResponse();
            auto &request_headers = request.getHeaders();
            auto &response_headers = response.getHeaders();

            usub::uvent::utils::DynamicBuffer buffer;
            buffer.reserve(MAX_READ_SIZE);

            while (true) {
                buffer.clear();

                ssize_t rdsz = co_await socket.async_read(buffer, MAX_READ_SIZE);

                if (rdsz <= 0) {
                    break;
                }
                socket.set_timeout_ms(usub::uvent::settings::timeout_duration_ms);
#ifdef UVENT_DEBUG
                spdlog::info("Read size: {}", rdsz);
#endif
                std::string request_string{buffer.data(), buffer.data() + buffer.size()};
                // http1.readCallbackSync(request_string, socket);
                co_await http1.readCallback(request_string, socket);

                const bool conn_close_resp = response_headers.containsValue(component::HeaderEnum::Connection, "close");
                const bool conn_close_req = response_headers.containsValue(component::HeaderEnum::Connection, "close");
                if (conn_close_req && !conn_close_resp) {
                    response.addHeader("Connection", "close");
                }

                while (!response.isSent() && request.getState() >= protocols::http::REQUEST_STATE::FINISHED) {
                    const std::string responseString = response.pull();


                    ssize_t wrsz = co_await socket.async_write((uint8_t *) responseString.data(), responseString.size());

                    if (wrsz <= 0) {

                        break;
                    }
#ifdef UVENT_DEBUG
                    spdlog::info("Write size: {}", wrsz);
#endif
                }

                bool has_bad_request = request.getState() >= usub::server::protocols::http::REQUEST_STATE::BAD_REQUEST;
                bool hasError = request.getState() == usub::server::protocols::http::REQUEST_STATE::ERROR;
                bool http10_no_keep_alive = request.getHTTPVersion() == usub::server::protocols::http::VERSION::HTTP_1_0 &&
                                            !request_headers.containsValue(usub::server::component::HeaderEnum::Connection, "keep-alive");

                if (response.isSent() && (has_bad_request || hasError || conn_close_resp || conn_close_req || http10_no_keep_alive)) {
#ifdef UVENT_DEBUG
                    if (has_bad_request)
                        spdlog::info("Closing connection: request state is BAD_REQUEST or worse (state = {})", static_cast<int>(request.getState()));
                    else if (hasError)
                        spdlog::info("Closing connection: request state is ERROR");
                    else if (conn_close_resp)
                        spdlog::info("Closing connection: response had 'Connection: close'");
                    else if (conn_close_req)
                        spdlog::info("Closing connection: request had 'Connection: close'");
                    else if (http10_no_keep_alive)
                        spdlog::info("Closing connection: HTTP/1.0 and no 'Connection: keep-alive'");
#endif
                    socket.shutdown();
                    co_return;
                }
                response.clear();
            }
            co_return;
        }

    protected:
        std::shared_ptr<RouterType> endpoint_handler_;
    };

    // Temporary FIX!!!! TODO!
    // template<class StreamHandler>
    // class Acceptor : public StreamHandler {
    // public:
    //     Acceptor(std::shared_ptr<Uvent> uvent,
    //              std::shared_ptr<typename StreamHandler::RouterType>   router,
    //              configuration::ConfigReader& cfg,
    //              size_t listener_index)
    //         : StreamHandler(std::move(router))
    //         , uvent_(std::move(uvent))
    //         {
    //             auto listeners = cfg.getListeners();
    //             if (listener_index >= listeners.size()) {
    //                 return;
    //             }
    //             const auto& listener = listeners[listener_index];
    //
    //             usub::uvent::settings::timeout_duration_ms = listener.timeout;
    //             std::string ip_addr = listener.ip_addr;
    //             server_socket_ = std::make_shared<usub::uvent::net::ServerSocket>(
    //                 ip_addr,
    //                 listener.port,
    //                 cfg.getBacklog(),
    //                 usub::uvent::utils::net::IPV(listener.ipv),
    //                 usub::uvent::utils::net::TCP);
    //         }
    //
    //     usub::uvent::task::Awaitable<void> loop() { //acceptCoroutine
    //         for (;;) {
    //             auto soc = co_await server_socket_->async_accept();
    // #ifdef UVENT_DEBUG
    //                 spdlog::info("{}", __PRETTY_FUNCTION__);
    // #endif
    //             if (soc) {
    //                 usub::uvent::system::co_spawn(this->clientCoroutine(std::move(soc)));
    //             }
    //         }
    //     }
    //
    // private:
    //     std::shared_ptr<usub::Uvent> uvent_;
    //     std::shared_ptr<usub::uvent::net::ServerSocket> server_socket_{nullptr};
    // };

    template<class StreamHandler>
    class Acceptor : public StreamHandler {
    public:
        Acceptor(std::shared_ptr<Uvent> uvent,
                 std::shared_ptr<typename StreamHandler::RouterType> router,
                 configuration::ConfigReader &cfg,
                 size_t listener_index)
            : StreamHandler(std::move(router)), uvent_(std::move(uvent)), cfg_(cfg), listener_index_(listener_index) {
            // Ничего не создаём здесь — переносим создание server_socket_ в loop().
        }

        usub::uvent::task::Awaitable<void> loop() {// acceptCoroutine
                                                   // // 1) Проверяем валидность индекса и достаём текущий listener из конфига
            auto listeners = cfg_.getListeners();
            if (listener_index_ >= listeners.size()) {
                co_return;// Нечего слушать
            }
            const auto &listener = listeners[listener_index_];

            // 2) Обновляем связанные настройки (например, таймаут).
            // Берём общесерверный timeout из [server] через getTimeout(), а не
            // listener.timeout: последний по умолчанию = 10 и в конфиге timeout
            // обычно лежит под [server], поэтому per-listener значение молча игнорировалось.
            usub::uvent::settings::timeout_duration_ms = cfg_.getTimeout();

            // 3) Лениво создаём серверный сокет, если он ещё не создан
            if (!server_socket_) {
                std::string ip_addr = listener.ip_addr;
                server_socket_ = std::make_shared<usub::uvent::net::TCPServerSocket>(
                        ip_addr,
                        listener.port,
                        cfg_.getBacklog(),
                        usub::uvent::utils::net::IPV(listener.ipv),
                        usub::uvent::utils::net::TCP);
            }

            // (опционально) если нужен форс-пересоздание при изменении конфига —
            // добавьте проверку и сбросьте server_socket_.reset(), затем пересоздайте.
            for (;;) {


                // 4) Принимаем соединение
                auto soc = co_await server_socket_->async_accept();
#ifdef UVENT_DEBUG
                spdlog::info("{}", __PRETTY_FUNCTION__);
#endif
                if (soc) {
                    usub::uvent::system::co_spawn(this->clientCoroutine(std::move(soc.value())));
                }
            }
        }

    private:
        std::shared_ptr<usub::Uvent> uvent_;
        configuration::ConfigReader &cfg_;
        size_t listener_index_{0};
        std::shared_ptr<usub::uvent::net::TCPServerSocket> server_socket_{nullptr};
    };


}// namespace usub::server

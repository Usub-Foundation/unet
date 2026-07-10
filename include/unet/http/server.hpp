#pragma once

#include <chrono>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <uvent/Uvent.h>


#include "unet/core/acceptor.hpp"
#include "unet/core/config.hpp"
#include "unet/core/streams/plaintext.hpp"
#include "unet/core/transport/tcp.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/router/radix.hpp"
#include "unet/http/router/regex.hpp"
#include "unet/http/session.hpp"
#include "unet/http/v1/connection.hpp"
#include "unet/http/v1/server_session.hpp"
#include "unet/http/v2/upgrade.hpp"


namespace usub::unet::http {

    template<class RouterType, typename... Streams>
    class ServerImpl {
    public:
        explicit ServerImpl(usub::Uvent &uvent, const usub::unet::core::Config &config)
            : config_(config), router_(std::make_shared<RouterType>()), runtime_(&uvent),
              h2c_enabled_(config_.getBool("HTTP.enable_h2c", false)),
              acceptors_(usub::unet::core::Acceptor<Streams>{}...) {
            wireH2cIfEnabled();
            (startAcceptor<Streams>(), ...);
        };

        explicit ServerImpl(usub::Uvent &uvent)
            : router_(std::make_shared<RouterType>()), runtime_(&uvent),
              acceptors_(usub::unet::core::Acceptor<Streams>{}...) {
            (startAcceptor<Streams>(), ...);
        }

        explicit ServerImpl(const usub::unet::core::Config &config)
            : config_(config), router_(std::make_shared<RouterType>()),
              h2c_enabled_(config_.getBool("HTTP.enable_h2c", false)),
              acceptors_(usub::unet::core::Acceptor<Streams>{}...) {
            wireH2cIfEnabled();
            (startAcceptor<Streams>(), ...);
        };

        explicit ServerImpl()
            : router_(std::make_shared<RouterType>()), acceptors_(usub::unet::core::Acceptor<Streams>{}...) {
            (startAcceptor<Streams>(), ...);
        }

        ~ServerImpl() = default;

        auto &handle(auto &&...args) { return this->router_->addRoute(std::forward<decltype(args)>(args)...); }

        auto &handleUpgrade(auto &&...args) {
            return this->router_->addUpgradeRoute(std::forward<decltype(args)>(args)...);
        }

        auto &addMiddleware(auto &&...args) {
            return this->router_->addMiddleware(std::forward<decltype(args)>(args)...);
        }

        auto &addErrorHandler(auto &&...args) {
            return this->router_->addErrorHandler(std::forward<decltype(args)>(args)...);
        }

        template<class Fn>
        void onHTTP1Connection(Fn &&fn) {
            router_->getMiddlewareChain().on_http1_connection = std::forward<Fn>(fn);
        }

        template<class Fn>
        void onHTTP2Connection(Fn &&fn) {
            router_->getMiddlewareChain().on_http2_connection_ = std::forward<Fn>(fn);
        }

        template<class Fn>
        void onHTTP2Stream(Fn &&fn) {
            router_->getMiddlewareChain().on_http2_stream = std::forward<Fn>(fn);
        }

        void h2FrameHandler(std::uint8_t type, v2::FrameHandler fn) {
            router_->getMiddlewareChain().h2_frame_handlers.registerHandler(type, fn);
        }
        void h2FrameHandler(v2::FRAME_TYPE type, v2::FrameHandler fn) {
            router_->getMiddlewareChain().h2_frame_handlers.registerHandler(type, fn);
        }

        void h2SettingHandler(std::uint16_t id, v2::SettingHandler fn) {
            router_->getMiddlewareChain().h2_setting_handlers.registerHandler(id, fn);
        }
        void h2SettingHandler(v2::SETTINGS id, v2::SettingHandler fn) {
            router_->getMiddlewareChain().h2_setting_handlers.registerHandler(id, fn);
        }

    private:
        void wireH2cIfEnabled() {
            if (!h2c_enabled_) return;
            auto &chain = router_->getMiddlewareChain();
            chain.h2c_upgrade = [router = this->router_,
                                 &chain](std::unique_ptr<usub::unet::core::transport::Transport> tr,
                                         usub::unet::Buffer carry, RequestReader req,
                                         v2::SettingsPayload settings) -> usub::uvent::task::Awaitable<void> {
                auto spawn =
                        v2::makeUpgradeSession(router, std::move(req), std::move(settings), chain.on_http2_connection_);
                co_await spawn(std::move(tr), std::move(carry));
                co_return;
            };
        }

        usub::unet::core::Config config_;
        std::shared_ptr<RouterType> router_;
        usub::Uvent *runtime_{nullptr};
        bool h2c_enabled_{false};

        template<class Socket, class Stream>
        usub::uvent::task::Awaitable<void> bootstrap(Stream &stream, Socket socket) {
            std::string alpn{};
            if constexpr (Stream::ssl) { alpn = co_await stream.negotiatedAlpn(socket); }
            co_await this->dispatchProtocol(stream, std::move(socket), std::move(alpn));
            co_return;
        }

        template<class Stream, class Socket>
        static void fillPeerFields(PeerInfo &p, Socket &s, std::string_view alpn) {
            p.alpn = alpn;
            p.ssl = Stream::ssl;
            auto addr = s.get_client_addr();
            std::visit(
                    [&](auto &sa) {
                        char buf[INET6_ADDRSTRLEN]{};
                        if constexpr (std::is_same_v<std::decay_t<decltype(sa)>, sockaddr_in>) {
                            if (::inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf))) p.ip = buf;
                            p.port = ntohs(sa.sin_port);
                        } else {
                            if (::inet_ntop(AF_INET6, &sa.sin6_addr, buf, sizeof(buf))) p.ip = buf;
                            p.port = ntohs(sa.sin6_port);
                        }
                    },
                    addr);
        }

        template<class Socket, class Stream>
        usub::uvent::task::Awaitable<void> dispatchProtocol(Stream &stream, Socket socket, std::string alpn) {
            PeerInfo peer{};
            fillPeerFields<Stream>(peer, socket, alpn);

            auto transport =
                    std::make_unique<usub::unet::core::transport::TCP<Stream, Socket>>(stream, std::move(socket));

            if (alpn == "h2") {
                v2::Connection c = this->router_->getMiddlewareChain().h2_config;
                c.peer = peer;
                auto h2 = std::make_shared<v2::ServerSession<RouterType>>(this->router_, std::move(c));
                if (auto &cb = router_->getMiddlewareChain().on_http2_connection_; cb) { cb(h2->connection()); }
                co_await h2->run(std::move(transport), {});
                co_return;
            }
            if (alpn == "http/1.1") {
                v1::Connection c{};
                c.peer = peer;
                auto session = std::make_shared<v1::ServerSession<RouterType>>(this->router_, std::move(c));
                co_await session->run(std::move(transport), {});
                co_return;
            }

            usub::unet::Buffer buffer;
            if (this->h2c_enabled_) {
                const ssize_t n = co_await transport->read(buffer);
                if (n <= 0) co_return;
                if (buffer.size() >= v2::h2_preface.size() &&
                    std::string_view{reinterpret_cast<const char *>(buffer.data()), v2::h2_preface.size()} ==
                            v2::h2_preface) {
                    v2::Connection c = this->router_->getMiddlewareChain().h2_config;
                    c.peer = peer;
                    auto h2 = std::make_shared<v2::ServerSession<RouterType>>(this->router_, std::move(c));
                    if (auto &cb = router_->getMiddlewareChain().on_http2_connection_; cb) { cb(h2->connection()); }
                    co_await h2->run(std::move(transport), std::move(buffer));
                    co_return;
                }
            }

            v1::Connection c{};
            c.peer = peer;
            auto session = std::make_shared<v1::ServerSession<RouterType>>(this->router_, std::move(c));
            co_await session->run(std::move(transport), std::move(buffer));
            co_return;
        }

        std::tuple<usub::unet::core::Acceptor<Streams>...> acceptors_;
        template<typename Stream>
        void startAcceptor() {
            auto &acc = std::get<usub::unet::core::Acceptor<Stream>>(acceptors_);
            auto make_on_connection = [this]() {
                return [this](Stream &stream, auto socket) -> usub::uvent::task::Awaitable<void> {
                    co_await this->bootstrap(stream, std::move(socket));
                };
            };
            if (runtime_) {
                runtime_->for_each_thread([&](int idx, usub::uvent::thread::ThreadLocalStorage *) {
                    usub::uvent::system::co_spawn_static(
                            acc.acceptLoop(make_on_connection(), this->config_, std::string_view{"HTTP"}), idx);
                });
            }
        }
    };

    using ServerRadix = ServerImpl<usub::unet::http::router::Radix, usub::unet::core::stream::PlainText>;
    using ServerRegex = ServerImpl<usub::unet::http::router::Regex, usub::unet::core::stream::PlainText>;

}// namespace usub::unet::http

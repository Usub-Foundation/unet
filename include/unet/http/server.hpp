#pragma once

#include <memory>
#include <string_view>

#include <uvent/Uvent.h>


#include "unet/core/acceptor.hpp"
#include "unet/core/config.hpp"
#include "unet/core/streams/plaintext.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/router/radix.hpp"
#include "unet/http/router/regex.hpp"
#include "unet/http/session.hpp"
#include "unet/http/v1/server_session.hpp"

// #include "unet/http/v2/server_session.hpp"


namespace usub::unet::http {

    template<class RouterType>
    class Bootstrap {
    public:
        explicit Bootstrap(std::shared_ptr<RouterType> router) : router_(std::move(router)) {}

        usub::uvent::task::Awaitable<SessionAction> onBytes(std::string_view data,
                                                            usub::unet::core::stream::Transport & /*transport*/) {
            if (decided_) {
                co_return SessionAction{.kind = SessionAction::Kind::Error};
            }

            pending_.append(data.data(), data.size());
            std::string_view probe{pending_};

            // Wait only while the currently buffered bytes still match an h2 preface prefix.
            if (probe.size() < kH2Preface.size() && is_h2_prefix(probe)) {
                co_return SessionAction{.kind = SessionAction::Kind::Continue};
            }

            decided_ = true;

            const bool is_h2_prior_knowledge =
                    probe.size() >= kH2Preface.size() && probe.substr(0, kH2Preface.size()) == kH2Preface;

            if (is_h2_prior_knowledge) {
                // TODO: when h2 session is ready
                co_return SessionAction{.kind = SessionAction::Kind::Close};
            }

            co_return SessionAction{
                    .kind             = SessionAction::Kind::Upgrade,
                    .carry_bytes      = std::move(pending_),
                    .next_session_box = std::any(SessionBox::make<ServerSession<VERSION::HTTP_1_1, RouterType>>(
                            this->router_)),
            };
        }

        usub::uvent::task::Awaitable<void> onClose() { co_return; }

    private:
        static constexpr std::string_view kH2Preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

        static bool is_h2_prefix(std::string_view v) {
            return v.size() <= kH2Preface.size() && kH2Preface.substr(0, v.size()) == v;
        }

        std::shared_ptr<RouterType> router_;
        std::string pending_;
        bool decided_{false};
    };


    // struct ServerConfig_idea {
    //     int uvent_threads{4};

    //     struct Connection {
    //         std::string ip_addres;
    //         std::uint16_t port;
    //         std::uint64_t backlog;
    //         enum class IPV { IPV4, IPV6 };
    //         IPV ip_version;
    //         enum class SocketType { TCP, UDP };
    //         SocketType socket_type;

    //         bool ssl = false;
    //     };
    // };

    // using ServerConfig = std::unordered_map<std::string, std::string>;

    template<class RouterType, typename... Streams>
    class ServerImpl {
    public:
        explicit ServerImpl(usub::Uvent &uvent, const usub::unet::core::Config &config)
            : config_(config), router_(std::make_shared<RouterType>()), runtime_(&uvent),
              acceptors_(usub::unet::core::Acceptor<Streams>{}...) {
            (start_acceptor<Streams>(), ...);
        };

        explicit ServerImpl(usub::Uvent &uvent)
            : router_(std::make_shared<RouterType>()), runtime_(&uvent),
              acceptors_(usub::unet::core::Acceptor<Streams>{}...) {
            (start_acceptor<Streams>(), ...);
        }

        //TODO: implement constructors
        explicit ServerImpl(const usub::unet::core::Config &config)
            : config_(config), router_(std::make_shared<RouterType>()),
              acceptors_(usub::unet::core::Acceptor<Streams>{}...) {
            (start_acceptor<Streams>(), ...);
        };

        explicit ServerImpl()
            : router_(std::make_shared<RouterType>()), acceptors_(usub::unet::core::Acceptor<Streams>{}...) {
            (start_acceptor<Streams>(), ...);
        }


        ~ServerImpl() = default;

        auto &handle(auto &&...args) { return this->router_->addRoute(std::forward<decltype(args)>(args)...); }

        auto &handleUpgrade(auto &&...args) {
            return this->router_->addUpgradeRoute(std::forward<decltype(args)>(args)...);
        }

        auto &addMiddleware(auto &&...args) {// allow for modifying router when wanted
            return this->router_->addMiddleware(std::forward<decltype(args)>(args)...);
        }

        auto &addErrorHandler(auto &&...args) {
            return this->router_->addErrorHandler(std::forward<decltype(args)>(args)...);
        }

    private:
        usub::unet::core::Config config_;
        std::shared_ptr<RouterType> router_;
        usub::Uvent *runtime_{nullptr};

        template<class Socket, class Stream>
        usub::uvent::task::Awaitable<void> runConnection(Stream &stream, Socket socket) {
            usub::uvent::utils::DynamicBuffer buffer;
            usub::unet::core::stream::Transport transport{
                    .send = [&](std::string_view out) -> usub::uvent::task::Awaitable<ssize_t> {
                        co_await stream.send(socket, out);
                        co_return static_cast<ssize_t>(out.size());
                    },
                    .close = [&]() -> usub::uvent::task::Awaitable<void> {
                        co_await stream.shutdown(socket);
                        co_return;
                    }};
            bool running = true;

            SessionBox active = SessionBox::make<Bootstrap<RouterType>>(this->router_);

            while (running) {
                buffer.clear();
                const ssize_t rdsz = co_await stream.read(socket, buffer);
                if (rdsz <= 0) {
                    co_await active.ops.on_close(active.state);
                    break;
                }

                SessionAction action = co_await active.ops.on_bytes(
                        active.state,
                        std::string_view{reinterpret_cast<const char *>(buffer.data()), static_cast<std::size_t>(rdsz)},
                        transport);

                // Handle chained upgrades in the same tick (bootstrap -> http1, http1 -> ws, etc.)
                while (action.kind == SessionAction::Kind::Upgrade) {
                    if (!action.next_session_box.has_value()) {
                        co_await transport.close();
                        co_return;
                    }
                    active = std::any_cast<SessionBox>(std::move(action.next_session_box));

                    if (action.carry_bytes.empty()) {
                        action = SessionAction{.kind = SessionAction::Kind::Continue};
                        break;
                    }
                    action = co_await active.ops.on_bytes(active.state, action.carry_bytes, transport);
                }

                if (!action.out_bytes.empty()) { co_await transport.send(action.out_bytes); }

                if (action.kind == SessionAction::Kind::Close || action.kind == SessionAction::Kind::Error) { break; }
            }

            co_await transport.close();
            co_return;
        }

        // TODO: Not sure that's the best
        std::tuple<usub::unet::core::Acceptor<Streams>...> acceptors_;
        template<typename Stream>
        void start_acceptor() {
            auto &acc = std::get<usub::unet::core::Acceptor<Stream>>(acceptors_);

            auto make_on_connection = [this]() {
                return [this](Stream &stream, auto socket) -> usub::uvent::task::Awaitable<void> {
                    co_await this->runConnection(stream, std::move(socket));
                };
            };

            if (runtime_) {
                runtime_->for_each_thread([&](int idx, usub::uvent::thread::ThreadLocalStorage *) {
                    usub::uvent::system::co_spawn_static(acc.acceptLoop(make_on_connection(), this->config_), idx);
                });
                return;
            }
        }
    };

    using ServerRadix = ServerImpl<usub::unet::http::router::Radix, usub::unet::core::stream::PlainText>;
    using ServerRegex = ServerImpl<usub::unet::http::router::Regex, usub::unet::core::stream::PlainText>;


}// namespace usub::unet::http

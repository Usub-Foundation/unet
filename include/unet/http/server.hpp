#pragma once

#include <memory>
#include <string_view>

#include <uvent/Uvent.h>


#include "unet/core/acceptor.hpp"
#include "unet/core/streams/plaintext.hpp"
#include "unet/http/request.hpp"
#include "unet/http/response.hpp"
#include "unet/http/router/radix.hpp"
#include "unet/http/session.hpp"
#include "unet/http/v1/server_session.hpp"
// #include "unet/http/v2/server_session.hpp"


namespace usub::unet::http {

    template<class... ServerSession>
    struct ServerSessionVisitor : ServerSession... {
        using ServerSession::operator()...;
    };
    template<class... ServerSession>
    ServerSessionVisitor(ServerSession...) -> ServerSessionVisitor<ServerSession...>;

    template<class RouterType>
    class Dispatcher {
    public:
        Dispatcher() = delete;
        explicit Dispatcher(std::shared_ptr<RouterType> router)
            : router_(std::move(router))// session_ starts as monostate
        {}

        usub::uvent::task::Awaitable<void> on_read(std::string_view data,
                                                   usub::unet::core::stream::Transport &transport) {
            std::string a;
            if (std::holds_alternative<std::monostate>(this->session_)) {
                if (data.size() >= h2_preface.size() && data.substr(0, h2_preface.size()) == h2_preface) {
                    // We assume preface comes in full
                    // session_.template emplace<ServerSession<VERSION::HTTP_2_0, RouterType>>(this->router_);
                } else {
                    session_.template emplace<ServerSession<VERSION::HTTP_1_1, RouterType>>(this->router_);
                }
            }

            co_await std::visit(ServerSessionVisitor{[&](std::monostate &) -> usub::uvent::task::Awaitable<void> {
                                                         co_return;// should not happen
                                                     },
                                                     [&](auto &s) -> usub::uvent::task::Awaitable<void> {
                                                         co_await s.on_read(data, transport);
                                                         co_return;
                                                     }},
                                this->session_);

            co_return;
        }

        usub::uvent::task::Awaitable<void> on_close() {
            std::visit(ServerSessionVisitor{[](std::monostate &) {}, [](auto &s) { s.on_close(); }}, this->session_);
            co_return;
        }

        usub::uvent::task::Awaitable<void> on_error(int ec) {
            std::visit(ServerSessionVisitor{[&](std::monostate &) {}, [&](auto &s) { s.on_error(ec); }},
                       this->session_);
            co_return;
        }

    private:
        std::shared_ptr<RouterType> router_;

        std::variant<std::monostate, ServerSession<VERSION::HTTP_1_1, RouterType>/*,
                     ServerSession<VERSION::HTTP_2_0, RouterType>*/>
                session_{};

        static constexpr std::string_view h2_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    };

    struct ServerConfig_idea {
        int uvent_threads{4};

        struct Connection {
            std::string ip_addres;
            std::uint16_t port;
            std::uint64_t backlog;
            enum class IPV { IPV4, IPV6 };
            IPV ip_version;
            enum class SocketType { TCP, UDP };
            SocketType socket_type;

            bool ssl = false;
        };
    };

    using ServerConfig = std::unordered_map<std::string, std::string>;

    template<class RouterType, typename... Streams>
    class ServerImpl {
    public:
        //TODO: implement constructors
        explicit ServerImpl(const ServerConfig &config)
            : config_(config), router_(std::make_shared<RouterType>()),
              acceptors_(usub::unet::core::Acceptor<Streams>{}...) {
            //TODO: for_each_thread
            (start_acceptor<Streams>(), ...);
        };

        // TODO: Make possible construction from existing
        // explicit ServerImpl(usub::Uvent &uvent);
        // ServerImpl(const ServerConfig &config, usub::Uvent &uvent);

        // ServerImpl() : router_(std::make_shared<RouterType>()) {
        //     //TODO: for_each_thread
        //     usub::unet::core::Acceptor<usub::unet::core::stream::PlainText> acceptor();
        //     usub::uvent::system::co_spawn(acceptor.acceptLoop<Dispatcher<RouterType>>(router_));
        //     return;
        // };

        explicit ServerImpl()
            : router_(std::make_shared<RouterType>()), acceptors_(usub::unet::core::Acceptor<Streams>{}...) {
            (start_acceptor<Streams>(), ...);
        }


        ~ServerImpl() = default;

        auto &handle(auto &&...args) { return this->router_->addHandler(std::forward<decltype(args)>(args)...); }

        auto &addMiddleware(auto &&...args) {// allow for modifying router when wanted
            return this->router_->addMiddleware(std::forward<decltype(args)>(args)...);
        }

        auto &addErrorHandler(auto &&...args) {
            return this->router_->addErrorHandler(std::forward<decltype(args)>(args)...);
        }

        // void run() { this->uvent_->run(); }

    private:
        ServerConfig config_;
        std::shared_ptr<RouterType> router_;
        // Dispatcher<RouterType> dispatcher_;

        // TODO: Not sure that's the best
        std::tuple<usub::unet::core::Acceptor<Streams>...> acceptors_;
        template<typename Stream>
        void start_acceptor() {
            auto &acc = std::get<usub::unet::core::Acceptor<Stream>>(acceptors_);
            usub::uvent::system::co_spawn(acc.template acceptLoop<Dispatcher<RouterType>>(router_));
        }
    };

    using ServerRadix = ServerImpl<usub::unet::http::router::Radix, usub::unet::core::stream::PlainText>;


}// namespace usub::unet::http

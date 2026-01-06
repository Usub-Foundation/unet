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


namespace usub::unet::http {


    template<class RouterType>
    class Dispatcher {
    public:
        Dispatcher() = delete;//("Unsafe to deafault construct")
        ~Dispatcher() = default;
        Dispatcher(std::shared_ptr<RouterType> router) : router_(router), session_(router_) {}

        usub::uvent::task::Awaitable<void> on_read(std::string_view data, usub::uvent::net::TCPClientSocket &socket) {
            // We need to determine the http version here, non tls allows for http0.9 http1.0 http1.1 and h2c

            co_await session_.on_read(data, socket);
            co_return;

            // We really expect that PRI will come in full, but no guarantees, this is not a commonly used protocol
            // and just buffering requests strings until we can make a decision is not acceptable performance wise.
            constexpr std::string_view h2c_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
            const bool looks_like_h2c =
                    data.size() >= h2c_preface.size() && data.substr(0, h2c_preface.size()) == h2c_preface;
            if (looks_like_h2c) {
                // TODO: initialize and hand off to HTTP/2 (h2c) handler
            } else {
                // TODO: initialize and hand off to HTTP/1.x parser/dispatcher
            }
            co_return;
        };
        usub::uvent::task::Awaitable<void> on_close() {
            session_.on_close();
            co_return;
        }
        usub::uvent::task::Awaitable<void> on_error(int error_code) {
            session_.on_error(error_code);
            co_return;
        }

    private:
        std::shared_ptr<RouterType> router_;
        //TODO: propper multi protocol handling std::variant seems like the best idea here?
        ServerSession<VERSION::HTTP_1_1, RouterType> session_;
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
            : config_(config), router_(std::make_shared<RouterType>()), uvent_(std::make_shared<usub::Uvent>(4)) {
            //TODO: for_each_thread
            usub::unet::core::Acceptor<usub::unet::core::stream::PlainText> acceptor(this->uvent_);
            usub::uvent::system::co_spawn(acceptor.acceptLoop<Dispatcher<RouterType>>(router_));
            return;
        };

        // TODO: Make possible construction from existing
        // explicit ServerImpl(usub::Uvent &uvent);
        // ServerImpl(const ServerConfig &config, usub::Uvent &uvent);

        // ServerImpl() : router_(std::make_shared<RouterType>()), uvent_(std::make_shared<usub::Uvent>(4)) {
        //     //TODO: for_each_thread
        //     usub::unet::core::Acceptor<usub::unet::core::stream::PlainText> acceptor(this->uvent_);
        //     usub::uvent::system::co_spawn(acceptor.acceptLoop<Dispatcher<RouterType>>(router_));
        //     return;
        // };

        explicit ServerImpl()
            : router_(std::make_shared<RouterType>()), uvent_(std::make_shared<usub::Uvent>(4)),
              acceptors_(usub::unet::core::Acceptor<Streams>{uvent_}...) {
            (start_acceptor<Streams>(), ...);
        }


        ~ServerImpl() = default;

        auto &handle(auto &&...args) { return this->router_->addHandler(std::forward<decltype(args)>(args)...); }

        auto &addMiddleware(auto &&...args) {// allow for modifying router when wanted
            return this->router_->addMiddleware(std::forward<decltype(args)>(args)...);
        }

        void run() { this->uvent_->run(); }

    private:
        ServerConfig config_;
        std::shared_ptr<RouterType> router_;
        std::shared_ptr<usub::Uvent> uvent_;
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

#pragma once

#include <uvent/Uvent.h>

namespace usub::unet::core {

    template<typename StreamHandler, typename Dispatcher>
    concept StreamHandlerFor =
            requires(StreamHandler sh,
                     usub::uvent::net::TCPClientSocket sock,
                     Dispatcher &d) {
                { sh.readLoop(std::move(sock), d) }
                  -> std::same_as<usub::uvent::task::Awaitable<void>>;
            };


    template<class StreamHandler>
    class Acceptor {
    public:
        Acceptor(std::shared_ptr<Uvent> uvent /*Other params*/) : uvent_(uvent) {}


        template<typename Dispatcher, typename RouterType>
            requires StreamHandlerFor<StreamHandler, Dispatcher>
        usub::uvent::task::Awaitable<void> acceptLoop(std::shared_ptr<RouterType> router) {
            // TODO: propper init
            usub::uvent::net::TCPServerSocket server_socket{
                    "0.0.0.0",
                    22813,
                    50,// backlog
                    usub::uvent::utils::net::IPV::IPV4,
                    usub::uvent::utils::net::TCP};

            for (;;) {
                auto soc = co_await server_socket.async_accept();

                if (soc) {
                    Dispatcher dispatcher{router};
                    usub::uvent::system::co_spawn(StreamHandler::readLoop(std::move(soc.value()), std::move(dispatcher)));
                }
            }
        }


    private:
        std::shared_ptr<Uvent>
                uvent_;
    };

}// namespace usub::unet::core

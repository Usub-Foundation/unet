#pragma once

#include <uvent/Uvent.h>

namespace usub::unet::core {
    template<class>
    inline constexpr bool always_false_v = false;

    template<class StreamHandler>
    class Acceptor {
    public:
        Acceptor() = default;
        ~Acceptor() = default;


        template<class OnConnection>
        usub::uvent::task::Awaitable<void> acceptLoop(OnConnection) {
            static_assert(always_false_v<StreamHandler>, "Acceptor not implemented for this stream type");
            //     // TODO: propper init
            //     usub::uvent::net::TCPServerSocket server_socket{"0.0.0.0", 22813,
            //                                                     50,// backlog
            //                                                     usub::uvent::utils::net::IPV::IPV4,
            //                                                     usub::uvent::utils::net::TCP};

            //     for (;;) {
            //         auto soc = co_await server_socket.async_accept();

            //         // PlainText stream;

            //         if (soc) {
            //             Dispatcher dispatcher{router};
            //             usub::uvent::system::co_spawn(dispatcher.run(stream, std::move(soc.value())));
            //             // StreamHandler::readLoop(std::move(soc.value()), std::move(dispatcher)));
            //         }
            //     }
            // }
            co_return;
        }
    };
}// namespace usub::unet::core

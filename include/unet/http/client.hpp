#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <uvent/Uvent.h>

#include "unet/core/acceptor.hpp"
#include "unet/core/streams/plaintext.hpp"
#include "unet/core/transport/tcp.hpp"
#include "unet/http/client_error.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"

#include "unet/http/v1/client_session.hpp"
#include "unet/http/v2/client_session.hpp"


namespace usub::unet::http {

    template<typename... Streams>
    class ClientImpl {
    public:
        explicit ClientImpl(usub::Uvent &uvent) : runtime_(&uvent),
                                                    streams_(std::make_unique<Streams>()...) {}

        ClientImpl() : streams_(std::make_unique<Streams>()...) {}

        usub::uvent::task::Awaitable<std::expected<Response, ClientError>>
        request(Request req) {
            auto reader = co_await stream(std::move(req));
            if (!reader) co_return std::unexpected(reader.error());

            Response resp{
                    .metadata = std::move(reader->metadata),
                    .headers  = std::move(reader->headers),
                    .body     = {},
                    .trailers = {},
            };
            auto body = co_await reader->readBody();
            if (body) resp.body = std::move(*body);
            resp.trailers = std::move(reader->trailers);
            co_return resp;
        }

        usub::uvent::task::Awaitable<std::expected<ResponseReader, ClientError>>
        stream(Request req) {
            if (req.metadata.uri.scheme.empty()) req.metadata.uri.scheme = "http";
            if (req.metadata.authority.empty()) {
                if (req.metadata.uri.authority.host.empty())
                    co_return std::unexpected(ClientError::InvalidRequest);
                req.metadata.authority = req.metadata.uri.authority.host;
                if (req.metadata.uri.authority.port != 0) {
                    req.metadata.authority += ':';
                    req.metadata.authority += std::to_string(req.metadata.uri.authority.port);
                }
            }
            co_return co_await this->dispatch(std::move(req));
        }

    private:
        usub::uvent::task::Awaitable<std::expected<ResponseReader, ClientError>>
        dispatch(Request req) {
            std::expected<ResponseReader, ClientError> result =
                    std::unexpected(ClientError::NoStreamForScheme);

            auto try_one = [&]<typename Stream>(std::unique_ptr<Stream> &s) -> usub::uvent::task::Awaitable<void> {
                if (result) co_return;
                if (!schemeMatches<Stream>(req.metadata.uri.scheme)) co_return;
                result = co_await this->runOnStream<Stream>(*s, req);
                co_return;
            };

            co_await std::apply(
                    [&](auto &...s) -> usub::uvent::task::Awaitable<void> {
                        (co_await try_one(s), ...);
                        co_return;
                    },
                    streams_);
            co_return result;
        }

        template<typename Stream>
        static bool schemeMatches(std::string_view scheme) {
            if constexpr (Stream::ssl) {
                return scheme == "https";
            } else {
                return scheme == "http";
            }
        }

        template<typename Stream>
        usub::uvent::task::Awaitable<std::expected<ResponseReader, ClientError>>
        runOnStream(Stream &stream, Request &req) {
            using SockType = usub::uvent::net::TCPClientSocket;

            SockType    sock;
            std::string host = req.metadata.uri.authority.host;
            std::string port = req.metadata.uri.authority.port != 0
                                       ? std::to_string(req.metadata.uri.authority.port)
                                       : std::string{"80"};

            auto err = co_await sock.async_connect(host, port);
            if (err) co_return std::unexpected(ClientError::ConnectFailed);

            auto transport = std::make_unique<
                    usub::unet::core::transport::TCP<Stream, SockType>>(stream, std::move(sock));

            auto session = std::make_shared<v1::ClientSession>();
            co_return co_await session->run(std::move(transport), std::move(req));
        }

        usub::Uvent                                  *runtime_{nullptr};
        std::tuple<std::unique_ptr<Streams>...>       streams_;
    };

    using ClientPlain = ClientImpl<usub::unet::core::stream::PlainText>;
}// namespace usub::unet::http

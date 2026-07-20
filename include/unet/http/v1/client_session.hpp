#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <uvent/Uvent.h>

#include "unet/core/io_provider.hpp"
#include "unet/core/transport/transport.hpp"
#include "unet/http/client_error.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/v1/wire/request_serializer.hpp"
#include "unet/http/v1/wire/response_parser.hpp"


namespace usub::unet::http::v1 {

    class ClientSession : public std::enable_shared_from_this<ClientSession> {
    public:
        using Transport = usub::unet::core::transport::Transport;

        usub::uvent::task::Awaitable<std::expected<ResponseReader, ClientError>>
        run(std::unique_ptr<Transport> transport, Request req) {
            fillDefaultHeaders(req);

            const std::string wire = RequestSerializer::serialize(req);
            const ssize_t sent = co_await transport->send(wire);
            if (sent < static_cast<ssize_t>(wire.size())) co_return std::unexpected(ClientError::WriteFailed);

            usub::unet::Buffer buf;
            std::string acc;
            ResponseParser parser;
            Response head{};

            while (parser.getContext().state != ResponseParser::STATE::HEADERS_DONE &&
                   parser.getContext().state != ResponseParser::STATE::BODY_UNTIL_CLOSE &&
                   parser.getContext().state != ResponseParser::STATE::COMPLETE) {
                buf.clear();
                const ssize_t n = co_await transport->read(buf);
                if (n <= 0) co_return std::unexpected(ClientError::ReadFailed);
                acc.append(reinterpret_cast<const char *>(buf.data()), buf.size());

                std::string_view view = acc;
                std::string_view::const_iterator cur = view.begin();
                std::string_view::const_iterator end = view.end();
                auto parsed = parser.parse(head, cur, end);
                if (!parsed) co_return std::unexpected(ClientError::ParseFailed);
                acc.erase(0, static_cast<std::size_t>(cur - view.begin()));
            }

            ResponseReader reader{};
            reader.metadata = std::move(head.metadata);
            reader.headers = std::move(head.headers);

            if (!head.body.empty()) (void) co_await reader.getBodyChannel().push(std::move(head.body));

            if (parser.getContext().state == ResponseParser::STATE::COMPLETE) {
                reader.getBodyChannel().close();
                co_return reader;
            }

            usub::uvent::system::co_spawn_static(this->bodyPump(this->shared_from_this(), std::move(transport),
                                                                std::move(parser), std::move(acc),
                                                                reader.sharedBodyChannel()),
                                                 0);
            co_return reader;
        }

    private:
        static void fillDefaultHeaders(Request &req) {
            if (req.metadata.version == VERSION{}) req.metadata.version = VERSION::HTTP_1_1;
            if (!req.headers.value("host") && !req.metadata.authority.empty())
                req.headers.addHeader(std::string_view{"Host"}, req.metadata.authority);
            if (!req.body.empty() && !req.headers.value("content-length"))
                req.headers.addHeader(std::string_view{"Content-Length"}, std::to_string(req.body.size()));
        }

        usub::uvent::task::Awaitable<void> bodyPump(std::shared_ptr<ClientSession> self,
                                                    std::unique_ptr<Transport> transport, ResponseParser parser,
                                                    std::string acc, std::shared_ptr<BodyReaderChannel> channel) {
            Response scratch{};
            while (!acc.empty() && parser.getContext().state != ResponseParser::STATE::COMPLETE) {
                scratch.body.clear();
                std::string_view view = acc;
                std::string_view::const_iterator cur = view.begin();
                std::string_view::const_iterator end = view.end();
                auto parsed = parser.parse(scratch, cur, end);
                acc.erase(0, static_cast<std::size_t>(cur - view.begin()));
                if (!parsed) {
                    channel->signalError(BODY_ERROR::PARSER_ERROR);
                    co_return;
                }
                if (!scratch.body.empty()) (void) co_await channel->push(std::move(scratch.body));
            }

            usub::unet::Buffer buf;
            while (parser.getContext().state != ResponseParser::STATE::COMPLETE) {
                buf.clear();
                const ssize_t n = co_await transport->read(buf);
                if (n <= 0) {
                    if (parser.getContext().state == ResponseParser::STATE::BODY_UNTIL_CLOSE) break;
                    channel->signalError(BODY_ERROR::PROTOCOL_ERROR);
                    co_return;
                }
                acc.assign(reinterpret_cast<const char *>(buf.data()), buf.size());
                while (!acc.empty() && parser.getContext().state != ResponseParser::STATE::COMPLETE) {
                    scratch.body.clear();
                    std::string_view view = acc;
                    std::string_view::const_iterator cur = view.begin();
                    std::string_view::const_iterator end = view.end();
                    auto parsed = parser.parse(scratch, cur, end);
                    acc.erase(0, static_cast<std::size_t>(cur - view.begin()));
                    if (!parsed) {
                        channel->signalError(BODY_ERROR::PARSER_ERROR);
                        co_return;
                    }
                    if (!scratch.body.empty()) (void) co_await channel->push(std::move(scratch.body));
                }
            }
            channel->close();
            co_return;
        }
    };

}// namespace usub::unet::http::v1

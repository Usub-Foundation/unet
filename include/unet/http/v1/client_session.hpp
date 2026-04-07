#pragma once

#include <limits>
#include <string_view>

#include "unet/core/streams/stream.hpp"
#include "unet/http/client_session.hpp"
#include "unet/http/session.hpp"
#include "unet/http/v1/wire/response_parser.hpp"

namespace usub::unet::http {
    template<>
    class ClientSession<VERSION::HTTP_1_1> {
    public:
        ClientSession() = default;
        ~ClientSession() = default;

        usub::uvent::task::Awaitable<SessionAction>
        onBytes(std::string_view data, usub::unet::core::stream::Transport & /*transport*/) {
            if (this->state_.complete) { co_return SessionAction{.kind = SessionAction::Kind::Close}; }
            if (!data.empty()) { this->state_.saw_bytes = true; }

            if (this->state_.read_until_close_mode) {
                if (data.size() > std::numeric_limits<std::size_t>::max() - this->state_.response.body.size()) {
                    this->state_.error = ClientError{
                            .code = ClientError::CODE::PARSE_FAILED,
                            .message = "response body exceeded max size in until-close mode",
                    };
                    co_return SessionAction{.kind = SessionAction::Kind::Error};
                }
                this->state_.response.body.append(data.data(), data.size());
                co_return SessionAction{.kind = SessionAction::Kind::Continue};
            }

            auto begin = data.begin();
            const auto end = data.end();
            auto parsed = this->parser_.parse(this->state_.response, begin, end);
            if (!parsed) {
                this->state_.error = ClientError{
                        .code = ClientError::CODE::PARSE_FAILED,
                        .message = parsed.error().message,
                        .parse_error = parsed.error(),
                };
                co_return SessionAction{.kind = SessionAction::Kind::Error};
            }

            auto &ctx = this->parser_.getContext();
            if (ctx.state == v1::ResponseParser::STATE::HEADERS_DONE) {
                if (ctx.after_headers == v1::ResponseParser::AfterHeaders::COMPLETE) {
                    this->state_.complete = true;
                    co_return SessionAction{.kind = SessionAction::Kind::Close};
                }
                if (ctx.after_headers == v1::ResponseParser::AfterHeaders::UNTIL_CLOSE) {
                    this->state_.read_until_close_mode = true;
                    co_return SessionAction{.kind = SessionAction::Kind::Continue};
                }
            }

            if (ctx.after_headers == v1::ResponseParser::AfterHeaders::UNTIL_CLOSE &&
                ctx.state == v1::ResponseParser::STATE::COMPLETE) {
                this->state_.read_until_close_mode = true;
                co_return SessionAction{.kind = SessionAction::Kind::Continue};
            }

            if (ctx.state == v1::ResponseParser::STATE::COMPLETE) {
                this->state_.complete = true;
                co_return SessionAction{.kind = SessionAction::Kind::Close};
            }

            co_return SessionAction{.kind = SessionAction::Kind::Continue};
        }

        usub::uvent::task::Awaitable<void> onClose() {
            if (this->state_.read_until_close_mode || this->state_.complete ||
                this->parser_.getContext().state == v1::ResponseParser::STATE::BODY_UNTIL_CLOSE ||
                this->parser_.getContext().state == v1::ResponseParser::STATE::COMPLETE) {
                this->state_.complete = true;
                co_return;
            }

            this->state_.error = ClientError{
                    .code = this->state_.saw_bytes ? ClientError::CODE::PARSE_FAILED : ClientError::CODE::READ_FAILED,
                    .message = this->state_.saw_bytes ? "connection closed before response was complete"
                                                      : "connection closed before response started",
            };
            co_return;
        }

        const ClientSessionState &state() const { return this->state_; }
        bool isComplete() const { return this->state_.complete; }
        bool hasError() const { return this->state_.error.has_value(); }
        bool sawBytes() const { return this->state_.saw_bytes; }
        const std::optional<ClientError> &error() const { return this->state_.error; }

    private:
        ClientSessionState state_{};
        v1::ResponseParser parser_{};
    };
}// namespace usub::unet::http

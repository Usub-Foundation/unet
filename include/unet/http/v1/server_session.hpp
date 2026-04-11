#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/session.hpp"
#include "unet/http/upgrade_context.hpp"
#include "unet/http/v1/wire/request_parser.hpp"
#include "unet/http/v1/wire/response_serializer.hpp"

namespace usub::unet::http {

    template<typename RouterType>
    class ServerSession<VERSION::HTTP_1_1, RouterType> {
    public:
        explicit ServerSession(std::shared_ptr<RouterType> router) : router_(std::move(router)) {}
        ServerSession() = delete;
        ~ServerSession() = default;

        usub::uvent::task::Awaitable<SessionAction> onBytes(std::string_view data,
                                                            usub::unet::core::stream::Transport &transport) {
            std::string_view::const_iterator begin = data.begin();
            const std::string_view::const_iterator end = data.end();
            auto &state = this->request_reader_.getContext().state;

        continue_parse:
            if (begin == end) { co_return {}; }
            auto result = this->request_reader_.step(this->request_, begin, end);

            if (!result) {
                this->response_.metadata.status_code = result.error().expected_status;
                this->router_->error("log", this->request_, this->response_);
                this->router_->error(std::to_string(this->response_.metadata.status_code), this->request_,
                                     this->response_);
                goto send_body;
            }

            switch (result.value().kind) {
                case STEP::CONTINUE: {
                    goto continue_parse;
                    co_return {};
                }
                case STEP::HEADERS: {
                    this->response_.metadata.version = this->request_.metadata.version;
                    auto match = this->router_->match(this->request_);
                    if (!match) {
                        state = v1::RequestParser::STATE::FAILED;
                        // TODO: Status code & message
                        this->response_.metadata.status_code = match.error();
                        this->router_->error("log", this->request_, this->response_);
                        this->router_->error(std::to_string(this->response_.metadata.status_code), this->request_,
                                             this->response_);
                        break;
                    }
                    this->current_match_ = std::move(match.value());
                    auto middleware_result =
                            this->router_->getMiddlewareChain().execute(MIDDLEWARE_PHASE::HEADER, this->request_,
                                                                        this->response_)
                                    ? this->router_->runRouteMiddleware(MIDDLEWARE_PHASE::HEADER, *this->current_match_,
                                                                        this->request_, this->response_)
                                    : false;
                    if (!middleware_result) { break; }
                    if (result.value().complete) { goto complete; }
                    goto continue_parse;
                }
                case STEP::BODY: {
                    auto middleware_result = this->router_->getMiddlewareChain().execute(
                                                     MIDDLEWARE_PHASE::BODY, this->request_, this->response_)
                                                     ? this->router_->runRouteMiddleware(
                                                               MIDDLEWARE_PHASE::BODY, *this->current_match_,
                                                               this->request_, this->response_)
                                                     : false;
                    if (!middleware_result) { break; }
                    goto continue_parse;
                    break;
                }
                case STEP::COMPLETE: {
                complete:
                    if (this->current_match_.has_value()) {
                        if (this->current_match_->route &&
                            this->current_match_->route->kind == RouteKind::Upgrade) {
                            UpgradeContext upgrade_ctx;
                            co_await this->router_->invokeUpgrade(*this->current_match_, this->request_,
                                                                  this->response_, upgrade_ctx);
                            if (upgrade_ctx.accepted) {
                                co_await transport.send(this->response_writer_.serialize(this->response_));
                                std::any next_box{upgrade_ctx.make_session()};
                                this->response_ = {};
                                this->request_  = {};
                                this->current_match_.reset();
                                co_return SessionAction{
                                    .kind             = SessionAction::Kind::Upgrade,
                                    .next_session_box = std::move(next_box),
                                };
                            }
                            // not accepted — fall through and send response_ as normal HTTP
                        } else {
                            co_await this->router_->invoke(*this->current_match_, this->request_, this->response_);
                        }
                    }
                    break;
                }
            }

        send_body:
            const SessionAction::Kind action_kind = this->prepareResponse();
            // co_await this->write_response(transport);
            co_await transport.send(this->response_writer_.serialize(this->response_));
            this->response_ = {};
            this->request_ = {};
            this->current_match_.reset();
            co_return SessionAction{.kind = action_kind};
        }

        usub::uvent::task::Awaitable<void> onClose() {
            // if (this->current_route_) {
            //     auto &context = this->request_reader_.getContext();
            //     context.state = v1::RequestParser::STATE::FAILED;
            // }
            co_return;
        }
        usub::uvent::task::Awaitable<void> on_error(int error_code) {
            if (this->current_match_.has_value()) {
                auto &context = this->request_reader_.getContext();
                context.state = v1::RequestParser::STATE::FAILED;
            }
            co_return;
        }


    private:
        Request request_{};
        Response response_{};
        v1::RequestParser request_reader_{};
        v1::ResponseSerializer response_writer_{};
        std::shared_ptr<RouterType> router_;
        std::optional<typename RouterType::MatchResult> current_match_{};

        SessionAction::Kind prepareResponse() {
            if (this->response_.metadata.status_code >= 400) {
                bool has_connection_header = false;
                for (auto &header: this->response_.headers) {
                    if (header.key == "connection") {
                        header.value = "close";
                        has_connection_header = true;
                    }
                }
                if (!has_connection_header) {
                    this->response_.headers.addHeader(std::string_view{"Connection"}, std::string_view{"close"});
                }
                return SessionAction::Kind::Close;
            }

            return SessionAction::Kind::Continue;
        }

        usub::uvent::task::Awaitable<void> send(usub::unet::core::stream::Transport &transport, std::string_view data) {
            co_await transport.send(data);
            co_return;
        }

    };// namespace usub::unet::http

    template<class RouterType>
    using Http1Session = ServerSession<VERSION::HTTP_1_1, RouterType>;
}// namespace usub::unet::http

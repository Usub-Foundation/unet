#pragma once

#include <memory>
#include <string_view>

#include "unet/http/request.hpp"
#include "unet/http/response.hpp"
#include "unet/http/router/route.hpp"
#include "unet/http/session.hpp"
#include "unet/http/v1/ingress_session.hpp"
#include "unet/http/v1/request_parser.hpp"
#include "unet/http/v1/response_serializer.hpp"

namespace usub::unet::http {

    template<typename RouterType>
    class ServerSession<VERSION::HTTP_1_1, RouterType> {
    public:
        explicit ServerSession(std::shared_ptr<RouterType> router) : router_(std::move(router)) {}
        ServerSession() = delete;
        ~ServerSession() = default;

        usub::uvent::task::Awaitable<void> on_read(std::string_view data,
                                                   usub::unet::core::stream::Transport &transport) {
            this->callbacks_.send = [&](std::string_view data) -> usub::uvent::task::Awaitable<void> {
                co_await this->send(transport, data);
                co_return;
            };
            co_await this->server_stream_.process(data, this->callbacks_);
            co_return;
        }


        usub::uvent::task::Awaitable<void> on_read_old(std::string_view data,
                                                       usub::unet::core::stream::Transport &transport) {
            //     std::string_view::const_iterator begin = data.begin();
            //     const std::string_view::const_iterator end = data.end();
            //     auto &state = this->request_reader_.getContext().state;

            // continue_parse:
            //     if (begin == end) { co_return; }
            //     auto result = this->request_reader_.parse(this->request_, begin, end);

            //     if (!result) {
            //         this->response_.metadata.status_code = result.error().expected_status;
            //         state = v1::RequestParser::STATE::FAILED;
            //     }

            //     if (!this->current_route_ && state == v1::RequestParser::STATE::HEADERS_DONE) {
            //         this->response_.metadata.version = this->request_.metadata.version;
            //         auto match = this->router_->match(this->request_);
            //         if (!match) {
            //             state = v1::RequestParser::STATE::FAILED;
            //             // TODO: Status code & message
            //             this->response_.metadata.status_code = match.error();
            //         } else {
            //             this->current_route_ = match.value();
            //         }
            //     }

            //     switch (state) {
            //         case v1::RequestParser::STATE::HEADERS_DONE:
            //             [[fallthrough]];
            //         case v1::RequestParser::STATE::TRAILERS_DONE: {
            //             auto middleware_result = this->invoke_middleware(MIDDLEWARE_PHASE::HEADER, request_, response_);
            //             if (!middleware_result) { break; }
            //             state = this->request_reader_.getContext().post_header_middleware_state;
            //             if (state != v1::RequestParser::STATE::COMPLETE) {
            //                 goto continue_parse;
            //             } else {
            //                 goto complete;
            //             }
            //             break;
            //         }
            //         case v1::RequestParser::STATE::DATA_CHUNK_DONE: {
            //             auto middleware_result = this->invoke_middleware(MIDDLEWARE_PHASE::BODY, request_, response_);
            //             if (!middleware_result) { break; }
            //             goto continue_parse;
            //             break;
            //         }
            //         case v1::RequestParser::STATE::COMPLETE: {
            //         complete:
            //             auto handler = this->current_route_->handler;
            //             co_await handler(request_, response_);

            //             break;
            //         }
            //         case v1::RequestParser::STATE::FAILED:
            //             this->router_->error("log", this->request_, this->response_);
            //             this->router_->error(std::to_string(this->response_.metadata.status_code), this->request_,
            //                                  this->response_);
            //             break;
            //         default:
            //             // any other state
            //             goto end;
            //             break;
            //     }
            // send_body:
            //     co_await this->write_response(transport);
            // end:
            //     co_return;
            co_return;
        };
        usub::uvent::task::Awaitable<void> on_close() {
            // if (this->current_route_) {
            //     auto &context = this->request_reader_.getContext();
            //     context.state = v1::RequestParser::STATE::FAILED;
            // }
            co_return;
        }
        usub::uvent::task::Awaitable<void> on_error(int error_code) {
            if (this->current_route_) {
                auto &context = this->request_reader_.getContext();
                context.state = v1::RequestParser::STATE::FAILED;
            }
            co_return;
        }


    private:
        v1::IngressSession server_stream_;
        std::shared_ptr<RouterType> router_;
        router::Route *current_route_;

        std::expected<router::Route *, STATUS_CODE> matchRoute(Request &request) {
            auto match = this->router_->match(request);
            if (!match) { return match; }
            this->current_route_ = match.value();
            return match;
        }

        bool invokeMiddleware(const MIDDLEWARE_PHASE &phase, Request &request, Response &response) {
            return this->router_->getMiddlewareChain().execute(phase, request, response)
                           ? this->current_route_->middleware_chain.execute(phase, request, response)
                           : false;
        }

        usub::uvent::task::Awaitable<void> invokeHandler(Request &request, Response &response) {
            if (this->current_route_) {
                auto handler = this->current_route_->handler;
                co_await handler(request, response);
                co_return;
            }
            co_return;
        }

        bool setRoute(Request &request) {
            this->response_.metadata.version = this->request_.metadata.version;
            auto match = this->router_->match(this->request_);
            if (!match) {
                // state = v1::RequestParser::STATE::FAILED;
                // TODO: Status code & message
                this->response_.metadata.status_code = match.error();
                return false;
            }
            this->current_route_ = match.value();
            return true;
        }

        bool invokeErrorHandler(const std::string level, Request &request, Response &response) {
            this->router_->error(level, request, response);
            return true;
        }

        usub::uvent::task::Awaitable<void> send(usub::unet::core::stream::Transport &transport, std::string_view data) {
            co_await transport.send(data);
            co_return;
        }

        AsyncCallback callbacks_{
                .on_headers_done = [this](Request &request, Response &response) -> bool {
                    return this->invokeMiddleware(MIDDLEWARE_PHASE::HEADER, request, response);
                },
                .on_body_chunk_done = [this](Request &request, Response &response) -> bool {
                    return this->invokeMiddleware(MIDDLEWARE_PHASE::BODY, request, response);
                },
                .on_error = [this](const std::string level, Request &request, Response &response) -> bool {
                    return this->invokeErrorHandler(level, request, response);
                },
                .invoke_handler = [this](Request &request, Response &response) -> usub::uvent::task::Awaitable<void> {
                    co_await this->invokeHandler(request, response);
                },
                .set_route = [this](Request &request) -> std::expected<router::Route *, STATUS_CODE> {
                    return this->matchRoute(request);
                },

        };
    };

    template<class RouterType>
    using Http1Session = ServerSession<VERSION::HTTP_1_1, RouterType>;
}// namespace usub::unet::http

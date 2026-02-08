#include "unet/http/v1/ingress_session.hpp"

namespace usub::unet::http::v1 {
    usub::uvent::task::Awaitable<void> IngressSession::process(std::string_view data, const AsyncCallback &callbacks) {
        std::string_view::const_iterator begin = data.begin();
        const std::string_view::const_iterator end = data.end();
        auto &state = this->request_reader_.getContext().state;

    continue_parse:
        if (begin == end) { co_return; }
        auto result = this->request_reader_.parse(this->request_, begin, end);

        if (!result) {
            this->response_.metadata.status_code = result.error().expected_status;
            state = v1::RequestParser::STATE::FAILED;
        }

        // if (!this->current_route_ && state == v1::RequestParser::STATE::HEADERS_DONE) {
        //     this->response_.metadata.version = this->request_.metadata.version;
        //     auto match = this->router_->match(this->request_);
        //     if (!match) {
        //         state = v1::RequestParser::STATE::FAILED;
        //         // TODO: Status code & message
        //         this->response_.metadata.status_code = match.error();
        //     } else {
        //         this->current_route_ = match.value();
        //     }
        // }

        switch (state) {
            case v1::RequestParser::STATE::HEADERS_DONE: {
                this->response_.metadata.version = this->request_.metadata.version;
                auto match = callbacks.set_route(this->request_);
                if (!match) {
                    state = v1::RequestParser::STATE::FAILED;
                    // TODO: Status code & message
                    this->response_.metadata.status_code = match.error();
                    co_return;
                }
                [[fallthrough]];
            }
            case v1::RequestParser::STATE::TRAILERS_DONE: {
                auto middleware_result = callbacks.on_headers_done(this->request_, this->response_);
                if (!middleware_result) { break; }
                state = this->request_reader_.getContext().post_header_middleware_state;
                if (state != v1::RequestParser::STATE::COMPLETE) {
                    goto continue_parse;
                } else {
                    goto complete;
                }
                break;
            }
            case v1::RequestParser::STATE::DATA_CHUNK_DONE: {
                auto middleware_result = callbacks.on_body_chunk_done(this->request_, this->response_);
                if (!middleware_result) { break; }
                goto continue_parse;
                break;
            }
            case v1::RequestParser::STATE::COMPLETE: {
            complete:
                // auto handler = this->current_route_->handler;
                co_await callbacks.invoke_handler(this->request_, this->response_);

                break;
            }
            case v1::RequestParser::STATE::FAILED:
                callbacks.on_error("log", this->request_, this->response_);
                callbacks.on_error(std::to_string(this->response_.metadata.status_code), this->request_,
                                   this->response_);
                // this->router_->error("log", this->request_, this->response_);
                // this->router_->error(std::to_string(this->response_.metadata.status_code), this->request_,
                //                      this->response_);
                break;
            default:
                // any other state
                goto end;
                break;
        }
    send_body:
        // co_await this->write_response(transport);
        co_await callbacks.send(this->response_writer_.serialize(this->response_));
    end:
        co_return;
    }
};// namespace usub::unet::http::v1
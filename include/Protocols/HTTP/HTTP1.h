#ifndef HTTP1_H
#define HTTP1_H

#include <string>

#include <uvent/net/Socket.h>
#include <uvent/tasks/Awaitable.h>

#include "Protocols/HTTP/EndpointHandler.h"
#include "Protocols/HTTP/Message.h"

namespace usub::server::protocols::http {

    template<class RouterType = usub::server::protocols::http::HTTPEndpointHandler>
    class HTTP1 {
    private:
        Request request_{};
        Response response_{};
        std::shared_ptr<RouterType> endpoint_handler_{};
        Route *matched_route_{nullptr};

    public:
        HTTP1() = default;
        HTTP1(const std::shared_ptr<RouterType> endpoint_handler) : endpoint_handler_{endpoint_handler} {
        }

        ~HTTP1() = default;

        HTTP1 &setEndpointHandler(const std::shared_ptr<RouterType> &endpoint_handler) {
            this->endpoint_handler_ = endpoint_handler;
            return *this;
        }

        // Response ErrorPageHandler(Request &request);

        // Unsecure
        usub::uvent::task::Awaitable<void> readCallback(const std::string &data, usub::uvent::net::TCPClientSocket &socket) {
#ifdef UVENT_DEBUG
            spdlog::info("Entering readCallback");
#endif
            std::optional<std::pair<Route *, bool>> match{};
            std::string::const_iterator c{};

            // auto parser = this->request_.parseHTTP1_X_yield(data);
            // bool ret{false};
        retry_parse:
            if (c == data.end()) co_return;
            // if (ret) co_return;
            c = this->request_.parseHTTP1_X(data, c);
            // Wait for the complete request line before routing.
            if (this->request_.getState() < REQUEST_STATE::PRE_HEADERS) co_return;

            if (!this->matched_route_) {
                match = this->endpoint_handler_->match(this->request_);
                this->response_.setHTTPVersion(this->request_.getHTTPVersion());
                this->response_.setSocket(&socket);
                if (match) {
                    auto &[route, methodAllowed] = match.value();
                    if (!methodAllowed) {// this->ErrorPageHandler(this->request_);
                        this->request_.setState(REQUEST_STATE::METHOD_NOT_ALLOWED);
                        this->response_.setStatus(405);
                        co_return;
                    }
                    this->response_.addHeader("Server", "usub");
                    this->response_.setRoute(route);
                    this->matched_route_ = route;
                } else {
                    this->request_.setState(REQUEST_STATE::NOT_FOUND);
                    this->response_.setStatus(404);
                    // this->ErrorPageHandler(this->request_)
                    co_return;
                }
            }

            bool middleware_rv;

            switch (this->request_.getState()) {
                case REQUEST_STATE::PRE_HEADERS:
                    middleware_rv = this->endpoint_handler_->getMiddlewareChain().execute(MiddlewarePhase::SETTINGS, this->request_, this->response_);
                    if (!middleware_rv) {
                        this->request_.setState(REQUEST_STATE::BAD_REQUEST);
                        co_return;
                    }

                    middleware_rv = this->matched_route_->middleware_chain.execute(MiddlewarePhase::SETTINGS, this->request_, this->response_);
                    if (!middleware_rv) {
                        this->request_.setState(REQUEST_STATE::BAD_REQUEST);
                        co_return;
                    }

                    goto retry_parse;
                case REQUEST_STATE::HEADERS_PARSED:
                    middleware_rv = this->endpoint_handler_->getMiddlewareChain().execute(MiddlewarePhase::HEADER, this->request_, this->response_);
                    if (!middleware_rv) {
                        this->request_.setState(REQUEST_STATE::BAD_REQUEST);
                        co_return;
                    }
                    middleware_rv = this->matched_route_->middleware_chain.execute(MiddlewarePhase::HEADER, this->request_, this->response_);
                    if (!middleware_rv) {
                        this->request_.setState(REQUEST_STATE::BAD_REQUEST);
                        co_return;
                    }

                    goto retry_parse;
                case REQUEST_STATE::DATA_FRAGMENT:
                    middleware_rv = this->endpoint_handler_->getMiddlewareChain().execute(MiddlewarePhase::BODY, this->request_, this->response_);
                    if (!middleware_rv) {
                        this->request_.setState(REQUEST_STATE::BAD_REQUEST);
                        co_return;
                    }
                    middleware_rv = this->matched_route_->middleware_chain.execute(MiddlewarePhase::BODY, this->request_, this->response_);
                    if (!middleware_rv) {
                        this->request_.setState(REQUEST_STATE::BAD_REQUEST);
                        co_return;
                    }

                    goto retry_parse;
                case REQUEST_STATE::FINISHED:

                    co_await this->matched_route_->handler(this->request_, this->response_);
                    middleware_rv = this->endpoint_handler_->getMiddlewareChain().execute(MiddlewarePhase::RESPONSE, this->request_, this->response_);
                    if (!middleware_rv) {
                        this->request_.setState(REQUEST_STATE::BAD_REQUEST);
                        co_return;
                    }

                    if (!this->response_.isSent()) {
                        middleware_rv = this->matched_route_->middleware_chain.execute(MiddlewarePhase::RESPONSE, this->request_, this->response_);
                    }
                    this->matched_route_ = {};
                    if (!middleware_rv || this->response_.isSent()) {
                        this->request_.setState(REQUEST_STATE::BAD_REQUEST);
                        co_return;
                    }
                    break;
                default:
                    this->matched_route_ = {};
                    break;
            }
            co_return;
        }

        void readCallbackSync(const std::string &data, usub::uvent::net::TCPClientSocket &socket) {
#ifdef UVENT_DEBUG
            spdlog::info("Entering readCallback");
#endif
            std::optional<std::pair<Route *, bool>> match{};
            std::string::const_iterator c{};

        retry_parse:
            if (c == data.end()) return;
            c = this->request_.parseHTTP1_X(data, c);

            // if (this->request_.getState() < STATE::HEADERS_PARSED) co_return;

            if (!this->matched_route_) {
                match = this->endpoint_handler_->match(this->request_);
                this->response_.setHTTPVersion(this->request_.getHTTPVersion());
                this->response_.setSocket(&socket);
                if (match) {
                    auto &[route, methodAllowed] = match.value();
                    this->response_.addHeader("Server", "usub");
                    this->response_.setRoute(route);
                    if (!methodAllowed) {// this->ErrorPageHandler(this->request_);
                        return;
                    }
                    this->matched_route_ = route;
                } else {
                    this->request_.setState(REQUEST_STATE::NOT_FOUND);
                    this->response_.setStatus(404);
                    // this->ErrorPageHandler(this->request_)
                    return;
                }
            }

            bool middleware_rv;

            switch (this->request_.getState()) {
                case REQUEST_STATE::PRE_HEADERS:
                    middleware_rv = this->endpoint_handler_->getMiddlewareChain().execute(MiddlewarePhase::SETTINGS, this->request_, this->response_);
                    if (!middleware_rv) {
                        return;
                    }
                    goto retry_parse;
                case REQUEST_STATE::HEADERS_PARSED:
                    middleware_rv = this->endpoint_handler_->getMiddlewareChain().execute(MiddlewarePhase::HEADER, this->request_, this->response_);
                    if (!middleware_rv) {
                        return;
                    }
                    goto retry_parse;
                case REQUEST_STATE::DATA_FRAGMENT:
                    middleware_rv = this->endpoint_handler_->getMiddlewareChain().execute(MiddlewarePhase::BODY, this->request_, this->response_);
                    if (!middleware_rv) {
                        return;
                    }
                    goto retry_parse;
                case REQUEST_STATE::FINISHED:

                    this->matched_route_->handler(this->request_, this->response_);
                    if (!this->response_.isSent()) {
                        middleware_rv = this->matched_route_->middleware_chain.execute(MiddlewarePhase::RESPONSE, this->request_, this->response_);
                    }
                    this->matched_route_ = {};
                    if (!middleware_rv || this->response_.isSent()) {
                        return;
                    }
                    break;
                default:
                    this->matched_route_ = {};
                    break;
            }
            return;
        }


        void writeCallback(const std::string &data) {
        }

        void errorCallback(const std::string &data) {
        }

        const usub::server::protocols::http::Request &getRequest() const {
            return this->request_;
        }

        usub::server::protocols::http::Request &getRequest() {
            return this->request_;
        }

        usub::server::protocols::http::Response &getResponse() {
            return this->response_;
        }
    };

}// namespace usub::server::protocols::http


#endif// HTTP1_H

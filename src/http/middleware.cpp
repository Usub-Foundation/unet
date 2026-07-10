#include "unet/http/middleware.hpp"

namespace usub::unet::http {

    MiddlewareChain &MiddlewareChain::emplace_back(MIDDLEWARE_PHASE phase,
                                                   std::function<MiddlewareFunctionType> middleware) {
        switch (phase) {
            case MIDDLEWARE_PHASE::HEADER:
                this->header_middlewares_.emplace_back(std::move(middleware));
                break;
            case MIDDLEWARE_PHASE::RESPONSE:
                this->response_middlewares_.emplace_back(std::move(middleware));
                break;
        }
        return *this;
    }

    MiddlewareChain &MiddlewareChain::addMiddleware(MIDDLEWARE_PHASE phase,
                                                    std::function<MiddlewareFunctionType> middleware) {
        switch (phase) {
            case MIDDLEWARE_PHASE::HEADER:
                this->header_middlewares_.emplace_back(std::move(middleware));
                break;
            case MIDDLEWARE_PHASE::RESPONSE:
                this->response_middlewares_.emplace_back(std::move(middleware));
                break;
        }
        return *this;
    }

    usub::uvent::task::Awaitable<bool>
    MiddlewareChain::execute(MIDDLEWARE_PHASE phase, RequestReader &request, ResponseWriter &response) const {
        const std::vector<std::function<MiddlewareFunctionType>> *middlewares = nullptr;

        switch (phase) {
            case MIDDLEWARE_PHASE::HEADER:
                middlewares = &this->header_middlewares_;
                break;
            case MIDDLEWARE_PHASE::RESPONSE:
                middlewares = &this->response_middlewares_;
                break;
        }

        if (middlewares) {
            for (const auto &middleware: *middlewares) {
                if (!co_await middleware(request, response)) {
                    // Middleware has handled the response; halt the chain
                    co_return false;
                }
            }
        }
        co_return true;
    }


}// namespace usub::unet::http

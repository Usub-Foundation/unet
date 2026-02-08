#include "unet/http/router/route.hpp"

namespace usub::unet::http::router {
    Route::Route(const std::set<std::string> &methods,
                 const std::vector<std::string> &params,
                 std::function<FunctionType> handler,
                 bool accept_all)
        : param_names(params), handler(handler) {
        if (accept_all) {
            this->accept_all_methods = accept_all;
            return;
        }
        this->allowed_method_tokenns = std::move(methods);
    }

    Route &Route::addMiddleware(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware) {
        this->middleware_chain.emplace_back(phase, std::move(middleware));
        return *this;
    }
}// namespace usub::unet::http::router
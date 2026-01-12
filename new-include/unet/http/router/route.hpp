#pragma once

#include <set>
#include <uvent/Uvent.h>

#include "unet/http/middleware.hpp"

namespace usub::unet::http::router {
    // TODO: That requires review

    /**
     * @typedef FunctionType
     * @brief Defines the signature for endpoint handler functions. For ease of changing the signature.
     *
     * Endpoint handler functions are asynchronous and handle HTTP requests and responses.
     *
     * @param request The HTTP request object.
     * @param response The HTTP response object.
     * @return usub::uvent::task::Awaitable<void> An awaitable task representing the asynchronous operation.
     *
     * @see Route
     * @see HTTPEndpointHandler
     */
    // using FunctionType = void(Request &, Response &);
    using FunctionType = usub::uvent::task::Awaitable<void>(Request &, Response &);

    /**
     * @struct Route
     * @brief Represents a route in the HTTP endpoint handling system.
     *
     * A `Route` defines the criteria for matching HTTP requests and specifies the middleware and handler
     * functions to process matched requests.
     */
    struct Route {
        /**
         * @brief Indicates whether the route accepts all method tokens.
         */
        bool accept_all_methods{};

        /**
         * @brief Set of allowed HTTP method tokens (e.g., GET, POST) for this route.
         */
        std::set<std::string> allowed_method_tokenns{};

        /**
         * @brief Middleware chain associated with this route.
         * 
         * Middleware functions added to this chain are executed in the specified phases
         * (HEADER, BODY, RESPONSE) when a request matches this route.
         */
        MiddlewareChain middleware_chain{};

        /**
         * @brief Vector of parameter names extracted from the route's path pattern.
         * 
         * These parameters are typically enclosed in `{}` brackets within the route's path. Though not limited to path only.
         */
        std::vector<std::string> param_names{};

        /**
         * @brief Handler function to process requests that match this route.
         * 
         * This function is responsible for generating the appropriate response based on the request.
         */
        std::function<FunctionType> handler{};

        /**
         * @brief Constructs a `Route` with the specified parameters.
         *
         * @param m Set of allowed HTTP methods.
         * @param regex Regular expression to match the request path.
         * @param params Vector of parameter names extracted from the path pattern.
         * @param f Handler function to process matched requests.
         * @param aa Optional flag indicating whether the route accepts all method tokens.
         *
         * @see MiddlewareChain
         * @see FunctionType
         */
        Route(const std::set<std::string> &methods, const std::vector<std::string> &params,
              std::function<FunctionType> handler, bool accept_all = false);

        /**
         * @brief Default constructor.
         *
         * Initializes a `Route` with default values.
         */
        Route() = default;

        /**
         * @brief Adds a middleware function to the specified phase for this route.
         *
         * @param phase The phase during which the middleware should be executed.
         * @param middleware The middleware function to add.
         * @return Route& Reference to the current `Route` object for chaining.
         *
         * @see MiddlewarePhase
         * @see MiddlewareFunctionType
         */
        Route &addMiddleware(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware);
    };
}// namespace usub::unet::http::router
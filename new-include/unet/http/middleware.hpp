#pragma once

#include <functional>

#include <uvent/Uvent.h>
#include <vector>

#include "unet/http/request.hpp"
#include "unet/http/response.hpp"

namespace usub::unet::http {


    /**
     * @enum MiddlewarePhase
     * @brief Represents the different phases in the middleware processing pipeline.
     *
     * Middleware functions can be executed during specific phases of handling an HTTP request and response.
     */
    enum class MIDDLEWARE_PHASE {
        METADATA,///< Middleware executed when the uri or pseudo headers become known, to set up handler specific limits
        HEADER,  ///< Middleware executed after the headers were parsed.
        BODY,    ///< Middleware executed during the body processing phase in certain data types.
        RESPONSE ///< Middleware executed during sending response, called only once per response.
    };

    /**
     * @typedef MiddlewareFunctionType
     * @brief Defines the signature for middleware functions.
     *
     * Middleware functions take a constant reference to a `Request` and a reference to a `Response`.
     * They return a boolean indicating whether to continue executing subsequent middleware.
     *
     * @param request The HTTP request object.
     * @param response The HTTP response object.
     * @return true If middleware processing should continue.
     * @return false If middleware processing should halt.
     * 
     * @note if false is returned, the middleware processing will halt and the response will not be sent automatically.
     */
    // using MiddlewareFunctionType = usub::uvent::task::Awaitable<bool>(Request &, Response &);
    using MiddlewareFunctionType = bool(Request &, Response &);

    using StatusHandlerFunctionType = usub::uvent::task::Awaitable<void>(Request &, Response &);

    /**
     * @typedef GenericErrorFunctionType
     * @brief Defines the signature for generic error handling functions.
     * 
     * These functions are invoked when an error occurs during HTTP on a network operation. or other unexpected situations.
     * They receive the original `Request`, the current `Response`, and an `UnetError` object detailing the error.
     * @param request The HTTP request object.
     * @param response The HTTP response object.
     * @param error The `UnetError` object containing error details.
     * 
     * @note Intended for logging mostly.
     */
    using GenericErrorFunctionType = usub::uvent::task::Awaitable<void>(
            const Request &, const Response &, const usub::unet::utils::UnetError &);

    /**
     * @class MiddlewareChain
     * @brief Manages a chain of middleware functions for processing HTTP requests and responses.
     *
     * The `MiddlewareChain` class allows adding, managing, and executing middleware functions
     * during different phases of handling an HTTP request. Middleware functions can perform tasks
     * such as authentication, logging, data validation, and response manipulation.
     *
     * @see MiddlewarePhase
     * @see MiddlewareFunctionType
     */
    class MiddlewareChain {
    private:
        /**
         * @brief Vector of middleware functions to be executed during the SETTINGS phase.
         *
         * Each middleware function in this vector is executed in the order they were added.
         */
        std::vector<std::function<MiddlewareFunctionType>> settings_middlewares_;

        /**
         * @brief Vector of middleware functions to be executed during the HEADER phase.
         *
         * Each middleware function in this vector is executed in the order they were added.
         */
        std::vector<std::function<MiddlewareFunctionType>> header_middlewares_;

        /**
         * @brief Vector of middleware functions to be executed during the BODY phase.
         *
         * Each middleware function in this vector is executed in the order they were added.
         */
        std::vector<std::function<MiddlewareFunctionType>> body_middlewares_;

        /**
         * @brief Vector of middleware functions to be executed during the RESPONSE phase.
         *
         * Each middleware function in this vector is executed in the order they were added.
         */
        std::vector<std::function<MiddlewareFunctionType>> response_middlewares_;

    public:
        /**
         * @brief Adds a middleware function to the specified phase.
         *
         * This function allows you to add a middleware function to a particular phase
         * (HEADER, BODY, or RESPONSE) of the middleware processing pipeline.
         *
         * @param phase The phase during which the middleware should be executed.
         * @param middleware The middleware function to add.
         * @return MiddlewareChain& Reference to the current `MiddlewareChain` object for chaining.
         *
         * @see MiddlewarePhase
         * @see MiddlewareFunctionType
         */
        MiddlewareChain &emplace_back(MIDDLEWARE_PHASE phase,
                                      std::function<MiddlewareFunctionType> middleware);
        MiddlewareChain &addMiddleware(MIDDLEWARE_PHASE phase,
                                       std::function<MiddlewareFunctionType> middleware);
        /**
         * @brief Executes all middleware functions associated with a specific phase.
         *
         * This function iterates through the middleware functions registered for the given phase
         * and executes them in the order they were added. If any middleware function returns `false`,
         * the execution halts, preventing subsequent middleware from running.
         *
         * @param phase The phase during which the middleware should be executed.
         * @param request The HTTP request object.
         * @param response The HTTP response object.
         * @return true If all middleware functions executed successfully.
         * @return false If any middleware function halted the execution.
         *
         * @see MiddlewarePhase
         * @see MiddlewareFunctionType
         */
        bool execute(MIDDLEWARE_PHASE phase, Request &request, Response &response) const;
    };


}// namespace usub::unet::http
#ifndef SERVER_H
#define SERVER_H

#include <functional>
#include <memory>
#include <string>
#include <tuple>

#include "Protocols/HTTP/EndpointHandler.h"
#include "Protocols/HTTP/RadixRouter.h"
#include "server/Acceptor.h"

namespace usub::server {
    template<class RouterType, template<typename> class... StreamHandlerTemplates>
    class ServerImpl {
    private:
        configuration::ConfigReader config_;
        std::shared_ptr<RouterType> endpoint_handler_{};
        std::shared_ptr<usub::Uvent> uvent_{nullptr};
        std::tuple<Acceptor<StreamHandlerTemplates<RouterType> >...> acceptors_;

        template<size_t... Indices>
        auto createAcceptors(std::index_sequence<Indices...>) {
            return std::tuple<Acceptor<StreamHandlerTemplates<RouterType> >...>(
                Acceptor<StreamHandlerTemplates<RouterType> >(uvent_, endpoint_handler_, config_, Indices)...);
        }

    public:
        ServerImpl(/* args */) = default;

        ServerImpl(const std::string &config_path)
            : config_(config_path), endpoint_handler_(std::make_shared<RouterType>()),
              uvent_(std::make_shared<usub::Uvent>(int(config_.getThreads()))),
              acceptors_(createAcceptors(std::make_index_sequence<sizeof...(StreamHandlerTemplates)>{})) {
            spawnAcceptors();
        }

        ServerImpl(std::shared_ptr<usub::Uvent> ext_uvent)
            : config_(), endpoint_handler_(std::make_shared<RouterType>()), uvent_(std::move(ext_uvent)),
              acceptors_(createAcceptors(std::make_index_sequence<sizeof...(StreamHandlerTemplates)>{})) {
            spawnAcceptors();
        }

        ~ServerImpl() = default;

        inline auto getConfig() const {
            return config_;
        }

        auto &handle(std::string_view method_token,
                     const std::string &endpoint,
                     const std::function<usub::server::protocols::http::FunctionType> function) {
            return this->endpoint_handler_->addHandler(method_token, endpoint, function);
        }

        auto &handle(std::set<std::string> methods,
                     const std::string &endpoint,
                     const std::function<usub::server::protocols::http::FunctionType> function) {
            return this->endpoint_handler_->addHandler(methods, endpoint, function);
        }

        template<typename T = RouterType>
            requires(not std::is_same_v<T, usub::server::protocols::http::RadixRouter>)
        auto &handle(std::initializer_list<const char *> methods,
                     const std::string &endpoint,
                     std::function<usub::server::protocols::http::FunctionType> function) {
            std::set<std::string> method_set{methods.begin(), methods.end()};
            return this->endpoint_handler_->addHandler(method_set, endpoint, std::move(function));
        }

        // для RadixRouter
        template<typename T = RouterType>
            requires std::is_same_v<T, usub::server::protocols::http::RadixRouter>
        auto &handle(std::string_view method,
                     const std::string &endpoint,
                     std::function<usub::server::protocols::http::FunctionType> function,
                     std::unordered_map<std::string_view, const usub::server::protocols::http::param_constraint *> &&
                     constraints) {
            std::set<std::string> methods{method.data()};
            return this->endpoint_handler_->addHandler(methods, endpoint, function, std::move(constraints));
        }

        template<typename T = RouterType>
            requires std::is_same_v<T, usub::server::protocols::http::RadixRouter>
        auto &handle(const std::set<std::string> &methods,
                     const std::string &endpoint,
                     std::function<usub::server::protocols::http::FunctionType> function,
                     std::unordered_map<std::string_view, const usub::server::protocols::http::param_constraint *> &&
                     constraints) {
            return this->endpoint_handler_->addHandler(methods, endpoint, function, std::move(constraints));
        }

        template<typename T = RouterType>
            requires std::is_same_v<T, usub::server::protocols::http::RadixRouter>
        auto &handle(std::initializer_list<const char *> methods,
                     const std::string &endpoint,
                     std::function<usub::server::protocols::http::FunctionType> function,
                     std::unordered_map<std::string_view, const usub::server::protocols::http::param_constraint *> &&
                             constraints = {}) {
            std::set<std::string> method_set{methods.begin(), methods.end()};
            return this->endpoint_handler_->addHandler(method_set, endpoint, function, std::move(constraints));
        }

        void handleWebsocket(std::string_view method_token, const std::string &endpoint,
                             const std::function<usub::server::protocols::http::FunctionType> &function) {
        }

        usub::server::protocols::http::MiddlewareChain &addMiddleware(
            usub::server::protocols::http::MiddlewarePhase phase,
            std::function<usub::server::protocols::http::MiddlewareFunctionType> middleware) {
            return this->endpoint_handler_->addMiddleware(phase, std::move(middleware));
        }

        void run() {
            std::cout << "Starting server on ports: ";
            const auto &listeners = config_.getListeners();
            if (listeners.empty()) {
                std::cout << "default: 8080";
            } else {
                for (size_t i = 0; i < listeners.size(); ++i) {
                    std::cout << listeners[i].port;
                    if (i < listeners.size() - 1) {
                        std::cout << ", ";
                    }
                }
            }
            std::cout << ", Version: 1.0.0." << std::endl;
            this->uvent_->run();
        }

        void stop() {
            this->uvent_->stop();
        }

        void spawnAcceptors() {
            std::apply([&](auto &... a) {
                           (usub::uvent::system::co_spawn(a.loop()), ...);
                       },
                       acceptors_);
        }

        std::shared_ptr<usub::Uvent> &getUvent() {
            return uvent_;
        }
    };

    using Server = usub::server::ServerImpl<protocols::http::HTTPEndpointHandler, PlainHTTPStreamHandler>;
} // namespace usub::server

using ServerHandler = usub::uvent::task::Awaitable<void>;

template<auto MemFn, class T>
auto bind_handler(T &obj) {
    return [&obj](auto &req, auto &res) -> usub::uvent::task::Awaitable<void> {
        co_await (obj.*MemFn)(req, res);
    };
}

#endif

#pragma once

#include <cctype>
#include <chrono>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string_view>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>

#include <uvent/Uvent.h>

#include "unet/core/streams/stream.hpp"
#include "unet/http/client_session.hpp"
#include "unet/http/client_types.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/v1/client_session.hpp"
#include "unet/http/v1/wire/request_serializer.hpp"

namespace usub::unet::http {
    template<typename... Streams>
    class ClientImpl {
    public:
        template<typename T, typename = void>
        struct has_ssl_tag : std::false_type {};

        template<typename T>
        struct has_ssl_tag<T, std::void_t<decltype(T::ssl)>> : std::bool_constant<static_cast<bool>(T::ssl)> {};

        ClientImpl() = default;
        explicit ClientImpl(Streams... streams) : streams_(std::move(streams)...) {}
        ~ClientImpl() = default;

        static constexpr bool has_ssl_stream = (has_ssl_tag<Streams>::value || ...);

        template<typename Stream>
        Stream &stream() {
            static_assert((std::is_same_v<Stream, Streams> || ...),
                          "Requested stream is not part of this ClientImpl specialization");
            return std::get<Stream>(this->streams_);
        }

        template<typename Stream>
        void setStreamConfig(const typename Stream::Config &config)
            requires(requires { typename Stream::Config; })
        {
            auto &state = std::get<ConnectionState<Stream>>(this->connection_states_);
            state.managed_stream_config = config;
            state.applied_stream_config.reset();
        }

        template<typename Stream>
        void setStreamConfig(typename Stream::Config &&config)
            requires(requires { typename Stream::Config; })
        {
            auto &state = std::get<ConnectionState<Stream>>(this->connection_states_);
            state.managed_stream_config = std::move(config);
            state.applied_stream_config.reset();
        }

        usub::uvent::task::Awaitable<void> close() {
            auto closer = [this]<typename Stream>() -> usub::uvent::task::Awaitable<void> {
                auto &stream_instance = std::get<Stream>(this->streams_);
                auto &state = std::get<ConnectionState<Stream>>(this->connection_states_);
                co_await this->closeConnection(stream_instance, state);
                co_return;
            };

            (co_await closer.template operator()<Streams>(), ...);
            co_return;
        }

        usub::uvent::task::Awaitable<std::expected<Response, ClientError>>
        request(Request request, const ClientRequestOptions &options = ClientRequestOptions{}) {
            if (request.metadata.uri.scheme.empty()) {
                if constexpr (has_ssl_stream) {
                    request.metadata.uri.scheme = "https";
                } else {
                    request.metadata.uri.scheme = "http";
                }
            }

            if (request.metadata.authority.empty()) {
                co_return std::unexpected(ClientError{
                        .code = ClientError::CODE::INVALID_REQUEST,
                        .message = "missing authority field reuqest.metadata.authority.empty() == true",
                });
            }

            if (request.metadata.uri.scheme == "https") {
                using SslStream = typename first_stream_by_ssl<true, Streams...>::type;
                if constexpr (!std::is_void_v<SslStream>) {
                    auto &stream_instance = std::get<SslStream>(this->streams_);
                    auto response = co_await this->requestWithStream(stream_instance, std::move(request), options);
                    co_return response;
                } else {
                    using PlainStream = typename first_stream_by_ssl<false, Streams...>::type;
                    if constexpr (!std::is_void_v<PlainStream>) {
                        auto &stream_instance = std::get<PlainStream>(this->streams_);
                        auto response = co_await this->requestWithStream(stream_instance, std::move(request), options);
                        co_return response;
                    }
                }
            } else if (request.metadata.uri.scheme == "http") {
                using PlainStream = typename first_stream_by_ssl<false, Streams...>::type;
                if constexpr (!std::is_void_v<PlainStream>) {
                    auto &stream_instance = std::get<PlainStream>(this->streams_);
                    auto response = co_await this->requestWithStream(stream_instance, std::move(request), options);
                    co_return response;
                }
            }

            co_return std::unexpected(ClientError{
                    .code = ClientError::CODE::INVALID_REQUEST,
                    .message = "no compatible stream for scheme: " + request.metadata.uri.scheme,
            });
        }

    private:
        template<typename T, typename = void>
        struct stream_is_ssl : std::false_type {};

        template<typename T>
        struct stream_is_ssl<T, std::void_t<decltype(T::ssl)>> : std::bool_constant<static_cast<bool>(T::ssl)> {};

        template<typename T, typename = void>
        struct has_stream_config : std::false_type {};

        template<typename T>
        struct has_stream_config<T, std::void_t<typename T::Config>> : std::true_type {};

        template<bool WantSsl, typename... Ts>
        struct first_stream_by_ssl {
            using type = void;
        };

        template<bool WantSsl, typename T, typename... Ts>
        struct first_stream_by_ssl<WantSsl, T, Ts...> {
            using type = std::conditional_t<stream_is_ssl<T>::value == WantSsl, T,
                                            typename first_stream_by_ssl<WantSsl, Ts...>::type>;
        };

        template<typename Stream, bool HasConfig = has_stream_config<Stream>::value>
        struct ConnectionConfigState {};

        template<typename Stream>
        struct ConnectionConfigState<Stream, true> {
            std::optional<typename Stream::Config> managed_stream_config{};
            std::optional<typename Stream::Config> applied_stream_config{};
        };

        template<typename Stream>
        struct ConnectionState : ConnectionConfigState<Stream> {
            usub::uvent::net::TCPClientSocket socket{};
            std::string reuse_key{};
            bool reusable{false};
            bool has_idle_expiry{false};
            std::chrono::steady_clock::time_point idle_expiry{};
        };

        struct RoutePlan {
            bool use_proxy{false};
            bool tunnel{false};
            std::string connect_host{};
            std::uint16_t connect_port{0};
            std::string reuse_key{};
            std::optional<std::string> proxy_authorization{};
        };

        static constexpr std::size_t max_retry_attempts = 2;

        static bool isWhitespace(char ch) {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        }

        static std::string_view trimView(std::string_view value) {
            while (!value.empty() && isWhitespace(value.front())) { value.remove_prefix(1); }
            while (!value.empty() && isWhitespace(value.back())) { value.remove_suffix(1); }
            return value;
        }

        static char toLower(char ch) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }

        static bool iequals(std::string_view lhs, std::string_view rhs) {
            if (lhs.size() != rhs.size()) { return false; }
            for (std::size_t i = 0; i < lhs.size(); ++i) {
                if (toLower(lhs[i]) != toLower(rhs[i])) { return false; }
            }
            return true;
        }

        static std::string toLowerCopy(std::string_view value) {
            std::string lowered;
            lowered.reserve(value.size());
            for (char ch: value) { lowered.push_back(toLower(ch)); }
            return lowered;
        }

        static bool containsHeaderToken(const usub::unet::header::Headers &headers, std::string_view header_name,
                                        std::string_view token) {
            for (const auto &header: headers.all(header_name)) {
                std::string_view value = header.value;
                while (!value.empty()) {
                    const std::size_t comma = value.find(',');
                    std::string_view part = (comma == std::string_view::npos) ? value : value.substr(0, comma);
                    part = trimView(part);
                    if (iequals(part, token)) { return true; }
                    if (comma == std::string_view::npos) { break; }
                    value.remove_prefix(comma + 1);
                }
            }
            return false;
        }

        static std::optional<std::chrono::seconds> parseKeepAliveTimeout(const usub::unet::header::Headers &headers) {
            for (const auto &header: headers.all("keep-alive")) {
                std::string_view value = header.value;
                while (!value.empty()) {
                    const std::size_t comma = value.find(',');
                    std::string_view part = (comma == std::string_view::npos) ? value : value.substr(0, comma);
                    part = trimView(part);

                    const std::size_t eq = part.find('=');
                    if (eq != std::string_view::npos) {
                        std::string_view key = trimView(part.substr(0, eq));
                        std::string_view raw = trimView(part.substr(eq + 1));
                        if (iequals(key, "timeout")) {
                            if (!raw.empty() && raw.front() == '"' && raw.back() == '"' && raw.size() >= 2) {
                                raw = raw.substr(1, raw.size() - 2);
                            }

                            std::uint64_t seconds = 0;
                            if (!raw.empty()) {
                                bool valid = true;
                                for (char ch: raw) {
                                    if (!std::isdigit(static_cast<unsigned char>(ch))) {
                                        valid = false;
                                        break;
                                    }
                                    seconds = seconds * 10 + static_cast<std::uint64_t>(ch - '0');
                                }
                                if (valid) {
                                    return std::chrono::seconds{
                                            static_cast<std::chrono::seconds::rep>(seconds)};
                                }
                            }
                        }
                    }

                    if (comma == std::string_view::npos) { break; }
                    value.remove_prefix(comma + 1);
                }
            }
            return std::nullopt;
        }

        static bool requestKeepsAlive(const Request &request) {
            if (containsHeaderToken(request.headers, "connection", "close")) { return false; }

            if (request.metadata.version == VERSION::HTTP_1_0) {
                return containsHeaderToken(request.headers, "connection", "keep-alive");
            }

            return true;
        }

        static bool responseKeepsAlive(const Response &response, bool read_until_close_mode) {
            if (read_until_close_mode) { return false; }
            if (containsHeaderToken(response.headers, "connection", "close")) { return false; }

            if (response.metadata.version == VERSION::HTTP_1_0) {
                return containsHeaderToken(response.headers, "connection", "keep-alive");
            }

            return true;
        }

        static bool socketIsOpen(usub::uvent::net::TCPClientSocket &socket) {
            auto *header = socket.get_raw_header();
            return header && header->fd >= 0;
        }

        static std::uint16_t defaultPortForRequest(const Request &request, bool is_ssl_stream) {
            if (request.metadata.uri.authority.port != 0) { return request.metadata.uri.authority.port; }
            return is_ssl_stream ? static_cast<std::uint16_t>(443) : static_cast<std::uint16_t>(80);
        }

        static std::string makeAuthority(const Request &request, std::uint16_t port) {
            if (!request.metadata.authority.empty()) {
                if (request.metadata.authority.find(':') != std::string::npos) {
                    return request.metadata.authority;
                }
                const bool default_port =
                        (request.metadata.uri.scheme == "https" && port == 443) ||
                        (request.metadata.uri.scheme == "http" && port == 80);
                if (default_port) { return request.metadata.authority; }
                return request.metadata.authority + ":" + std::to_string(port);
            }

            std::string authority = request.metadata.uri.authority.host;
            const bool default_port =
                    (request.metadata.uri.scheme == "https" && port == 443) ||
                    (request.metadata.uri.scheme == "http" && port == 80);
            if (!default_port) { authority += ":" + std::to_string(port); }
            return authority;
        }

        static std::string buildAbsoluteTarget(const Request &request, std::uint16_t port) {
            std::string absolute_target = request.metadata.uri.scheme;
            absolute_target += "://";
            absolute_target += makeAuthority(request, port);
            if (!request.metadata.uri.path.empty()) {
                absolute_target += request.metadata.uri.path;
            } else {
                absolute_target += "/";
            }
            if (!request.metadata.uri.query.empty()) {
                absolute_target += "?";
                absolute_target += request.metadata.uri.query;
            }
            return absolute_target;
        }

        static std::string encodeBase64(std::string_view input) {
            static constexpr char alphabet[] =
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

            std::string encoded;
            encoded.reserve(((input.size() + 2) / 3) * 4);

            for (std::size_t i = 0; i < input.size(); i += 3) {
                const std::uint32_t b0 = static_cast<unsigned char>(input[i]);
                const std::uint32_t b1 = (i + 1 < input.size()) ? static_cast<unsigned char>(input[i + 1]) : 0U;
                const std::uint32_t b2 = (i + 2 < input.size()) ? static_cast<unsigned char>(input[i + 2]) : 0U;
                const std::uint32_t chunk = (b0 << 16U) | (b1 << 8U) | b2;

                encoded.push_back(alphabet[(chunk >> 18U) & 0x3fU]);
                encoded.push_back(alphabet[(chunk >> 12U) & 0x3fU]);
                encoded.push_back(i + 1 < input.size() ? alphabet[(chunk >> 6U) & 0x3fU] : '=');
                encoded.push_back(i + 2 < input.size() ? alphabet[chunk & 0x3fU] : '=');
            }

            return encoded;
        }

        static std::optional<std::string> buildProxyAuthorization(const ClientRequestOptions &options) {
            if (!options.proxy.has_value()) { return std::nullopt; }

            const auto &proxy = *options.proxy;
            if (!proxy.username.has_value() && !proxy.password.has_value()) { return std::nullopt; }

            const std::string credentials = proxy.username.value_or("") + ":" + proxy.password.value_or("");
            return "Basic " + encodeBase64(credentials);
        }

        template<typename Stream>
        static std::expected<RoutePlan, ClientError> buildRoutePlan(const Request &request,
                                                                    const ClientRequestOptions &options) {
            RoutePlan plan{};
            const std::uint16_t target_port = defaultPortForRequest(request, stream_is_ssl<Stream>::value);
            const std::string authority = makeAuthority(request, target_port);

            if (!options.proxy.has_value()) {
                plan.connect_host = request.metadata.uri.authority.host;
                plan.connect_port = target_port;
                plan.reuse_key = "direct|" + request.metadata.uri.scheme + "|" + authority;
                return plan;
            }

            const auto &proxy = *options.proxy;
            if (proxy.host.empty() || proxy.port == 0) {
                return std::unexpected(ClientError{
                        .code = ClientError::CODE::INVALID_REQUEST,
                        .message = "proxy configuration requires host and port",
                });
            }

            plan.use_proxy = true;
            plan.connect_host = proxy.host;
            plan.connect_port = proxy.port;
            plan.proxy_authorization = buildProxyAuthorization(options);

            if constexpr (stream_is_ssl<Stream>::value) {
                plan.tunnel = true;
                plan.reuse_key = "proxy-connect|" + proxy.host + ":" + std::to_string(proxy.port) + "|" + authority +
                                 "|" + plan.proxy_authorization.value_or("");
            } else {
                plan.reuse_key = "proxy-http|" + proxy.host + ":" + std::to_string(proxy.port) + "|" + authority + "|" +
                                 plan.proxy_authorization.value_or("");
            }

            return plan;
        }

        template<typename Stream>
        static void clearConnectionState(ConnectionState<Stream> &state) {
            state.socket = {};
            state.reuse_key.clear();
            state.reusable = false;
            state.has_idle_expiry = false;
            state.idle_expiry = {};
        }

        static std::expected<std::uint16_t, ClientError> parseStatusCodeFromResponse(std::string_view response_head) {
            const std::size_t line_end = response_head.find("\r\n");
            const std::string_view status_line =
                    line_end == std::string_view::npos ? response_head : response_head.substr(0, line_end);

            const std::size_t first_space = status_line.find(' ');
            if (first_space == std::string_view::npos) {
                return std::unexpected(ClientError{
                        .code = ClientError::CODE::PROXY_FAILED,
                        .message = "proxy returned malformed status line",
                });
            }

            std::size_t second_space = status_line.find(' ', first_space + 1);
            if (second_space == std::string_view::npos) { second_space = status_line.size(); }

            const std::string_view code_view = status_line.substr(first_space + 1, second_space - first_space - 1);
            if (code_view.size() != 3 || !std::isdigit(static_cast<unsigned char>(code_view[0])) ||
                !std::isdigit(static_cast<unsigned char>(code_view[1])) ||
                !std::isdigit(static_cast<unsigned char>(code_view[2]))) {
                return std::unexpected(ClientError{
                        .code = ClientError::CODE::PROXY_FAILED,
                        .message = "proxy returned invalid status code",
                });
            }

            return static_cast<std::uint16_t>((code_view[0] - '0') * 100 + (code_view[1] - '0') * 10 + (code_view[2] - '0'));
        }

        static usub::uvent::task::Awaitable<bool> writeAll(usub::uvent::net::TCPClientSocket socket,
                                                           std::string_view data) {
            std::size_t offset = 0;
            while (offset < data.size()) {
                const ssize_t written =
                        co_await socket.async_write(reinterpret_cast<uint8_t *>(const_cast<char *>(data.data() + offset)),
                                                    data.size() - offset);
                if (written <= 0) { co_return false; }
                offset += static_cast<std::size_t>(written);
            }
            co_return true;
        }

        static usub::uvent::task::Awaitable<std::expected<void, ClientError>>
        establishProxyTunnel(usub::uvent::net::TCPClientSocket socket, const Request &request, const RoutePlan &plan,
                             std::uint16_t target_port) {
            std::string connect_request = "CONNECT " + request.metadata.uri.authority.host + ":" +
                                          std::to_string(target_port) + " HTTP/1.1\r\n";
            connect_request += "Host: " + request.metadata.uri.authority.host + ":" + std::to_string(target_port) + "\r\n";
            if (plan.proxy_authorization.has_value()) {
                connect_request += "Proxy-Authorization: " + *plan.proxy_authorization + "\r\n";
            }
            connect_request += "\r\n";

            const bool write_ok = co_await writeAll(socket, connect_request);
            if (!write_ok) {
                co_return std::unexpected(ClientError{
                        .code = ClientError::CODE::PROXY_FAILED,
                        .message = "failed to write CONNECT request to proxy",
                });
            }

            usub::uvent::utils::DynamicBuffer buffer{};
            buffer.reserve(16 * 1024);
            std::string response_head{};

            for (int i = 0; i < 32; ++i) {
                const std::size_t end_of_headers = response_head.find("\r\n\r\n");
                if (end_of_headers != std::string::npos) {
                    auto status = parseStatusCodeFromResponse(
                            std::string_view{response_head.data(), end_of_headers + 4});
                    if (!status) { co_return std::unexpected(status.error()); }
                    if (*status != 200) {
                        co_return std::unexpected(ClientError{
                                .code = ClientError::CODE::PROXY_FAILED,
                                .message = "proxy CONNECT failed with status " + std::to_string(*status),
                        });
                    }
                    co_return {};
                }

                buffer.clear();
                const ssize_t read_size = co_await socket.async_read(buffer, static_cast<std::size_t>(16 * 1024));
                if (read_size <= 0) {
                    co_return std::unexpected(ClientError{
                            .code = ClientError::CODE::PROXY_FAILED,
                            .message = "proxy closed CONNECT response early",
                    });
                }
                response_head.append(reinterpret_cast<const char *>(buffer.data()), static_cast<std::size_t>(read_size));
            }

            co_return std::unexpected(ClientError{
                    .code = ClientError::CODE::PROXY_FAILED,
                    .message = "proxy CONNECT response headers exceeded limit",
            });
        }

        static std::string serializeRequestForRoute(const Request &request, const RoutePlan &plan,
                                                    std::uint16_t target_port) {
            if (!plan.use_proxy || plan.tunnel) { return v1::RequestSerializer::serialize(request); }

            Request proxied_request = request;
            proxied_request.metadata.uri.path = buildAbsoluteTarget(request, target_port);
            proxied_request.metadata.uri.query.clear();
            if (plan.proxy_authorization.has_value() && !proxied_request.headers.contains("proxy-authorization")) {
                proxied_request.headers.addHeader("proxy-authorization", *plan.proxy_authorization);
            }
            return v1::RequestSerializer::serialize(proxied_request);
        }

        template<typename Config>
        static bool sameManagedConfig(const Config &lhs, const Config &rhs) {
            if constexpr (requires { lhs.mode; rhs.mode; }) {
                if (lhs.mode != rhs.mode) { return false; }
            }
            if constexpr (requires { lhs.verify_peer; rhs.verify_peer; }) {
                if (lhs.verify_peer != rhs.verify_peer) { return false; }
            }
            if constexpr (requires { lhs.server_name; rhs.server_name; }) {
                if (lhs.server_name != rhs.server_name) { return false; }
            }
            if constexpr (requires { lhs.key_file; rhs.key_file; }) {
                if (lhs.key_file != rhs.key_file) { return false; }
            }
            if constexpr (requires { lhs.cert_file; rhs.cert_file; }) {
                if (lhs.cert_file != rhs.cert_file) { return false; }
            }
            return true;
        }

        template<typename Stream>
        void configureManagedStream(Stream &stream_instance, ConnectionState<Stream> &state, const Request &request) {
            if constexpr (stream_is_ssl<Stream>::value && has_stream_config<Stream>::value) {
                typename Stream::Config config =
                        state.managed_stream_config.value_or(typename Stream::Config{});

                if constexpr (requires { config.mode = Stream::MODE::CLIENT; }) {
                    config.mode = Stream::MODE::CLIENT;
                }

                if constexpr (requires { config.server_name; }) {
                    if (!config.server_name.has_value() || config.server_name->empty()) {
                        config.server_name = request.metadata.uri.authority.host;
                    }
                }

                if (!state.applied_stream_config.has_value() ||
                    !sameManagedConfig(*state.applied_stream_config, config)) {
                    stream_instance.setConfig(config);
                    state.applied_stream_config = std::move(config);
                }
            }
        }

        template<typename Stream>
        usub::uvent::task::Awaitable<void> closeConnection(Stream &stream_instance, ConnectionState<Stream> &state) {
            if (socketIsOpen(state.socket)) {
                co_await stream_instance.shutdown(state.socket);
            }
            clearConnectionState(state);
            co_return;
        }

        template<typename Stream>
        usub::uvent::task::Awaitable<std::expected<bool, ClientError>>
        ensureConnected(Stream &stream_instance, ConnectionState<Stream> &state, const Request &request,
                        const ClientRequestOptions &options, const RoutePlan &plan, std::uint16_t target_port) {
            if (state.has_idle_expiry && std::chrono::steady_clock::now() >= state.idle_expiry) {
                co_await closeConnection(stream_instance, state);
            }

            const bool can_reuse = state.reusable && socketIsOpen(state.socket) && state.reuse_key == plan.reuse_key;
            if (can_reuse) { co_return true; }

            if (socketIsOpen(state.socket)) {
                co_await closeConnection(stream_instance, state);
            }

            configureManagedStream(stream_instance, state, request);

            state.socket = {};
            std::string connect_host = plan.connect_host;
            std::string connect_port = std::to_string(plan.connect_port);
            auto connect_error =
                    co_await state.socket.async_connect(connect_host, connect_port, options.connect_timeout);
            if (connect_error.has_value()) {
                clearConnectionState(state);
                co_return std::unexpected(ClientError{
                        .code = ClientError::CODE::CONNECT_FAILED,
                        .message = "connect failed",
                });
            }

            if (plan.use_proxy && plan.tunnel) {
                auto tunnel_result = co_await establishProxyTunnel(state.socket, request, plan, target_port);
                if (!tunnel_result) {
                    co_await closeConnection(stream_instance, state);
                    co_return std::unexpected(tunnel_result.error());
                }
            }

            state.reuse_key = plan.reuse_key;
            state.reusable = false;
            state.has_idle_expiry = false;
            state.idle_expiry = {};
            co_return false;
        }

        template<typename Stream>
        usub::uvent::task::Awaitable<std::expected<Response, ClientError>>
        requestWithStream(Stream &stream_instance, Request request, const ClientRequestOptions &options) {
            if (request.metadata.method_token.empty()) { request.metadata.method_token = "GET"; }

            if (request.metadata.version != VERSION::HTTP_1_0 && request.metadata.version != VERSION::HTTP_1_1) {
                request.metadata.version = VERSION::HTTP_1_1;
            }
            if (request.metadata.uri.path.empty()) { request.metadata.uri.path = "/"; }

            if (!request.headers.contains("host")) {
                if (!request.metadata.uri.authority.host.empty()) {
                    request.headers.addHeader("host", request.metadata.authority);
                }
            }

            const auto route_plan_result = buildRoutePlan<Stream>(request, options);
            if (!route_plan_result) { co_return std::unexpected(route_plan_result.error()); }
            const RoutePlan route_plan = *route_plan_result;
            const std::uint16_t target_port = defaultPortForRequest(request, stream_is_ssl<Stream>::value);
            const std::string serialized = serializeRequestForRoute(request, route_plan, target_port);
            auto &state = std::get<ConnectionState<Stream>>(this->connection_states_);
            usub::unet::core::stream::Transport transport{
                    .send = [](std::string_view) -> usub::uvent::task::Awaitable<ssize_t> { co_return 0; },
                    .sendFile = []() -> usub::uvent::task::Awaitable<ssize_t> { co_return 0; },
                    .close = []() -> usub::uvent::task::Awaitable<void> { co_return; },
            };

            for (std::size_t attempt = 0; attempt < max_retry_attempts; ++attempt) {
                ClientSession<VERSION::HTTP_1_1> session{};
                bool should_retry = false;
                auto connect_result = co_await ensureConnected(stream_instance, state, request, options, route_plan,
                                                               target_port);
                if (!connect_result) { co_return std::unexpected(connect_result.error()); }

                const bool reused_connection = *connect_result;
                co_await stream_instance.send(state.socket, serialized);

                usub::uvent::utils::DynamicBuffer buffer{};
                while (!session.isComplete() && !session.hasError()) {
                    const ssize_t read_size = co_await stream_instance.read(state.socket, buffer);
                    if (read_size < 0) {
                        co_await closeConnection(stream_instance, state);
                        if (reused_connection && !session.sawBytes() && attempt + 1 < max_retry_attempts) {
                            should_retry = true;
                            break;
                        }
                        co_return std::unexpected(ClientError{
                                .code = ClientError::CODE::READ_FAILED,
                                .message = "read failed",
                        });
                    }

                    if (read_size == 0) {
                        co_await session.onClose();
                        continue;
                    } else {
                        std::string_view chunk{reinterpret_cast<const char *>(buffer.data()),
                                               static_cast<std::size_t>(read_size)};
                        const SessionAction action = co_await session.onBytes(chunk, transport);
                        if (action.kind == SessionAction::Kind::Error) { break; }
                    }
                }

                if (should_retry) { continue; }
                if (session.hasError()) {
                    const ClientError error = *session.error();
                    co_await closeConnection(stream_instance, state);
                    const bool retryable =
                            reused_connection && !session.sawBytes() &&
                            error.code == ClientError::CODE::READ_FAILED &&
                            attempt + 1 < max_retry_attempts;
                    if (retryable) { continue; }
                    co_return std::unexpected(error);
                }

                const bool keep_alive =
                        requestKeepsAlive(request) &&
                        responseKeepsAlive(session.state().response, session.state().read_until_close_mode);
                if (!keep_alive) {
                    co_await closeConnection(stream_instance, state);
                } else if (auto timeout = parseKeepAliveTimeout(session.state().response.headers); timeout.has_value()) {
                    if (*timeout <= std::chrono::seconds::zero()) {
                        co_await closeConnection(stream_instance, state);
                    } else {
                        state.reusable = true;
                        state.has_idle_expiry = true;
                        state.idle_expiry = std::chrono::steady_clock::now() + *timeout;
                    }
                } else {
                    state.reusable = true;
                    state.has_idle_expiry = false;
                    state.idle_expiry = {};
                }

                co_return session.state().response;
            }

            co_return std::unexpected(ClientError{
                    .code = ClientError::CODE::CONNECT_FAILED,
                    .message = "retry limit exceeded",
            });
        }

        std::tuple<Streams...> streams_{};
        std::tuple<ConnectionState<Streams>...> connection_states_{};
    };
}// namespace usub::unet::http

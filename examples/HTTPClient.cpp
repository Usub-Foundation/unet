#include <iostream>
#include <string>
#include <utility>

#include <uvent/Uvent.h>

#include <unet/core/streams/plaintext.hpp>
#ifdef _WIN32
#include <unet/core/streams/schannel.hpp>
#else
#include <unet/core/streams/openssl.hpp>
#endif
#include <unet/http/client.hpp>


namespace request_builders {

    usub::unet::http::Request buildGetHttp(std::string path) {
        usub::unet::http::Request req{};
        req.metadata.method_token       = "GET";
        req.metadata.uri.scheme         = "http";
        req.metadata.uri.authority.host = "127.0.0.1";
        req.metadata.uri.authority.port = 8080;
        req.metadata.uri.path           = std::move(path);
        return req;
    }

    usub::unet::http::Request buildPostHttp(std::string path, std::string body) {
        auto req = buildGetHttp(std::move(path));
        req.metadata.method_token = "POST";
        req.body                  = std::move(body);
        return req;
    }

    usub::unet::http::Request buildGetHttps(std::string path) {
        usub::unet::http::Request req{};
        req.metadata.method_token       = "GET";
        req.metadata.uri.scheme         = "https";
        req.metadata.uri.authority.host = "127.0.0.1";
        req.metadata.uri.authority.port = 8443;
        req.metadata.uri.path           = std::move(path);
        return req;
    }

}// namespace request_builders


namespace response_printers {

    void printResponse(std::string_view label,
                       const std::expected<usub::unet::http::Response,
                                            usub::unet::http::ClientError> &result) {
        std::cout << label << " -> ";
        if (!result) {
            std::cout << "client error " << static_cast<int>(result.error()) << "\n";
            return;
        }
        std::cout << result->metadata.status_code
                  << " (body_size=" << result->body.size() << ")\n";
        if (!result->body.empty()) {
            std::cout << "  body: " << result->body;
            if (result->body.back() != '\n') std::cout << "\n";
        }
    }

}// namespace response_printers


namespace plaintext_client_walkthrough {

    usub::uvent::task::Awaitable<void> run() {
        usub::unet::http::ClientImpl<usub::unet::core::stream::PlainText> client;

        response_printers::printResponse(
                "GET  /hello ", co_await client.request(request_builders::buildGetHttp("/hello")));

        response_printers::printResponse(
                "POST /echo  ", co_await client.request(
                                       request_builders::buildPostHttp("/echo", "hello from unet client")));

        response_printers::printResponse(
                "GET  /greet ", co_await client.request(request_builders::buildGetHttp("/greet/unet")));

        response_printers::printResponse(
                "GET  /404   ", co_await client.request(request_builders::buildGetHttp("/does-not-exist")));
        co_return;
    }

}// namespace plaintext_client_walkthrough


namespace tls_client_walkthrough {

    // The TLS client speaks https by scheme. Peer cert verification is on by
    // default; hitting an example server with a self-signed cert therefore
    // fails at the SSL handshake, and the request returns ReadFailed.
    // Adjust the stream config once (verify_peer=false, or point at a CA file)
    // to talk to the local HTTPServer example — this demo just shows the shape.

    usub::uvent::task::Awaitable<void> run() {
#ifdef _WIN32
        usub::unet::http::ClientImpl<
                usub::unet::core::stream::SChannelStream<"h2", "http/1.1">> client;
#else
        usub::unet::http::ClientImpl<
                usub::unet::core::stream::OpenSSLStream<"h2", "http/1.1">> client;
#endif

        response_printers::printResponse(
                "GET  /hello (https)",
                co_await client.request(request_builders::buildGetHttps("/hello")));
        co_return;
    }

}// namespace tls_client_walkthrough


int main() {
    std::cout.setf(std::ios::unitbuf);

    usub::Uvent uvent{1};

    usub::uvent::system::co_spawn_static(
            []() -> usub::uvent::task::Awaitable<void> {
                std::cout << "-- plaintext walkthrough --\n";
                co_await plaintext_client_walkthrough::run();

                std::cout << "\n-- tls walkthrough (self-signed will fail unless verify_peer=false is wired) --\n";
                co_await tls_client_walkthrough::run();

                co_return;
            }(),
            0);

    uvent.run();
    return 0;
}

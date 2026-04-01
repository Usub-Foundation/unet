#include <chrono>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <uvent/Uvent.h>

#include "unet/core/streams/openssl.hpp"
#include "unet/core/streams/plaintext.hpp"
#include "unet/mail/imap/client.hpp"

namespace {
    using ImapClient = usub::unet::mail::imap::ClientImpl<usub::unet::core::stream::PlainText,
                                                          usub::unet::core::stream::OpenSSLStream>;

    using usub::unet::mail::imap::ClientError;
    using usub::unet::mail::imap::ClientNetworkOptions;
    using usub::unet::mail::imap::NetworkEndpoint;
    using usub::unet::mail::imap::command::Login;
    using usub::unet::mail::imap::command::Select;

    [[nodiscard]] std::optional<std::string> getEnv(std::string_view name) {
        if (const char *value = std::getenv(std::string(name).c_str()); value != nullptr) { return std::string(value); }
        return std::nullopt;
    }

    [[nodiscard]] bool envEnabled(std::string_view name) {
        const auto value = getEnv(name);
        if (!value.has_value()) { return false; }

        std::string lowered = *value;
        for (char &ch: lowered) {
            if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
        }
        return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
    }

    [[nodiscard]] std::string formatClientError(const ClientError &error) {
        std::string message = error.message;
        if (error.core_error.has_value()) {
            message.append(" | core=");
            message.append(error.core_error->message);
        }
        return message;
    }

    usub::uvent::task::Awaitable<std::expected<void, std::string>> runLive() {
        const auto username = getEnv("UNET_IMAP_TEST_USER");
        const auto password = getEnv("UNET_IMAP_TEST_PASSWORD");
        if (!username.has_value() || !password.has_value()) {
            std::cout << "imap live test skipped\n";
            co_return {};
        }

        NetworkEndpoint endpoint{};
        endpoint.host = getEnv("UNET_IMAP_TEST_HOST").value_or("imap.gmail.com");
        endpoint.scheme = getEnv("UNET_IMAP_TEST_SCHEME").value_or("imaps");
        endpoint.port = getEnv("UNET_IMAP_TEST_PORT").has_value()
                                ? static_cast<std::uint16_t>(std::stoul(*getEnv("UNET_IMAP_TEST_PORT")))
                                : std::uint16_t{0};

        const bool verify_peer = !envEnabled("UNET_IMAP_TEST_INSECURE_TLS");
        ImapClient client{};

        if (endpoint.scheme == "imaps") {
            usub::unet::core::stream::OpenSSLStream::Config ssl{};
            ssl.mode = usub::unet::core::stream::OpenSSLStream::MODE::CLIENT;
            ssl.verify_peer = verify_peer;
            ssl.server_name = endpoint.host;
            client.stream<usub::unet::core::stream::OpenSSLStream>().setConfig(std::move(ssl));
        }

        ClientNetworkOptions options{};
        options.connect_timeout = std::chrono::seconds{6};

        auto connected = co_await client.connect(endpoint, options);
        if (!connected) { co_return std::unexpected("connect failed: " + formatClientError(connected.error())); }

        auto capability = co_await client.capability();
        if (!capability) { co_return std::unexpected("capability failed: " + formatClientError(capability.error())); }

        auto login = co_await client.login(Login{.username = *username, .password = *password});
        if (!login) { co_return std::unexpected("login failed: " + formatClientError(login.error())); }

        auto mailbox = getEnv("UNET_IMAP_TEST_MAILBOX").value_or("INBOX");
        auto select = co_await client.select(Select{.mailbox_name = mailbox});
        if (!select) { co_return std::unexpected("select failed: " + formatClientError(select.error())); }

        std::cout << "selected mailbox=" << mailbox
                  << " exists=" << select->exists.value_or(0)
                  << " capabilities=" << capability->capabilities.size() << '\n';

        co_await client.close();
        co_return {};
    }

}// namespace

int main() {
    usub::Uvent runtime{2};
    std::optional<std::expected<void, std::string>> result{};

    usub::uvent::system::co_spawn([&]() -> usub::uvent::task::Awaitable<void> {
        result = co_await runLive();
        runtime.stop();
        co_return;
    }());

    runtime.run();

    if (!result.has_value()) {
        std::cerr << "imap live test did not produce a result\n";
        return 1;
    }
    if (!result->has_value()) {
        std::cerr << result->error() << '\n';
        return 1;
    }
    return 0;
}

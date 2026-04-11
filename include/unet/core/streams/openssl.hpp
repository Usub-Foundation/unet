#pragma once

#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <uvent/Uvent.h>

#include "unet/core/acceptor.hpp"
#include "unet/core/config.hpp"
#include "unet/core/streams/alpn.hpp"

namespace usub::unet::core {
    namespace stream {
        template<FixedString... Alpns>
        class OpenSSLStream {
        public:
            static constexpr bool ssl{true};

            enum class MODE : std::uint8_t {
                SERVER,
                CLIENT,
            };

            struct Config {
                MODE mode{MODE::CLIENT};
                std::optional<std::string> key_file{};
                std::optional<std::string> cert_file{};
                std::optional<std::string> server_name{};
                bool verify_peer{true};
            };

            OpenSSLStream() {
                this->config_.mode = MODE::CLIENT;
                this->initContext();
            }

            OpenSSLStream(std::string_view key, std::string_view cert) {
                this->config_.mode = MODE::SERVER;
                this->config_.key_file = std::string(key);
                this->config_.cert_file = std::string(cert);
                this->initContext();
            }

            explicit OpenSSLStream(Config config) : config_(std::move(config)) { this->initContext(); }

            ~OpenSSLStream() {
                std::lock_guard<std::mutex> lock(this->session_mutex_);
                this->sessions_.clear();
            }

            void setConfig(Config config) {
                std::lock_guard<std::mutex> lock(this->session_mutex_);
                this->sessions_.clear();
                this->config_ = std::move(config);
                this->initContext();
            }

            usub::uvent::task::Awaitable<ssize_t> read(usub::uvent::net::TCPClientSocket &socket,
                                                       usub::uvent::utils::DynamicBuffer &buffer) {
                buffer.reserve(kIoBufferSize);
                buffer.clear();
                std::array<char, kIoBufferSize> plain_buffer{};

                auto [fd, session] = this->getOrCreateSession(socket);
                if (!session) { co_return -1; }

                auto handshake = co_await this->ensureHandshake(*session, socket);
                if (!handshake) { co_return -1; }

                for (;;) {
                    const int rc =
                            ::SSL_read(session->ssl.get(), plain_buffer.data(), static_cast<int>(plain_buffer.size()));
                    if (rc > 0) {
                        buffer.append(reinterpret_cast<const uint8_t *>(plain_buffer.data()),
                                      static_cast<std::size_t>(rc));
                        co_return static_cast<ssize_t>(rc);
                    }

                    const int err = ::SSL_get_error(session->ssl.get(), rc);
                    if (err == SSL_ERROR_ZERO_RETURN || (err == SSL_ERROR_SYSCALL && rc == 0)) {
                        this->eraseSession(fd);
                        co_return 0;
                    }
                    if (err == SSL_ERROR_WANT_READ) {
                        auto pump = co_await this->pumpSocketToBio(*session, socket);
                        if (pump <= 0) {
                            this->eraseSession(fd);
                            co_return pump;
                        }
                        continue;
                    }
                    if (err == SSL_ERROR_WANT_WRITE) {
                        auto flush = co_await this->flushBioToSocket(*session, socket);
                        if (flush <= 0) {
                            this->eraseSession(fd);
                            co_return -1;
                        }
                        continue;
                    }

                    this->eraseSession(fd);
                    co_return -1;
                }
            }

            usub::uvent::task::Awaitable<void> send(usub::uvent::net::TCPClientSocket &socket, std::string_view data) {
                if (data.empty()) { co_return; }

                auto [fd, session] = this->getOrCreateSession(socket);
                if (!session) { co_return; }

                auto handshake = co_await this->ensureHandshake(*session, socket);
                if (!handshake) {
                    this->eraseSession(fd);
                    co_return;
                }

                std::size_t offset = 0;
                while (offset < data.size()) {
                    const std::size_t remaining = data.size() - offset;
                    const std::size_t to_write = remaining > static_cast<std::size_t>(INT_MAX)
                                                         ? static_cast<std::size_t>(INT_MAX)
                                                         : remaining;

                    const int rc = ::SSL_write(session->ssl.get(), data.data() + offset, static_cast<int>(to_write));
                    if (rc > 0) {
                        offset += static_cast<std::size_t>(rc);
                        auto flush = co_await this->flushBioToSocket(*session, socket);
                        if (flush <= 0) {
                            this->eraseSession(fd);
                            co_return;
                        }
                        continue;
                    }

                    const int err = ::SSL_get_error(session->ssl.get(), rc);
                    if (err == SSL_ERROR_WANT_READ) {
                        auto pump = co_await this->pumpSocketToBio(*session, socket);
                        if (pump <= 0) {
                            this->eraseSession(fd);
                            co_return;
                        }
                        continue;
                    }
                    if (err == SSL_ERROR_WANT_WRITE) {
                        auto flush = co_await this->flushBioToSocket(*session, socket);
                        if (flush <= 0) {
                            this->eraseSession(fd);
                            co_return;
                        }
                        continue;
                    }

                    this->eraseSession(fd);
                    co_return;
                }

                auto flush = co_await this->flushBioToSocket(*session, socket);
                if (flush <= 0) { this->eraseSession(fd); }
                co_return;
            }

            usub::uvent::task::Awaitable<void> sendFile(usub::uvent::net::TCPClientSocket &/*socket*/) { co_return; }

            usub::uvent::task::Awaitable<void> shutdown(usub::uvent::net::TCPClientSocket &socket) {
                auto [fd, session] = this->findSession(socket);
                if (session) {
                    (void) ::SSL_shutdown(session->ssl.get());
                    (void) co_await this->flushBioToSocket(*session, socket);
                    this->eraseSession(fd);
                }

                socket.shutdown();
                co_return;
            }

            MODE mode() const { return this->config_.mode; }

        private:
            struct SSLContextDeleter {
                void operator()(SSL_CTX *ctx) const {
                    if (ctx) { ::SSL_CTX_free(ctx); }
                }
            };

            struct SSLDeleter {
                void operator()(SSL *ssl) const {
                    if (ssl) { ::SSL_free(ssl); }
                }
            };

            struct Session {
                std::unique_ptr<SSL, SSLDeleter> ssl{nullptr};
                bool handshake_done{false};
            };

            static constexpr std::size_t kIoBufferSize = 16 * 1024;
            using AlpnConfig = AlpnWireFormatTraits<Alpns...>;
            static constexpr auto kAlpnWireFormat = AlpnConfig::kWireFormat;

            static void initOpenSSLOnce() {
                static bool initialized = []() {
                    (void) ::OPENSSL_init_ssl(0, nullptr);
                    return true;
                }();
                (void) initialized;
            }

            void initContext() {
                initOpenSSLOnce();

                SSL_CTX *ctx = nullptr;
                if (this->config_.mode == MODE::SERVER) {
                    ctx = ::SSL_CTX_new(::TLS_server_method());
                    if (!ctx) {
                        this->ctx_.reset();
                        return;
                    }

                    if (!this->config_.cert_file.has_value() || !this->config_.key_file.has_value()) {
                        this->ctx_.reset(ctx);
                        return;
                    }

                    if (::SSL_CTX_use_certificate_file(ctx, this->config_.cert_file->c_str(), SSL_FILETYPE_PEM) != 1) {
                        ::SSL_CTX_free(ctx);
                        this->ctx_.reset();
                        return;
                    }
                    if (::SSL_CTX_use_PrivateKey_file(ctx, this->config_.key_file->c_str(), SSL_FILETYPE_PEM) != 1) {
                        ::SSL_CTX_free(ctx);
                        this->ctx_.reset();
                        return;
                    }
                    if (::SSL_CTX_check_private_key(ctx) != 1) {
                        ::SSL_CTX_free(ctx);
                        this->ctx_.reset();
                        return;
                    }

                    ::SSL_CTX_set_alpn_select_cb(
                            ctx,
                            [](SSL * /*ssl*/, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                               unsigned int inlen, void * /*arg*/) -> int {
                                int sel = ::SSL_select_next_proto(const_cast<unsigned char **>(out), outlen,
                                                                  kAlpnWireFormat.data(),
                                                                  static_cast<unsigned int>(kAlpnWireFormat.size()), in,
                                                                  inlen);
                                if (sel == OPENSSL_NPN_NEGOTIATED) { return SSL_TLSEXT_ERR_OK; }
                                return SSL_TLSEXT_ERR_NOACK;
                            },
                            nullptr);
                } else {
                    ctx = ::SSL_CTX_new(::TLS_client_method());
                    if (!ctx) {
                        this->ctx_.reset();
                        return;
                    }

                    if (this->config_.verify_peer) {
                        ::SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
                        (void) ::SSL_CTX_set_default_verify_paths(ctx);
                    } else {
                        ::SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
                    }
                }

                this->ctx_.reset(ctx);
            }

            static std::optional<int> getSocketFd(usub::uvent::net::TCPClientSocket &socket) {
                auto *header = socket.get_raw_header();
                if (!header || header->fd < 0) { return std::nullopt; }
                return header->fd;
            }

            std::pair<int, Session *> findSession(usub::uvent::net::TCPClientSocket &socket) {
                auto fd_opt = getSocketFd(socket);
                if (!fd_opt.has_value()) { return {-1, nullptr}; }
                const int fd = *fd_opt;

                std::lock_guard<std::mutex> lock(this->session_mutex_);
                auto it = this->sessions_.find(fd);
                if (it == this->sessions_.end()) { return {fd, nullptr}; }
                return {fd, it->second.get()};
            }

            std::pair<int, Session *> getOrCreateSession(usub::uvent::net::TCPClientSocket &socket) {
                auto fd_opt = getSocketFd(socket);
                if (!fd_opt.has_value()) { return {-1, nullptr}; }
                const int fd = *fd_opt;

                std::lock_guard<std::mutex> lock(this->session_mutex_);
                auto it = this->sessions_.find(fd);
                if (it != this->sessions_.end()) { return {fd, it->second.get()}; }
                if (!this->ctx_) { return {fd, nullptr}; }

                SSL *ssl = ::SSL_new(this->ctx_.get());
                if (!ssl) { return {fd, nullptr}; }

                BIO *in_bio = ::BIO_new(BIO_s_mem());
                BIO *out_bio = ::BIO_new(BIO_s_mem());
                if (!in_bio || !out_bio) {
                    if (in_bio) { ::BIO_free(in_bio); }
                    if (out_bio) { ::BIO_free(out_bio); }
                    ::SSL_free(ssl);
                    return {fd, nullptr};
                }

                ::BIO_set_mem_eof_return(in_bio, -1);
                ::BIO_set_mem_eof_return(out_bio, -1);
                ::SSL_set_bio(ssl, in_bio, out_bio);

                if (this->config_.mode == MODE::SERVER) {
                    ::SSL_set_accept_state(ssl);
                } else {
                    ::SSL_set_connect_state(ssl);
                    (void) ::SSL_set_alpn_protos(ssl, kAlpnWireFormat.data(),
                                                 static_cast<unsigned int>(kAlpnWireFormat.size()));
                    if (this->config_.server_name.has_value() && !this->config_.server_name->empty()) {
                        (void) ::SSL_set_tlsext_host_name(ssl, this->config_.server_name->c_str());
                        if (this->config_.verify_peer) {
                            if (auto *param = ::SSL_get0_param(ssl); param) {
                                (void) ::X509_VERIFY_PARAM_set1_host(param, this->config_.server_name->c_str(), 0);
                            }
                        }
                    }
                }

                auto inserted = this->sessions_.emplace(fd, std::make_unique<Session>());
                inserted.first->second->ssl.reset(ssl);
                return {fd, inserted.first->second.get()};
            }

            void eraseSession(int fd) {
                if (fd < 0) { return; }
                std::lock_guard<std::mutex> lock(this->session_mutex_);
                this->sessions_.erase(fd);
            }

            usub::uvent::task::Awaitable<ssize_t> flushBioToSocket(Session &session,
                                                                   usub::uvent::net::TCPClientSocket &socket) {
                std::array<char, kIoBufferSize> network_buffer{};
                BIO *out_bio = ::SSL_get_wbio(session.ssl.get());
                if (!out_bio) { co_return -1; }

                while (BIO_pending(out_bio) > 0) {
                    const int n = ::BIO_read(out_bio, network_buffer.data(), static_cast<int>(network_buffer.size()));
                    if (n <= 0) { co_return -1; }

                    std::size_t offset = 0;
                    while (offset < static_cast<std::size_t>(n)) {
                        auto *ptr = reinterpret_cast<uint8_t *>(network_buffer.data() + offset);
                        const ssize_t wr = co_await socket.async_write(ptr, static_cast<std::size_t>(n) - offset);
                        if (wr <= 0) { co_return -1; }
                        offset += static_cast<std::size_t>(wr);
                    }
                }

                co_return 1;
            }

            usub::uvent::task::Awaitable<ssize_t> pumpSocketToBio(Session &session,
                                                                  usub::uvent::net::TCPClientSocket &socket) {
                std::array<char, kIoBufferSize> network_buffer{};
                const ssize_t rd = co_await socket.async_read(reinterpret_cast<uint8_t *>(network_buffer.data()),
                                                              network_buffer.size());
                if (rd <= 0) { co_return rd; }

                BIO *in_bio = ::SSL_get_rbio(session.ssl.get());
                if (!in_bio) { co_return -1; }

                const int wrote = ::BIO_write(in_bio, network_buffer.data(), static_cast<int>(rd));
                if (wrote <= 0) { co_return -1; }
                co_return rd;
            }

            usub::uvent::task::Awaitable<bool> ensureHandshake(Session &session,
                                                               usub::uvent::net::TCPClientSocket &socket) {
                if (session.handshake_done) { co_return true; }

                for (;;) {
                    const int rc = ::SSL_do_handshake(session.ssl.get());

                    const auto flush = co_await this->flushBioToSocket(session, socket);
                    if (flush <= 0) { co_return false; }

                    if (rc == 1) {
                        session.handshake_done = true;
                        co_return true;
                    }

                    const int err = ::SSL_get_error(session.ssl.get(), rc);
                    if (err == SSL_ERROR_WANT_READ) {
                        const auto pump = co_await this->pumpSocketToBio(session, socket);
                        if (pump <= 0) { co_return false; }
                        continue;
                    }
                    if (err == SSL_ERROR_WANT_WRITE) { continue; }

                    co_return false;
                }
            }

        private:
            Config config_{};
            std::unique_ptr<SSL_CTX, SSLContextDeleter> ctx_{nullptr};
            std::unordered_map<int, std::unique_ptr<Session>> sessions_{};
            std::mutex session_mutex_{};
        };
    }// namespace stream

    template<stream::FixedString... Alpns>
    class Acceptor<stream::OpenSSLStream<Alpns...>> {
    public:
        Acceptor() = default;
        ~Acceptor() = default;

        template<class OnConnection>
        usub::uvent::task::Awaitable<void> acceptLoop(OnConnection on_connection, Config &config) {
            const Config::Object empty_section{};
            const Config::Object *section_ptr = config.getObject("HTTP.OpenSSLStream");
            const Config::Object &section = section_ptr ? *section_ptr : empty_section;

            std::string host = config.getString(section, "host", "127.0.0.1");
            const std::uint64_t raw_port = config.getUInt(section, "port", 443);
            const std::uint16_t port =
                    (raw_port <= static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max()))
                            ? static_cast<std::uint16_t>(raw_port)
                            : static_cast<std::uint16_t>(443);

            std::int64_t backlog_cfg = config.getInt(section, "backlog", 50);
            if (backlog_cfg <= 0) { backlog_cfg = 50; }
            const int backlog = (backlog_cfg > static_cast<std::int64_t>(std::numeric_limits<int>::max()))
                                        ? std::numeric_limits<int>::max()
                                        : static_cast<int>(backlog_cfg);

            const std::int64_t version = config.getInt(section, "version", 4);
            const auto ip_version =
                    (version == 6) ? usub::uvent::utils::net::IPV::IPV6 : usub::uvent::utils::net::IPV::IPV4;

            std::string tcp = config.getString(section, "tcp", "tcp");
            for (char &ch: tcp) {
                if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
            }
            const auto socket_type = (tcp == "udp") ? usub::uvent::utils::net::UDP : usub::uvent::utils::net::TCP;

            const std::string key_file = config.getString(section, "key", "key.pem");
            const std::string cert_file = config.getString(section, "cert", "cert.pem");

            usub::uvent::net::TCPServerSocket server_socket{host, static_cast<int>(port), backlog, ip_version,
                                                            socket_type};

            stream::OpenSSLStream<Alpns...> stream(key_file, cert_file);

            for (;;) {
                auto soc = co_await server_socket.async_accept();
                if (!soc) { continue; }
                usub::uvent::system::co_spawn(on_connection(stream, std::move(soc.value())));
            }
            co_return;
        }
    };

}// namespace usub::unet::core
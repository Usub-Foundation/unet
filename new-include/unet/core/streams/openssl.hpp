#pragma once

#include <memory>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <uvent/Uvent.h>

#include "unet/core/acceptor.hpp"

namespace usub::unet::core {

    // TODO: think about this approach
    // using SSL_CTX_ptr = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
    // using SSL_ptr = std::unique_ptr<SSL, decltype(&SSL_free)>;

    // SSL_CTX_ptr make_ctx(const char *key, const char *cert) {
    //     SSL_CTX_ptr ctx(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
    //     if (!ctx) return nullptr;
    //     if (SSL_CTX_use_certificate_file(ctx.get(), cert, SSL_FILETYPE_PEM) != 1) return nullptr;
    //     if (SSL_CTX_use_PrivateKey_file(ctx.get(), key, SSL_FILETYPE_PEM) != 1) return nullptr;
    //     return ctx;
    // }

    // SSL_ptr make_ssl(SSL_CTX *ctx) { return SSL_ptr(SSL_new(ctx), SSL_free); }

    SSL_CTX *openssl_create_ctx(const char *key_file, const char *cert_file) {
        SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
        SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM);
        SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM);

        return ctx;
    }

    SSL *openssl_create_ssl(SSL_CTX *ctx) {
        SSL *ssl;
        ssl = SSL_new(ctx);
        if (!ssl) {
            // error_log("Could not create SSL/TLS session object: %s",
            // ERR_error_string(ERR_get_error(), nullptr));
        }
        return ssl;
    }

    class OpenSSLStream {
    public:
        OpenSSLStream() = delete;

        // TODO: Implement
        OpenSSLStream(std::string_view key, std::string_view cert) {
            // TODO: Propper config passing
            ctx_ = openssl_create_ctx(key.data(), cert.data());

            // TODO: Protocol specific alpn REMOVE
            static const unsigned char alpn_proto[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};

            SSL_CTX_set_alpn_select_cb(
                    ctx_,
                    [](SSL * /*ssl*/, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                       unsigned int inlen, void * /*arg*/) -> int {
                        int sel = SSL_select_next_proto(const_cast<unsigned char **>(out), outlen, alpn_proto,
                                                        sizeof(alpn_proto), in, inlen);
                        if (sel == OPENSSL_NPN_NEGOTIATED) { return SSL_TLSEXT_ERR_OK; }
                        return SSL_TLSEXT_ERR_NOACK;
                    },
                    nullptr);
        }
        ~OpenSSLStream() {
            //TODO: Implement
        }

        template<typename Dispatcher>
        usub::uvent::task::Awaitable<void> readLoop(usub::uvent::net::TCPClientSocket socket, Dispatcher &&dispatcher) {

            SSL *ssl = openssl_create_ssl(this->ctx_);
            if (!ssl) {
                socket.shutdown();
                co_return;
            }
            BIO *in_bio = BIO_new(BIO_s_mem());
            BIO *out_bio = BIO_new(BIO_s_mem());
            BIO_set_mem_eof_return(in_bio, -1);
            BIO_set_mem_eof_return(out_bio, -1);
            SSL_set_accept_state(ssl);
            SSL_set_bio(ssl, in_bio, out_bio);

            usub::uvent::utils::DynamicBuffer buffer;
            static constexpr size_t MAX_READ_SIZE = 16 * 1024;
            buffer.reserve(MAX_READ_SIZE);
            std::vector<char> ssl_buffer(MAX_READ_SIZE);

            bool handshake_done = false;
            while (!handshake_done) {
                buffer.clear();

                ssize_t rdsz = co_await socket.async_read(buffer, MAX_READ_SIZE);
                if (rdsz <= 0) {
                    SSL_free(ssl);
                    co_return;
                }
                BIO_write(in_bio, buffer.data(), int(rdsz));

                for (;;) {
                    int rc = SSL_do_handshake(ssl);
                    int err = SSL_get_error(ssl, rc);

                    while (BIO_pending(out_bio) > 0) {
                        int n = BIO_read(out_bio, ssl_buffer.data(), int(MAX_READ_SIZE));
                        if (n > 0) { co_await socket.async_write((uint8_t *) ssl_buffer.data(), n); }
                    }

                    if (rc == 1) {
                        handshake_done = true;
                        break;
                    }
                    if (err == SSL_ERROR_WANT_READ) { break; }
                    if (err == SSL_ERROR_WANT_WRITE) { continue; }

                    SSL_free(ssl);
                    co_return;
                }
            }

            const unsigned char *sel = nullptr;
            unsigned int sel_len = 0;
            SSL_get0_alpn_selected(ssl, &sel, &sel_len);

            if (sel_len != 8 || std::memcmp(sel, "http/1.1", 8) != 0) {

                SSL_shutdown(ssl);
                SSL_free(ssl);
                socket.shutdown();
                co_return;
            }

            while (true) {
                buffer.clear();

                ssize_t rdsz = co_await socket.async_read(buffer, MAX_READ_SIZE);

                if (rdsz <= 0) {
                    co_await dispatcher.on_close();
                    break;
                }
                BIO_write(in_bio, buffer.data(), int(buffer.size()));
                socket.set_timeout_ms(20000);

                bool should_cleanup = false;
                while (true) {
                    int ret = SSL_is_init_finished(ssl) ? ::SSL_read(ssl, ssl_buffer.data(), MAX_READ_SIZE)
                                                        : ::SSL_do_handshake(ssl);
                    if (ret > 0) {
                        // std::string request_string{ssl_buffer.data(),
                        //                            ssl_buffer.data() + ssl_buffer.size()};
                        // TODO: another function?
                        co_await dispatcher.on_read(std::string_view{reinterpret_cast<const char *>(ssl_buffer.data()),
                                                                     ret /*ssl_buffer.size()*/},
                                                    socket);
                        // http1.readCallbackSync(request_string, socket);
                    } else {
                        int err = SSL_get_error(ssl, ret);
                        if (err == SSL_ERROR_WANT_READ) { break; }
                        if (err == SSL_ERROR_WANT_WRITE) {
                            while (BIO_pending(out_bio) > 0) {
                                int n = BIO_read(out_bio, ssl_buffer.data(), MAX_READ_SIZE);
                                if (n > 0) {
                                    co_await socket.async_write((uint8_t *) ssl_buffer.data(), std::size_t(n));
                                }
                            }
                            continue;
                        }
                        should_cleanup = true;
                        break;
                    }
                }
                if (should_cleanup) { break; }

                // TODO: this should probably become callback
                // while (!response.isSent() &&
                //        request.getState() >= protocols::http::REQUEST_STATE::FINISHED) {
                //     const std::string out = response.pull();
                //     SSL_write(ssl, out.data(), int(out.size()));
                // }
                // response.clear();

                // while (BIO_pending(out_bio) > 0) {
                //     int n = BIO_read(out_bio, ssl_buffer.data(), MAX_READ_SIZE);
                //     if (n > 0) {
                //         co_await socket->async_write((uint8_t *) ssl_buffer.data(), std::size_t(n));
                //     }
                // }
            }

            SSL_shutdown(ssl);
            SSL_free(ssl);
            socket.shutdown();
            co_return;
        }


    private:
        SSL_CTX *ctx_{nullptr};
    };

    // TODO: make specific Acceptor overload
    template<>
    class Acceptor<OpenSSLStream> {
    public:
        Acceptor(std::shared_ptr<Uvent> uvent /*Other params*/) : uvent_(uvent) {}

        template<typename Dispatcher, typename RouterType>
            requires StreamHandlerFor<OpenSSLStream, Dispatcher>
        usub::uvent::task::Awaitable<void> acceptLoop(std::shared_ptr<RouterType> router) {
            // TODO: propper init
            usub::uvent::net::TCPServerSocket server_socket{"0.0.0.0", 22814,
                                                            50,// backlog
                                                            usub::uvent::utils::net::IPV::IPV4,
                                                            usub::uvent::utils::net::TCP};
            OpenSSLStream stream("key.pem", "cert.pem");

            for (;;) {
                auto soc = co_await server_socket.async_accept();

                if (soc) {
                    Dispatcher dispatcher{router};
                    usub::uvent::system::co_spawn(stream.readLoop(std::move(soc.value()), std::move(dispatcher)));
                }
            }
        }


    private:
        std::shared_ptr<Uvent> uvent_;
    };

}// namespace usub::unet::core
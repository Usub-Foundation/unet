#pragma once
#define USE_OPEN_SSL
#ifdef USE_OPEN_SSL

#include "utils/ssl/SSLHelper.h"
#include <Uvent.h>
#include <memory>

#include "Protocols/HTTP/HTTP1.h"

static constexpr std::size_t NET_BUF_SIZE = 16 * 1024;

namespace usub::server {

    template<class TRouter>
    class TLSHTTPStreamHandler {
    public:
        using RouterType = TRouter;

        TLSHTTPStreamHandler(std::shared_ptr<RouterType> router) : router_(std::move(router)) {
            ctx_ = usub::utils::ssl::create_http1_ctx("key.pem", "cert.pem");
            static const unsigned char alpn_proto[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};

            SSL_CTX_set_alpn_select_cb(
                    ctx_,
                    [](SSL * /*ssl*/, const unsigned char **out, unsigned char *outlen,
                       const unsigned char *in, unsigned int inlen, void * /*arg*/) -> int {
                        int sel = SSL_select_next_proto(const_cast<unsigned char **>(out), outlen,
                                                        alpn_proto, sizeof(alpn_proto), in, inlen);
                        if (sel == OPENSSL_NPN_NEGOTIATED) { return SSL_TLSEXT_ERR_OK; }
                        return SSL_TLSEXT_ERR_NOACK;
                    },
                    nullptr);
        }

        usub::uvent::task::Awaitable<void> clientCoroutine(usub::uvent::net::SocketHolder socket) {
            SSL *ssl = usub::utils::ssl::create_ssl(ctx_);
            if (!ssl) {
                socket->shutdown();
                co_return;
            }

            BIO *in_bio = BIO_new(BIO_s_mem());
            BIO *out_bio = BIO_new(BIO_s_mem());
            BIO_set_mem_eof_return(in_bio, -1);
            BIO_set_mem_eof_return(out_bio, -1);
            SSL_set_accept_state(ssl);
            SSL_set_bio(ssl, in_bio, out_bio);

            // I/O
            usub::uvent::utils::DynamicBuffer net_buffer;
            net_buffer.reserve(NET_BUF_SIZE);
            std::vector<char> ssl_buffer(NET_BUF_SIZE);

            // handshake
            bool handshake_done = false;
            while (!handshake_done) {
                net_buffer.clear();

                ssize_t rd = co_await socket->async_read(net_buffer, NET_BUF_SIZE);
                if (rd <= 0) {
                    SSL_free(ssl);
                    co_return;
                }
                BIO_write(in_bio, net_buffer.data(), int(rd));

                for (;;) {
                    int rc = SSL_do_handshake(ssl);
                    int err = SSL_get_error(ssl, rc);

                    while (BIO_pending(out_bio) > 0) {
                        int n = BIO_read(out_bio, ssl_buffer.data(), int(NET_BUF_SIZE));
                        if (n > 0) {
                            co_await socket->async_write((uint8_t *) ssl_buffer.data(), n);
                        }
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

            // alpn
            const unsigned char *sel = nullptr;
            unsigned int sel_len = 0;
            SSL_get0_alpn_selected(ssl, &sel, &sel_len);

            if (sel_len != 8 || std::memcmp(sel, "http/1.1", 8) != 0) {

                SSL_shutdown(ssl);
                SSL_free(ssl);
                socket->shutdown();
                co_return;
            }

            // http
            protocols::http::HTTP1 http1{router_};
            auto &request = http1.getRequest();
            auto &response = http1.getResponse();

            bool should_cleanup = false;

            while (true) {
                net_buffer.clear();
                ssize_t rd = co_await socket->async_read(net_buffer, NET_BUF_SIZE);
                if (rd <= 0) {
                    should_cleanup = true;
                    break;
                }
                BIO_write(in_bio, net_buffer.data(), int(net_buffer.size()));

                // Handshake или decrypt
                while (true) {
                    int ret = SSL_is_init_finished(ssl)
                                      ? ::SSL_read(ssl, ssl_buffer.data(), NET_BUF_SIZE)
                                      : ::SSL_do_handshake(ssl);
                    if (ret > 0) {
                        std::string request_string{ssl_buffer.data(),
                                                   ssl_buffer.data() + ssl_buffer.size()};
                        http1.readCallbackSync(request_string, socket);
                    } else {
                        int err = SSL_get_error(ssl, ret);
                        if (err == SSL_ERROR_WANT_READ) { break; }
                        if (err == SSL_ERROR_WANT_WRITE) {
                            while (BIO_pending(out_bio) > 0) {
                                int n = BIO_read(out_bio, ssl_buffer.data(), NET_BUF_SIZE);
                                if (n > 0) {
                                    co_await socket->async_write((uint8_t *) ssl_buffer.data(),
                                                                 std::size_t(n));
                                }
                            }
                            continue;
                        }
                        should_cleanup = true;
                        break;
                    }
                }
                if (should_cleanup) { break; }

                while (!response.isSent() &&
                       request.getState() >= protocols::http::REQUEST_STATE::FINISHED) {
                    const std::string out = response.pull();
                    SSL_write(ssl, out.data(), int(out.size()));
                }
                response.clear();

                while (BIO_pending(out_bio) > 0) {
                    int n = BIO_read(out_bio, ssl_buffer.data(), NET_BUF_SIZE);
                    if (n > 0) {
                        co_await socket->async_write((uint8_t *) ssl_buffer.data(), std::size_t(n));
                    }
                }
            }

            SSL_shutdown(ssl);
            SSL_free(ssl);
            socket->shutdown();
            co_return;
        }

    private:
        std::shared_ptr<RouterType> router_;
        SSL_CTX *ctx_{nullptr};
    };

}// namespace usub::server
#endif// USE_OPEN_SSL
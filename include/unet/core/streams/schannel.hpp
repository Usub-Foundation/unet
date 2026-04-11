#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <uvent/Uvent.h>

#include "unet/core/acceptor.hpp"
#include "unet/core/config.hpp"
#include "unet/core/streams/alpn.hpp"

#ifdef _WIN32
#define SECURITY_WIN32
#include <windows.h>
#include <wincrypt.h>
#include <security.h>
#include <schannel.h>
#endif

namespace usub::unet::core::stream {
#ifdef _WIN32
    template<FixedString... Alpns>
    class SChannelStream {
    public:
        static constexpr bool ssl{true};

        enum class MODE : std::uint8_t {
            SERVER,
            CLIENT,
        };

        struct Config {
            MODE mode{MODE::CLIENT};
            std::optional<std::string> server_name{};
            bool verify_peer{true};
            std::optional<std::string> pfx_file{};
            std::optional<std::string> pfx_password{};
            std::optional<std::vector<std::uint8_t>> pfx_blob{};
        };

        SChannelStream() { this->initCredentials(); }

        SChannelStream(std::string_view pfx_file, std::string_view pfx_password = {}) {
            this->config_.mode = MODE::SERVER;
            this->config_.pfx_file = std::string(pfx_file);
            this->config_.pfx_password = std::string(pfx_password);
            this->initCredentials();
        }

        explicit SChannelStream(Config config) : config_(std::move(config)) { this->initCredentials(); }

        ~SChannelStream() {
            std::lock_guard<std::mutex> lock(this->session_mutex_);
            this->sessions_.clear();
            this->freeCredentials();
        }

        void setConfig(Config config) {
            std::lock_guard<std::mutex> lock(this->session_mutex_);
            this->sessions_.clear();
            this->config_ = std::move(config);
            this->freeCredentials();
            this->initCredentials();
        }

        usub::uvent::task::Awaitable<ssize_t> read(usub::uvent::net::TCPClientSocket &socket,
                                                   usub::uvent::utils::DynamicBuffer &buffer) {
            buffer.clear();

            auto [key, session] = this->getOrCreateSession(socket);
            if (!session) { co_return -1; }

            if (!co_await this->ensureHandshake(*session, socket)) {
                this->eraseSession(key);
                co_return -1;
            }

            if (!session->pending_plaintext.empty()) {
                buffer.append(reinterpret_cast<const uint8_t *>(session->pending_plaintext.data()),
                              session->pending_plaintext.size());
                const ssize_t size = static_cast<ssize_t>(session->pending_plaintext.size());
                session->pending_plaintext.clear();
                co_return size;
            }

            for (;;) {
                if (session->encrypted_input.empty()) {
                    const ssize_t rd = co_await this->readSocketToEncryptedBuffer(*session, socket);
                    if (rd <= 0) {
                        this->eraseSession(key);
                        co_return rd;
                    }
                }

                SecBuffer buffers[4]{};
                buffers[0].BufferType = SECBUFFER_DATA;
                buffers[0].pvBuffer = session->encrypted_input.data();
                buffers[0].cbBuffer = static_cast<unsigned long>(session->encrypted_input.size());
                buffers[1].BufferType = SECBUFFER_EMPTY;
                buffers[2].BufferType = SECBUFFER_EMPTY;
                buffers[3].BufferType = SECBUFFER_EMPTY;

                SecBufferDesc desc{};
                desc.ulVersion = SECBUFFER_VERSION;
                desc.cBuffers = 4;
                desc.pBuffers = buffers;

                SECURITY_STATUS status = ::DecryptMessage(&session->context, &desc, 0, nullptr);
                if (status == SEC_E_INCOMPLETE_MESSAGE) {
                    const ssize_t rd = co_await this->readSocketToEncryptedBuffer(*session, socket);
                    if (rd <= 0) {
                        this->eraseSession(key);
                        co_return rd;
                    }
                    continue;
                }
                if (status == SEC_I_CONTEXT_EXPIRED) {
                    this->eraseSession(key);
                    co_return 0;
                }
                if (status == SEC_I_RENEGOTIATE) {
                    this->eraseSession(key);
                    co_return -1;
                }
                if (status != SEC_E_OK) {
                    this->eraseSession(key);
                    co_return -1;
                }

                std::size_t extra_size = 0;
                const char *extra_ptr = nullptr;
                for (const auto &sec_buffer: buffers) {
                    if (sec_buffer.BufferType == SECBUFFER_DATA && sec_buffer.cbBuffer > 0) {
                        buffer.append(reinterpret_cast<const uint8_t *>(sec_buffer.pvBuffer), sec_buffer.cbBuffer);
                    } else if (sec_buffer.BufferType == SECBUFFER_EXTRA && sec_buffer.cbBuffer > 0) {
                        extra_size = sec_buffer.cbBuffer;
                        extra_ptr = reinterpret_cast<const char *>(sec_buffer.pvBuffer);
                    }
                }

                if (extra_size > 0 && extra_ptr) {
                    const char *input_end = session->encrypted_input.data() + session->encrypted_input.size();
                    session->encrypted_input.assign(input_end - static_cast<std::ptrdiff_t>(extra_size), input_end);
                } else {
                    session->encrypted_input.clear();
                }

                if (buffer.size() > 0) { co_return static_cast<ssize_t>(buffer.size()); }
            }
        }

        usub::uvent::task::Awaitable<void> send(usub::uvent::net::TCPClientSocket &socket, std::string_view data) {
            if (data.empty()) { co_return; }

            auto [key, session] = this->getOrCreateSession(socket);
            if (!session) { co_return; }

            if (!co_await this->ensureHandshake(*session, socket)) {
                this->eraseSession(key);
                co_return;
            }

            std::size_t offset = 0;
            while (offset < data.size()) {
                const std::size_t chunk_size =
                        std::min<std::size_t>(data.size() - offset, session->stream_sizes.cbMaximumMessage);
                std::vector<char> encrypted(session->stream_sizes.cbHeader + chunk_size +
                                            session->stream_sizes.cbTrailer);

                std::memcpy(encrypted.data() + session->stream_sizes.cbHeader, data.data() + offset, chunk_size);

                SecBuffer buffers[4]{};
                buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
                buffers[0].pvBuffer = encrypted.data();
                buffers[0].cbBuffer = session->stream_sizes.cbHeader;
                buffers[1].BufferType = SECBUFFER_DATA;
                buffers[1].pvBuffer = encrypted.data() + session->stream_sizes.cbHeader;
                buffers[1].cbBuffer = static_cast<unsigned long>(chunk_size);
                buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
                buffers[2].pvBuffer = encrypted.data() + session->stream_sizes.cbHeader + chunk_size;
                buffers[2].cbBuffer = session->stream_sizes.cbTrailer;
                buffers[3].BufferType = SECBUFFER_EMPTY;

                SecBufferDesc desc{};
                desc.ulVersion = SECBUFFER_VERSION;
                desc.cBuffers = 4;
                desc.pBuffers = buffers;

                if (::EncryptMessage(&session->context, 0, &desc, 0) != SEC_E_OK) {
                    this->eraseSession(key);
                    co_return;
                }

                const std::size_t total_to_send =
                        static_cast<std::size_t>(buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer);
                if (!co_await this->writeAll(socket, encrypted.data(), total_to_send)) {
                    this->eraseSession(key);
                    co_return;
                }

                offset += chunk_size;
            }

            co_return;
        }

        usub::uvent::task::Awaitable<void> sendFile(usub::uvent::net::TCPClientSocket &/*socket*/) { co_return; }

        usub::uvent::task::Awaitable<void> shutdown(usub::uvent::net::TCPClientSocket &socket) {
            auto [key, session] = this->findSession(socket);
            if (session) { this->eraseSession(key); }
            socket.shutdown();
            co_return;
        }

        MODE mode() const { return this->config_.mode; }

    private:
        struct Session {
            CtxtHandle context{};
            bool has_context{false};
            bool handshake_done{false};
            bool has_stream_sizes{false};
            SecPkgContext_StreamSizes stream_sizes{};
            std::vector<char> encrypted_input{};
            std::vector<char> pending_plaintext{};

            ~Session() {
                if (this->has_context) { ::DeleteSecurityContext(&this->context); }
            }
        };

        using SessionKey = std::uintptr_t;

        static constexpr std::size_t kIoBufferSize = 16 * 1024;
        using AlpnConfig = AlpnWireFormatTraits<Alpns...>;
        static constexpr auto kAlpnWireFormat = AlpnConfig::kWireFormat;
        static constexpr std::size_t kApplicationProtocolsListSize =
                offsetof(SEC_APPLICATION_PROTOCOL_LIST, ProtocolList) + kAlpnWireFormat.size();
        static constexpr std::size_t kApplicationProtocolsSize =
                offsetof(SEC_APPLICATION_PROTOCOLS, ProtocolLists) + kApplicationProtocolsListSize;
        static const std::array<unsigned char, kApplicationProtocolsSize> &applicationProtocolsBuffer() {
            static const std::array<unsigned char, kApplicationProtocolsSize> bytes = []() {
                std::array<unsigned char, kApplicationProtocolsSize> bytes{};

                auto *protocols = reinterpret_cast<SEC_APPLICATION_PROTOCOLS *>(bytes.data());
                protocols->ProtocolListsSize = static_cast<unsigned long>(kApplicationProtocolsListSize);

                auto *list = reinterpret_cast<SEC_APPLICATION_PROTOCOL_LIST *>(bytes.data() +
                                                                               offsetof(SEC_APPLICATION_PROTOCOLS,
                                                                                        ProtocolLists));
                list->ProtoNegoExt = SecApplicationProtocolNegotiationExt_ALPN;
                list->ProtocolListSize = static_cast<unsigned short>(kAlpnWireFormat.size());

                std::memcpy(bytes.data() + offsetof(SEC_APPLICATION_PROTOCOLS, ProtocolLists) +
                                    offsetof(SEC_APPLICATION_PROTOCOL_LIST, ProtocolList),
                            kAlpnWireFormat.data(), kAlpnWireFormat.size());
                return bytes;
            }();
            return bytes;
        }

        void initCredentials() {
            SCHANNEL_CRED credentials{};
            credentials.dwVersion = SCHANNEL_CRED_VERSION;
            credentials.dwFlags = SCH_USE_STRONG_CRYPTO;
            credentials.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_2_SERVER;

            unsigned long usage = SECPKG_CRED_OUTBOUND;
            if (this->config_.mode == MODE::SERVER) {
                if (!this->loadServerCertificate()) { return; }
                credentials.cCreds = 1;
                credentials.paCred = &this->server_certificate_;
                usage = SECPKG_CRED_INBOUND;
            } else {
                credentials.dwFlags |= SCH_CRED_NO_DEFAULT_CREDS;
                if (this->config_.verify_peer) {
                    credentials.dwFlags |= SCH_CRED_AUTO_CRED_VALIDATION;
                } else {
                    credentials.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_SERVERNAME_CHECK;
                }
            }

            TimeStamp expiry{};
            const SECURITY_STATUS status =
                    ::AcquireCredentialsHandleW(nullptr, const_cast<wchar_t *>(UNISP_NAME_W), usage, nullptr,
                                                &credentials, nullptr, nullptr, &this->credentials_, &expiry);
            if (status == SEC_E_OK) {
                this->credentials_ready_ = true;
            }
        }

        void freeCredentials() {
            if (this->credentials_ready_) {
                ::FreeCredentialsHandle(&this->credentials_);
                this->credentials_ready_ = false;
            }
            if (this->server_certificate_) {
                ::CertFreeCertificateContext(this->server_certificate_);
                this->server_certificate_ = nullptr;
            }
            if (this->server_cert_store_) {
                ::CertCloseStore(this->server_cert_store_, 0);
                this->server_cert_store_ = nullptr;
            }
        }

        static std::optional<SessionKey> getSocketKey(usub::uvent::net::TCPClientSocket &socket) {
            auto *header = socket.get_raw_header();
            if (!header || header->fd == INVALID_SOCKET) { return std::nullopt; }
            return static_cast<SessionKey>(header->fd);
        }

        std::pair<SessionKey, Session *> findSession(usub::uvent::net::TCPClientSocket &socket) {
            auto key_opt = getSocketKey(socket);
            if (!key_opt.has_value()) { return {0, nullptr}; }

            std::lock_guard<std::mutex> lock(this->session_mutex_);
            auto it = this->sessions_.find(*key_opt);
            if (it == this->sessions_.end()) { return {*key_opt, nullptr}; }
            return {*key_opt, it->second.get()};
        }

        std::pair<SessionKey, Session *> getOrCreateSession(usub::uvent::net::TCPClientSocket &socket) {
            auto key_opt = getSocketKey(socket);
            if (!key_opt.has_value()) { return {0, nullptr}; }

            std::lock_guard<std::mutex> lock(this->session_mutex_);
            auto it = this->sessions_.find(*key_opt);
            if (it != this->sessions_.end()) { return {*key_opt, it->second.get()}; }
            if (!this->credentials_ready_) { return {*key_opt, nullptr}; }

            auto [inserted, _] = this->sessions_.emplace(*key_opt, std::make_unique<Session>());
            return {*key_opt, inserted->second.get()};
        }

        void eraseSession(SessionKey key) {
            std::lock_guard<std::mutex> lock(this->session_mutex_);
            this->sessions_.erase(key);
        }

        static std::optional<std::wstring> toWide(std::string_view value) {
            if (value.empty()) { return std::nullopt; }
            const int needed = ::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                                     nullptr, 0);
            if (needed <= 0) { return std::nullopt; }
            std::wstring wide(static_cast<std::size_t>(needed), L'\0');
            if (::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(),
                                      needed) != needed) {
                return std::nullopt;
            }
            return wide;
        }

        usub::uvent::task::Awaitable<bool> ensureHandshake(Session &session, usub::uvent::net::TCPClientSocket &socket) {
            if (this->config_.mode == MODE::SERVER) { co_return co_await this->ensureServerHandshake(session, socket); }
            if (session.handshake_done) { co_return true; }
            if (!this->credentials_ready_) { co_return false; }

            std::optional<std::wstring> target_name = std::nullopt;
            if (this->config_.server_name.has_value()) { target_name = toWide(*this->config_.server_name); }

            for (;;) {
                if (session.has_context && session.encrypted_input.empty()) {
                    const ssize_t rd = co_await this->readSocketToEncryptedBuffer(session, socket);
                    if (rd <= 0) { co_return false; }
                }

                const unsigned long request_flags =
                        ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                        ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_EXTENDED_ERROR;

                SecBuffer in_buffers[2]{};
                unsigned long in_count = 0;
                bool has_token_input = false;
                unsigned long extra_bytes = 0;

                if (!session.encrypted_input.empty()) {
                    in_buffers[in_count].BufferType = SECBUFFER_TOKEN;
                    in_buffers[in_count].pvBuffer = session.encrypted_input.data();
                    in_buffers[in_count].cbBuffer = static_cast<unsigned long>(session.encrypted_input.size());
                    ++in_count;
                    in_buffers[in_count].BufferType = SECBUFFER_EMPTY;
                    in_buffers[in_count].pvBuffer = nullptr;
                    in_buffers[in_count].cbBuffer = 0;
                    ++in_count;
                    has_token_input = true;
                }

                SecBufferDesc in_desc{};
                in_desc.ulVersion = SECBUFFER_VERSION;
                in_desc.cBuffers = in_count;
                in_desc.pBuffers = in_buffers;

                SecBuffer out_buffers[2]{};
                out_buffers[0].BufferType = SECBUFFER_TOKEN;
                out_buffers[1].BufferType = SECBUFFER_ALERT;

                SecBufferDesc out_desc{};
                out_desc.ulVersion = SECBUFFER_VERSION;
                out_desc.cBuffers = 2;
                out_desc.pBuffers = out_buffers;

                CtxtHandle new_context{};
                unsigned long context_attributes = 0;
                TimeStamp expiry{};

                SECURITY_STATUS status = ::InitializeSecurityContextW(
                        &this->credentials_, session.has_context ? &session.context : nullptr,
                        target_name.has_value() ? target_name->data() : nullptr, request_flags, 0, 0,
                        has_token_input ? &in_desc : nullptr, 0,
                        session.has_context ? &session.context : &new_context,
                        &out_desc, &context_attributes, &expiry);
                if (!session.has_context &&
                    (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED || status == SEC_I_COMPLETE_NEEDED ||
                     status == SEC_I_COMPLETE_AND_CONTINUE)) {
                    session.context = new_context;
                    session.has_context = true;
                }

                if (status == SEC_I_COMPLETE_NEEDED || status == SEC_I_COMPLETE_AND_CONTINUE) {
                    if (::CompleteAuthToken(&session.context, &out_desc) != SEC_E_OK) { co_return false; }
                    status = (status == SEC_I_COMPLETE_AND_CONTINUE) ? SEC_I_CONTINUE_NEEDED : SEC_E_OK;
                }

                if (out_buffers[0].pvBuffer && out_buffers[0].cbBuffer > 0) {
                    const bool wrote =
                            co_await this->writeAll(socket, reinterpret_cast<const char *>(out_buffers[0].pvBuffer),
                                                    static_cast<std::size_t>(out_buffers[0].cbBuffer));
                    ::FreeContextBuffer(out_buffers[0].pvBuffer);
                    if (!wrote) { co_return false; }
                }

                if (has_token_input && in_count >= 2 && in_buffers[in_count - 1].BufferType == SECBUFFER_EXTRA) {
                    extra_bytes = in_buffers[in_count - 1].cbBuffer;
                }

                if (extra_bytes > 0) {
                    const char *end = session.encrypted_input.data() + session.encrypted_input.size();
                    session.encrypted_input.assign(end - static_cast<std::ptrdiff_t>(extra_bytes), end);
                } else if (has_token_input) {
                    session.encrypted_input.clear();
                }

                if (status == SEC_E_OK) {
                    if (::QueryContextAttributesW(&session.context, SECPKG_ATTR_STREAM_SIZES,
                                                  &session.stream_sizes) != SEC_E_OK) {
                        co_return false;
                    }
                    session.has_stream_sizes = true;
                    session.handshake_done = true;
                    co_return true;
                }

                if (status == SEC_E_INCOMPLETE_MESSAGE || status == SEC_I_CONTINUE_NEEDED) {
                    const ssize_t rd = co_await this->readSocketToEncryptedBuffer(session, socket);
                    if (rd <= 0) { co_return false; }
                    continue;
                }

                co_return false;
            }
        }

        usub::uvent::task::Awaitable<bool> ensureServerHandshake(Session &session,
                                                                 usub::uvent::net::TCPClientSocket &socket) {
            if (session.handshake_done) { co_return true; }
            if (!this->credentials_ready_) { co_return false; }

            for (;;) {
                if (session.encrypted_input.empty()) {
                    const ssize_t rd = co_await this->readSocketToEncryptedBuffer(session, socket);
                    if (rd <= 0) { co_return false; }
                }

                const unsigned long request_flags =
                        ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY |
                        ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM | ASC_REQ_EXTENDED_ERROR;

                SecBuffer in_buffers[2]{};
                in_buffers[0].BufferType = SECBUFFER_TOKEN;
                in_buffers[0].pvBuffer = session.encrypted_input.data();
                in_buffers[0].cbBuffer = static_cast<unsigned long>(session.encrypted_input.size());
                in_buffers[1].BufferType = SECBUFFER_EMPTY;

                SecBufferDesc in_desc{};
                in_desc.ulVersion = SECBUFFER_VERSION;
                in_desc.cBuffers = 2;
                in_desc.pBuffers = in_buffers;

                SecBuffer out_buffer{};
                out_buffer.BufferType = SECBUFFER_TOKEN;

                SecBuffer alert_buffer{};
                alert_buffer.BufferType = SECBUFFER_ALERT;

                SecBufferDesc out_desc{};
                out_desc.ulVersion = SECBUFFER_VERSION;
                SecBuffer out_buffers[2]{out_buffer, alert_buffer};
                out_desc.cBuffers = 2;
                out_desc.pBuffers = out_buffers;

                unsigned long context_attributes = 0;
                TimeStamp expiry{};
                CtxtHandle new_context{};

                SECURITY_STATUS status = ::AcceptSecurityContext(
                        &this->credentials_, session.has_context ? &session.context : nullptr, &in_desc, request_flags,
                        0, session.has_context ? &session.context : &new_context, &out_desc,
                        &context_attributes, &expiry);
                if (!session.has_context &&
                    (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED || status == SEC_I_COMPLETE_NEEDED ||
                     status == SEC_I_COMPLETE_AND_CONTINUE)) {
                    session.context = new_context;
                    session.has_context = true;
                }

                if (status == SEC_I_COMPLETE_NEEDED || status == SEC_I_COMPLETE_AND_CONTINUE) {
                    if (::CompleteAuthToken(&session.context, &out_desc) != SEC_E_OK) { co_return false; }
                    status = (status == SEC_I_COMPLETE_AND_CONTINUE) ? SEC_I_CONTINUE_NEEDED : SEC_E_OK;
                }

                if (out_buffers[0].pvBuffer && out_buffers[0].cbBuffer > 0) {
                    const bool wrote =
                            co_await this->writeAll(socket, reinterpret_cast<const char *>(out_buffers[0].pvBuffer),
                                                    static_cast<std::size_t>(out_buffers[0].cbBuffer));
                    ::FreeContextBuffer(out_buffers[0].pvBuffer);
                    if (!wrote) { co_return false; }
                }

                unsigned long extra_bytes = 0;
                if (in_buffers[1].BufferType == SECBUFFER_EXTRA) { extra_bytes = in_buffers[1].cbBuffer; }

                if (extra_bytes > 0) {
                    const char *end = session.encrypted_input.data() + session.encrypted_input.size();
                    session.encrypted_input.assign(end - static_cast<std::ptrdiff_t>(extra_bytes), end);
                } else {
                    session.encrypted_input.clear();
                }

                if (status == SEC_E_OK) {
                    if (::QueryContextAttributesW(&session.context, SECPKG_ATTR_STREAM_SIZES,
                                                  &session.stream_sizes) != SEC_E_OK) {
                        co_return false;
                    }
                    session.has_stream_sizes = true;
                    session.handshake_done = true;
                    co_return true;
                }

                if (status == SEC_E_INCOMPLETE_MESSAGE || status == SEC_I_CONTINUE_NEEDED) {
                    const ssize_t rd = co_await this->readSocketToEncryptedBuffer(session, socket);
                    if (rd <= 0) { co_return false; }
                    continue;
                }

                co_return false;
            }
        }

        static usub::uvent::task::Awaitable<ssize_t> readSocketToEncryptedBuffer(Session &session,
                                                                                  usub::uvent::net::TCPClientSocket &socket) {
            std::array<char, kIoBufferSize> network_buffer{};
            const ssize_t rd = co_await socket.async_read(reinterpret_cast<uint8_t *>(network_buffer.data()),
                                                          network_buffer.size());
            if (rd > 0) {
                session.encrypted_input.insert(session.encrypted_input.end(), network_buffer.data(),
                                               network_buffer.data() + rd);
            }
            co_return rd;
        }

        static usub::uvent::task::Awaitable<bool> writeAll(usub::uvent::net::TCPClientSocket &socket, const char *data,
                                                           std::size_t size) {
            std::size_t offset = 0;
            while (offset < size) {
                auto *ptr = reinterpret_cast<uint8_t *>(const_cast<char *>(data + offset));
                const ssize_t wr = co_await socket.async_write(ptr, size - offset);
                if (wr <= 0) { co_return false; }
                offset += static_cast<std::size_t>(wr);
            }
            co_return true;
        }

        static std::optional<std::vector<std::uint8_t>> readBinaryFile(std::string_view path) {
            std::ifstream input(std::string(path), std::ios::binary);
            if (!input) { return std::nullopt; }
            input.seekg(0, std::ios::end);
            const auto size = input.tellg();
            if (size < 0) { return std::nullopt; }
            input.seekg(0, std::ios::beg);
            std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
            if (!data.empty()) {
                input.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
                if (!input) { return std::nullopt; }
            }
            return data;
        }

        bool loadServerCertificate() {
            std::vector<std::uint8_t> pfx_data{};
            if (this->config_.pfx_blob.has_value()) {
                pfx_data = *this->config_.pfx_blob;
            } else if (this->config_.pfx_file.has_value()) {
                auto file_data = readBinaryFile(*this->config_.pfx_file);
                if (!file_data.has_value()) { return false; }
                pfx_data = std::move(*file_data);
            } else {
                return false;
            }

            if (pfx_data.empty()) { return false; }

            CRYPT_DATA_BLOB blob{};
            blob.pbData = pfx_data.data();
            blob.cbData = static_cast<unsigned long>(pfx_data.size());

            const std::wstring password =
                    this->config_.pfx_password.has_value() ? toWide(*this->config_.pfx_password).value_or(L"") : L"";

            unsigned long import_flags = CRYPT_USER_KEYSET;

            this->server_cert_store_ = ::PFXImportCertStore(&blob, password.c_str(), import_flags);
            if (!this->server_cert_store_) { return false; }

            PCCERT_CONTEXT current = nullptr;
            while ((current = ::CertEnumCertificatesInStore(this->server_cert_store_, current)) != nullptr) {
                DWORD property_size = 0;
                if (::CertGetCertificateContextProperty(current, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &property_size) ||
                    ::CertGetCertificateContextProperty(current, CERT_KEY_CONTEXT_PROP_ID, nullptr, &property_size)) {
                    this->server_certificate_ = ::CertDuplicateCertificateContext(current);
                    break;
                }
            }

            if (!this->server_certificate_) {
                current = ::CertEnumCertificatesInStore(this->server_cert_store_, nullptr);
                if (current) { this->server_certificate_ = ::CertDuplicateCertificateContext(current); }
            }

            return this->server_certificate_ != nullptr;
        }

    private:
        Config config_{};
        CredHandle credentials_{};
        bool credentials_ready_{false};
        HCERTSTORE server_cert_store_{nullptr};
        PCCERT_CONTEXT server_certificate_{nullptr};
        std::unordered_map<SessionKey, std::unique_ptr<Session>> sessions_{};
        std::mutex session_mutex_{};
    };
#else
    template<FixedString... Alpns>
    class SChannelStream {
    public:
        static constexpr bool ssl{true};

        enum class MODE : std::uint8_t {
            SERVER,
            CLIENT,
        };

        struct Config {
            MODE mode{MODE::CLIENT};
            std::optional<std::string> server_name{};
            bool verify_peer{true};
            std::optional<std::string> pfx_file{};
            std::optional<std::string> pfx_password{};
            std::optional<std::vector<std::uint8_t>> pfx_blob{};
        };

        SChannelStream() = default;
        explicit SChannelStream(Config config) : config_(std::move(config)) {}
        ~SChannelStream() = default;

        void setConfig(Config config) { this->config_ = std::move(config); }

        usub::uvent::task::Awaitable<ssize_t> read(usub::uvent::net::TCPClientSocket,
                                                   usub::uvent::utils::DynamicBuffer &) {
            co_return -1;
        }

        usub::uvent::task::Awaitable<void> send(usub::uvent::net::TCPClientSocket&, std::string_view) { co_return; }
        usub::uvent::task::Awaitable<void> sendFile(usub::uvent::net::TCPClientSocket&) { co_return; }
        usub::uvent::task::Awaitable<void> shutdown(usub::uvent::net::TCPClientSocket &socket) {
            socket.shutdown();
            co_return;
        }

        MODE mode() const { return this->config_.mode; }

    private:
        Config config_{};
    };
#endif

    template<FixedString... Alpns>
    using WinSSLStream = SChannelStream<Alpns...>;
}// namespace usub::unet::core::stream

#ifdef _WIN32
namespace usub::unet::core {
    template<stream::FixedString... Alpns>
    class Acceptor<stream::SChannelStream<Alpns...>> {
    public:
        Acceptor() = default;
        ~Acceptor() = default;

        template<class OnConnection>
        usub::uvent::task::Awaitable<void> acceptLoop(OnConnection on_connection, Config &config) {
            const Config::Object empty_section{};
            const Config::Object *section_ptr = config.getObject("HTTP.SChannelStream");
            const Config::Object &section = section_ptr ? *section_ptr : empty_section;

            std::string host = config.getString(section, "host", "127.0.0.1");
            const std::uint64_t raw_port = config.getUInt(section, "port", 4443);
            const std::uint16_t port =
                    (raw_port <= static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max()))
                            ? static_cast<std::uint16_t>(raw_port)
                            : static_cast<std::uint16_t>(4443);

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

            using StreamType = stream::SChannelStream<Alpns...>;
            typename StreamType::Config tls_config{};
            tls_config.mode = StreamType::MODE::SERVER;
            tls_config.verify_peer = false;
            tls_config.pfx_file = config.getString(section, "pfx", "server.pfx");
            tls_config.pfx_password = config.getString(section, "password", "");

            usub::uvent::net::TCPServerSocket server_socket{host, static_cast<int>(port), backlog, ip_version,
                                                            socket_type};
            StreamType stream{tls_config};

            for (;;) {
                auto soc = co_await server_socket.async_accept();
                if (!soc) { continue; }
                usub::uvent::system::co_spawn(on_connection(stream, std::move(soc.value())));
            }
            co_return;
        }
    };
}// namespace usub::unet::core
#endif

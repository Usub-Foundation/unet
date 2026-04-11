#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <uvent/Uvent.h>

#include "unet/core/config.hpp"
#include "unet/core/streams/schannel.hpp"
#include "unet/http.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
    constexpr const char *kTestPfxBase64 =
            "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";

    bool metadataMiddle(const usub::unet::http::Request &, usub::unet::http::Response &) { return true; }
    bool headerMiddle(const usub::unet::http::Request &, usub::unet::http::Response &) { return true; }

    ServerHandler handlerFunction(usub::unet::http::Request &request, usub::unet::http::Response &response) {
        std::cout << "Query Params:\n" << request.metadata.uri.query << "\n";
        response.setStatus(200).addHeader("Content-Type", "text/html").setBody("Hello World! How are you \n");
        co_return;
    }

#ifdef _WIN32
    std::optional<std::vector<std::uint8_t>> decode_base64(std::string_view base64_text) {
        DWORD bytes_required = 0;
        if (!::CryptStringToBinaryA(base64_text.data(), static_cast<DWORD>(base64_text.size()), CRYPT_STRING_BASE64,
                                    nullptr, &bytes_required, nullptr, nullptr)) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> bytes(bytes_required);
        if (!::CryptStringToBinaryA(base64_text.data(), static_cast<DWORD>(base64_text.size()), CRYPT_STRING_BASE64,
                                    bytes.data(), &bytes_required, nullptr, nullptr)) {
            return std::nullopt;
        }
        bytes.resize(bytes_required);
        return bytes;
    }

    bool write_binary_file(const std::filesystem::path &path, const std::vector<std::uint8_t> &data) {
        std::ofstream out(path, std::ios::binary);
        if (!out) { return false; }
        out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        return out.good();
    }

    usub::unet::core::Config make_schannel_config(const std::string &pfx_path) {
        using Config = usub::unet::core::Config;
        Config config{};
        Config::Object section{};

        Config::Value host{};
        host.data = std::string{"127.0.0.1"};
        section.emplace("host", std::move(host));

        Config::Value port{};
        port.data = static_cast<std::uint64_t>(4443);
        section.emplace("port", std::move(port));

        Config::Value pfx{};
        pfx.data = pfx_path;
        section.emplace("pfx", std::move(pfx));

        Config::Value password{};
        password.data = std::string{"unet-test"};
        section.emplace("password", std::move(password));

        Config::Value section_value{};
        section_value.data = std::move(section);
        config.root.emplace("HTTP.SChannelStream", std::move(section_value));
        return config;
    }
#endif
}// namespace

int main() {
#ifndef _WIN32
    std::cerr << "schannel http server test requires windows\n";
    return 2;
#else
    const auto pfx_blob = decode_base64(kTestPfxBase64);
    if (!pfx_blob.has_value()) {
        std::cerr << "failed to decode embedded test pfx\n";
        return 2;
    }

    const auto pfx_path = (std::filesystem::current_path() / "server.pfx").string();
    if (!write_binary_file(pfx_path, *pfx_blob)) {
        std::cerr << "failed to write server.pfx\n";
        return 2;
    }

    usub::Uvent uvent{4};
    auto config = make_schannel_config(pfx_path);
    usub::unet::http::ServerImpl<usub::unet::http::router::Radix, usub::unet::core::stream::SChannelStream<>> server{
            uvent, config};
    server.addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER, metadataMiddle);
    server.handle("GET", "/path", handlerFunction)
            .addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER, headerMiddle)
            .addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER, headerMiddle);
    uvent.run();
    return 0;
#endif
}

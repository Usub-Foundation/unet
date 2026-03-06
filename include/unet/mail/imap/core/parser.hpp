#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "unet/mail/imap/core/error.hpp"
#include "unet/mail/imap/core/response.hpp"

namespace usub::unet::mail::imap::core {

    struct ParserLimits {
        std::size_t max_line_bytes{16 * 1024};
        std::size_t max_literal_bytes{8 * 1024 * 1024};
        std::size_t max_buffer_bytes{8 * 1024 * 1024};
    };

    class ResponseParser {
    public:
        explicit ResponseParser(ParserLimits limits = {});

        std::expected<void, Error> feed(std::string_view bytes);
        std::expected<std::optional<Response>, Error> next();

        void reset();

        [[nodiscard]] std::size_t bufferedBytes() const noexcept;

    private:
        struct LiteralSpec {
            std::size_t bytes{0};
            bool non_synchronizing{false};
        };

        std::expected<Response, Error> parseSimpleLine(std::string_view line) const;
        std::expected<Response, Error>
        parseLineWithLiteral(std::string_view line, std::string_view literal, std::string_view trailer) const;

        static std::optional<LiteralSpec> parseLiteralSuffix(std::string_view line);
        static std::size_t findCrlf(std::string_view input, std::size_t from) noexcept;

        void compactBuffer();

        ParserLimits limits_{};
        std::string buffer_{};
        std::size_t cursor_{0};
    };

}// namespace usub::unet::mail::imap::core

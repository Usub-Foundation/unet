#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "unet/mail/imap/core/error.hpp"

namespace usub::unet::mail::imap::core {

    struct SequenceNumber {
        enum class Kind : std::uint8_t {
            Value,
            Asterisk,
        };

        Kind kind{Kind::Value};
        std::uint32_t value{0};

        [[nodiscard]] bool valid() const noexcept;
    };

    struct SequenceRange {
        SequenceNumber first{};
        SequenceNumber last{};
    };

    class SequenceSet {
    public:
        static std::expected<SequenceSet, Error> parse(std::string_view input);

        [[nodiscard]] const std::vector<SequenceRange> &ranges() const noexcept;
        [[nodiscard]] std::string toString() const;

    private:
        std::vector<SequenceRange> ranges_{};
    };

}// namespace usub::unet::mail::imap::core

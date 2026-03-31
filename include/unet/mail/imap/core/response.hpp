#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "unet/mail/imap/core/data_format.hpp"

namespace usub::unet::mail::imap::core {

    enum class ResponseCondition : std::uint8_t {
        OK,
        NO,
        BAD,
        PREAUTH,
        BYE,
        UNKNOWN,
    };

    struct ResponseCode {
        Atom name{};
        std::string text{};
    };

    struct StatusInfo {
        ResponseCondition condition{ResponseCondition::UNKNOWN};
        std::optional<ResponseCode> code{};
        std::string text{};
    };

    struct TaggedResponse {
        Atom tag{};
        StatusInfo status{};
    };

    struct UntaggedStatusResponse {
        StatusInfo status{};
    };

    struct UntaggedNumericResponse {
        std::uint32_t number{0};
        Atom atom{};
        std::string text{};
        std::optional<String> literal{};
    };

    struct UntaggedDataResponse {
        Atom atom{};
        std::string text{};
        std::optional<String> literal{};
    };

    struct UntaggedResponse {
        std::variant<UntaggedStatusResponse, UntaggedNumericResponse, UntaggedDataResponse> payload{};
    };

    struct ContinuationResponse {
        std::string text{};
    };

    struct Response {
        enum class Kind : std::uint8_t {
            Tagged,
            Untagged,
            Continuation,
        };

        Kind kind{Kind::Untagged};
        std::variant<TaggedResponse, UntaggedResponse, ContinuationResponse> data{};
        std::string raw{};
    };

    [[nodiscard]] bool isOk(const StatusInfo &status) noexcept;
    [[nodiscard]] std::optional<std::uint32_t> extractExists(const Response &response) noexcept;
    [[nodiscard]] std::optional<std::string_view> extractFetchLiteral(const Response &response) noexcept;

}// namespace usub::unet::mail::imap::core

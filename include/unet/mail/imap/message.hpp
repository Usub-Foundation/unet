#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace usub::unet::mail::imap {

    struct Atom {
        std::string value;
    };
    struct Flag {
        std::string value;
    };

    using Number = std::uint64_t;

    struct String {
        enum class FORM {
            SYNCHRONIZING_LITERAL,
            NON_SYNCHRONIZING_LITERAL,
            QUOTED,
        };
        FORM type;
        std::string value{};
    };

    struct String8 {
        std::vector<std::uint8_t> value{};
    };

    struct NIL {};

    struct ParenthesizedListValue;
    using ParenthesizedList = std::vector<ParenthesizedListValue>;

    struct ParenthesizedListValue {
        using Data = std::variant<Atom, String, Number, String8, Flag, NIL, ParenthesizedList>;
        Data data;
    };

}// namespace usub::unet::mail::imap

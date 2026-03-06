#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace usub::unet::mail::imap::core {

    struct Atom {
        std::string value{};
    };

    using Number = std::uint64_t;

    struct String {
        enum class Form : std::uint8_t {
            SynchronizingLiteral,
            NonSynchronizingLiteral,
            Quoted,
        };

        Form form{Form::Quoted};
        std::string value{};
    };

    struct Literal8 {
        std::vector<std::uint8_t> value{};
    };

    struct NIL {};

    using AString = std::variant<Atom, String>;
    using NString = std::variant<NIL, String>;

    struct Value;
    using ParenthesizedList = std::vector<Value>;

    struct Value {
        using Data = std::variant<Atom, Number, String, Literal8, NIL, ParenthesizedList>;
        Data data{};
    };

}// namespace usub::unet::mail::imap::core

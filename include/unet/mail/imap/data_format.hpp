#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace usub::unet::mail::imap {

    struct Atom {
        std::string value;
    };

    using Number = std::uint64_t;

    struct String {
        enum class Form {
            SynchronizingLiteral,   // {n}\r\n...
            NonSynchronizingLiteral,// {n+}\r\n...
            Quoted,                 // "..."
        };

        Form form{Form::Quoted};
        std::string value{};
    };

    struct Literal8 {
        std::vector<std::uint8_t> value{};
    };

    struct NIL {};

    using AString = std::variant<Atom, String>;// atom / string
    using NString = std::variant<NIL, String>; // NIL / string

    struct Value;
    using ParenthesizedList = std::vector<Value>;

    struct Value {
        using Data = std::variant<Atom, Number, String, Literal8, NIL, ParenthesizedList>;
        Data data;
    };

}// namespace usub::unet::mail::imap

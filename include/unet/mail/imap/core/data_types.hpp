#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace usub::unet::mail::imap {
    using Nil = std::monostate;// Just to represent not just emoty string but nil

    using NString = std::variant<Nil, std::string>;

    using Star = std::monostate;
    using SequenceSetValue = std::variant<std::monostate, std::uint32_t>;
    using SequenceSetItem = std::pair<SequenceSetValue, std::optional<SequenceSetValue>>;
    using SequenceSet = std::vector<SequenceSetItem>;

}// namespace usub::unet::mail::imap
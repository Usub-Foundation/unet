#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "unet/mail/imap/core/data_format.hpp"

namespace usub::unet::mail::imap::core {

    struct UID {
        std::uint32_t value{0};
        [[nodiscard]] bool valid() const noexcept { return value != 0; }
    };

    struct MessageSequenceNumber {
        std::uint32_t value{0};
        [[nodiscard]] bool valid() const noexcept { return value != 0; }
    };

    using MessageHandle = std::variant<MessageSequenceNumber, UID>;

    struct Flag {
        Atom value{};
    };

    using Flags = std::vector<Flag>;

    struct InternalDate {
        String value{};
    };

    using RFC822Size = std::uint64_t;

    struct Address {
        NString name{};
        NString route{};
        NString mailbox{};
        NString host{};
    };

    using AddressList = std::vector<Address>;
    using NAddressList = std::variant<NIL, AddressList>;

    struct Envelope {
        NString date{};
        NString subject{};
        NAddressList from{};
        NAddressList sender{};
        NAddressList reply_to{};
        NAddressList to{};
        NAddressList cc{};
        NAddressList bcc{};
        NString in_reply_to{};
        NString message_id{};
    };

    struct BodyStructure {};

    struct MessageAttributes {
        std::optional<UID> uid{};
        std::optional<MessageSequenceNumber> msn{};
        std::optional<Flags> flags{};
        std::optional<InternalDate> internal_date{};
        std::optional<RFC822Size> rfc822_size{};
        std::optional<Envelope> envelope{};
        std::optional<BodyStructure> body_structure{};
    };

}// namespace usub::unet::mail::imap::core

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "unet/mail/imap/data_format.hpp"

namespace usub::unet::mail::imap {

    struct UID {
        std::uint32_t value{0};// non-zero
        [[nodiscard]] bool valid() const noexcept { return value != 0; }
    };

    struct MessageSequenceNumber {
        std::uint32_t value{0};// 1..N
        [[nodiscard]] bool valid() const noexcept { return value != 0; }
    };

    struct Flag {
        Atom value;
    };

    using Flags = std::vector<Flag>;

    struct InternalDate {
        String value;// IMAP date-time string
    };

    using RFC822Size = std::uint64_t;

    struct Address {
        NString name;   // addr-name (NIL/string)
        NString route;  // addr-adl (usually NIL)
        NString mailbox;// addr-mailbox (NIL/string)
        NString host;   // addr-host (NIL/string)
    };

    using AddressList = std::vector<Address>;
    using NAddressList = std::variant<NIL, AddressList>;

    struct Envelope {
        NString date;
        NString subject;
        NAddressList from;
        NAddressList sender;
        NAddressList reply_to;
        NAddressList to;
        NAddressList cc;
        NAddressList bcc;
        NString in_reply_to;
        NString message_id;
    };

    // TODO:
    struct BodyStructure {};

    struct MessageAttributes {
        std::optional<UID> uid;
        std::optional<MessageSequenceNumber> msn;
        std::optional<Flags> flags;
        std::optional<InternalDate> internal_date;
        std::optional<RFC822Size> rfc822_size;
        std::optional<Envelope> envelope;
        std::optional<BodyStructure> body_structure;
    };

}// namespace usub::unet::mail::imap

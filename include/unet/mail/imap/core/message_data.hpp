#pragma once

#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "unet/mail/imap/core/data_types.hpp"

namespace usub::unet::mail::imap {
    [[nodiscard]] inline bool asciiEqual(std::string_view lhs, std::string_view rhs) {
        if (lhs.size() != rhs.size()) { return false; }

        for (std::size_t i = 0; i < lhs.size(); ++i) {
            const auto left = static_cast<unsigned char>(lhs[i]);
            const auto right = static_cast<unsigned char>(rhs[i]);
            if (std::tolower(left) != std::tolower(right)) { return false; }
        }
        return true;
    }

    struct HeaderFields {
        std::vector<std::pair<std::string, std::string>> values{};

        [[nodiscard]] std::optional<std::string> getFirst(std::string_view name) const {
            for (const auto &[field_name, field_value]: values) {
                if (field_name.size() != name.size()) { continue; }

                bool equal = true;
                for (std::size_t i = 0; i < name.size(); ++i) {
                    const auto left = static_cast<unsigned char>(field_name[i]);
                    const auto right = static_cast<unsigned char>(name[i]);
                    if (std::tolower(left) != std::tolower(right)) {
                        equal = false;
                        break;
                    }
                }

                if (equal) { return field_value; }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::vector<std::string> getAll(std::string_view name) const {
            std::vector<std::string> out{};
            for (const auto &[field_name, field_value]: values) {
                if (asciiEqual(field_name, name)) { out.push_back(field_value); }
            }
            return out;
        }
    };

    struct MessageDataValue;
    using MessageDataList = std::vector<MessageDataValue>;
    struct BodyStructure;
    struct Envelope;

    using BodyFieldParameters = std::vector<std::pair<std::string, NString>>;

    struct Address {
        NString name{};
        NString route{};
        NString mailbox{};
        NString host{};
    };

    using AddressList = std::vector<Address>;
    using NAddressList = std::variant<Nil, AddressList>;

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

    struct BodyDisposition {
        std::string type{};
        BodyFieldParameters parameters{};
    };

    struct BodyPartFields {
        std::string media_type{};
        std::string media_subtype{};
        BodyFieldParameters parameters{};
        NString id{};
        NString description{};
        std::string transfer_encoding{};
        std::optional<std::uint64_t> octet_count{};
    };

    struct BodyBasicPart {
        BodyPartFields fields{};
    };

    struct BodyTextPart {
        BodyPartFields fields{};
        std::optional<std::uint64_t> line_count{};
    };

    struct BodyMultipart {
        std::vector<BodyStructure> parts{};
        std::string media_subtype{};
        BodyFieldParameters parameters{};
        std::optional<BodyDisposition> disposition{};
        std::vector<std::string> languages{};
        NString location{};
    };

    struct BodyStructure {
        using Data = std::variant<BodyBasicPart, BodyTextPart, BodyMultipart, MessageDataList>;
        Data data{};

        [[nodiscard]] const BodyBasicPart *asBasic() const noexcept { return std::get_if<BodyBasicPart>(&data); }
        [[nodiscard]] const BodyTextPart *asText() const noexcept { return std::get_if<BodyTextPart>(&data); }
        [[nodiscard]] const BodyMultipart *asMultipart() const noexcept { return std::get_if<BodyMultipart>(&data); }
        [[nodiscard]] const MessageDataList *asGenericList() const noexcept { return std::get_if<MessageDataList>(&data); }
    };

    struct MessageDataValue {
        using Data =
                std::variant<Nil, std::uint64_t, std::string, HeaderFields, MessageDataList,
                             std::shared_ptr<BodyStructure>, std::shared_ptr<Envelope>>;
        Data data{};

        [[nodiscard]] bool isNil() const noexcept { return std::holds_alternative<Nil>(data); }
        [[nodiscard]] const std::uint64_t *asNumber() const noexcept { return std::get_if<std::uint64_t>(&data); }
        [[nodiscard]] const std::string *asString() const noexcept { return std::get_if<std::string>(&data); }
        [[nodiscard]] const HeaderFields *asHeaders() const noexcept { return std::get_if<HeaderFields>(&data); }
        [[nodiscard]] const MessageDataList *asList() const noexcept { return std::get_if<MessageDataList>(&data); }
        [[nodiscard]] const BodyStructure *asBodyStructure() const noexcept {
            const auto *value = std::get_if<std::shared_ptr<BodyStructure>>(&data);
            if (value == nullptr || !value->operator bool()) { return nullptr; }
            return value->get();
        }
        [[nodiscard]] const Envelope *asEnvelope() const noexcept {
            const auto *value = std::get_if<std::shared_ptr<Envelope>>(&data);
            if (value == nullptr || !value->operator bool()) { return nullptr; }
            return value->get();
        }
    };

    struct MessageDataItem {
        std::string name{};
        MessageDataValue value{};

        [[nodiscard]] bool hasName(std::string_view other) const noexcept { return asciiEqual(name, other); }
    };

    [[nodiscard]] inline const MessageDataItem *findMessageDataItem(const std::vector<MessageDataItem> &items,
                                                                    std::string_view name) noexcept {
        for (const auto &item: items) {
            if (item.hasName(name)) { return &item; }
        }
        return nullptr;
    }

    [[nodiscard]] inline const NString *findBodyFieldParameter(const BodyFieldParameters &parameters,
                                                               std::string_view name) noexcept {
        for (const auto &[parameter_name, parameter_value]: parameters) {
            if (asciiEqual(parameter_name, name)) { return &parameter_value; }
        }
        return nullptr;
    }

}// namespace usub::unet::mail::imap

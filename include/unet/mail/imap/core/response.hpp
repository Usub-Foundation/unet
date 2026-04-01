#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "unet/mail/imap/core/data_types.hpp"
#include "unet/mail/imap/core/message_data.hpp"

namespace usub::unet::mail::imap::response {

    struct ResponseCode {
        std::string name{};
        std::optional<std::string> data{};
    };

    struct Ok {
        std::optional<ResponseCode> code{};
        std::string text{};
    };

    struct No {
        std::optional<ResponseCode> code{};
        std::string text{};
    };

    struct Bad {
        std::optional<ResponseCode> code{};
        std::string text{};
    };

    struct Preauth {
        std::optional<ResponseCode> code{};
        std::string text{};
    };

    struct Bye {
        std::optional<ResponseCode> code{};
        std::string text{};
    };

    struct GenericStatus {
        std::string name{};
        std::optional<ResponseCode> code{};
        std::string text{};
    };

    using TaggedStatusData = std::variant<Ok, No, Bad, GenericStatus>;
    using UntaggedStatusData = std::variant<Ok, No, Bad, Preauth, Bye, GenericStatus>;

    template<typename Data>
    struct Tagged {
        std::string tag{};
        Data data{};
    };

    template<typename Data>
    struct Untagged {
        Data data{};
    };

    template<typename Data = std::string>
    struct Continuation {
        Data data{};
    };

    struct CapabilityData {
        std::vector<std::string> capabilities{};
    };

    struct EnabledData {
        std::vector<std::string> capabilities{};
    };

    struct FlagsData {
        std::vector<std::string> flags{};
    };

    struct Exists {
        std::uint32_t count{};
    };

    struct Recent {
        std::uint32_t count{};
    };

    struct Expunge {
        std::uint32_t sequence_number{};
    };

    struct SearchData {
        std::vector<std::uint32_t> matches{};
        std::optional<std::uint32_t> min{};
        std::optional<std::uint32_t> max{};
        std::optional<std::uint32_t> count{};
    };

    struct ListItem {
        std::vector<std::string> attributes{};
        NString hierarchy_delimiter{};
        std::string mailbox_name{};
    };

    struct ListData {
        ListItem item{};
    };

    struct NamespaceDescriptor {
        std::string prefix{};
        NString hierarchy_delimiter{};
        std::vector<std::string> extensions{};
    };

    struct NamespaceData {
        std::vector<NamespaceDescriptor> personal{};
        std::vector<NamespaceDescriptor> other_users{};
        std::vector<NamespaceDescriptor> shared{};
    };

    struct StatusAttribute {
        std::string name{};
        std::uint64_t value{};
    };

    struct StatusData {
        std::string mailbox_name{};
        std::vector<StatusAttribute> attributes{};
    };

    struct FetchData {
        std::uint32_t sequence_number{};
        std::vector<MessageDataItem> items{};
    };

    struct GenericData {
        std::string name{};
        std::optional<std::string> text{};
    };

    using UntaggedData = std::variant<CapabilityData, EnabledData, FlagsData, Exists, Recent, Expunge, SearchData,
                                      ListData, NamespaceData, StatusData, FetchData, GenericData>;

    using TaggedStatus = Tagged<TaggedStatusData>;
    using UntaggedStatus = Untagged<UntaggedStatusData>;
    using UntaggedServerData = Untagged<UntaggedData>;
    using ServerResponse = std::variant<TaggedStatus, UntaggedStatus, UntaggedServerData, Continuation<>>;

    struct Completion {
        std::string tag{};
        TaggedStatusData status{};
    };

    struct Capability {
        std::vector<std::string> capabilities{};
    };

    struct Noop {};
    struct Logout {};
    struct StartTls {};
    struct Authenticate {};
    struct Login {};
    struct Create {};
    struct Delete {};
    struct Rename {};
    struct Subscribe {};
    struct Unsubscribe {};
    struct Check {};
    struct Close {};
    struct Unselect {};
    struct ExpungeResult {};

    struct Enable {
        std::vector<std::string> enabled_capabilities{};
    };

    struct ListedMailbox {
        std::vector<std::string> attributes{};
        NString hierarchy_delimiter{};
        std::string mailbox_name{};
    };

    struct List {
        std::vector<ListedMailbox> mailboxes{};
    };

    struct Namespace {
        std::vector<NamespaceDescriptor> personal{};
        std::vector<NamespaceDescriptor> other_users{};
        std::vector<NamespaceDescriptor> shared{};
    };

    struct Select {
        std::vector<std::string> flags{};
        std::vector<std::string> permanent_flags{};
        std::optional<std::uint32_t> exists{};
        std::optional<std::uint32_t> recent{};
        std::optional<std::uint32_t> unseen{};
        std::optional<std::uint32_t> uid_next{};
        std::optional<std::uint32_t> uid_validity{};
        bool read_only{false};
    };

    struct Examine {
        std::vector<std::string> flags{};
        std::vector<std::string> permanent_flags{};
        std::optional<std::uint32_t> exists{};
        std::optional<std::uint32_t> recent{};
        std::optional<std::uint32_t> unseen{};
        std::optional<std::uint32_t> uid_next{};
        std::optional<std::uint32_t> uid_validity{};
        bool read_only{false};
    };

    struct StatusResult {
        std::string mailbox_name{};
        std::vector<StatusAttribute> attributes{};
    };

    struct Append {
        std::optional<std::uint32_t> uid_validity{};
        std::optional<std::uint32_t> assigned_uid{};
    };

    struct IdleEvent {
        ServerResponse response{};
    };

    struct Idle {
        std::vector<IdleEvent> events{};
    };

    struct Search {
        std::vector<std::uint32_t> matches{};
        std::optional<std::uint32_t> min{};
        std::optional<std::uint32_t> max{};
        std::optional<std::uint32_t> count{};
    };

    struct FetchedMessage {
        std::uint32_t sequence_number{};
        std::vector<MessageDataItem> items{};

        [[nodiscard]] const MessageDataItem *findItem(std::string_view name) const noexcept {
            return findMessageDataItem(items, name);
        }
    };

    struct Fetch {
        std::vector<FetchedMessage> messages{};
    };

    struct Store {
        std::vector<FetchedMessage> updated_messages{};
    };

    struct Copy {
        std::optional<std::uint32_t> uid_validity{};
        std::optional<SequenceSet> source_uids{};
        std::optional<SequenceSet> destination_uids{};
    };

    struct Move {
        std::optional<std::uint32_t> uid_validity{};
        std::optional<SequenceSet> source_uids{};
        std::optional<SequenceSet> destination_uids{};
    };

}// namespace usub::unet::mail::imap::response

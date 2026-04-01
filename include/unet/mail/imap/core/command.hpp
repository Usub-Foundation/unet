#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "unet/mail/imap/core/data_types.hpp"

namespace usub::unet::mail::imap {

    namespace command {
        template<typename CommandData>
        struct Command {
            std::string tag{};
            CommandData data{};
        };

        template<typename ContinuationData = std::string>
        struct Continuation {
            ContinuationData data{};
        };

        struct Capability {};
        struct Noop {};
        struct Logout {};
        struct StartTls {};
        struct Namespace {};
        struct Idle {};
        struct Check {};
        struct Close {};
        struct Unselect {};
        struct Expunge {};

        struct Authenticate {
            std::string sasl_auth_name{};
            std::optional<std::string> initial_response{};
        };

        struct Login {
            std::string username{};
            std::string password{};
        };

        struct Enable {
            std::vector<std::string> capability_names{};
        };

        struct Select {
            std::string mailbox_name{};
        };

        struct Examine {
            std::string mailbox_name{};
        };

        struct Create {
            std::string mailbox_name{};
        };

        struct Delete {
            std::string mailbox_name{};
        };

        struct Subscribe {
            std::string mailbox_name{};
        };

        struct Unsubscribe {
            std::string mailbox_name{};
        };

        struct Rename {
            std::string existing_mailbox_name{};
            std::string new_mailbox_name{};
        };

        struct ListBasic {
            std::string reference_name{};
            std::string mailbox_name{};
        };

        struct ListExtended {
            std::optional<std::vector<std::string>> selection_options{};
            std::string reference_name{};
            std::vector<std::string> mailbox_patterns{};
            std::optional<std::vector<std::string>> return_options{};
        };

        using List = std::variant<ListBasic, ListExtended>;

        struct Status {
            std::string mailbox_name{};
            std::vector<std::string> status_data_item_names{};
        };

        struct Append {
            std::string mailbox_name{};
            std::optional<std::vector<std::string>> flag_list{};
            std::optional<std::string> date_time{};
            std::string message{};
        };

        struct SearchResultSpecifier {
            std::vector<std::string> return_options{};
        };

        struct SearchKey {
            struct All {};
            struct Answered {};
            struct Deleted {};
            struct Draft {};
            struct Flagged {};
            struct New {};
            struct Old {};
            struct Recent {};
            struct Seen {};
            struct Unanswered {};
            struct Undeleted {};
            struct Undraft {};
            struct Unflagged {};
            struct Unseen {};

            struct Bcc {
                std::string value{};
            };

            struct Before {
                std::string value{};
            };

            struct Body {
                std::string value{};
            };

            struct Cc {
                std::string value{};
            };

            struct From {
                std::string value{};
            };

            struct Keyword {
                std::string value{};
            };

            struct Larger {
                std::uint32_t value{};
            };

            struct On {
                std::string value{};
            };

            struct SentBefore {
                std::string value{};
            };

            struct SentOn {
                std::string value{};
            };

            struct SentSince {
                std::string value{};
            };

            struct Since {
                std::string value{};
            };

            struct Smaller {
                std::uint32_t value{};
            };

            struct Subject {
                std::string value{};
            };

            struct Text {
                std::string value{};
            };

            struct To {
                std::string value{};
            };

            struct Uid {
                SequenceSet value{};
            };

            struct Header {
                std::string field_name{};
                std::string value{};
            };

            struct Not {
                std::shared_ptr<SearchKey> key{};
            };

            struct Or {
                std::shared_ptr<SearchKey> left{};
                std::shared_ptr<SearchKey> right{};
            };

            struct Group {
                std::vector<std::shared_ptr<SearchKey>> keys{};
            };

            using Data = std::variant<All, Answered, Deleted, Draft, Flagged, New, Old, Recent, Seen, Unanswered,
                                      Undeleted, Undraft, Unflagged, Unseen, Bcc, Before, Body, Cc, From, Keyword,
                                      Larger, On, SentBefore, SentOn, SentSince, Since, Smaller, Subject, Text, To, Uid,
                                      Header, Not, Or, Group>;

            Data data{};
        };

        struct Search {
            std::optional<SearchResultSpecifier> result_specifier{};
            std::optional<std::string> charset{};
            std::vector<SearchKey> search_criteria{};
        };

        struct Fetch {
            SequenceSet sequence_set{};
            std::vector<std::string> data_item_names{};
        };

        struct Store {
            enum class MODE : std::uint8_t {
                REPLACE,
                ADD,
                REMOVE,
            };

            SequenceSet sequence_set{};
            MODE mode{MODE::REPLACE};
            bool silent{false};
            std::vector<std::string> flag_list{};
        };

        struct Copy {
            SequenceSet sequence_set{};
            std::string mailbox_name{};
        };

        struct Move {
            SequenceSet sequence_set{};
            std::string mailbox_name{};
        };

        using CapabilityRequest = Command<Capability>;
        using NoopRequest = Command<Noop>;
        using LogoutRequest = Command<Logout>;
        using StartTlsRequest = Command<StartTls>;
        using AuthenticateRequest = Command<Authenticate>;
        using LoginRequest = Command<Login>;
        using EnableRequest = Command<Enable>;
        using SelectRequest = Command<Select>;
        using ExamineRequest = Command<Examine>;
        using CreateRequest = Command<Create>;
        using DeleteRequest = Command<Delete>;
        using RenameRequest = Command<Rename>;
        using SubscribeRequest = Command<Subscribe>;
        using UnsubscribeRequest = Command<Unsubscribe>;
        using ListRequest = Command<List>;
        using NamespaceRequest = Command<Namespace>;
        using StatusRequest = Command<Status>;
        using AppendRequest = Command<Append>;
        using IdleRequest = Command<Idle>;
        using CheckRequest = Command<Check>;
        using CloseRequest = Command<Close>;
        using UnselectRequest = Command<Unselect>;
        using ExpungeRequest = Command<Expunge>;
        using SearchRequest = Command<Search>;
        using FetchRequest = Command<Fetch>;
        using StoreRequest = Command<Store>;
        using CopyRequest = Command<Copy>;
        using MoveRequest = Command<Move>;

        using ClientMessage =
                std::variant<CapabilityRequest, NoopRequest, LogoutRequest, StartTlsRequest, AuthenticateRequest,
                             LoginRequest, EnableRequest, SelectRequest, ExamineRequest, CreateRequest, DeleteRequest,
                             RenameRequest, SubscribeRequest, UnsubscribeRequest, ListRequest, NamespaceRequest,
                             StatusRequest, AppendRequest, IdleRequest, CheckRequest, CloseRequest, UnselectRequest,
                             ExpungeRequest, SearchRequest, FetchRequest, StoreRequest, CopyRequest, MoveRequest,
                             Continuation<>>;
    }// namespace command

}// namespace usub::unet::mail::imap

#include <cassert>
#include <iostream>
#include <string>

#include "unet/mail/imap/core/command.hpp"
#include "unet/mail/imap/core/message_data.hpp"
#include "unet/mail/imap/core/response.hpp"
#include "unet/mail/imap/wire/command_serializer.hpp"
#include "unet/mail/imap/wire/response_parser.hpp"

using usub::unet::mail::imap::BodyMultipart;
using usub::unet::mail::imap::BodyTextPart;
using usub::unet::mail::imap::Envelope;
using usub::unet::mail::imap::HeaderFields;
using usub::unet::mail::imap::MessageDataItem;
using usub::unet::mail::imap::MessageDataValue;
using usub::unet::mail::imap::Nil;
using usub::unet::mail::imap::SequenceSet;
using usub::unet::mail::imap::SequenceSetItem;
using usub::unet::mail::imap::SequenceSetValue;
using usub::unet::mail::imap::asciiEqual;
using usub::unet::mail::imap::command::Command;
using usub::unet::mail::imap::command::Fetch;
using usub::unet::mail::imap::command::Login;
using usub::unet::mail::imap::command::Search;
using usub::unet::mail::imap::command::SearchKey;
using usub::unet::mail::imap::command::SearchResultSpecifier;
using usub::unet::mail::imap::command::Store;
using usub::unet::mail::imap::findBodyFieldParameter;
using usub::unet::mail::imap::findMessageDataItem;
using usub::unet::mail::imap::response::CapabilityData;
using usub::unet::mail::imap::response::FetchData;
using usub::unet::mail::imap::response::Ok;
using usub::unet::mail::imap::response::TaggedStatus;
using usub::unet::mail::imap::response::UntaggedServerData;
using usub::unet::mail::imap::response::UntaggedStatus;
using usub::unet::mail::imap::wire::CommandSerializer;
using usub::unet::mail::imap::wire::ResponseParser;

namespace {

    usub::unet::mail::imap::response::ServerResponse requireOne(ResponseParser &parser, std::string_view chunk) {
        auto fed = parser.feed(chunk);
        assert(fed.has_value());

        auto parsed = parser.next();
        assert(parsed.has_value());
        assert(parsed->has_value());
        return **parsed;
    }

    const MessageDataItem &requireItem(const std::vector<MessageDataItem> &items, std::string_view name) {
        const auto *item = findMessageDataItem(items, name);
        assert(item != nullptr);
        return *item;
    }

    void test_command_serializer() {
        auto login = CommandSerializer::serialize(Command<Login>{
                .tag = "A001",
                .data = Login{.username = "alice", .password = "s3cr3t"},
        });
        assert(login.has_value());
        assert(*login == "A001 LOGIN \"alice\" \"s3cr3t\"\r\n");

        Search search_data{};
        search_data.result_specifier = SearchResultSpecifier{.return_options = {"MIN", "COUNT"}};
        search_data.search_criteria = {
                SearchKey{.data = SearchKey::Flagged{}},
                SearchKey{.data = SearchKey::Since{.value = "1-Feb-1994"}},
                SearchKey{.data = SearchKey::Not{.key = std::make_shared<SearchKey>(
                                                         SearchKey{.data = SearchKey::From{.value = "Smith"}})}},
        };
        auto search = CommandSerializer::serialize(Command<Search>{.tag = "A282", .data = std::move(search_data)});
        assert(search.has_value());
        assert(*search == "A282 SEARCH RETURN (MIN COUNT) FLAGGED SINCE 1-Feb-1994 NOT FROM \"Smith\"\r\n");

        Store store_data{};
        store_data.sequence_set.push_back(SequenceSetItem{SequenceSetValue{std::uint32_t{1}}, std::nullopt});
        store_data.mode = Store::MODE::ADD;
        store_data.silent = true;
        store_data.flag_list = {"\\Seen", "\\Flagged"};

        auto store = CommandSerializer::serialize(Command<Store>{.tag = "A003", .data = std::move(store_data)});
        assert(store.has_value());
        assert(*store == "A003 STORE 1 +FLAGS.SILENT (\\Seen \\Flagged)\r\n");
    }

    void test_parse_status_and_capability() {
        ResponseParser parser{};
        auto greeting = requireOne(parser, "* OK [CAPABILITY IMAP4rev1 STARTTLS AUTH=PLAIN] ready\r\n");

        const auto *status = std::get_if<UntaggedStatus>(&greeting);
        assert(status != nullptr);

        const auto *ok = std::get_if<Ok>(&status->data);
        assert(ok != nullptr);
        assert(ok->code.has_value());
        assert(ok->code->name == "CAPABILITY");
        assert(ok->code->data.has_value());
        assert(*ok->code->data == "IMAP4rev1 STARTTLS AUTH=PLAIN");
        assert(ok->text == "ready");

        auto capabilities = requireOne(parser, "* CAPABILITY IMAP4rev1 IDLE UIDPLUS\r\n");
        const auto *untagged = std::get_if<UntaggedServerData>(&capabilities);
        assert(untagged != nullptr);
        const auto *capability = std::get_if<CapabilityData>(&untagged->data);
        assert(capability != nullptr);
        assert(capability->capabilities.size() == 3);
        assert(capability->capabilities[1] == "IDLE");
    }

    void test_parse_header_literal() {
        ResponseParser parser{};
        auto response = requireOne(
                parser,
                "* 1 FETCH (BODY[HEADER] {38}\r\nSubject: Test\r\nFrom: a@example.com\r\n\r\n)\r\n");

        const auto *untagged = std::get_if<UntaggedServerData>(&response);
        assert(untagged != nullptr);
        const auto *fetch = std::get_if<FetchData>(&untagged->data);
        assert(fetch != nullptr);
        assert(fetch->sequence_number == 1);

        const auto &item = requireItem(fetch->items, "BODY[HEADER]");
        const auto *headers = item.value.asHeaders();
        assert(headers != nullptr);
        assert(headers->getFirst("subject").has_value());
        assert(*headers->getFirst("subject") == "Test");
        assert(headers->getFirst("from").has_value());
        assert(*headers->getFirst("from") == "a@example.com");
    }

    void test_parse_bodystructure() {
        ResponseParser parser{};
        auto response = requireOne(
                parser,
                "* 7 FETCH (BODYSTRUCTURE ((\"TEXT\" \"PLAIN\" (\"CHARSET\" \"US-ASCII\") NIL NIL \"7BIT\" 1152 23) "
                "(\"TEXT\" \"PLAIN\" (\"CHARSET\" \"US-ASCII\" \"NAME\" \"cc.diff\") "
                "\"<960723163407.20117h@cac.washington.edu>\" \"Compiler diff\" \"BASE64\" 4554 73) \"MIXED\"))\r\n");

        const auto *untagged = std::get_if<UntaggedServerData>(&response);
        assert(untagged != nullptr);
        const auto *fetch = std::get_if<FetchData>(&untagged->data);
        assert(fetch != nullptr);

        const auto &item = requireItem(fetch->items, "BODYSTRUCTURE");
        const auto *body = item.value.asBodyStructure();
        assert(body != nullptr);

        const auto *multipart = body->asMultipart();
        assert(multipart != nullptr);
        assert(asciiEqual(multipart->media_subtype, "MIXED"));
        assert(multipart->parts.size() == 2);

        const auto *first_text = multipart->parts[0].asText();
        assert(first_text != nullptr);
        assert(asciiEqual(first_text->fields.media_type, "TEXT"));
        assert(asciiEqual(first_text->fields.media_subtype, "PLAIN"));
        assert(first_text->line_count.has_value());
        assert(*first_text->line_count == 23);

        const auto *charset = findBodyFieldParameter(first_text->fields.parameters, "CHARSET");
        assert(charset != nullptr);
        assert(std::holds_alternative<std::string>(*charset));
        assert(std::get<std::string>(*charset) == "US-ASCII");

        const auto *second_text = multipart->parts[1].asText();
        assert(second_text != nullptr);
        const auto *name = findBodyFieldParameter(second_text->fields.parameters, "NAME");
        assert(name != nullptr);
        assert(std::holds_alternative<std::string>(*name));
        assert(std::get<std::string>(*name) == "cc.diff");
        assert(second_text->fields.octet_count.has_value());
        assert(*second_text->fields.octet_count == 4554);
    }

    void test_parse_envelope() {
        ResponseParser parser{};
        auto response = requireOne(
                parser,
                "* 9 FETCH (ENVELOPE (\"Mon, 7 Feb 2022 12:34:56 +0000\" \"Hello\" "
                "((\"Alice\" NIL \"alice\" \"example.com\")) "
                "((\"Alice\" NIL \"alice\" \"example.com\")) "
                "((\"Alice\" NIL \"alice\" \"example.com\")) "
                "((\"Bob\" NIL \"bob\" \"example.com\")) "
                "NIL NIL NIL \"<msg@example.com>\"))\r\n");

        const auto *untagged = std::get_if<UntaggedServerData>(&response);
        assert(untagged != nullptr);
        const auto *fetch = std::get_if<FetchData>(&untagged->data);
        assert(fetch != nullptr);

        const auto &item = requireItem(fetch->items, "ENVELOPE");
        const auto *envelope = item.value.asEnvelope();
        assert(envelope != nullptr);

        assert(std::holds_alternative<std::string>(envelope->date));
        assert(std::get<std::string>(envelope->date) == "Mon, 7 Feb 2022 12:34:56 +0000");
        assert(std::holds_alternative<std::string>(envelope->subject));
        assert(std::get<std::string>(envelope->subject) == "Hello");

        assert(std::holds_alternative<usub::unet::mail::imap::AddressList>(envelope->from));
        const auto &from = std::get<usub::unet::mail::imap::AddressList>(envelope->from);
        assert(from.size() == 1);
        assert(std::holds_alternative<std::string>(from.front().name));
        assert(std::get<std::string>(from.front().name) == "Alice");
        assert(std::holds_alternative<std::string>(from.front().mailbox));
        assert(std::get<std::string>(from.front().mailbox) == "alice");
        assert(std::holds_alternative<usub::unet::mail::imap::AddressList>(envelope->to));
        const auto &to = std::get<usub::unet::mail::imap::AddressList>(envelope->to);
        assert(std::holds_alternative<std::string>(to.front().mailbox));
        assert(std::get<std::string>(to.front().mailbox) == "bob");
        assert(std::holds_alternative<Nil>(envelope->cc));
        assert(std::holds_alternative<Nil>(envelope->bcc));
        assert(std::holds_alternative<std::string>(envelope->message_id));
        assert(std::get<std::string>(envelope->message_id) == "<msg@example.com>");
    }

    void test_tagged_completion() {
        ResponseParser parser{};
        auto response = requireOne(parser, "A044 BAD No such command as \"BLURDYBLOOP\"\r\n");

        const auto *tagged = std::get_if<TaggedStatus>(&response);
        assert(tagged != nullptr);
        assert(tagged->tag == "A044");
        assert(std::holds_alternative<usub::unet::mail::imap::response::Bad>(tagged->data));
    }

}// namespace

int main() {
    test_command_serializer();
    test_parse_status_and_capability();
    test_parse_header_literal();
    test_parse_bodystructure();
    test_parse_envelope();
    test_tagged_completion();

    std::cout << "imap core tests passed\n";
    return 0;
}

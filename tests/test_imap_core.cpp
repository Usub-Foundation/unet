#include <cassert>
#include <iostream>
#include <optional>
#include <string>

#include "unet/mail/imap/client.hpp"
#include "unet/mail/imap/core/capability.hpp"
#include "unet/mail/imap/core/encoder.hpp"
#include "unet/mail/imap/core/parser.hpp"
#include "unet/mail/imap/core/sequence_set.hpp"

using usub::unet::mail::imap::ClientSession;
using usub::unet::mail::imap::core::CommandEncoder;
using usub::unet::mail::imap::core::COMMAND;
using usub::unet::mail::imap::core::Response;
using usub::unet::mail::imap::core::ResponseCondition;
using usub::unet::mail::imap::core::ResponseParser;
using usub::unet::mail::imap::core::SequenceSet;
using usub::unet::mail::imap::core::SessionState;
using usub::unet::mail::imap::core::String;
using usub::unet::mail::imap::core::TaggedResponse;
using usub::unet::mail::imap::core::UntaggedNumericResponse;
using usub::unet::mail::imap::core::UntaggedStatusResponse;
using usub::unet::mail::imap::core::Value;
using usub::unet::mail::imap::core::extractExists;
using usub::unet::mail::imap::core::extractFetchLiteral;
using usub::unet::mail::imap::core::parseCapabilities;

namespace {

    Response require_one(ResponseParser &parser, std::string_view chunk) {
        auto fed = parser.feed(chunk);
        assert(fed.has_value());

        auto parsed = parser.next();
        if (!parsed.has_value()) {
            std::cerr << "parse error: " << parsed.error().message << " @ " << parsed.error().offset << '\n';
        }
        assert(parsed.has_value());
        assert(parsed->has_value());
        return **parsed;
    }

    void test_parser_status_and_capability_code() {
        ResponseParser parser{};

        auto greeting = require_one(parser, "* OK [CAPABILITY IMAP4rev1 STARTTLS AUTH=PLAIN] hi\r\n");
        assert(greeting.kind == Response::Kind::Untagged);

        const auto &untagged = std::get<usub::unet::mail::imap::core::UntaggedResponse>(greeting.data);
        const auto *status = std::get_if<UntaggedStatusResponse>(&untagged.payload);
        assert(status != nullptr);
        assert(status->status.condition == ResponseCondition::OK);
        assert(status->status.code.has_value());

        auto capabilities = parseCapabilities(greeting);
        assert(capabilities.has_value());
        assert(capabilities->has("IMAP4rev1"));
        assert(capabilities->has("STARTTLS"));
        assert(capabilities->has("AUTH=PLAIN"));
    }

    void test_parser_literal_and_tagged() {
        ResponseParser parser{};

        auto first = require_one(parser, "* 1 FETCH (BODY[] {5}\r\nHello)\r\n");
        assert(first.kind == Response::Kind::Untagged);

        const auto &untagged = std::get<usub::unet::mail::imap::core::UntaggedResponse>(first.data);
        const auto *numeric = std::get_if<UntaggedNumericResponse>(&untagged.payload);
        assert(numeric != nullptr);
        assert(numeric->number == 1);
        assert(numeric->atom.value == "FETCH");
        assert(numeric->literal.has_value());
        assert(numeric->literal->value == "Hello");

        auto fed = parser.feed("A0001 OK FETCH done\r\n");
        assert(fed.has_value());

        auto second = parser.next();
        assert(second.has_value());
        assert(second->has_value());
        assert((**second).kind == Response::Kind::Tagged);

        const auto &tagged = std::get<TaggedResponse>((**second).data);
        assert(tagged.tag.value == "A0001");
        assert(tagged.status.condition == ResponseCondition::OK);
    }

    void test_encoder() {
        auto login = CommandEncoder::encodeLogin("A0001", "alice", "s3cr3t");
        assert(login.has_value());
        assert(*login == "A0001 LOGIN \"alice\" \"s3cr3t\"\r\n");

        auto invalid = CommandEncoder::encodeSimple("BAD-TAG", COMMAND::NOOP);
        assert(!invalid.has_value());
    }

    void test_sequence_set() {
        auto parsed = SequenceSet::parse("1:4,6,9:*");
        assert(parsed.has_value());
        assert(parsed->ranges().size() == 3);
        assert(parsed->toString() == "1:4,6,9:*");

        auto invalid = SequenceSet::parse("1,,2");
        assert(!invalid.has_value());
    }

    void test_client_session_state_flow() {
        ClientSession session{};

        auto command_before_greeting = session.buildCommand(COMMAND::CAPABILITY);
        assert(!command_before_greeting.has_value());

        auto fed_greeting = session.feed("* OK ready\r\n");
        assert(fed_greeting.has_value());

        auto greeting = session.nextResponse();
        assert(greeting.has_value());
        assert(greeting->has_value());
        assert(session.state() == SessionState::NotAuthenticated);

        std::vector<Value> login_args{
                Value{.data = String{.form = String::Form::Quoted, .value = "alice"}},
                Value{.data = String{.form = String::Form::Quoted, .value = "pwd"}},
        };

        auto login_cmd = session.buildCommand(COMMAND::LOGIN, login_args);
        assert(login_cmd.has_value());
        assert(login_cmd->rfind("A0001 LOGIN", 0) == 0);

        auto fed_login = session.feed("A0001 OK LOGIN completed\r\n");
        assert(fed_login.has_value());

        auto login_response = session.nextResponse();
        assert(login_response.has_value());
        assert(login_response->has_value());
        assert(session.state() == SessionState::Authenticated);

        auto select_cmd = session.buildCommand(
                COMMAND::SELECT,
                {Value{.data = String{.form = String::Form::Quoted, .value = "INBOX"}}});
        assert(select_cmd.has_value());
        assert(select_cmd->rfind("A0002 SELECT", 0) == 0);

        auto fed_select = session.feed("A0002 OK [READ-WRITE] SELECT completed\r\n");
        assert(fed_select.has_value());
        auto select_response = session.nextResponse();
        assert(select_response.has_value());
        assert(select_response->has_value());
        assert(session.state() == SessionState::Selected);

        auto fed_bye = session.feed("* BYE logging out\r\n");
        assert(fed_bye.has_value());
        auto bye_response = session.nextResponse();
        assert(bye_response.has_value());
        assert(bye_response->has_value());
        assert(session.state() == SessionState::Logout);
    }

    void test_response_extract_helpers() {
        ResponseParser parser{};

        auto exists = require_one(parser, "* 4 EXISTS\r\n");
        auto exists_value = extractExists(exists);
        assert(exists_value.has_value());
        assert(*exists_value == 4);
        assert(!extractFetchLiteral(exists).has_value());

        auto fetch = require_one(parser, "* 1 FETCH (BODY[TEXT] {5}\r\nHello)\r\n");
        auto literal = extractFetchLiteral(fetch);
        assert(literal.has_value());
        assert(*literal == "Hello");
        assert(!extractExists(fetch).has_value());

        auto status = require_one(parser, "* OK ready\r\n");
        assert(!extractExists(status).has_value());
        assert(!extractFetchLiteral(status).has_value());
    }

}// namespace

int main() {
    test_parser_status_and_capability_code();
    test_parser_literal_and_tagged();
    test_encoder();
    test_sequence_set();
    test_client_session_state_flow();
    test_response_extract_helpers();

    std::cout << "imap core tests passed\n";
    return 0;
}

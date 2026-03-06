#include "unet/mail/imap/client.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace usub::unet::mail::imap {
    namespace {

        [[nodiscard]] bool validTagPrefix(std::string_view prefix) noexcept {
            if (prefix.empty()) { return false; }
            for (unsigned char c: prefix) {
                if (std::isalnum(c) == 0) { return false; }
            }
            return true;
        }

    }// namespace

    ClientSession::ClientSession(ClientSessionOptions options)
        : options_(std::move(options)), parser_(options_.parser_limits), tag_counter_(options_.initial_tag_counter) {}

    std::expected<std::string, core::Error>
    ClientSession::buildCommand(core::COMMAND command, std::vector<core::Value> arguments) {
        if (!validTagPrefix(options_.tag_prefix)) {
            return std::unexpected(
                    core::Error{.code = core::ErrorCode::InvalidToken, .message = "invalid client tag prefix"});
        }

        if (!state_machine_.canSend(command)) {
            return std::unexpected(core::Error{.code = core::ErrorCode::InvalidState,
                                               .message = "command is invalid in current IMAP session state"});
        }

        auto tag = nextTag();
        if (!tag) { return std::unexpected(tag.error()); }

        auto encoded = core::CommandEncoder::encode(core::CommandRequest{
                .tag = *tag,
                .command = command,
                .arguments = std::move(arguments),
        });
        if (!encoded) { return std::unexpected(encoded.error()); }

        pending_.push_back(PendingCommand{.tag = *tag, .command = command});
        return encoded;
    }

    std::expected<void, core::Error> ClientSession::feed(std::string_view bytes) { return parser_.feed(bytes); }

    std::expected<std::optional<core::Response>, core::Error> ClientSession::nextResponse() {
        auto parsed = parser_.next();
        if (!parsed) { return std::unexpected(parsed.error()); }
        if (!parsed->has_value()) { return std::optional<core::Response>{}; }

        auto apply = applyResponseToState(**parsed);
        if (!apply) { return std::unexpected(apply.error()); }

        return std::optional<core::Response>{std::move(**parsed)};
    }

    const core::StateMachine &ClientSession::stateMachine() const noexcept { return state_machine_; }

    core::SessionState ClientSession::state() const noexcept { return state_machine_.state(); }

    const std::deque<PendingCommand> &ClientSession::pendingCommands() const noexcept { return pending_; }

    void ClientSession::reset() {
        parser_.reset();
        state_machine_.reset();
        pending_.clear();
        tag_counter_ = options_.initial_tag_counter;
        greeting_seen_ = false;
    }

    std::expected<std::string, core::Error> ClientSession::nextTag() {
        if (tag_counter_ > options_.max_tag_counter) {
            return std::unexpected(core::Error{.code = core::ErrorCode::LimitExceeded,
                                               .message = "imap client tag counter exhausted"});
        }

        std::ostringstream out;
        out << options_.tag_prefix << std::setw(4) << std::setfill('0') << tag_counter_;
        ++tag_counter_;
        return out.str();
    }

    std::expected<void, core::Error> ClientSession::applyResponseToState(const core::Response &response) {
        if (!greeting_seen_) {
            auto greeting = state_machine_.onGreeting(response);
            if (!greeting) { return std::unexpected(greeting.error()); }
            greeting_seen_ = true;
            return {};
        }

        if (response.kind == core::Response::Kind::Continuation) { return {}; }

        if (response.kind == core::Response::Kind::Untagged) {
            const auto &untagged = std::get<core::UntaggedResponse>(response.data);
            return state_machine_.onUntaggedResponse(untagged);
        }

        const auto &tagged = std::get<core::TaggedResponse>(response.data);
        if (pending_.empty()) {
            return std::unexpected(core::Error{.code = core::ErrorCode::InvalidState,
                                               .message = "received tagged response with no pending command"});
        }

        const auto pending = pending_.front();
        if (pending.tag != tagged.tag.value) {
            return std::unexpected(core::Error{.code = core::ErrorCode::InvalidState,
                                               .message = "received tagged response out of order"});
        }

        pending_.pop_front();
        return state_machine_.onTaggedResponse(pending.command, tagged);
    }

}// namespace usub::unet::mail::imap

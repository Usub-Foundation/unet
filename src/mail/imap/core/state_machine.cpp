#include "unet/mail/imap/core/state_machine.hpp"

namespace usub::unet::mail::imap::core {

    SessionState StateMachine::state() const noexcept { return state_; }

    void StateMachine::reset() noexcept { state_ = SessionState::Disconnected; }

    std::expected<void, Error> StateMachine::onGreeting(const Response &response) {
        if (state_ != SessionState::Disconnected) {
            return std::unexpected(
                    Error{.code = ErrorCode::InvalidState, .message = "greeting can be applied only once"});
        }

        if (response.kind != Response::Kind::Untagged) {
            return std::unexpected(
                    Error{.code = ErrorCode::InvalidSyntax, .message = "server greeting must be untagged"});
        }

        const auto &untagged = std::get<UntaggedResponse>(response.data);
        const auto *status = std::get_if<UntaggedStatusResponse>(&untagged.payload);
        if (!status) {
            return std::unexpected(
                    Error{.code = ErrorCode::InvalidSyntax, .message = "server greeting must be status response"});
        }

        switch (status->status.condition) {
            case ResponseCondition::OK:
                state_ = SessionState::NotAuthenticated;
                return {};
            case ResponseCondition::PREAUTH:
                state_ = SessionState::Authenticated;
                return {};
            case ResponseCondition::BYE:
                state_ = SessionState::Logout;
                return {};
            default:
                return std::unexpected(
                        Error{.code = ErrorCode::InvalidState, .message = "invalid greeting status condition"});
        }
    }

    std::expected<void, Error> StateMachine::onTaggedResponse(COMMAND command, const TaggedResponse &response) {
        if (state_ == SessionState::Disconnected) {
            return std::unexpected(
                    Error{.code = ErrorCode::InvalidState, .message = "tagged response before greeting"});
        }

        if (state_ == SessionState::Logout) {
            return std::unexpected(
                    Error{.code = ErrorCode::InvalidState, .message = "tagged response after logout"});
        }

        if (response.status.condition == ResponseCondition::BYE) {
            state_ = SessionState::Logout;
            return {};
        }

        if (response.status.condition != ResponseCondition::OK) {
            return {};
        }

        switch (command) {
            case COMMAND::LOGIN:
            case COMMAND::AUTHENTICATE:
                state_ = SessionState::Authenticated;
                return {};

            case COMMAND::SELECT:
            case COMMAND::EXAMINE:
                state_ = SessionState::Selected;
                return {};

            case COMMAND::UNSELECT:
            case COMMAND::CLOSE:
                state_ = SessionState::Authenticated;
                return {};

            case COMMAND::LOGOUT:
                state_ = SessionState::Logout;
                return {};

            default:
                return {};
        }
    }

    std::expected<void, Error> StateMachine::onUntaggedResponse(const UntaggedResponse &response) {
        if (const auto *status = std::get_if<UntaggedStatusResponse>(&response.payload)) {
            if (status->status.condition == ResponseCondition::BYE) { state_ = SessionState::Logout; }
        }
        return {};
    }

    bool StateMachine::canSend(COMMAND command) const noexcept {
        switch (state_) {
            case SessionState::Disconnected:
                return false;

            case SessionState::NotAuthenticated:
                switch (command) {
                    case COMMAND::CAPABILITY:
                    case COMMAND::NOOP:
                    case COMMAND::LOGOUT:
                    case COMMAND::STARTTLS:
                    case COMMAND::AUTHENTICATE:
                    case COMMAND::LOGIN:
                        return true;
                    default:
                        return false;
                }

            case SessionState::Authenticated:
                switch (command) {
                    case COMMAND::CAPABILITY:
                    case COMMAND::NOOP:
                    case COMMAND::LOGOUT:
                    case COMMAND::ENABLE:
                    case COMMAND::SELECT:
                    case COMMAND::EXAMINE:
                    case COMMAND::CREATE:
                    case COMMAND::DELETE:
                    case COMMAND::RENAME:
                    case COMMAND::SUBSCRIBE:
                    case COMMAND::UNSUBSCRIBE:
                    case COMMAND::LIST:
                    case COMMAND::NAMESPACE:
                    case COMMAND::STATUS:
                    case COMMAND::APPEND:
                        return true;
                    default:
                        return false;
                }

            case SessionState::Selected:
                switch (command) {
                    case COMMAND::CAPABILITY:
                    case COMMAND::NOOP:
                    case COMMAND::LOGOUT:
                    case COMMAND::ENABLE:
                    case COMMAND::CLOSE:
                    case COMMAND::UNSELECT:
                    case COMMAND::EXPUNGE:
                    case COMMAND::SEARCH:
                    case COMMAND::FETCH:
                    case COMMAND::STORE:
                    case COMMAND::COPY:
                    case COMMAND::MOVE:
                    case COMMAND::UID:
                    case COMMAND::IDLE:
                        return true;
                    default:
                        return false;
                }

            case SessionState::Logout:
                return false;
        }

        return false;
    }

}// namespace usub::unet::mail::imap::core

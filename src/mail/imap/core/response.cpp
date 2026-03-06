#include "unet/mail/imap/core/response.hpp"

namespace usub::unet::mail::imap::core {

    bool isOk(const StatusInfo &status) noexcept { return status.condition == ResponseCondition::OK; }

}// namespace usub::unet::mail::imap::core

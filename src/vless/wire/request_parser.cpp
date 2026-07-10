#include "unet/vless/wire/request_parser.hpp"

#include "unet/vless/core/message.hpp"

namespace usub::unet::vless {
    std::expected<void, ParseError> RequestParser::step(Request &request, std::string_view::const_iterator &begin,
                                                        const std::string_view::const_iterator end) {
        auto &context = this->context_;
        auto &bytes_read = context.bytes_read;
        auto &state = context.state;

        while (begin != end) {
            switch (state) {
                case STATE::PROTOCOL_VERSION: {
                    request.protocol_version = static_cast<std::uint8_t>(*begin);
                    begin++;
                    state = STATE::UUID;
                    [[fallthrough]];
                }
                case STATE::UUID: {
                    while (bytes_read < 16 && begin != end) {
                        request.uuid[bytes_read] = static_cast<std::byte>(*begin);
                        begin++;
                        bytes_read++;
                    }
                    if (bytes_read == 16) {
                        bytes_read = 0;
                        state = STATE::ADDONS_LENGTH;
                    }
                    break;
                }
                case STATE::ADDONS_LENGTH: {
                    request.addons_length = static_cast<std::uint8_t>(*begin);
                    begin++;
                    state = STATE::ADDONS;
                    [[fallthrough]];
                }
                case STATE::ADDONS: {
                    while (bytes_read < request.addons_length && begin != end) {
                        request.addons.push_back(static_cast<std::byte>(*begin));
                        begin++;
                        bytes_read++;
                    }
                    if (bytes_read == request.addons_length) {
                        bytes_read = 0;
                        state = STATE::COMMAND;
                    }
                    break;
                }
                case STATE::COMMAND: {
                    request.command = static_cast<std::uint8_t>(*begin);
                    begin++;
                    state = STATE::DESTINATION_PORT;
                    [[fallthrough]];
                }
                case STATE::DESTINATION_PORT: {
                    if (bytes_read == 0 && begin != end) {
                        request.destination_port = request.destination_port | static_cast<std::uint8_t>(*begin);
                        request.destination_port = request.destination_port << 8;
                        begin++;
                        bytes_read++;
                    }
                    if (bytes_read == 1 && begin != end) {
                        request.destination_port = request.destination_port | static_cast<std::uint8_t>(*begin);
                        bytes_read = 0;
                        begin++;
                        state = STATE::ADDRESS_TYPE;
                    }
                    [[fallthrough]];
                }
                case STATE::ADDRESS_TYPE: {
                    if (begin == end) { break; }
                    request.address_type = static_cast<std::uint8_t>(*begin);
                    begin++;
                    switch (request.address_type) {
                        case static_cast<std::uint8_t>(ADDRESS_TYPE::IPV4): {
                            request.address.emplace<std::array<std::byte, 4>>();
                            state = STATE::ADDRESS_IP4;
                            break;
                        }
                        case static_cast<std::uint8_t>(ADDRESS_TYPE::DOMAIN_ADDR): {
                            request.address.emplace<Domain>();
                            state = STATE::ADDRESS_DOMAIN_LENGTH;
                            break;
                        }
                        case static_cast<std::uint8_t>(ADDRESS_TYPE::IPV6): {
                            request.address.emplace<std::array<std::byte, 16>>();
                            state = STATE::ADDRESS_IP6;
                            break;
                        }
                        default: {
                            state = STATE::ADDRESS_OTHER;
                            break;
                        }
                    }
                    break;
                }
                case STATE::ADDRESS_IP4: {
                    auto &address = std::get<std::array<std::byte, 4>>(request.address);
                    while (bytes_read < 4 && begin != end) {
                        address[bytes_read] = static_cast<std::byte>(*begin);
                        begin++;
                        bytes_read++;
                    }
                    if (bytes_read == 4) {
                        bytes_read = 0;
                        state = STATE::COMPLETE;
                        return {};
                    }
                    break;
                }
                case STATE::ADDRESS_DOMAIN_LENGTH: {
                    auto &address = std::get<Domain>(request.address);
                    address.length = static_cast<std::uint8_t>(*begin);
                    begin++;
                    if (address.length == 0) {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{.code = ParseError::CODE::INVALID_DOMAIN_LENGTH,
                                                          .message = "domain length must be non-zero"});
                    }
                    address.value.reserve(address.length);
                    state = STATE::ADDRESS_DOMAIN;
                    [[fallthrough]];
                }
                case STATE::ADDRESS_DOMAIN: {
                    auto &address = std::get<Domain>(request.address);
                    while (bytes_read < address.length && begin != end) {
                        address.value.push_back(static_cast<char>(*begin));
                        begin++;
                        bytes_read++;
                    }
                    if (bytes_read == address.length) {
                        bytes_read = 0;
                        state = STATE::COMPLETE;
                        return {};
                    }
                    break;
                }
                case STATE::ADDRESS_IP6: {
                    auto &address = std::get<std::array<std::byte, 16>>(request.address);
                    while (bytes_read < 16 && begin != end) {
                        address[bytes_read] = static_cast<std::byte>(*begin);
                        begin++;
                        bytes_read++;
                    }
                    if (bytes_read == 16) {
                        bytes_read = 0;
                        state = STATE::COMPLETE;
                        return {};
                    }
                    break;
                }
                case STATE::ADDRESS_OTHER: {
                    state = STATE::FAILED;
                    return std::unexpected(ParseError{.code = ParseError::CODE::UNKNOWN_ADDRESS_TYPE,
                                                      .message = "unknown address type"});
                }
                case STATE::COMPLETE: {
                    return {};
                }
                case STATE::FAILED: {
                    return std::unexpected(ParseError{.code = ParseError::CODE::FAILED_STATE,
                                                      .message = "step() called on parser in FAILED state"});
                }
            }
        }
        return {};
    }
}// namespace usub::unet::vless
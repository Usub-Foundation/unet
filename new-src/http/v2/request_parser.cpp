#include "unet/http/v2/request_parser.hpp"

namespace usub::unet::http::v2 {
    std::expected<Request, ParseError> RequestParser::parse(const std::string_view raw_request) {}

    std::expected<void, ParseError> RequestParser::parse(Request &request, std::string_view::const_iterator &begin,
                                                         const std::string_view::const_iterator end) {}

    RequestParser::ParserContext &RequestParser::getContext() { return this->context_; }
};// namespace usub::unet::http::v2
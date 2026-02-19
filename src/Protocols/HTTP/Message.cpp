#include "Protocols/HTTP/Message.h"
#include "Protocols/HTTP/EndpointHandler.h"


usub::server::protocols::http::Headers &usub::server::protocols::http::Message::getHeaders() noexcept {
    return this->headers_;
}

const usub::server::protocols::http::Headers &usub::server::protocols::http::Message::getHeaders() const noexcept {
    return this->headers_;
}

// std::vector<unsigned char> &usub::server::protocols::http::Message::getData() {
//     return this->data_;
// }

// const std::vector<unsigned char> &usub::server::protocols::http::Message::getData() const {
//     return this->data_;
// }

enum usub::server::protocols::http::VERSION &usub::server::protocols::http::Message::getHTTPVersion() {
    return this->http_version_;
}

const enum usub::server::protocols::http::VERSION &usub::server::protocols::http::Message::getHTTPVersion() const {
    return this->http_version_;
}

void usub::server::protocols::http::Message::setHTTPVersion(const enum usub::server::protocols::http::VERSION &HTTPVersion) {
    this->http_version_ = HTTPVersion;
}

std::string usub::server::protocols::http::Request::getFullURL() {
    return this->urn_.string();
}

std::string &usub::server::protocols::http::Request::getURL() {
    return this->urn_.getPath();
}

const std::string &usub::server::protocols::http::Request::getURL() const {
    return this->urn_.getPath();
    ;// this->urn_.getPath();
}

// std::pair<std::string, std::string> &usub::server::protocols::http::Request::getServerName() {
//     return this->server_name_;
// }

// const std::pair<std::string, std::string> &usub::server::protocols::http::Request::getServerName() const {
//     return this->server_name_;
// }

usub::server::component::url::QueryParams &usub::server::protocols::http::Request::getQueryParams() {
    return this->urn_.getQueryParams();
}

const usub::server::component::url::QueryParams &usub::server::protocols::http::Request::getQueryParams() const {
    return this->urn_.getQueryParams();
}

std::string &usub::server::protocols::http::Request::getRequestMethod() {
    return this->method_token_;
}

const std::string &usub::server::protocols::http::Request::getRequestMethod() const {
    return this->method_token_;
}

usub::server::protocols::http::REQUEST_STATE &usub::server::protocols::http::Request::getState() {
    return this->state_;
}

const usub::server::protocols::http::REQUEST_STATE &usub::server::protocols::http::Request::getState() const {
    return this->state_;
}

void usub::server::protocols::http::Request::setState(const REQUEST_STATE &state) {
    this->state_ = state;
}

std::string usub::server::protocols::http::Request::getBody() {
    return this->body_;
}
std::string usub::server::protocols::http::Response::getBodyHex() const {
    return getBodyHex(this->body_.size());
}

std::string usub::server::protocols::http::Response::getBodyHex(size_t max_bytes) const {
    static constexpr char H[] = "0123456789ABCDEF";

    const size_t n = std::min(max_bytes, this->body_.size());
    std::string out;
    out.reserve(n * 2);

    const unsigned char *p = reinterpret_cast<const unsigned char *>(this->body_.data());
    for (size_t i = 0; i < n; ++i) {
        unsigned char b = p[i];
        out.push_back(H[b >> 4]);
        out.push_back(H[b & 0x0F]);
    }
    return out;
}

std::string::const_iterator usub::server::protocols::http::Request::parseHTTP1_0(const std::string &request, std::string::const_iterator start_pos) {
    std::string::const_iterator rv = this->parseHTTP1_1(request, start_pos);
    this->http_version_ = VERSION::HTTP_1_0;
    return rv;
}

std::string::const_iterator usub::server::protocols::http::Request::parseHTTP1_1(const std::string &request, std::string::const_iterator start_pos) {
    std::string::const_iterator rv = this->parseHTTP1_X(request, start_pos);
    this->http_version_ = VERSION::HTTP_1_1;
    return rv;
}

std::string::const_iterator
usub::server::protocols::http::Request::parseHTTP1_X(const std::string &request,
                                                     std::string::const_iterator start_pos) {
    if (request.empty()) {
        this->state_ = REQUEST_STATE::BAD_REQUEST;
        return {};
    }
    if (this->state_ == REQUEST_STATE::BAD_REQUEST) return {};
    else if (this->state_ == REQUEST_STATE::FINISHED)
        this->clear();

    auto c = (start_pos == std::string::const_iterator()) ? request.begin() : start_pos;


    // Local vars have better perfomance, so we cache the most used, TODO!
    std::pair<std::string, std::string> &data_value_pair_cached = this->data_value_pair_;
    REQUEST_STATE &state_cached = this->state_;
    size_t &line_size_cached = this->line_size_;

    for (c; c != request.end();) {
        if (this->line_size_ >= 8192 && this->state_ < REQUEST_STATE::DATA_CONTENT_LENGTH) {
            if (this->state_ == REQUEST_STATE::METHOD || this->state_ == REQUEST_STATE::PATH || this->state_ == REQUEST_STATE::VERSION) {
                this->state_ = REQUEST_STATE::URI_TOO_LONG;
                return c;
            } else if (this->state_ == REQUEST_STATE::HEADERS_KEY || this->state_ == REQUEST_STATE::HEADERS_VALUE) {
                this->state_ = REQUEST_STATE::REQUEST_HEADER_FIELDS_TOO_LARGE;
                return c;
            }
            this->state_ = REQUEST_STATE::BAD_REQUEST;
            return c;
        } else if (this->line_size_ > this->max_data_size_) {
            this->state_ = REQUEST_STATE::BAD_REQUEST;
            return c;
        }
        switch (this->state_) {
            case REQUEST_STATE::METHOD:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::METHOD; ++c, this->line_size_++) {
                    if (usub::utils::isTchar(*c)) [[likely]] {
                        this->method_token_ += *c;// Append character
                    } else if (*c == ' ') {
                        if (this->method_token_.empty()) {
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            break;
                        }
                        this->state_ = REQUEST_STATE::TARGET_START;
                    } else [[unlikely]] {
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        return c;
                    }
                }
            case REQUEST_STATE::TARGET_START:
                if (*c == '/') [[likely]] {
                    this->state_ = REQUEST_STATE::ORIGIN_FORM;
                } else if (*c == '*') [[unlikely]] {
                    // not ready yet
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    return c;
                    this->urn_.getPath().push_back('*');
                    this->state_ = REQUEST_STATE::ASTERISK_FORM;
                    c++;
                } else if (isalpha(*c)) {// Check for scheme (e.g., "http://")
                                         // not ready yet
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    return c;
                    this->state_ = REQUEST_STATE::ABSOLUTE_FORM_SCHEME;
                } else [[unlikely]] {
                    // not ready yet
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    return c;
                    this->state_ = REQUEST_STATE::AUTHORITY_FORM;
                }
                break;
            case REQUEST_STATE::ORIGIN_FORM: {
                std::string &url = this->urn_.getPath();
                for (c; c != request.end() && this->state_ == REQUEST_STATE::ORIGIN_FORM && this->line_size_ <= this->max_uri_size_; ++c, this->line_size_++) {
                    if (component::URN::isPathChar(*c)) [[likely]] {
                        url.push_back(*c);
                    } else if (*c == '?') [[likely]] {
                        this->state_ = REQUEST_STATE::QUERY_KEY;
                    } else if (*c == ' ') [[likely]] {
                        this->state_ = REQUEST_STATE::VERSION;
                    } else [[unlikely]] {
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        return c;
                    }
                }
                break;
            }
            case REQUEST_STATE::ASTERISK_FORM:
                if (*c == ' ') [[likely]] {
                    this->state_ = REQUEST_STATE::VERSION;
                } else [[unlikely]] {
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    return c;
                }
                c++;
                break;
            case REQUEST_STATE::QUERY_KEY: {
                std::string &query_params_string = this->urn_.getQueryParams().string();
                for (c; c != request.end() && this->state_ == REQUEST_STATE::QUERY_KEY; ++c, ++this->line_size_) {
                    if (component::URN::isQueryChar(*c)) [[likely]] {
                        query_params_string.push_back(*c);
                        if (*c != '=') [[likely]] {
                            this->data_value_pair_.first.push_back(*c);
                        } else if (*c == '&') [[unlikely]] {
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            return c;
                        } else {
                            this->state_ = REQUEST_STATE::QUERY_VALUE;
                        }
                    } else [[unlikely]] {
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        return c;
                    }
                }
                break;
            }
            case REQUEST_STATE::QUERY_VALUE: {
                usub::server::component::url::QueryParams &query_params = this->urn_.getQueryParams();
                std::string &query_params_string = query_params.string();
                for (c; c != request.end() && this->state_ == REQUEST_STATE::QUERY_VALUE; ++c, this->line_size_++) {
                    if (component::URN::isQueryChar(*c)) [[likely]] {
                        query_params_string.push_back(*c);
                        if (*c != '&') [[likely]] {
                            this->data_value_pair_.second.push_back(*c);
                        } else {
                            query_params.addQueryParam(std::move(this->data_value_pair_.first), std::move(this->data_value_pair_.second));
                            this->data_value_pair_.first.clear();
                            this->data_value_pair_.second.clear();
                            this->state_ = REQUEST_STATE::QUERY_KEY;
                        }
                    } else if (*c == ' ') [[likely]] {
                        query_params.addQueryParam(std::move(this->data_value_pair_.first), std::move(this->data_value_pair_.second));
                        this->data_value_pair_.first.clear();
                        this->data_value_pair_.second.clear();
                        this->state_ = REQUEST_STATE::VERSION;
                    } else [[unlikely]] {
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        return c;
                    }
                }
                break;
            }
            // this is unused, since it's never transfered to server still usefull to have
            case REQUEST_STATE::FRAGMENT: {
                std::string &fragment = this->urn_.getFragment();
                switch (*c) {
                    case '\r':
                    case '\n':
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        return c;// Incorrect character in fragment
                    case ' ':
                        this->state_ = REQUEST_STATE::VERSION;
                        break;
                    default:
                        fragment.push_back(*c);
                        break;
                }
                break;
            }
            case REQUEST_STATE::VERSION:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::VERSION; ++c, ++this->line_size_) {
                    switch (*c) {
                        case ' ':
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            return c;
                        case '\r':
                            if (!carriage_return) [[likely]] {
                                carriage_return = true;
                                break;
                            }
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            return c;
                        case '\n':
                            if (!carriage_return) {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                return c;
                            }
                            // TODO: May compare using int comparison instead of string comparison
                            if (this->data_value_pair_.first == "HTTP/1.1") [[likely]] {
                                this->http_version_ = VERSION::HTTP_1_1;
                            } else if (this->data_value_pair_.first == "HTTP/1.0") [[likely]] {
                                this->http_version_ = VERSION::HTTP_1_0;
                            } else [[unlikely]] {
                                this->http_version_ = VERSION::BROKEN;
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                return c;
                            }
                            this->data_value_pair_.first.clear();
                            this->line_size_ = 0;
                            this->state_ = REQUEST_STATE::PRE_HEADERS;
                            return c;
                            break;
                        default:
                            if (this->line_size_ > this->max_uri_size_) [[unlikely]] {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                break;
                            }

                            this->data_value_pair_.first.push_back(*c);
                            break;
                    }
                }
                break;
            case REQUEST_STATE::PRE_HEADERS:
                this->carriage_return = false;
                c++;
                this->state_ = REQUEST_STATE::HEADERS_KEY;
            case REQUEST_STATE::HEADERS_KEY:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::HEADERS_KEY && this->line_size_ <= this->max_headers_size_; ++c, ++this->line_size_) {
                    switch (*c) {
                        case ' ':
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            return c;
                        case '\r':
                            if (!carriage_return) [[likely]] {
                                this->carriage_return = true;
                                break;
                            }
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            return c;
                        case '\n':
                            if (carriage_return) [[likely]] {
                                this->state_ = REQUEST_STATE::HEADERS_PARSED;
                                return c;
                            }
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            return c;
                        case ':':
                            if (!this->data_value_pair_.first.empty()) [[likely]] {
                                this->state_ = REQUEST_STATE::HEADERS_VALUE;
                            } else [[unlikely]] {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                return c;
                            }
                            break;
                        default:
                            if (this->carriage_return) [[unlikely]] {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                return c;
                            }
                            if (usub::utils::isTchar(*c)) [[likely]] {
                                this->data_value_pair_.first.push_back(std::tolower(*c));
                            } else [[unlikely]] {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                return c;
                            }
                            break;
                    }
                }
                break;
            case REQUEST_STATE::HEADERS_VALUE:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::HEADERS_VALUE && this->line_size_ <= this->max_headers_size_; ++c, ++this->line_size_) {
                    switch (*c) {
                        case '\r':
                            if (!carriage_return) [[likely]] {
                                this->carriage_return = true;
                                break;
                            }
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            return c;
                        case '\n':
                            if (this->carriage_return && !this->data_value_pair_.second.empty()) [[likely]] {
                                usub::utils::trim(this->data_value_pair_.second);
                                auto result = this->headers_.addHeader<Request>(std::move(this->data_value_pair_.first), std::move(data_value_pair_.second));
                                if (!result) [[unlikely]] {
                                    const usub::server::utils::error::ParseError &err = result.error();
                                    if (err.severity == usub::server::utils::error::ErrorSeverity::Critical) {
                                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                                        return c;
                                    }
                                }
                                this->data_value_pair_.first.clear();
                                this->data_value_pair_.second.clear();
                                this->carriage_return = false;
                                this->state_ = REQUEST_STATE::HEADERS_KEY;
                                break;
                            }
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            return c;
                        case ' ':
                            this->data_value_pair_.second.push_back(*c);
                            break;
                        default:
                            if (usub::utils::isVcharOrObsText(*c)) {
                                this->data_value_pair_.second.push_back(*c);
                            } else {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                return c;
                            }
                            break;
                    }
                }
                break;
            case REQUEST_STATE::HEADERS_PARSED: {
                this->line_size_ = 0;
                auto headers = this->headers_;
                auto content_length = headers[usub::server::component::HeaderEnum::Content_Length].size() == 1 ? std::stoi(headers[usub::server::component::HeaderEnum::Content_Length][0]) : -1;
                if (content_length > 0) [[likely]] {
                    this->helper_.size_ = content_length;
                    this->state_ = REQUEST_STATE::DATA_CONTENT_LENGTH;
                } else if (content_length == 0) [[unlikely]] {
                    this->helper_.size_ = 0;
                    this->state_ = REQUEST_STATE::FINISHED;
                } else if (!headers[usub::server::component::HeaderEnum::Transfer_Encoding].empty()) [[likely]] {
                    if (headers[usub::server::component::HeaderEnum::Transfer_Encoding].back() == "chunked") {
                        this->state_ = REQUEST_STATE::DATA_CHUNKED_SIZE;
                    } else {
                        this->state_ = REQUEST_STATE::UNSUPPORTED_MEDIA_TYPE;
                        return c;
                    }
                } else if (this->method_token_ == "GET" || this->method_token_ == "HEAD") [[likely]] {
                    this->state_ = REQUEST_STATE::FINISHED;
                } else {
                    this->state_ = REQUEST_STATE::LENGTH_REQUIRED;
                    return c;
                }
                // size_t amount_of_encodings = 0;
                // auto& transfer_encoding = headers[usub::server::component::HeaderEnum::Transfer_Encoding]
                // if (!headers.Transfer_Encoding.empty()) {
                //     if (headers.Transfer_Encoding.size() != 1) {
                //         amount_of_encodings += headers.Transfer_Encoding.size();
                //         if (amount_of_encodings > 2) {
                //             this->state_ = REQUEST_STATE::BAD_REQUEST;
                //             return c;
                //         }
                //         DecoderChainFactory::instance().create(headers.Transfer_Encoding, this->encryptors_chain);
                //     }
                // }
                // if (!headers.Content_Encoding.empty()) {
                //     amount_of_encodings += headers.Content_Encoding.size();
                //     if (amount_of_encodings > 2 + 2) {
                //         this->state_ = REQUEST_STATE::BAD_REQUEST;
                //         return c;
                //     }
                //     DecoderChainFactory::instance().create(headers.Content_Encoding, this->encryptors_chain);
                // }
                carriage_return = false;
                c++;
                break;
            }
            case REQUEST_STATE::DATA_CONTENT_LENGTH:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::DATA_CONTENT_LENGTH && this->line_size_ <= this->max_data_size_; ++c, ++line_size_) {

                    this->body_.push_back(*c);
                    if (this->body_.size() == this->helper_.size_) {
                        for (auto decompressor: this->encryptors_chain) {
                            decompressor->decompress(this->body_);
                            if (decompressor->getState() != 2) {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                return c;
                            } else {
                                this->state_ = REQUEST_STATE::FINISHED;
                                return c;
                            }
                        }
                        this->state_ = REQUEST_STATE::FINISHED;
                        return c;
                    } else if (this->body_.size() > this->helper_.size_) {
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        return c;
                    }
                }
                if (this->line_size_ > this->max_data_size_) {
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    return c;
                }
                break;
            case REQUEST_STATE::DATA_FRAGMENT:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::DATA_FRAGMENT && this->line_size_ <= this->max_data_size_; ++c, ++line_size_) {
                    if (*c == '\r') {
                        if (this->carriage_return) {
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            return c;
                        }
                        this->carriage_return = true;
                    }
                    if (*c == '\n') {
                        if (!this->carriage_return) {
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            return c;
                        }
                        this->carriage_return = false;
                        this->body_ = std::move(this->data_value_pair_.first);
                        this->data_value_pair_.first.clear();
                        this->state_ = REQUEST_STATE::DATA_CHUNKED_SIZE;
                    }
                }
                if (this->line_size_ > this->max_data_size_) {
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    return c;
                }

            case REQUEST_STATE::DATA_CHUNKED_SIZE:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::DATA_CHUNKED_SIZE && this->line_size_ <= this->max_data_size_; ++c, ++line_size_) {
                    switch (*c) {
                        case '\r':
                            if (carriage_return) {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                return c;
                            }
                            carriage_return = true;
                            break;
                        case '\n':
                            if (!carriage_return) {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                return c;
                            }
                            if (this->newline && !this->body_.empty() && this->carriage_return && this->helper_.size_ == 0) {
                                this->state_ = REQUEST_STATE::FINISHED;
                            }
                            this->helper_.size_ = std::stoull(this->data_value_pair_.first, nullptr, 16);
                            if ((this->line_size_ + this->helper_.size_) > this->max_data_size_) {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                return c;
                            }
                            if (this->helper_.size_ != 0) {
                                this->data_value_pair_.first.clear();
                                this->newline = false;
                                this->carriage_return = false;
                                this->state_ = REQUEST_STATE::DATA_CHUNKED;
                            } else {
                                this->newline = true;
                                this->carriage_return = false;
                            }
                            break;
                        default:
                            if (std::isxdigit(*c)) {
                                this->data_value_pair_.first.push_back(*c);
                            } else {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                return c;
                            }
                            break;
                    }
                }
                if (this->line_size_ > this->max_data_size_) {
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    return c;
                }

                break;
            case REQUEST_STATE::DATA_CHUNKED:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::DATA_CHUNKED && this->line_size_ <= this->max_data_size_; ++c, ++line_size_) {
                    this->data_value_pair_.first.push_back(*c);
                    if (this->data_value_pair_.first.size() == this->helper_.size_) {
                        for (auto decompressor: this->encryptors_chain) {
                            decompressor->decompress(this->data_value_pair_.first);
                            if (decompressor->getState() == 2) {
                                this->state_ = REQUEST_STATE::DATA_FRAGMENT;
                                return c;
                            } else {
                                this->state_ = REQUEST_STATE::DATA_CHUNKED_SIZE;
                                return c;
                            }
                        }
                        this->state_ = REQUEST_STATE::DATA_FRAGMENT;
                        c++;
                        return c;
                    }
                }
                if (this->line_size_ > this->max_data_size_) {
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    c = request.end();
                    return c;
                }
                break;
            default:
                c = request.end();
                break;
        }
    }
    return c;
}

void usub::server::protocols::http::Request::clear() {
    this->carriage_return = false;
    this->newline = false;
    this->state_ = REQUEST_STATE::METHOD;
    this->method_token_.clear();
    this->body_.clear();
    this->headers_.clear();
    this->urn_ = usub::server::component::URN();
    // this->server_name_ = {};
    this->line_size_ = 0;
    this->state_ = REQUEST_STATE::METHOD;
    this->http_version_ = VERSION::NONE;
    this->data_value_pair_ = {};
}

bool usub::server::protocols::http::Response::isSent() {
    return (this->state_ == RESPONSE_STATE::SENT);
}

// usub::server::protocols::http::SEND_STATE usub::server::protocols::http::Response::getState() {
//     return this->send_state_;
// }

usub::server::protocols::http::Response::~Response() {
    if (this->fd_ != -1) {
        close(this->fd_);
        this->fd_ = -1;
    }
}

usub::server::protocols::http::Response &usub::server::protocols::http::Response::setSocket(usub::uvent::net::TCPClientSocket *socket) {
    this->socket_ = socket;
    return *this;
}

usub::server::protocols::http::Response &usub::server::protocols::http::Response::setRoute(usub::server::protocols::http::Route *route) {
    this->matched_route_ = route;
    return *this;
}

usub::server::protocols::http::Response &usub::server::protocols::http::Response::setSent() {
    this->state_ = RESPONSE_STATE::SENT;
    return *this;
}

usub::server::protocols::http::Response &usub::server::protocols::http::Response::addHeader(const std::string &key, const std::string &value) {
    std::string kc = key;
    std::string vc = value;
    this->headers_.addHeader<Response>(std::move(kc), std::move(vc));
    return *this;
}

usub::server::protocols::http::Response &usub::server::protocols::http::Response::setStatus(uint16_t status_code) {
    this->status_code_ = std::to_string(status_code);
    this->status_message_ = Response::code_status_map_.contains(this->status_code_) ? Response::code_status_map_.at(this->status_code_) : "Unknown Message";
    return *this;
}

usub::server::protocols::http::Response &usub::server::protocols::http::Response::setStatus(std::string_view status_code) {
    if (status_code.find_first_not_of("0123456789") != std::string::npos)
        return *this;
    this->status_code_ = status_code;
    this->status_message_ = Response::code_status_map_.at(this->status_code_);
    return *this;
}

usub::server::protocols::http::Response &usub::server::protocols::http::Response::setMessage(std::string_view message) {
    this->status_message_ = message;
    return *this;
}

usub::server::protocols::http::Response &usub::server::protocols::http::Response::setBody(const std::string &data, const std::string &content_type) {
    this->body_ = data;
    this->helper_.offset_ = 0;
    this->helper_.size_ = data.size();
    this->headers_.addHeader<Response>("Content-Length", std::to_string(data.size()));
    std::string content_type_c = content_type;
    if (!content_type.empty())
        this->headers_.addHeader<Response>("Content-Type", std::move(content_type_c));
    if (this->fd_ != -1) {
        close(fd_);
        this->fd_ = -1;
    }
    return *this;
}

uint16_t usub::server::protocols::http::Response::getStatus() const {
    try {
        return static_cast<uint16_t>(std::stoul(this->status_code_));
    } catch (...) {
        return 0;
    }
}

const std::string &usub::server::protocols::http::Response::getBody() const {
    return this->body_;
}


#ifdef _WIN32// TODO: Temporary solution
#include <fcntl.h>
#include <io.h>

// Provide POSIX-style aliases so the rest of your code remains unchanged
#define open _open
#define close _close
#define read _read
#define O_RDONLY _O_RDONLY
#endif


usub::server::protocols::http::Response &usub::server::protocols::http::Response::setFile(const std::string &filename, const std::string &content_type) {
    this->helper_.offset_ = 0;
    this->fd_ = open(filename.c_str(), O_RDONLY);
    if (this->fd_ == -1) {
        this->status_code_ = "404";
        this->status_message_ = "Not Found";
        this->headers_.addHeader<Response>(std::string("Content-Length"), std::string("0"));
        return *this;
    }
    this->helper_.buffer_ = false;
    off_t currentPos = lseek(this->fd_, 0, SEEK_CUR);
    off_t size = lseek(this->fd_, 0, SEEK_END);
    lseek(this->fd_, currentPos, SEEK_SET);
    this->helper_.size_ = size;
    if (this->helper_.chunked_) {
        this->headers_.addHeader<Response>(std::string("Transfer-Encoding"), std::string("chunked"));
    } else {
        this->headers_.addHeader<Response>(std::string("Content-Length"), std::to_string(size));
    }
    this->body_ = "";
    return *this;
}

usub::server::protocols::http::Response &usub::server::protocols::http::Response::setChunked() {
    this->helper_.chunked_ = true;
    this->headers_.erase("Content-Length");
    // this->headers_.addHeader<Response>(std::string("Transfer-Encoding"), std::string("chunked"));
    return *this;
}

usub::server::protocols::http::Response &usub::server::protocols::http::Response::setContentLength() {
    this->helper_.chunked_ = false;
    auto transfer_encoding = this->headers_["Transfer-Encoding"];
    transfer_encoding.erase(std::find(transfer_encoding.begin(), transfer_encoding.end(), "chunked"));
    this->headers_.erase("Transfer-Encoding");
    for (auto &encoding: transfer_encoding) {
        this->headers_.addHeader<Response>(std::string("Transfer-Encoding"), std::move(encoding));
    }
    this->headers_.addHeader<Response>(std::string("Content-Length"), std::to_string(this->helper_.size_));
    return *this;
}

const std::unordered_map<std::string, std::string> usub::server::protocols::http::Response::code_status_map_{
        {"100", "Continue"},
        {"101", "Switching Protocols"},
        {"102", "Processing"},
        {"103", "Early Hints"},

        {"200", "OK"},
        {"201", "Created"},
        {"202", "Accepted"},
        {"203", "Non-Authoritative Information"},
        {"204", "No Content"},
        {"205", "Reset Content"},
        {"206", "Partial Content"},

        {"300", "Multiple Choices"},
        {"301", "Moved Permanently"},
        {"302", "Found"},
        {"303", "See Other"},
        {"304", "Not Modified"},
        {"307", "Temporary Redirect"},
        {"308", "Permanent Redirect"},

        {"400", "Bad Request"},
        {"401", "Unauthorized"},
        {"402", "Payment Required"},
        {"403", "Forbidden"},
        {"404", "Not Found"},
        {"405", "Method Not Allowed"},
        {"406", "Not Acceptable"},
        {"407", "Proxy Authentication Required"},
        {"408", "Request Timeout"},
        {"409", "Conflict"},
        {"410", "Gone"},
        {"411", "Length Required"},
        {"412", "Precondition Failed"},
        {"413", "Payload Too Large"},
        {"414", "URI Too Long"},
        {"415", "Unsupported Media Type"},
        {"416", "Range Not Satisfiable"},
        {"417", "Expectation Failed"},
        {"418", "I'm a teapot"},
        {"422", "Unprocessable Entity"},
        {"426", "Upgrade Required"},
        {"428", "Precondition Required"},
        {"429", "Too Many Requests"},
        {"431", "Request Header Fields Too Large"},
        {"451", "Unavailable For Legal Reasons"},

        {"500", "Internal Server Error"},
        {"501", "Not Implemented"},
        {"502", "Bad Gateway"},
        {"503", "Service Unavailable"},
        {"504", "Gateway Timeout"},
        {"505", "HTTP Version Not Supported"},
        {"511", "Network Authentication Required"}};

std::string usub::server::protocols::http::Response::pull() {
    std::string res;
    if (this->helper_.add_metadata_) {

        res.reserve(100 + this->headers_.size());

        switch (this->http_version_) {
            case VERSION::HTTP_1_0:
                res.append("HTTP/1.0 ");
                break;
            case VERSION::HTTP_1_1:
                res.append("HTTP/1.1 ");
                break;
            default:
                std::cerr << "HTTP/0.9 is not supported" << std::endl;
                res.append("HTTP/1.0 ");
                break;
        }

        res.append(this->status_code_);
        res.append(" ");
        res.append(this->status_message_);
        res.append("\r\n");
        res.append(this->headers_.string());
        res.append("\r\n");
        this->helper_.add_metadata_ = false;
    }


    if (this->helper_.buffer_) {
        if (this->helper_.chunked_) {
            this->helper_.offset_ += this->body_.size();
            res.append(std::to_string(this->body_.size()));
            res.append("\r\n");
            res.append(this->body_);
            res.append("\r\n");
            res.append("0\r\n\r\n");
        } else {
            this->helper_.offset_ += this->body_.size();
            res.append(this->body_);
        }
    } else {
        size_t current_write = 0;
        if (this->helper_.chunked_) {
            const size_t bufferSize = this->helper_.chunk_size_;
            std::vector<char> buffer(bufferSize);
            ssize_t bytesRead;
            while ((bytesRead = read(this->fd_, buffer.data(), buffer.size())) > 0) {
                this->helper_.offset_ += bytesRead;
                current_write += bytesRead;
                char hexSize[17];
                std::sprintf(hexSize, "%zx", bytesRead);
                res.append(hexSize);
                res.append("\r\n");
                res.append(buffer.data(), bytesRead);
                res.append("\r\n");
                if (current_write >= this->helper_.max_write_size_) {
                    break;
                }
            }
            if (this->helper_.size_ == this->helper_.offset_) {
                res.append("0\r\n\r\n");
            }
        } else {
            const size_t bufferSize = 4096;// Use a buffer size of 4 KB
            std::vector<char> buffer(bufferSize);

            ssize_t bytesRead;
            while ((bytesRead = read(this->fd_, buffer.data(), buffer.size())) > 0) {
                this->helper_.offset_ += bytesRead;
                current_write += bytesRead;
                res.append(buffer.data(), bytesRead);
                if (current_write >= this->helper_.max_write_size_) {
                    break;
                }
            }
        }
    }
    if (this->helper_.size_ == this->helper_.offset_) {
        this->state_ = RESPONSE_STATE::SENT;
    }

    return res;
}

std::string usub::server::protocols::http::Response::string() const {
    std::string res;


    res.reserve(100 + this->headers_.size());

    switch (this->http_version_) {
        case VERSION::HTTP_1_0:
            res.append("HTTP/1.0 ");
            break;
        case VERSION::HTTP_1_1:
            res.append("HTTP/1.1 ");
            break;
        default:
            std::cerr << "HTTP/0.9 is not supported" << std::endl;
            res.append("HTTP/1.0 ");
            break;
    }

    res.append(this->status_code_);
    res.append(" ");
    res.append(this->status_message_);
    res.append("\r\n");
    res.append(this->headers_.string());
    res.append("\r\n");
    // this->helper_.add_metadata_ = false;


    if (this->helper_.buffer_) {
        if (this->helper_.chunked_) {
            this->helper_.offset_ += this->body_.size();
            res.append(std::to_string(this->body_.size()));
            res.append("\r\n");
            res.append(this->body_);
            res.append("\r\n");
            res.append("0\r\n\r\n");
        } else {
            this->helper_.offset_ += this->body_.size();
            res.append(this->body_);
        }
    } else {
        if (this->helper_.chunked_) {
            const size_t bufferSize = this->helper_.chunk_size_;
            std::vector<char> buffer(bufferSize);

            ssize_t bytesRead;
            while ((bytesRead = read(this->fd_, buffer.data(), buffer.size())) > 0) {
                this->helper_.offset_ += bytesRead;
                char hexSize[17];
                std::sprintf(hexSize, "%zx", bytesRead);
                res.append(hexSize);
                res.append("\r\n");
                res.append(buffer.data(), bytesRead);
                res.append("\r\n");
            }
            if (this->helper_.size_ == this->helper_.offset_) {
                res.append("0\r\n\r\n");
            }
        } else {
            const size_t bufferSize = 4096;// Use a buffer size of 4 KB
            std::vector<char> buffer(bufferSize);

            ssize_t bytesRead;
            while ((bytesRead = read(this->fd_, buffer.data(), buffer.size())) > 0) {
                this->helper_.offset_ += bytesRead;
                res.append(buffer.data(), bytesRead);
            }
        }
    }

    return res;
}

size_t usub::server::protocols::http::Response::size() const {
    return size_t(8192);
}

void usub::server::protocols::http::Response::setState(const RESPONSE_STATE &state) {
    this->state_ = state;
}

void usub::server::protocols::http::Response::clear() {
    this->status_code_ = "500";
    this->status_message_ = "Internal Server Error";
    this->state_ = RESPONSE_STATE::SENDING;
    this->headers_.clear();
    this->body_.clear();
    this->data_value_pair_ = {};
    this->helper_ = {};
    if (this->fd_ != -1) {
        close(fd_);
        this->fd_ = -1;
    }

    //    this->http_version_ = NONE;
}

//usub::uvent::task::Awaitable<void> usub::server::protocols::http::Response::send_coro() {
//        ssize_t wrsize = co_await *this->socket_->async_write(reinterpret_cast<uint8_t *>(const_cast<char *>(str.data())), str.size());
//        if (wrsize <= 0) {
//            this->send_state_ = FAILED;
//        }
//        co_return;
//}
//
//
//usub::uvent::task::Awaitable<void> usub::server::protocols::http::Response::send() {
//    std::string str = this->string(); // Make a local copy of the string
//
//    // Lambda to handle async_write logic
//    usub::uvent::system::co_spawn(this->send_coro(),usub::uvent::task::DETACHED);
//
//    co_return;
//}


usub::uvent::task::Awaitable<bool> usub::server::protocols::http::Request::parseHTTP1_X_yield(const std::string &request) {
    if (request.empty()) {
        this->state_ = REQUEST_STATE::BAD_REQUEST;
        co_return true;
    }

    auto c = request.begin();


    // Local vars have better perfomance, so we cache the most used, TODO!
    std::pair<std::string, std::string> &data_value_pair_cached = this->data_value_pair_;
    REQUEST_STATE &state_cached = this->state_;
    size_t &line_size_cached = this->line_size_;

    for (c; c != request.end();) {
        if (this->line_size_ >= 8192 && this->state_ < REQUEST_STATE::DATA_CONTENT_LENGTH) {
            if (this->state_ == REQUEST_STATE::METHOD || this->state_ == REQUEST_STATE::PATH || this->state_ == REQUEST_STATE::VERSION) {
                this->state_ = REQUEST_STATE::URI_TOO_LONG;
                co_return true;
            } else if (this->state_ == REQUEST_STATE::HEADERS_KEY || this->state_ == REQUEST_STATE::HEADERS_VALUE) {
                this->state_ = REQUEST_STATE::REQUEST_HEADER_FIELDS_TOO_LARGE;
                co_return true;
            }
            this->state_ = REQUEST_STATE::BAD_REQUEST;
            co_return true;
        } else if (this->line_size_ > this->max_data_size_) {
            this->state_ = REQUEST_STATE::BAD_REQUEST;
            co_return true;
        }
        switch (this->state_) {
            case REQUEST_STATE::METHOD:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::METHOD; ++c, this->line_size_++) {
                    if (usub::utils::isTchar(*c)) [[likely]] {
                        this->method_token_ += *c;// Append character
                    } else if (*c == ' ') {
                        if (this->method_token_.empty()) {
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            break;
                        }
                        this->state_ = REQUEST_STATE::TARGET_START;
                    } else [[unlikely]] {
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        co_return true;
                    }
                }
            case REQUEST_STATE::TARGET_START:
                if (*c == '/') [[likely]] {
                    this->state_ = REQUEST_STATE::ORIGIN_FORM;
                } else if (*c == '*') [[unlikely]] {
                    // not ready yet
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    co_return true;
                    this->urn_.getPath().push_back('*');
                    this->state_ = REQUEST_STATE::ASTERISK_FORM;
                    c++;
                } else if (isalpha(*c)) {// Check for scheme (e.g., "http://")
                                         // not ready yet
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    co_return true;
                    this->state_ = REQUEST_STATE::ABSOLUTE_FORM_SCHEME;
                } else [[unlikely]] {
                    // not ready yet
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    co_return true;
                    this->state_ = REQUEST_STATE::AUTHORITY_FORM;
                }
                break;
            case REQUEST_STATE::ORIGIN_FORM: {
                std::string &url = this->urn_.getPath();
                for (c; c != request.end() && this->state_ == REQUEST_STATE::ORIGIN_FORM && this->line_size_ <= this->max_uri_size_; ++c, this->line_size_++) {
                    if (component::URN::isPathChar(*c)) [[likely]] {
                        url.push_back(*c);
                    } else if (*c == '?') [[likely]] {
                        this->state_ = REQUEST_STATE::QUERY_KEY;
                    } else if (*c == ' ') [[likely]] {
                        this->state_ = REQUEST_STATE::VERSION;
                    } else [[unlikely]] {
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        co_return true;
                    }
                }
                break;
            }
            case REQUEST_STATE::ASTERISK_FORM:
                if (*c == ' ') [[likely]] {
                    this->state_ = REQUEST_STATE::VERSION;
                } else [[unlikely]] {
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    co_return true;
                }
                c++;
                break;
            case REQUEST_STATE::QUERY_KEY: {
                std::string &query_params_string = this->urn_.getQueryParams().string();
                for (c; c != request.end() && this->state_ == REQUEST_STATE::QUERY_KEY; ++c, ++this->line_size_) {
                    if (component::URN::isQueryChar(*c)) [[likely]] {
                        query_params_string.push_back(*c);
                        if (*c != '=') [[likely]] {
                            this->data_value_pair_.first.push_back(*c);
                        } else if (*c == '&') [[unlikely]] {
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            co_return true;
                        } else {
                            this->state_ = REQUEST_STATE::QUERY_VALUE;
                        }
                    } else [[unlikely]] {
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        co_return true;
                    }
                }
                break;
            }
            case REQUEST_STATE::QUERY_VALUE: {
                usub::server::component::url::QueryParams &query_params = this->urn_.getQueryParams();
                std::string &query_params_string = query_params.string();
                for (c; c != request.end() && this->state_ == REQUEST_STATE::QUERY_VALUE; ++c, this->line_size_++) {
                    if (component::URN::isQueryChar(*c)) [[likely]] {
                        query_params_string.push_back(*c);
                        if (*c != '&') [[likely]] {
                            this->data_value_pair_.second.push_back(*c);
                        } else {
                            query_params.addQueryParam(std::move(this->data_value_pair_.first), std::move(this->data_value_pair_.second));
                            this->data_value_pair_.first.clear();
                            this->data_value_pair_.second.clear();
                            this->state_ = REQUEST_STATE::QUERY_KEY;
                        }
                    } else if (*c == ' ') [[likely]] {
                        query_params.addQueryParam(std::move(this->data_value_pair_.first), std::move(this->data_value_pair_.second));
                        this->data_value_pair_.first.clear();
                        this->data_value_pair_.second.clear();
                        this->state_ = REQUEST_STATE::VERSION;
                    } else [[unlikely]] {
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        co_return true;
                    }
                }
                break;
            }
            // this is unused, since it's never transfered to server still usefull to have
            case REQUEST_STATE::FRAGMENT: {
                std::string &fragment = this->urn_.getFragment();
                switch (*c) {
                    case '\r':
                    case '\n':
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        co_return true;// Incorrect character in fragment
                    case ' ':
                        this->state_ = REQUEST_STATE::VERSION;
                        break;
                    default:
                        fragment.push_back(*c);
                        break;
                }
                break;
            }
            case REQUEST_STATE::VERSION:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::VERSION; ++c, ++this->line_size_) {
                    switch (*c) {
                        case ' ':
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            co_return true;
                        case '\r':
                            if (!carriage_return) [[likely]] {
                                carriage_return = true;
                                break;
                            }
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            co_return true;
                        case '\n':
                            if (!carriage_return) {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                co_return true;
                            }
                            // TODO: May compare using int comparison instead of string comparison
                            if (this->data_value_pair_.first == "HTTP/1.1") [[likely]] {
                                this->http_version_ = VERSION::HTTP_1_1;
                            } else if (this->data_value_pair_.first == "HTTP/1.0") [[likely]] {
                                this->http_version_ = VERSION::HTTP_1_0;
                            } else [[unlikely]] {
                                this->http_version_ = VERSION::BROKEN;
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                co_return true;
                            }
                            this->data_value_pair_.first.clear();
                            this->line_size_ = 0;
                            this->state_ = REQUEST_STATE::PRE_HEADERS;
                            co_yield false;
                            break;
                        default:
                            if (this->line_size_ > this->max_uri_size_) [[unlikely]] {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                break;
                            }

                            this->data_value_pair_.first.push_back(*c);
                            break;
                    }
                }
                break;
            case REQUEST_STATE::PRE_HEADERS:
                this->carriage_return = false;
                c++;
                this->state_ = REQUEST_STATE::HEADERS_KEY;
            case REQUEST_STATE::HEADERS_KEY:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::HEADERS_KEY && this->line_size_ <= this->max_headers_size_;) {
                    switch (*c) {
                        case ' ':
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            co_return true;
                        case '\r':
                            if (!carriage_return) [[likely]] {
                                this->carriage_return = true;
                                c++;
                                this->line_size_++;
                                break;
                            }
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            co_return true;
                        case '\n':
                            if (carriage_return) [[likely]] {
                                this->state_ = REQUEST_STATE::HEADERS_PARSED;
                                co_yield false;

                                break;
                            }
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            co_return true;
                        case ':':
                            if (!this->data_value_pair_.first.empty()) [[likely]] {
                                this->state_ = REQUEST_STATE::HEADERS_VALUE;
                            } else [[unlikely]] {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                co_return true;
                            }
                            c++;
                            this->line_size_++;
                            break;
                        default:
                            if (this->carriage_return) [[unlikely]] {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                co_return true;
                            }
                            if (usub::utils::isTchar(*c)) [[likely]] {
                                this->data_value_pair_.first.push_back(std::tolower(*c));
                            } else [[unlikely]] {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                co_return true;
                            }
                            c++;
                            this->line_size_++;

                            break;
                    }
                }
                break;
            case REQUEST_STATE::HEADERS_VALUE:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::HEADERS_VALUE && this->line_size_ <= this->max_headers_size_; ++c, ++this->line_size_) {
                    switch (*c) {
                        case '\r':
                            if (!carriage_return) [[likely]] {
                                this->carriage_return = true;
                                break;
                            }
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            co_return true;
                        case '\n':
                            if (this->carriage_return && !this->data_value_pair_.second.empty()) [[likely]] {
                                usub::utils::trim(this->data_value_pair_.second);
                                auto result = this->headers_.addHeader<Request>(std::move(this->data_value_pair_.first), std::move(data_value_pair_.second));
                                if (!result) [[unlikely]] {
                                    const usub::server::utils::error::ParseError &err = result.error();
                                    if (err.severity == usub::server::utils::error::ErrorSeverity::Critical) {
                                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                                        co_return true;
                                    }
                                }
                                this->data_value_pair_.first.clear();
                                this->data_value_pair_.second.clear();
                                this->carriage_return = false;
                                this->state_ = REQUEST_STATE::HEADERS_KEY;
                                break;
                            }
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            co_return true;
                        case ' ':
                            this->data_value_pair_.second.push_back(*c);
                            break;
                        default:
                            if (usub::utils::isVcharOrObsText(*c)) {
                                this->data_value_pair_.second.push_back(*c);
                            } else {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                co_return true;
                            }
                            break;
                    }
                }
                break;
            case REQUEST_STATE::HEADERS_PARSED: {
                this->line_size_ = 0;
                auto headers = this->headers_;
                auto content_length = headers[usub::server::component::HeaderEnum::Content_Length].size() == 1 ? std::stoi(headers[usub::server::component::HeaderEnum::Content_Length][0]) : -1;
                if (content_length > 0) [[likely]] {
                    this->helper_.size_ = content_length;
                    this->state_ = REQUEST_STATE::DATA_CONTENT_LENGTH;
                } else if (content_length == 0) [[unlikely]] {
                    this->helper_.size_ = 0;
                    this->state_ = REQUEST_STATE::FINISHED;
                } else if (!headers[usub::server::component::HeaderEnum::Transfer_Encoding].empty()) [[likely]] {
                    if (headers[usub::server::component::HeaderEnum::Transfer_Encoding].back() == "chunked") {
                        this->state_ = REQUEST_STATE::DATA_CHUNKED_SIZE;
                    } else {
                        this->state_ = REQUEST_STATE::UNSUPPORTED_MEDIA_TYPE;
                        co_return true;
                    }
                } else if (this->method_token_ == "GET" || this->method_token_ == "HEAD") [[likely]] {
                    this->state_ = REQUEST_STATE::FINISHED;
                } else {
                    this->state_ = REQUEST_STATE::LENGTH_REQUIRED;
                    co_return true;
                }
                // size_t amount_of_encodings = 0;
                // auto& transfer_encoding = headers[usub::server::component::HeaderEnum::Transfer_Encoding]
                // if (!headers.Transfer_Encoding.empty()) {
                //     if (headers.Transfer_Encoding.size() != 1) {
                //         amount_of_encodings += headers.Transfer_Encoding.size();
                //         if (amount_of_encodings > 2) {
                //             this->state_ = REQUEST_STATE::BAD_REQUEST;
                //             return c;
                //         }
                //         DecoderChainFactory::instance().create(headers.Transfer_Encoding, this->encryptors_chain);
                //     }
                // }
                // if (!headers.Content_Encoding.empty()) {
                //     amount_of_encodings += headers.Content_Encoding.size();
                //     if (amount_of_encodings > 2 + 2) {
                //         this->state_ = REQUEST_STATE::BAD_REQUEST;
                //         return c;
                //     }
                //     DecoderChainFactory::instance().create(headers.Content_Encoding, this->encryptors_chain);
                // }
                carriage_return = false;
                c++;
                break;
            }
            case REQUEST_STATE::DATA_CONTENT_LENGTH:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::DATA_CONTENT_LENGTH && this->line_size_ <= this->max_data_size_; ++c, ++line_size_) {

                    this->body_.push_back(*c);
                    if (this->body_.size() == this->helper_.size_) {
                        for (auto decompressor: this->encryptors_chain) {
                            decompressor->decompress(this->body_);
                            if (decompressor->getState() != 2) {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                co_return true;
                            } else {
                                this->state_ = REQUEST_STATE::FINISHED;
                                co_return true;
                            }
                        }
                        this->state_ = REQUEST_STATE::FINISHED;
                        co_return true;
                    } else if (this->body_.size() > this->helper_.size_) {
                        this->state_ = REQUEST_STATE::BAD_REQUEST;
                        co_return true;
                    }
                }
                if (this->line_size_ > this->max_data_size_) {
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    co_return true;
                }
                break;
            case REQUEST_STATE::DATA_FRAGMENT:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::DATA_FRAGMENT && this->line_size_ <= this->max_data_size_; ++c, ++line_size_) {
                    if (*c == '\r') {
                        if (this->carriage_return) {
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            co_return true;
                        }
                        this->carriage_return = true;
                    }
                    if (*c == '\n') {
                        if (!this->carriage_return) {
                            this->state_ = REQUEST_STATE::BAD_REQUEST;
                            co_return true;
                        }
                        this->carriage_return = false;
                        this->body_ = std::move(this->data_value_pair_.first);
                        this->data_value_pair_.first.clear();
                        this->state_ = REQUEST_STATE::DATA_CHUNKED_SIZE;
                    }
                }
                if (this->line_size_ > this->max_data_size_) {
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    co_return true;
                }

            case REQUEST_STATE::DATA_CHUNKED_SIZE:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::DATA_CHUNKED_SIZE && this->line_size_ <= this->max_data_size_; ++c, ++line_size_) {
                    switch (*c) {
                        case '\r':
                            if (carriage_return) {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                co_return true;
                            }
                            carriage_return = true;
                            break;
                        case '\n':
                            if (!carriage_return) {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                co_return true;
                            }
                            if (this->newline && !this->body_.empty() && this->carriage_return && this->helper_.size_ == 0) {
                                this->state_ = REQUEST_STATE::FINISHED;
                            }
                            this->helper_.size_ = std::stoull(this->data_value_pair_.first, nullptr, 16);
                            if ((this->line_size_ + this->helper_.size_) > this->max_data_size_) {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                co_return true;
                            }
                            if (this->helper_.size_ != 0) {
                                this->data_value_pair_.first.clear();
                                this->newline = false;
                                this->carriage_return = false;
                                this->state_ = REQUEST_STATE::DATA_CHUNKED;
                            } else {
                                this->newline = true;
                                this->carriage_return = false;
                            }
                            break;
                        default:
                            if (std::isxdigit(*c)) {
                                this->data_value_pair_.first.push_back(*c);
                            } else {
                                this->state_ = REQUEST_STATE::BAD_REQUEST;
                                co_return true;
                            }
                            break;
                    }
                }
                if (this->line_size_ > this->max_data_size_) {
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    co_return true;
                }

                break;
            case REQUEST_STATE::DATA_CHUNKED:
                for (c; c != request.end() && this->state_ == REQUEST_STATE::DATA_CHUNKED && this->line_size_ <= this->max_data_size_; ++c, ++line_size_) {
                    this->data_value_pair_.first.push_back(*c);
                    if (this->data_value_pair_.first.size() == this->helper_.size_) {
                        for (auto decompressor: this->encryptors_chain) {
                            decompressor->decompress(this->data_value_pair_.first);
                            if (decompressor->getState() == 2) {
                                this->state_ = REQUEST_STATE::DATA_FRAGMENT;
                                co_yield false;
                            } else {
                                this->state_ = REQUEST_STATE::DATA_CHUNKED_SIZE;
                                co_yield false;
                            }
                        }
                        this->state_ = REQUEST_STATE::DATA_FRAGMENT;
                        c++;
                        co_return true;
                    }
                }
                if (this->line_size_ > this->max_data_size_) {
                    this->state_ = REQUEST_STATE::BAD_REQUEST;
                    c = request.end();
                    co_return true;
                }
                break;
            default:
                c = request.end();
                break;
        }
    }
    co_return true;
}

void usub::server::protocols::http::Request::setUri(const std::string &uri) {// TODO: this is just not correct, redo
    this->urn_.getPath() = uri;
}

usub::server::protocols::http::Request &usub::server::protocols::http::Request::setBody(const std::string &data, const std::string &content_type) {
    this->body_ = data;
    this->helper_.offset_ = 0;
    this->helper_.size_ = data.size();
    this->headers_.addHeader<usub::server::protocols::http::Request>(std::string("Content-Length"), std::to_string(data.size()));
    if (!content_type.empty()) {
        this->headers_.addHeader<usub::server::protocols::http::Request>(std::string("Content-Type"), std::string(content_type));
    }
    return *this;
}


std::string usub::server::protocols::http::Request::string() {
    // Build request line
    const std::string &method = this->method_token_.empty() ? static_cast<const std::string &>("GET") : this->method_token_;
    std::string target;

    // Prefer full URL if available, fallback to path, default to "/"
    try {
        target = this->getFullURL();
    } catch (...) {
        // If getFullURL throws or isn't available, fallback to getURL()
        target = this->getURL();
    }
    if (target.empty()) {
        target = "/";
    }

    // Version to string
    const char *version_str = "HTTP/1.1";
    switch (this->http_version_) {
        case VERSION::HTTP_1_0:
            version_str = "HTTP/1.0";
            break;
        case VERSION::HTTP_1_1:
        case VERSION::HTTP_1_X:
        case VERSION::NONE:
            version_str = "HTTP/1.1";
            break;
        default:
            version_str = "HTTP/1.1";
            break;
    }

    std::string out;
    out.reserve(method.size() + target.size() + this->body_.size() + 128);

    out.append(method).push_back(' ');
    out.append(target).push_back(' ');
    out.append(version_str).append("\r\n");

    // Headers
    bool has_host = false;
    bool has_content_length = false;

    // Write existing headers and detect Host/Content-Length
    for (const auto &kv: this->headers_) {// assumes Headers provides STL-like iteration
        const auto &key = kv.first;
        const auto &values = kv.second;

        if (key == "host") has_host = true;
        if (key == "content-length") has_content_length = true;

        for (const auto &v: values) {
            out.append(key).append(": ").append(v).append("\r\n");
        }
    }

    // Add Host header for HTTP/1.1 if missing and we have authority
    if (!has_host && (this->http_version_ == VERSION::HTTP_1_1 || this->http_version_ == VERSION::HTTP_1_X || this->http_version_ == VERSION::NONE)) {
        if (!this->authority_.empty()) {
            out.append("host: ").append(this->authority_).append("\r\n");
        }
    }

    // Add Content-Length if missing and we have a body
    if (!has_content_length && !this->body_.empty()) {
        out.append("content-length: ").append(std::to_string(this->body_.size())).append("\r\n");
    }

    // End of headers
    out.append("\r\n");

    // Body
    out.append(this->body_);

    return out;
}

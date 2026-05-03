#include <cassert>
#include <iostream>
#include <string>

#include "unet/http/core/request.hpp"

int main() {
    usub::unet::http::Request request;
    request.headers.addHeader("Cookie", "session=abc");
    request.headers.addHeader("Set-Cookie", "a=1");
    request.headers.addHeader("Set-Cookie", "b=2");
    request.headers.addHeader("Content-Type", "multipart/form-data");

    auto &headers = request.getHeaders();

    const auto &cookie_values = headers.at("Cookie");
    assert(!cookie_values.empty());
    assert(cookie_values[0] == "session=abc");

    const auto &lower_cookie_values = headers.at("cookie");
    assert(!lower_cookie_values.empty());
    assert(lower_cookie_values[0] == "session=abc");

    const auto &set_cookie_values = headers.at("set-cookie");
    assert(set_cookie_values.size() == 2);
    assert(set_cookie_values[0] == "a=1");
    assert(set_cookie_values[1] == "b=2");

    auto content_type = headers.find("Content-Type");
    assert(content_type != headers.end());
    assert(content_type->first == "content-type");
    assert(content_type->second[0] == "multipart/form-data");

    auto missing = headers.find("Authorization");
    assert(missing == headers.end());

    const auto &const_headers = request.getHeaders();
    auto const_content_type = const_headers.find("content-type");
    assert(const_content_type != const_headers.end());
    assert(const_content_type->second[0] == "multipart/form-data");

    std::size_t list_count = 0;
    for (const auto &header: headers) {
        assert(!header.key.empty());
        ++list_count;
    }
    assert(list_count == 4);

    std::cout << "All header compatibility tests passed.\n";
    return 0;
}

#include <cassert>
#include <iostream>
#include <string>
#include <string_view>

#include "unet/http/core/request.hpp"

int main() {
    using namespace std::string_view_literals;

    usub::unet::http::Request request;
    request.headers.addHeader("Cookie"sv, "session=abc"sv);
    request.headers.addHeader("Set-Cookie"sv, "a=1"sv);
    request.headers.addHeader("Set-Cookie"sv, "b=2"sv);
    request.headers.addHeader("Content-Type"sv, "multipart/form-data"sv);

    auto headers = request.getHeaders();

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

    auto direct_find = request.getHeaders().find("Content-Type");
    assert(direct_find != request.getHeaders().end());
    assert(direct_find->second[0] == "multipart/form-data");

    const auto const_headers = request.getHeaders();
    auto const_content_type = const_headers.find("content-type");
    assert(const_content_type != const_headers.end());
    assert(const_content_type->second[0] == "multipart/form-data");

    std::size_t compat_count = 0;
    for (const auto &[name, values]: headers) {
        assert(!name.empty());
        assert(!values.empty());
        ++compat_count;
    }
    assert(compat_count == 3);

    std::size_t list_count = 0;
    for (const auto &header: request.headers) {
        assert(!header.key.empty());
        ++list_count;
    }
    assert(list_count == 4);

    std::cout << "All header compatibility tests passed.\n";
    return 0;
}

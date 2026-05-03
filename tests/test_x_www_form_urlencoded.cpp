#include <cassert>
#include <iostream>
#include <string>

#include "unet/http/core/request.hpp"
#include "unet/mime/application/x_www_form_urlencoded.hpp"

namespace form = usub::unet::mime::application::x_www_form_urlencoded;

int main() {
    auto decoded = form::decode_component("hello+world%21");
    assert(decoded.has_value());
    assert(*decoded == "hello world!");

    assert(form::encode_component("hello world!") == "hello+world%21");

    auto fields = form::parse("name=Jane+Doe&tag=c%2B%2B&tag=http&empty=&flag");
    assert(fields.has_value());
    assert(fields->size() == 5);
    assert((*fields)[0].name == "name");
    assert((*fields)[0].value == "Jane Doe");
    assert((*fields)[1].name == "tag");
    assert((*fields)[1].value == "c++");
    assert((*fields)[3].name == "empty");
    assert((*fields)[3].value.empty());
    assert((*fields)[4].name == "flag");
    assert((*fields)[4].value.empty());

    auto mapped = form::parse_to_map("tag=c%2B%2B&tag=http");
    assert(mapped.has_value());
    assert(mapped->at("tag").size() == 2);
    assert(mapped->at("tag")[0] == "c++");
    assert(mapped->at("tag")[1] == "http");

    assert(form::serialize(*fields) == "name=Jane+Doe&tag=c%2B%2B&tag=http&empty=&flag=");

    auto invalid = form::parse("bad=%XX");
    assert(!invalid.has_value());

    usub::unet::http::Request request;
    request.metadata.uri.query = "account_id=acct_1&tag=c%2B%2B&tag=http";

    auto query_params = request.getQueryParams();
    assert(query_params.at("account_id")[0] == "acct_1");
    assert(query_params.at("tag").size() == 2);
    assert(query_params.at("tag")[0] == "c++");
    assert(query_params.at("tag")[1] == "http");

    request.metadata.uri.query = "account_id=acct_2";
    auto updated_query_params = request.getQueryParams();
    assert(updated_query_params.at("account_id")[0] == "acct_2");
    assert(!updated_query_params.contains("tag"));

    std::cout << "All x-www-form-urlencoded tests passed.\n";
    return 0;
}

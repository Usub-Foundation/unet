#include <cassert>
#include <iostream>
#include <string>


#include "unet/mime/multipart/form_data/generic.hpp"

using namespace usub::unet::mime::multipart;

int main() {
    std::string boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    std::string body;
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"field1\"\r\n";
    body += "\r\n";
    body += "value1\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"field2\"\r\n";
    body += "Content-Type: text/plain\r\n";
    body += "\r\n";
    body += "value2\r\n";
    body += "--" + boundary + "--\r\n";

    try {
        FormData parser(boundary, body);
        auto &parts = parser.parts_by_name();
        assert(parts.count("field1") == 1);
        assert(parts.count("field2") == 1);
        assert(parts.at("field1")[0].data == "value1");
        assert(parts.at("field2")[0].data == "value2");

        std::cout << "All basic tests passed.\n";
    } catch (const std::exception &e) {
        std::cerr << "parse_form_data failed: " << e.what() << std::endl;
        return 2;
    }
    return 0;
}

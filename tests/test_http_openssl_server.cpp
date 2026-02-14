#include <unet/core/streams/openssl.hpp>
#include <unet/http.hpp>

bool metadataMiddle(const usub::unet::http::Request &request, usub::unet::http::Response &response) {
    std::cout << "metadata middleware reached" << std::endl;
    return true;
}

bool globalMetadataMiddle(const usub::unet::http::Request &request, usub::unet::http::Response &response) {
    std::cout << "global metadata middleware reached" << std::endl;
    return true;
}

bool headerMiddle(const usub::unet::http::Request &request, usub::unet::http::Response &response) {
    std::cout << "header middleware reached" << std::endl;
    return true;
}

bool globalHeaderMiddle(const usub::unet::http::Request &request, usub::unet::http::Response &response) {
    std::cout << "global header middleware reached" << std::endl;
    return true;
}

bool bodyMiddle(const usub::unet::http::Request &request, usub::unet::http::Response &response) {
    std::cout << "body middleware reached" << std::endl;
    return true;
}

bool responseMiddle(const usub::unet::http::Request &request, usub::unet::http::Response &response) {
    std::cout << "request middleware reached " << std::endl;
    return true;
}

ServerHandler handlerFunction(usub::unet::http::Request &request, usub::unet::http::Response &response) {

    // auto headers = request.getHeaders();
    // for (const auto &[name, values]: headers) {
    //     std::cout << "Header: " << name << "\n";
    //     for (const auto &val: values) {
    //         std::cout << "  Value: " << val << "\n";
    //     }
    // }
    // std::cout << "Matched :" << request.getURL() << std::endl;
    // for (auto &[k, v]: request.uri_params) {
    //     std::cout << "param[" << k << "] = " << v << '\n';
    // }
    std::cout << "Query Params:\n" << request.metadata.uri.query << "\n";

    response.setStatus(200)
            // .setStatusMessage("NOK")
            .addHeader("Content-Type", "text/html")
            .setBody("Hello World! How are you \n");
    // co_await test1();
    co_return;
}

int main() {
    usub::Uvent uvent{4};
    usub::unet::http::ServerImpl<usub::unet::http::router::Radix, usub::unet::core::stream::OpenSSLStream> server;
    server.addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER, globalHeaderMiddle);
    server.handle("GET", "/path", handlerFunction)
            .addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER, headerMiddle)
            .addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER, headerMiddle);
    ;
    uvent.run();
}
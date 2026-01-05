#include <unet/http.hpp>

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
    std::cout << "Query Params:\n"
              << request.metadata.uri.query << "\n";

    response.setStatus(200)
            .setStatusMessage("OK")
            .addHeader("Content-Type", "text/html")
            .setBody("Hello World! How are you \n");
    // co_await test1();
    co_return;
}

int main() {
    usub::unet::http::ServerRadix server;
    server.handle("GET", "/path", handlerFunction);
    server.run();
}
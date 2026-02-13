// #include "unet/http/v1/egress_session.hpp"

// namespace usub::unet::http::v1 {
//     usub::uvent::task::Awaitable<void> EgressSession::process(std::string_view data, const AsyncCallback &callbacks) {
//         std::string_view::const_iterator begin = data.begin();
//         const std::string_view::const_iterator end = data.end();
//         auto &state = this->response_reader_.getContext().state;

//     continue_parse:
//         if (begin == end) { co_return; }
//         auto result = this->response_reader_.parse(this->response_, begin, end);
//         if (!result) {
//             this->response_.metadata.status_code = result.error().expected_status;
//             state = v1::ResponseParser::STATE::FAILED;
//         }

//         // if (!this->current_route_ && state == v1::RequestParser::STATE::HEADERS_DONE) {
//         //     this->response_.metadata.version = this->request_.metadata.version;
//         //     auto match = this->router_->match(this->request_);
//         //     if (!match) {
//         //         state = v1::RequestParser::STATE::FAILED;
//         //         // TODO: Status code & message
//         //         this->response_.metadata.status_code = match.error();
//         //     } else {
//         //         this->current_route_ = match.value();
//         //     }
//         // }

//         switch (state) {}
//     send_body:
//         // co_await this->write_response(transport);
//     end:
//         co_return;
//     }
// };// namespace usub::unet::http::v1
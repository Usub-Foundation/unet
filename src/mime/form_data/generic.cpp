#include "unet/mime/multipart/form_data/generic.hpp"

namespace usub::unet::mime::multipart {

    FormData::FormData(std::string boundary) : boundary_(std::move(boundary)) {}

    FormData::FormData(std::string boundary, std::string raw_data) : boundary_(std::move(boundary)) {
        auto res = parse(std::move(raw_data));
        if (!res) throw std::runtime_error(res.error());
    }

    std::vector<Part> &FormData::operator[](const std::string &name) { return this->parts_by_name_[name]; }

    const std::vector<Part> &FormData::operator[](const std::string &name) const {
        static const std::vector<Part> empty_vec;
        auto it = this->parts_by_name_.find(name);
        return it == this->parts_by_name_.end() ? empty_vec : it->second;
    }

    std::vector<Part> &FormData::at(const std::string &name) { return this->parts_by_name_.at(name); }
    const std::vector<Part> &FormData::at(const std::string &name) const { return this->parts_by_name_.at(name); }

    bool FormData::contains(const std::string &name) const { return this->parts_by_name_.find(name) != this->parts_by_name_.end(); }

    std::size_t FormData::size() const { return this->parts_by_name_.size(); }
    bool FormData::empty() const { return this->parts_by_name_.empty(); }
    void FormData::clear() { this->parts_by_name_.clear(); }
    std::size_t FormData::erase(const std::string &name) { return this->parts_by_name_.erase(name); }

    FormData::iterator FormData::begin() { return this->parts_by_name_.begin(); }
    FormData::iterator FormData::end() { return this->parts_by_name_.end(); }
    FormData::const_iterator FormData::begin() const { return this->parts_by_name_.begin(); }
    FormData::const_iterator FormData::end() const { return this->parts_by_name_.end(); }

    const FormData::parts_map_t &FormData::parts_by_name() const { return this->parts_by_name_; }
    FormData::parts_map_t &FormData::parts_by_name() { return this->parts_by_name_; }

    std::expected<void, std::string> FormData::parse(std::string input) {
        std::istringstream stream(input);
        std::string line;

        std::string detectedBoundary;
        bool isFirstLine = true;
        bool readingData = false;

        std::optional<std::string> rawDisposition;
        std::optional<std::string> rawContentType;
        std::ostringstream dataBuffer;
        std::unordered_map<std::string, std::vector<std::string>, usub::utils::CaseInsensitiveHash, usub::utils::CaseInsensitiveEqual> extra_Headers;

        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (isFirstLine) {
                if (line.rfind("--", 0) == 0) {
                    detectedBoundary = line.substr(2);
                } else {
                    return std::unexpected("Invalid multipart input: first line must start with '--'.");
                }

                if (this->boundary_.empty()) {
                    return std::unexpected("Boundary must be provided.");
                }

                if (this->boundary_ != detectedBoundary) {
                    return std::unexpected("Boundary mismatch");
                }

                isFirstLine = false;
                continue;
            }

            if (line == "--" + detectedBoundary || line == "--" + detectedBoundary + "--") {
                if (rawDisposition.has_value()) {
                    std::unordered_map<std::string, std::string> disp;
                    for (auto &seg: usub::utils::split(rawDisposition.value(), ';')) {
                        usub::utils::trim(seg);
                        auto eq = seg.find('=');
                        if (eq == std::string::npos) {
                            continue;
                        }
                        std::string keyPart = seg.substr(0, eq);
                        std::string valPart = seg.substr(eq + 1);

                        usub::utils::trim(keyPart);
                        usub::utils::trim(valPart);

                        if (valPart.size() >= 2 &&
                            valPart.front() == '"' &&
                            valPart.back() == '"') {
                            valPart = valPart.substr(1, valPart.size() - 2);
                        }

                        disp.emplace(usub::utils::toLower(keyPart), std::move(valPart));
                    }

                    std::string ctype = rawContentType.value_or("text/plain; charset=US-ASCII");

                    std::string content = dataBuffer.str();

                    if (!content.empty() && content.back() == '\n')
                        content.pop_back();
                    if (!content.empty() && content.back() == '\r')
                        content.pop_back();

                    Part part{
                            std::move(ctype),
                            std::move(disp),
                            std::move(content),
                            std::move(extra_Headers)};

                    auto itName = part.disposition.find("name");
                    if (itName != part.disposition.end()) {
                        this->parts_by_name_[itName->second].push_back(std::move(part));
                    }

                    rawDisposition.reset();
                    rawContentType.reset();
                    dataBuffer.str("");
                    dataBuffer.clear();
                    extra_Headers.clear();
                }

                if (line == "--" + detectedBoundary + "--") {
                    break;
                }

                readingData = false;
                continue;
            }

            if (!readingData && line.find("Content-Disposition:") == 0) {
                rawDisposition = usub::utils::trim_copy(line.substr(std::string("Content-Disposition:").length()));
            } else if (!readingData && line.find("Content-Type:") == 0) {
                rawContentType = usub::utils::trim_copy(line.substr(std::string("Content-Type:").length()));
            } else if (!readingData && line.find(":") != std::string::npos) {
                auto colonPos = line.find(":");
                std::string headerName = usub::utils::trim_copy(line.substr(0, colonPos));
                std::string headerValue = usub::utils::trim_copy(line.substr(colonPos + 1));

                if (!rawDisposition.has_value()) continue;
                extra_Headers[headerName].push_back(headerValue);
            } else if (!readingData && line.empty()) {
                readingData = true;
            } else if (readingData) {
                dataBuffer << line << '\n';
            }
        }

        return {};
    }

}// namespace usub::unet::mime::multipart

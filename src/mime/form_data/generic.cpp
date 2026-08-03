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
    if (this->boundary_.empty())
        return std::unexpected("Boundary must be provided.");

    const std::string delim = "--" + this->boundary_;

    std::size_t pos = input.find(delim);
    if (pos == std::string::npos)
        return std::unexpected("Invalid multipart input: opening boundary not found.");
    pos += delim.size();

    while (pos <= input.size()) {
        // Closing delimiter: "--boundary--"
        if (input.compare(pos, 2, "--") == 0)
            break;

        // A CRLF (tolerate a bare LF) must follow the delimiter.
        if (input.compare(pos, 2, "\r\n") == 0)
            pos += 2;
        else if (pos < input.size() && input[pos] == '\n')
            pos += 1;
        else
            return std::unexpected("Malformed multipart: expected CRLF after boundary.");

        // Part headers end at the first blank line.
        std::size_t headers_end = input.find("\r\n\r\n", pos);
        std::size_t sep_len = 4;
        if (headers_end == std::string::npos) {
            headers_end = input.find("\n\n", pos);
            sep_len = 2;
            if (headers_end == std::string::npos)
                return std::unexpected("Malformed multipart: unterminated part headers.");
        }
        const std::string header_block = input.substr(pos, headers_end - pos);
        const std::size_t data_start = headers_end + sep_len;

        // Body runs up to the CRLF (tolerate bare LF) immediately preceding the
        // next delimiter — extracted verbatim, byte for byte.
        std::size_t next = input.find(delim, data_start);
        if (next == std::string::npos)
            return std::unexpected("Malformed multipart: closing boundary not found.");
        std::size_t data_end = next;
        if (data_end >= 2 && input.compare(data_end - 2, 2, "\r\n") == 0)
            data_end -= 2;
        else if (data_end >= 1 && input[data_end - 1] == '\n')
            data_end -= 1;

        // ---- part headers are ASCII: safe to scan line by line ----
        std::optional<std::string> rawDisposition;
        std::optional<std::string> rawContentType;
        std::unordered_map<std::string, std::vector<std::string>,
                           usub::utils::CaseInsensitiveHash,
                           usub::utils::CaseInsensitiveEqual>
            extra_Headers;

        std::size_t hp = 0;
        while (hp < header_block.size()) {
            std::size_t eol = header_block.find('\n', hp);
            std::string line = header_block.substr(
                hp, eol == std::string::npos ? std::string::npos : eol - hp);
            hp = (eol == std::string::npos) ? header_block.size() : eol + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            if (line.find("Content-Disposition:") == 0) {
                rawDisposition = usub::utils::trim_copy(
                    line.substr(std::string("Content-Disposition:").length()));
            } else if (line.find("Content-Type:") == 0) {
                rawContentType =
                    usub::utils::trim_copy(line.substr(std::string("Content-Type:").length()));
            } else if (auto colon = line.find(':'); colon != std::string::npos) {
                std::string headerName = usub::utils::trim_copy(line.substr(0, colon));
                std::string headerValue = usub::utils::trim_copy(line.substr(colon + 1));
                extra_Headers[headerName].push_back(std::move(headerValue));
            }
        }

        // No Content-Disposition -> no name to key on; skip the part.
        if (!rawDisposition.has_value()) {
            pos = next + delim.size();
            continue;
        }

        std::unordered_map<std::string, std::string> disp;
        for (auto &seg : usub::utils::split(rawDisposition.value(), ';')) {
            usub::utils::trim(seg);
            auto eq = seg.find('=');
            if (eq == std::string::npos)
                continue;
            std::string keyPart = seg.substr(0, eq);
            std::string valPart = seg.substr(eq + 1);
            usub::utils::trim(keyPart);
            usub::utils::trim(valPart);
            if (valPart.size() >= 2 && valPart.front() == '"' && valPart.back() == '"')
                valPart = valPart.substr(1, valPart.size() - 2);
            disp.emplace(usub::utils::toLower(keyPart), std::move(valPart));
        }

        Part part{
            rawContentType.value_or("text/plain; charset=US-ASCII"),
            std::move(disp),
            input.substr(data_start, data_end - data_start),  // raw bytes — binary-safe
            std::move(extra_Headers)};

        auto itName = part.disposition.find("name");
        if (itName != part.disposition.end())
            this->parts_by_name_[itName->second].push_back(std::move(part));

        pos = next + delim.size();
    }

    return {};
}


}// namespace usub::unet::mime::multipart

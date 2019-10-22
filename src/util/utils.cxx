#include "util/utils.h"
#include <regex>

class compact_json_writer final : public Json::StreamWriterBuilder {
public:
    compact_json_writer() {
        settings_["indentation"] = "";
    }
};

static compact_json_writer g_write_builder;

std::string khorost::data::json_string(const Json::Value& value) {
    return writeString(g_write_builder, value);
}

bool khorost::data::parse_json(char const* begin_doc, char const* end_doc, Json::Value& value) {
    Json::CharReaderBuilder reader_builder;
    Json::String errs;
    auto reader(reader_builder.newCharReader());
    return reader->parse(begin_doc, end_doc, &value, &errs);
}

std::string khorost::data::escape_string(const std::string& s_) {
    std::string r;

    for (std::string::const_iterator cit=s_.begin();cit!=s_.end();++cit) {
        if (*cit=='\'') {
            r.append("\\\'");
        } else if (*cit=='\n') {
            r.append("\\n");
        } else if (*cit=='\r') {
        } else {
            r.push_back(*cit);
        }
    }
    return r;
}

static boost::posix_time::ptime               s_time_t_epoch(boost::gregorian::date(1970, 1, 1));

boost::posix_time::time_duration khorost::data::epoch_diff(boost::posix_time::ptime pt) {
    return pt - s_time_t_epoch;
}

boost::posix_time::ptime khorost::data::epoch_microseconds2ptime(uint64_t ms) {
    return s_time_t_epoch + boost::posix_time::microseconds(ms);
}

boost::posix_time::ptime khorost::data::epoch_milliseconds2ptime(uint64_t ms) {
    return s_time_t_epoch + boost::posix_time::milliseconds(ms);
}

// The trimming method comes from https://stackoverflow.com/a/1798170/1613961
std::string trim(const std::string& str, const std::string& newline = "\r\n") {
    const auto strBegin = str.find_first_not_of(newline);
    if (strBegin == std::string::npos)
        return ""; // no content

    const auto strEnd = str.find_last_not_of(newline);
    const auto strRange = strEnd - strBegin + 1;

    return str.substr(strBegin, strRange);
}

std::string khorost::data::clear_html_tags(const std::string source) {
    std::regex stripFormatting("<[^>]*(>|$)"); //match any character between '<' and '>', even when end tag is missing

    std::string s1 = std::regex_replace(source, stripFormatting, "");
    std::string s2 = trim(s1);
    std::string s3 = std::regex_replace(s2, std::regex("\\&nbsp;"), " ");
    return s3;
}


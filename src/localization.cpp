#include "localization.hpp"
#include "paths.hpp"
#include "safe_file.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
namespace paint {
namespace {
LanguageCatalog active_catalog;
std::vector<LanguageInfo> languages{{"en-us", "English (built in)", false, {}}};
std::string load_error;
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) { if (first == 0) { return false; } continue; }
        unsigned int count = 0, code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; code = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; code = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; code = first & 7; minimum = 0x10000; }
        else { return false; }
        if (count > text.size() - i) { return false; }
        for (unsigned int part = 0; part < count; ++part) {
            const unsigned char next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) { return false; }
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) { return false; }
    }
    return true;
}
void append_utf8(std::string& value, unsigned int code) {
    if (code < 0x80) { value.push_back(static_cast<char>(code)); }
    else if (code < 0x800) {
        value.push_back(static_cast<char>(0xc0 | (code >> 6)));
        value.push_back(static_cast<char>(0x80 | (code & 63)));
    } else if (code < 0x10000) {
        value.push_back(static_cast<char>(0xe0 | (code >> 12)));
        value.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
        value.push_back(static_cast<char>(0x80 | (code & 63)));
    } else {
        value.push_back(static_cast<char>(0xf0 | (code >> 18)));
        value.push_back(static_cast<char>(0x80 | ((code >> 12) & 63)));
        value.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
        value.push_back(static_cast<char>(0x80 | (code & 63)));
    }
}
class StringObjectReader {
  public:
    explicit StringObjectReader(std::string_view source) : source_(source) {}
    std::map<std::string, std::string, std::less<>> read() {
        std::map<std::string, std::string, std::less<>> result;
        if (source_.starts_with("\xef\xbb\xbf")) { position_ = 3; }
        require('{');
        if (take('}')) { finish(); return result; }
        do {
            const std::string key = string();
            require(':');
            const std::string value = string();
            if (key.empty() || value.empty() || result.size() >= 4096 || !result.emplace(key, value).second) {
                throw std::runtime_error("Empty or duplicate language entry, or too many entries.");
            }
            if (take('}')) { finish(); return result; }
            require(',');
        } while (true);
    }
  private:
    std::string_view source_;
    std::size_t position_ = 0;
    void whitespace() {
        while (position_ < source_.size() && (source_[position_] == ' ' || source_[position_] == '\n' ||
               source_[position_] == '\r' || source_[position_] == '\t')) { ++position_; }
    }
    bool take(char wanted) {
        whitespace();
        if (position_ == source_.size() || source_[position_] != wanted) { return false; }
        ++position_; return true;
    }
    void require(char wanted) {
        if (!take(wanted)) { throw std::runtime_error("Language pack must be a JSON object of strings."); }
    }
    void finish() {
        whitespace();
        if (position_ != source_.size()) { throw std::runtime_error("Trailing data in language pack."); }
    }
    unsigned int hex() {
        unsigned int result = 0;
        for (int i = 0; i < 4; ++i) {
            if (position_ == source_.size()) { throw std::runtime_error("Incomplete JSON Unicode escape."); }
            const char c = source_[position_++];
            result <<= 4;
            if (c >= '0' && c <= '9') { result += c - '0'; }
            else if (c >= 'a' && c <= 'f') { result += c - 'a' + 10; }
            else if (c >= 'A' && c <= 'F') { result += c - 'A' + 10; }
            else { throw std::runtime_error("Invalid JSON Unicode escape."); }
        }
        return result;
    }
    std::string string() {
        require('"');
        std::string value;
        while (position_ < source_.size()) {
            const unsigned char c = static_cast<unsigned char>(source_[position_++]);
            if (c == '"') {
                if (!valid_utf8(value)) { throw std::runtime_error("Language text is not valid UTF-8."); }
                return value;
            }
            if (c < 32 || value.size() > 65536) { throw std::runtime_error("Invalid or oversized language string."); }
            if (c != '\\') { value.push_back(static_cast<char>(c)); continue; }
            if (position_ == source_.size()) { break; }
            const char escape = source_[position_++];
            switch (escape) {
            case '"': case '\\': case '/': value.push_back(escape); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'u': {
                unsigned int code = hex();
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (position_ + 2 > source_.size() || source_.substr(position_, 2) != "\\u") {
                        throw std::runtime_error("Missing JSON low surrogate.");
                    }
                    position_ += 2;
                    const unsigned int low = hex();
                    if (low < 0xdc00 || low > 0xdfff) { throw std::runtime_error("Invalid JSON low surrogate."); }
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                }
                if (code == 0 || (code >= 0xdc00 && code <= 0xdfff)) { throw std::runtime_error("Invalid JSON code point."); }
                append_utf8(value, code); break;
            }
            default: throw std::runtime_error("Invalid JSON string escape.");
            }
        }
        throw std::runtime_error("Unterminated JSON string.");
    }
};
std::string metadata(const std::map<std::string, std::string, std::less<>>& strings, const char* name) {
    const std::map<std::string, std::string, std::less<>>::const_iterator found = strings.find(name);
    if (found == strings.end()) { throw std::runtime_error("Missing language pack metadata."); }
    return (*found).second;
}
std::vector<std::string> placeholders(std::string_view text) {
    std::vector<std::string> result;
    for (std::size_t start = text.find('{'); start != std::string_view::npos; start = text.find('{', start + 1)) {
        const std::size_t end = text.find('}', start + 1);
        if (end == std::string_view::npos) { break; }
        const std::string_view name = text.substr(start + 1, end - start - 1);
        bool valid = !name.empty();
        for (const char c : name) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) { valid = false; }
        }
        if (valid) { result.emplace_back(name); }
    }
    std::sort(result.begin(), result.end());
    return result;
}
bool language_before(const LanguageInfo& left, const LanguageInfo& right) { return left.tag < right.tag; }
std::string base_language(std::string_view tag) { return std::string(tag.substr(0, tag.find('-'))); }
} // namespace
std::string normalize_language_tag(std::string_view tag) {
    const std::size_t end = tag.find_first_of(".@");
    std::string result;
    for (const char c : tag.substr(0, end)) {
        if (c == '_') { result.push_back('-'); }
        else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else { return {}; }
    }
    if (result == "c" || result == "posix") { return "en-us"; }
    if (result == "iw" || result.starts_with("iw-")) { result.replace(0, 2, "he"); }
    if (result == "in" || result.starts_with("in-")) { result.replace(0, 2, "id"); }
    if (result.size() > 63 || result.empty() || result.front() == '-' || result.back() == '-' || result.find("--") != std::string::npos) { return {}; }
    return result;
}
bool LanguageCatalog::load(const std::filesystem::path& file, std::string_view expected_tag, std::string& error) {
    *this = LanguageCatalog{};
    error.clear();
    try {
        const std::vector<std::uint8_t> bytes = read_regular_file_bounded(path_to_utf8(file), 1024 * 1024, "Cannot read language pack.");
        const std::string source(bytes.begin(), bytes.end());
        StringObjectReader reader(source);
        std::map<std::string, std::string, std::less<>> parsed = reader.read();
        LanguageInfo info;
        info.tag = normalize_language_tag(metadata(parsed, "@language"));
        info.name = metadata(parsed, "@name");
        const std::string direction = metadata(parsed, "@direction");
        if (info.tag.empty() || info.tag == "en-us" || info.tag != normalize_language_tag(expected_tag) ||
            info.name.size() > 160 || (direction != "ltr" && direction != "rtl")) {
            throw std::runtime_error("Invalid language identity or direction.");
        }
        for (const std::pair<const std::string, std::string>& entry : parsed) {
            if (!entry.first.starts_with('@') && placeholders(entry.first) != placeholders(entry.second)) {
                throw std::runtime_error("Language placeholders must match the English message.");
            }
        }
        info.right_to_left = direction == "rtl";
        info.file = file;
        parsed.erase("@language"); parsed.erase("@name"); parsed.erase("@direction");
        strings_ = std::move(parsed);
        info_ = std::move(info);
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
std::string LanguageCatalog::text(std::string_view english) const {
    const std::map<std::string, std::string, std::less<>>::const_iterator found = strings_.find(english);
    return found == strings_.end() ? std::string(english) : (*found).second;
}
std::vector<LanguageInfo> discover_languages(const std::filesystem::path& folder) {
    std::vector<LanguageInfo> result{{"en-us", "English (built in)", false, {}}};
    std::error_code error;
    std::filesystem::directory_iterator files(folder, error), end;
    std::size_t scanned = 0;
    for (; !error && files != end && scanned++ < 1024; files.increment(error)) {
        const std::filesystem::path file = (*files).path();
        if (file.extension() != ".json" || std::filesystem::is_symlink((*files).symlink_status(error))) { continue; }
        LanguageCatalog candidate;
        std::string detail;
        if (candidate.load(file, path_to_utf8(file.stem()), detail)) { result.push_back(candidate.info()); }
        if (result.size() >= 128) { break; }
    }
    std::sort(result.begin() + 1, result.end(), language_before);
    return result;
}
std::string match_language(const std::vector<std::string>& preferences, const std::vector<LanguageInfo>& available) {
    for (const std::string& preference : preferences) {
        const std::string tag = normalize_language_tag(preference);
        if (tag.empty()) { continue; }
        if (base_language(tag) == "en") { return "en-us"; }
        for (const LanguageInfo& info : available) { if (info.tag == tag) { return info.tag; } }
        if (tag.starts_with("zh")) {
            const std::string wanted = tag.find("hant") != std::string::npos || tag == "zh-tw" || tag == "zh-hk" || tag == "zh-mo" ? "zh-tw" : "zh-cn";
            for (const LanguageInfo& info : available) { if (info.tag == wanted) { return info.tag; } }
            continue;
        }
        if (base_language(tag) == "pa") {
            const std::string wanted = tag.find("arab") != std::string::npos || tag.ends_with("-pk") ? "pa-pk" : "pa-in";
            for (const LanguageInfo& info : available) { if (info.tag == wanted) { return info.tag; } }
        }
        for (const LanguageInfo& info : available) {
            if (base_language(info.tag) == base_language(tag)) { return info.tag; }
        }
    }
    return "en-us";
}
void initialize_language(const std::filesystem::path& folder, std::string_view preference) {
    active_catalog = LanguageCatalog{};
    load_error.clear();
    languages = discover_languages(folder);
    const std::vector<std::string> requested = preference.empty() || preference == "system"
        ? system_languages() : std::vector<std::string>{std::string(preference)};
    const std::string selected = match_language(requested, languages);
    for (const LanguageInfo& info : languages) {
        if (info.tag == selected && !info.file.empty()) {
            static_cast<void>(active_catalog.load(info.file, selected, load_error)); break;
        }
    }
}
const std::vector<LanguageInfo>& available_languages() { return languages; }
const LanguageInfo& current_language() { return active_catalog.info(); }
const std::string& language_load_error() { return load_error; }
std::string tr(std::string_view english) { return active_catalog.text(english); }
std::string tr_format(std::string_view english, const std::map<std::string, std::string, std::less<>>& arguments) {
    const std::string translated = tr(english);
    std::string result;
    for (std::size_t i = 0; i < translated.size();) {
        const std::size_t close = translated[i] == '{' ? translated.find('}', i + 1) : std::string::npos;
        if (close != std::string::npos) {
            const std::map<std::string, std::string, std::less<>>::const_iterator found = arguments.find(translated.substr(i + 1, close - i - 1));
            if (found != arguments.end()) { result += (*found).second; i = close + 1; continue; }
        }
        result.push_back(translated[i++]);
    }
    return result;
}
} // namespace paint

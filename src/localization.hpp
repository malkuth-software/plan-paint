#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace paint {
struct LanguageInfo {
    std::string tag, name;
    bool right_to_left = false;
    std::filesystem::path file;
};
class LanguageCatalog {
  public:
    bool load(const std::filesystem::path& file, std::string_view expected_tag, std::string& error);
    std::string text(std::string_view english) const;
    const LanguageInfo& info() const noexcept { return info_; }
    std::size_t size() const noexcept { return strings_.size(); }
  private:
    LanguageInfo info_{"en-us", "English (built in)", false, {}};
    std::map<std::string, std::string, std::less<>> strings_;
};
std::string normalize_language_tag(std::string_view tag);
std::vector<std::string> system_languages();
std::filesystem::path application_language_directory();
std::string match_language(const std::vector<std::string>& preferences, const std::vector<LanguageInfo>& available);
std::vector<LanguageInfo> discover_languages(const std::filesystem::path& folder);
// English literals at call sites are the compiled-in source of truth. A missing
// key, corrupt pack or absent language directory can never remove that fallback.
void initialize_language(const std::filesystem::path& folder, std::string_view preference = "system");
const std::vector<LanguageInfo>& available_languages();
const LanguageInfo& current_language();
const std::string& language_load_error();
std::string tr(std::string_view english);
std::string tr_format(std::string_view english, const std::map<std::string, std::string, std::less<>>& arguments);
} // namespace paint

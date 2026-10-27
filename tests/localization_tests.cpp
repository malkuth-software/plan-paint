#include "localization.hpp"
#include "desktop.hpp"
#include "paths.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary); file << text;
    require(static_cast<bool>(file), "write fixture");
}
void run() {
    using paint::LanguageInfo;
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "plan-paint-language-tests";
    std::filesystem::create_directories(folder);
    const std::filesystem::path pack = folder / "fr-fr.json";
    const std::string valid = R"({"@language":"fr-fr","@name":"Français","@direction":"ltr","Open…":"Ouvrir…","File {name}":"Fichier {name}","Smile":"\uD83D\uDE42"})";
    write(pack, valid);
    paint::LanguageCatalog catalog;
    std::string error;
    require(catalog.load(pack, "fr-fr", error), "valid UTF-8 pack loads");
    require(catalog.text("Open…") == "Ouvrir…", "message translated");
    require(catalog.text("Absent") == "Absent", "missing message uses compiled English");
    require(catalog.text("Smile") == "🙂", "surrogate pairs decode");
    const std::string bad[] = {
        R"({"@language":"fr-fr","@name":"F","@direction":"rtl","x":"a","x":"b"})",
        R"({"@language":"fr-fr","@name":"F","@direction":"ltr","x":"\uD800"})",
        R"({"@language":"fr-fr","@name":"F","@direction":"ltr","x":"\uDC00"})",
        R"({"@language":"fr-fr","@name":"F","@direction":"ltr","x":"\u0000"})",
        R"({"@language":"fr-fr","@name":"F","@direction":"ltr","x":2})",
        R"({"@language":"fr-fr","@name":"F","@direction":"ltr","x":""})",
        R"({"@language":"fr-fr","@name":"F","@direction":"ltr","File {name}":"Fichier {other}"})",
        R"({"@language":"fr-fr","@name":"F","@direction":"bad","x":"yes"})",
        R"({"@language":"en-us","@name":"F","@direction":"ltr","x":"yes"})",
        R"({"@language":"fr-fr","@name":"F","@direction":"ltr"}garbage)",
        std::string("{\"@language\":\"fr-fr\",\"@name\":\"") + char(0xc0) + char(0x80) + "\",\"@direction\":\"ltr\"}"
    };
    for (const std::string& source : bad) {
        write(pack, source);
        require(!catalog.load(pack, "fr-fr", error) && !error.empty(), "reject malformed pack");
        require(catalog.text("Open…") == "Open…" && catalog.info().tag == "en-us", "failed load clears previous language");
    }
    write(pack, valid);
    require(!catalog.load(pack, "de-de", error), "reject identity mismatch");
    const std::vector<LanguageInfo> languages{{"en-us", "English", false, {}}, {"fr-fr", "Français", false, {}},
        {"zh-cn", "简体中文", false, {}}, {"zh-tw", "繁體中文", false, {}}, {"he-il", "עברית", true, {}}, {"pa-in", "ਪੰਜਾਬੀ", false, {}}, {"pa-pk", "پنجابی", true, {}}};
    require(paint::normalize_language_tag("FR_ca.UTF-8") == "fr-ca", "POSIX locale normalization");
    require(paint::normalize_language_tag("iw_IL") == "he-il", "legacy Hebrew alias");
    require(paint::normalize_language_tag("../de-de").empty(), "no path traversal in tag");
    require(paint::match_language({"xx", "fr-CA"}, languages) == "fr-fr", "ordered regional fallback");
    require(paint::match_language({"zh-Hant-HK"}, languages) == "zh-tw", "traditional Chinese script");
    require(paint::match_language({"zh-Hans-SG"}, languages) == "zh-cn", "simplified Chinese script");
    require(paint::match_language({"en-GB", "fr"}, languages) == "en-us", "English preference wins");
    require(paint::match_language({"pa-Arab-PK"}, languages) == "pa-pk", "Punjabi script fallback");
    require(paint::match_language({"xx"}, languages) == "en-us", "unknown locale fallback");
    paint::initialize_language(folder, "fr-fr");
    require(paint::tr("Open…") == "Ouvrir…", "selected catalog activated");
    require(paint::tr_format("File {name}", {{"name", "unmodified-{name}.png"}}) == "Fichier unmodified-{name}.png", "substitutions not translated or recursively expanded");
    paint::initialize_language(folder / "absent", "fr-fr");
    require(paint::tr("Open…") == "Open…", "missing directory cannot remove English");
    paint::EditorSettings settings;
    settings.storage_path = (folder / "settings.txt").string();
    settings.language = "zh-tw"; settings.canvas_controls = true; settings.save();
    paint::EditorSettings restored; restored.storage_path = settings.storage_path; restored.load();
    require(restored.language == "zh-tw" && restored.canvas_controls, "settings round trip");
    std::filesystem::remove(pack);
    std::filesystem::remove(folder / "settings.txt");
    std::filesystem::remove(folder);
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            std::size_t count = 0;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(paint::path_from_utf8(argv[1]))) {
                if (entry.path().extension() != ".json") { continue; }
                paint::LanguageCatalog pack;
                std::string error;
                require(pack.load(entry.path(), paint::path_to_utf8(entry.path().stem()), error), error.c_str());
                require(pack.size() >= 679, "bundled pack includes interface and full help");
                ++count;
            }
            require(count >= 22, "all promised language packs are bundled");
        }
        run(); std::cout << "Language fallback, parser, locale and settings checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

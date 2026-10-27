#include "asset_browser.hpp"
#include "codecs.hpp"
#include "fixtures/icon_reference.hpp"
#include "paths.hpp"
#include "safe_file.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
struct Directory {
    std::filesystem::path path;
    Directory()
        : path(std::filesystem::temp_directory_path() /
               ("paint-assets-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        require(std::filesystem::create_directory(path), "temporary folder failed");
    }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::string file(const std::string& name) const {
        return paint::path_to_utf8(path / paint::path_from_utf8(name));
    }
};
void sentinel(const std::string& path) {
    paint::write_file_atomic({'x'}, path, "fixture write failed");
}
void listings() {
    Directory directory;
    std::filesystem::create_directory(directory.path / "z-folder");
    for (const std::string& name :
         {"b.PNG", "A.ico", "c.cur", "d.svg", "wrong.txt", "disabled.gif", "unsupported.psd"}) {
        sentinel(directory.file(name));
    }
    const std::string root = paint::path_to_utf8(directory.path);
    const paint::AssetListing all = paint::scan_assets(root);
    require(all.entries.size() == 5 && !all.truncated && all.entries[0].name == "z-folder" &&
                all.entries[1].name == "A.ico" && all.entries[2].name == "b.PNG",
            "folder/file order or extension filter");
    require(paint::scan_assets(root, "PNG").entries.size() == 1, "ASCII case-insensitive search");
    const paint::AssetListing icons = paint::scan_assets(root, {}, paint::AssetFilter::Icons);
    require(icons.entries.size() == 2 && icons.entries[1].kind == paint::AssetKind::Icon,
            "type filtering must retain navigation folders");
    require(paint::scan_assets(root, {}, paint::AssetFilter::All, 2).truncated, "listing cap not reported");
    require(paint::scan_assets(directory.file("z-folder")).entries.empty(), "empty directory listing");
    std::atomic<bool> cancel{true};
    const paint::AssetListing cancelled =
        paint::scan_assets(root, {}, paint::AssetFilter::All, 8192, &cancel);
    require(cancelled.cancelled && cancelled.entries.empty(), "cancelled listing published partial entries");
    bool failed = false;
    try {
        static_cast<void>(paint::scan_assets(directory.file("missing")));
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "missing folder presented as empty");
}
void previews() {
    Directory directory;
    paint::Image original;
    original.reset(2, 1, {220, 100, 40, 128});
    original.set(1, 0, {90, 80, 70, 0});
    const std::string path = directory.file("sample.png");
    paint::save_image(original, path);
    const paint::AssetPreview preview = paint::load_asset_preview(path);
    require(preview.images.frames.size() == 1 && preview.preferred == 0 && paint::asset_unchanged(preview),
            "loaded asset metadata");
    const paint::Image stamp = paint::asset_stamp(preview, 0);
    require(paint::equal(stamp.get(1, 0), original.get(1, 0)), "stamp lost hidden RGB or alpha");
    const paint::Image proof = paint::render_asset_preview(preview.images.frames[0], 4, 3);
    require(proof.width == 4 && proof.height == 3 && proof.get(1, 1).a == 255 && proof.get(1, 1).r > 220 &&
                paint::equal(proof.get(2, 1), {250, 250, 250, 255}),
            "centered alpha preview or checker composition");
    require(paint::equal(preview.images.frames[0].image.get(1, 0), original.get(1, 0)),
            "proof mutated source");
    paint::ImageContainer icon;
    icon.kind = paint::ContainerKind::Icon;
    paint::IconFrame small, large;
    small.image.reset(16, 16, {1, 2, 3, 255});
    large.image.reset(32, 32, {4, 5, 6, 255});
    icon.frames = {small, large, small};
    paint::save_container(icon, directory.file("family.ico"));
    const paint::AssetPreview family = paint::load_asset_preview(directory.file("family.ico"));
    require(family.preferred == 1 && family.images.frames.size() == 3 &&
                paint::asset_stamp(family, 2).width == 16,
            "representation identity or default choice");
    paint::AssetPreview xor_preview;
    paint::IconFrame xor_frame;
    xor_frame.image.reset(1, 1, {255, 255, 255, 0});
    xor_frame.xor_pixels = {{255, 255, 255, 255}};
    xor_preview.images.frames = {xor_frame};
    const paint::Image xor_proof = paint::render_asset_preview(xor_frame, 1, 1);
    require(paint::equal(xor_proof.get(0, 0), {5, 5, 5, 255}), "legacy XOR proof is not backdrop-dependent");
    bool refused = false;
    try {
        static_cast<void>(paint::asset_stamp(xor_preview, 0));
    } catch (const std::exception&) {
        refused = true;
    }
    require(refused, "XOR silently flattened into stamp");
    sentinel(path);
    require(!paint::asset_unchanged(preview), "external change not detected");
    bool corrupt = false;
    try {
        static_cast<void>(paint::load_asset_preview(path));
    } catch (const std::exception&) {
        corrupt = true;
    }
    require(corrupt, "corrupt file preview accepted");
    std::filesystem::remove(paint::path_from_utf8(path));
    require(!paint::asset_unchanged(preview), "deleted file remained current");
}
void bookmarks() {
    Directory directory;
    const std::string path = directory.file("asset-folders.txt");
    require(paint::load_asset_bookmarks(path).empty(), "missing bookmarks should be empty");
    const std::vector<std::string> paths = {directory.file("missing % folder"), directory.file("line\nbreak"),
                                            directory.file("caf\xc3\xa9")};
    paint::save_asset_bookmarks(paths, path);
    require(paint::load_asset_bookmarks(path) == paths, "bookmarks lost missing paths, UTF-8 or escaping");
    const std::vector<std::uint8_t> bytes = paint::read_image_bytes(path);
    const std::string text(bytes.begin(), bytes.end());
    require(text.starts_with("Plan Paint asset folders v1\n") && text.find("%25") != std::string::npos &&
                text.find("%0A") != std::string::npos,
            "bookmarks are not the documented text format");
    bool invalid = false;
    try {
        paint::save_asset_bookmarks({"relative"}, path);
    } catch (const std::exception&) {
        invalid = true;
    }
    require(invalid && paint::load_asset_bookmarks(path) == paths,
            "failed bookmark save replaced existing data");
    sentinel(path);
    invalid = false;
    try {
        static_cast<void>(paint::load_asset_bookmarks(path));
    } catch (const std::exception&) {
        invalid = true;
    }
    require(invalid, "corrupt bookmark file silently erased");
}
} // namespace
int main() {
    try {
        listings();
        previews();
        bookmarks();
        std::cout << "Asset browser contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

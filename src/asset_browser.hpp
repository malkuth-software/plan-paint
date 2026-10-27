#pragma once
#include "atlas.hpp"
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>
namespace paint {
enum class AssetKind { Folder, Icon, Cursor, Bitmap, Vector };
enum class AssetFilter { All, Icons, Cursors, Bitmaps, Vectors };
struct AssetFingerprint {
    std::uintmax_t bytes = 0;
    std::filesystem::file_time_type modified;
};
struct AssetEntry {
    std::string path, name;
    AssetKind kind = AssetKind::Bitmap;
};
struct AssetListing {
    std::string directory;
    std::vector<AssetEntry> entries;
    bool truncated = false, cancelled = false;
};
struct AssetPreview {
    std::string path;
    AssetFingerprint fingerprint;
    ImageContainer images;
    std::size_t preferred = 0;
};
// One folder, no recursive traversal. ASCII case folding; other UTF-8 bytes match literally.
// Directories remain visible through type filters. Cancellation returns no partial listing.
AssetListing scan_assets(const std::string& directory, const std::string& query = {},
                         AssetFilter filter = AssetFilter::All, std::size_t limit = 8192,
                         const std::atomic<bool>* cancel = nullptr);
AssetFingerprint asset_fingerprint(const std::string& path);
bool asset_unchanged(const AssetPreview& preview);
// Uses ordinary public import codecs and their pixel limits. Initial encoded size <= 32 MB.
// Size/time checks detect ordinary external edits; they are not a content identity guarantee.
AssetPreview load_asset_preview(const std::string& path);
Image render_asset_preview(const IconFrame& frame, int width, int height);
Image asset_stamp(const AssetPreview& preview, std::size_t representation);
// Open UTF-8 text, percent-escaped paths, one per line. Up to 64 explicit bookmarks.
std::vector<std::string> load_asset_bookmarks(const std::string& path);
void save_asset_bookmarks(const std::vector<std::string>& folders, const std::string& path);
} // namespace paint

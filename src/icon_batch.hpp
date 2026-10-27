#pragma once
#include "asset_browser.hpp"
#include "icon_export.hpp"
#include "icon_palette.hpp"
#include "icon_stamp.hpp"
#include "icon_states.hpp"
namespace paint {
enum class IconBatchOperation { Convert, Normalize, States, Representations, Stamp, Palette };
struct IconBatchSettings {
    IconBatchOperation operation = IconBatchOperation::Convert;
    IconSizePolicy sizes_policy = IconSizePolicy::AddMissing;
    std::vector<int> sizes{16, 24, 32, 48, 64, 128, 256};
    IconStateRecipe recipe = icon_state_preset(IconStatePreset::Disabled);
    IconExportCollision collision = IconExportCollision::Skip;
    bool new_directory = true;
    std::string stamp_path;
    IconStampSettings stamp;
    std::string palette_path;
    IconPaletteOptions palette;
};
struct IconBatchSource {
    std::string path;
    AssetFingerprint fingerprint;
    std::size_t preferred = 0;
};
struct IconBatchEntry {
    std::size_t source = 0;
    int representation = -1;
    std::string filename, summary, diagnostic;
    IconExportState state = IconExportState::Planned;
    bool replaces_existing = false;
};
struct IconBatchPlan {
    IconBatchSettings settings;
    std::filesystem::path directory;
    bool create_directory = false;
    std::vector<IconBatchSource> sources;
    std::vector<IconBatchEntry> entries;
    IconBatchSource stamp_source;
    Image preview_before, preview_after; // Bounded first-image proof, at most 96 by 96 each.
};
// Preflight and execution decode one bounded source at a time. No source pixels
// are retained except bounded proof thumbnails. A changed source requires a fresh review.
using IconBatchProgress = void (*)(const IconBatchPlan&, void*);
IconBatchPlan plan_icon_batch(const std::vector<std::string>& sources, const std::string& directory,
                              const IconBatchSettings& settings, const std::atomic<bool>& cancel);
bool icon_batch_ready(const IconBatchPlan& plan);
void run_icon_batch(IconBatchPlan& plan, const std::atomic<bool>& cancel,
                    IconBatchProgress progress = nullptr, void* context = nullptr);
std::string icon_batch_report(const IconBatchPlan& plan);
} // namespace paint

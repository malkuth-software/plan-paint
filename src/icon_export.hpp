#pragma once
#include "atlas.hpp"
#include <atomic>
#include <filesystem>
#include <memory>
namespace paint {
enum class IconExportCollision { Skip, Rename, Replace };
enum class IconExportState { Planned, Running, Completed, Skipped, Failed, Cancelled };
struct IconExportEntry {
    int source_index = 0;
    int width = 0, height = 0;
    IconFormat source_format;
    std::string filename;
    IconExportState state = IconExportState::Planned;
    bool replaces_existing = false;
    std::string diagnostic;
};
struct IconExportPlan {
    std::shared_ptr<const ImageContainer> source;
    std::filesystem::path directory;
    IconExportCollision collision = IconExportCollision::Skip;
    std::vector<IconExportEntry> entries;
};
std::string icon_export_stem(const std::string& source_name);
IconExportPlan plan_icon_export(std::shared_ptr<const ImageContainer> source,
                                const std::vector<int>& selection, const std::string& directory,
                                const std::string& stem, IconExportCollision collision);
bool icon_export_ready(const IconExportPlan& plan);
// Named callback runs on the caller's worker. Cancellation keeps completed files.
using IconExportProgress = void (*)(const IconExportPlan&, void*);
void run_icon_export(IconExportPlan& plan, const std::atomic<bool>& cancel,
                     IconExportProgress progress = nullptr, void* context = nullptr);
std::string icon_export_state_name(IconExportState state);
std::string icon_export_report(const IconExportPlan& plan);
} // namespace paint

#include "icon_export.hpp"
#include "codecs.hpp"
#include "paths.hpp"
#include "safe_file.hpp"
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
namespace paint {
namespace {
bool occupied(const std::filesystem::path& path) {
    const std::filesystem::file_status status = std::filesystem::symlink_status(path);
    return status.type() != std::filesystem::file_type::not_found;
}
bool valid_stem(const std::string& stem) {
    if (stem.empty() || stem.size() > 80 || stem.find_first_of("<>:\"/\\|?*") != std::string::npos) {
        return false;
    }
    for (unsigned char ch : stem) {
        if (ch < 32) {
            return false;
        }
    }
    return true;
}
std::string quote_json(const std::string& value) {
    std::string result = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') {
            result += '\\';
            result += static_cast<char>(ch);
        } else if (ch < 32) {
            result += "\\u00";
            result += hex[ch >> 4];
            result += hex[ch & 15];
        } else {
            result += static_cast<char>(ch);
        }
    }
    return result + '"';
}
std::string representation_name(const std::string& stem, const IconFrame& frame, int index) {
    std::ostringstream name;
    name << stem << '-' << frame.image.width << 'x' << frame.image.height << '-';
    if (frame.source) {
        name << "src" << (*frame.source).format.bits_per_pixel << "bpp";
    } else {
        name << "rgba";
    }
    name << '-' << std::setfill('0') << std::setw(3) << index + 1 << ".png";
    return name.str();
}
} // namespace
std::string icon_export_stem(const std::string& source_name) {
    std::string stem = path_to_utf8(path_from_utf8(source_name).stem());
    if (!valid_stem(stem)) {
        return "icon";
    }
    return stem;
}
IconExportPlan plan_icon_export(std::shared_ptr<const ImageContainer> source,
                                const std::vector<int>& selection, const std::string& directory,
                                const std::string& stem, IconExportCollision collision) {
    if (!source || selection.empty() || selection.size() > 256) {
        throw std::runtime_error("Choose 1 to 256 representations to export.");
    }
    if (!valid_stem(stem)) {
        throw std::runtime_error(
            "Use a filename prefix of 1 to 80 bytes without path separators or reserved characters.");
    }
    if (directory.empty() || !std::filesystem::is_directory(path_from_utf8(directory))) {
        throw std::runtime_error("Choose an existing destination folder.");
    }
    IconExportPlan plan;
    plan.source = std::move(source);
    plan.directory = std::filesystem::canonical(path_from_utf8(directory));
    plan.collision = collision;
    std::set<std::string> names;
    for (int index : selection) {
        if (index < 0 || static_cast<std::size_t>(index) >= (*plan.source).frames.size()) {
            throw std::runtime_error("A selected representation is unavailable.");
        }
        const IconFrame& frame = (*plan.source).frames[index];
        IconExportEntry entry;
        entry.source_index = index;
        entry.width = frame.image.width;
        entry.height = frame.image.height;
        entry.source_format = frame.source ? (*frame.source).format : IconFormat{};
        entry.filename = representation_name(stem, frame, index);
        const std::string base = entry.filename.substr(0, entry.filename.size() - 4);
        int suffix = 2;
        while (names.contains(entry.filename)) {
            entry.filename = base + "-copy" + std::to_string(suffix++) + ".png";
        }
        if (frame.image.width < 1 || frame.image.height < 1 || frame.image.width > 256 ||
            frame.image.height > 256 ||
            frame.image.pixels.size() != static_cast<std::size_t>(frame.image.width) * frame.image.height) {
            entry.state = IconExportState::Failed;
            entry.diagnostic = "PNG export requires a valid icon image no larger than 256 by 256.";
        }
        for (std::size_t pixel = 0; pixel < frame.image.pixels.size(); ++pixel) {
            if (legacy_xor_pixel(frame, pixel)) {
                entry.state = IconExportState::Failed;
                entry.diagnostic = "PNG cannot retain background-dependent XOR. Use an opaque contact sheet "
                                   "to proof this image.";
                break;
            }
        }
        if (entry.state == IconExportState::Planned &&
            occupied(plan.directory / path_from_utf8(entry.filename))) {
            if (collision == IconExportCollision::Skip) {
                entry.state = IconExportState::Skipped;
                entry.diagnostic = "Destination already exists.";
            } else if (collision == IconExportCollision::Rename) {
                suffix = 2;
                do {
                    entry.filename = base + "-" + std::to_string(suffix++) + ".png";
                    if (suffix > 10002) {
                        throw std::runtime_error(
                            "Too many filename collisions. Choose another prefix or folder.");
                    }
                } while (names.contains(entry.filename) ||
                         occupied(plan.directory / path_from_utf8(entry.filename)));
            } else {
                const std::filesystem::file_status status =
                    std::filesystem::symlink_status(plan.directory / path_from_utf8(entry.filename));
                if (status.type() != std::filesystem::file_type::regular) {
                    entry.state = IconExportState::Failed;
                    entry.diagnostic = "Replacement requires a regular file, not a folder or symbolic link.";
                } else {
                    entry.replaces_existing = true;
                }
            }
        }
        names.insert(entry.filename);
        plan.entries.push_back(std::move(entry));
    }
    return plan;
}
bool icon_export_ready(const IconExportPlan& plan) {
    bool pending = false;
    for (const IconExportEntry& entry : plan.entries) {
        if (entry.state == IconExportState::Failed) {
            return false;
        }
        pending = pending || entry.state == IconExportState::Planned;
    }
    return pending;
}
void run_icon_export(IconExportPlan& plan, const std::atomic<bool>& cancel, IconExportProgress progress,
                     void* context) {
    if (!icon_export_ready(plan)) {
        throw std::runtime_error("Review a valid export manifest before running.");
    }
    for (IconExportEntry& entry : plan.entries) {
        if (entry.state != IconExportState::Planned) {
            continue;
        }
        if (cancel.load(std::memory_order_relaxed)) {
            entry.state = IconExportState::Cancelled;
            entry.diagnostic = "Cancelled before this output was published.";
            continue;
        }
        entry.state = IconExportState::Running;
        if (progress) {
            progress(plan, context);
        }
        try {
            const std::filesystem::path target = plan.directory / path_from_utf8(entry.filename);
            if (std::filesystem::canonical(plan.directory) != plan.directory) {
                throw std::runtime_error("Destination folder changed since review.");
            }
            const bool exists = occupied(target);
            if (exists && plan.collision == IconExportCollision::Skip) {
                entry.state = IconExportState::Skipped;
                entry.diagnostic = "Destination exists at execution; left unchanged.";
            } else {
                if (exists && plan.collision == IconExportCollision::Rename) {
                    throw std::runtime_error(
                        "Reviewed filename is now occupied. Review again to choose another name.");
                }
                if (exists &&
                    std::filesystem::symlink_status(target).type() != std::filesystem::file_type::regular) {
                    throw std::runtime_error("Destination is not a regular file; left unchanged.");
                }
                const std::vector<std::uint8_t> bytes =
                    encode_png((*plan.source).frames[entry.source_index].image);
                if (cancel.load(std::memory_order_relaxed)) {
                    entry.state = IconExportState::Cancelled;
                    entry.diagnostic = "Cancelled before this output was published.";
                } else {
                    write_file_atomic(bytes, path_to_utf8(target),
                                      "Could not publish PNG. Check destination permissions, space and "
                                      "filename collisions.",
                                      plan.collision == IconExportCollision::Replace);
                    entry.state = IconExportState::Completed;
                }
            }
        } catch (const std::exception& error) {
            entry.state = IconExportState::Failed;
            entry.diagnostic = error.what();
        }
        if (progress) {
            progress(plan, context);
        }
    }
    if (progress) {
        progress(plan, context);
    }
}
std::string icon_export_state_name(IconExportState state) {
    switch (state) {
    case IconExportState::Planned:
        return "Ready";
    case IconExportState::Running:
        return "Running";
    case IconExportState::Completed:
        return "Completed";
    case IconExportState::Skipped:
        return "Skipped";
    case IconExportState::Failed:
        return "Failed";
    case IconExportState::Cancelled:
        return "Cancelled";
    }
    return "Unknown";
}
std::string icon_export_report(const IconExportPlan& plan) {
    std::ostringstream json;
    json << "{\n  \"schemaVersion\": 1,\n  \"operation\": \"icon-representations-to-png\",\n  \"directory\": "
         << quote_json(path_to_utf8(plan.directory)) << ",\n  \"collisionPolicy\": "
         << quote_json(plan.collision == IconExportCollision::Skip     ? "skip"
                       : plan.collision == IconExportCollision::Rename ? "rename"
                                                                       : "replace")
         << ",\n  \"outputs\": [\n";
    for (std::size_t i = 0; i < plan.entries.size(); ++i) {
        const IconExportEntry& entry = plan.entries[i];
        json << "    {\"sourceOrdinal\": " << entry.source_index + 1 << ", \"width\": " << entry.width
             << ", \"height\": " << entry.height
             << ", \"sourceBitsPerPixel\": " << entry.source_format.bits_per_pixel
             << ", \"outputFormat\": \"RGBA PNG\", \"filename\": " << quote_json(entry.filename)
             << ", \"state\": " << quote_json(icon_export_state_name(entry.state))
             << ", \"diagnostic\": " << quote_json(entry.diagnostic) << '}'
             << (i + 1 < plan.entries.size() ? ",\n" : "\n");
    }
    return json.str() + "  ]\n}\n";
}
} // namespace paint

#include "conv.hpp"
#include "document.hpp"
#include "icon_palette.hpp"
#include "import_codecs.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace paint {
IconFrame generate_icon_target(const ImageContainer& sources, const IconGenerationTarget& target,
                               bool cursor_output) {
    if (target.source_index < 0 || target.source_index >= static_cast<int>(sources.frames.size())) {
        throw std::runtime_error("Choose an available source image for every target.");
    }
    if (target.width < 1 || target.height < 1 || target.width > 256 || target.height > 256) {
        throw std::runtime_error("Icon dimensions must be between 1 and 256 pixels.");
    }
    if (target.bits_per_pixel != 32 && target.bits_per_pixel != 4 && target.bits_per_pixel != 8) {
        throw std::runtime_error("Choose RGBA, 4-bit or 8-bit output.");
    }
    const IconFrame& source = sources.frames[target.source_index];
    if (source.image.width < 1 || source.image.height < 1 || source.image.pixels.empty()) {
        throw std::runtime_error("The source image is empty.");
    }
    for (std::size_t pixel = 0; pixel < source.image.pixels.size(); ++pixel) {
        if (legacy_xor_pixel(source, pixel)) {
            throw std::runtime_error("A legacy XOR image cannot be a generation source.");
        }
    }
    IconFrame result;
    if (svg_source_matches(source)) {
        result.image = rasterize_svg(*source.svg_source, target.width, target.height);
    } else if (source.image.width == target.width && source.image.height == target.height) {
        result.image = source.image;
    } else {
        conv_resize(source.image, target.width, target.height, result.image);
    }
    if (cursor_output && sources.kind == ContainerKind::Cursor) {
        result.hotspot_x = std::clamp(
            static_cast<int>(std::lround((source.hotspot_x + 0.5) * target.width / source.image.width - 0.5)),
            0, target.width - 1);
        result.hotspot_y =
            std::clamp(static_cast<int>(
                           std::lround((source.hotspot_y + 0.5) * target.height / source.image.height - 0.5)),
                       0, target.height - 1);
    }
    if (target.bits_per_pixel != 32) {
        IconPaletteOptions options;
        options.bits_per_pixel = target.bits_per_pixel;
        result = convert_icon_palette(result, options).frame;
    }
    return result;
}
void Document::generate_icon_targets(const std::vector<IconGenerationTarget>& targets,
                                     const std::vector<int>& removals, bool cursor) {
    const bool existing = atlas.kind == AtlasKind::Icon || atlas.kind == AtlasKind::Cursor;
    ImageContainer sources = output_container(cursor);
    sources.kind = atlas.kind == AtlasKind::Cursor ? ContainerKind::Cursor
                   : existing                      ? ContainerKind::Icon
                                                   : ContainerKind::Image;
    const int count = existing ? static_cast<int>(sources.frames.size()) : 0;
    std::vector<bool> removed(count, false), replaced(count, false);
    int output_count = count;
    for (int index : removals) {
        if (index < 0 || index >= count || removed[index]) {
            throw std::runtime_error("The removal list contains an unavailable or repeated image.");
        }
        removed[index] = true;
        --output_count;
    }
    for (const IconGenerationTarget& target : targets) {
        const int index = target.existing_index;
        if (index < -1 || index >= count || (index >= 0 && (removed[index] || replaced[index]))) {
            throw std::runtime_error("Choose each replacement once and do not also remove it.");
        }
        if (index < 0) {
            ++output_count;
        } else {
            replaced[index] = true;
        }
    }
    if (output_count < 1 || output_count > 256) {
        throw std::runtime_error("ICO/CUR must contain between 1 and 256 images.");
    }
    const AtlasKind kind = cursor ? AtlasKind::Cursor : AtlasKind::Icon;
    if (targets.empty() && removals.empty() && atlas.kind == kind) {
        return;
    }
    // Resolve every source from the same immutable snapshot, even when that
    // source is itself replaced or removed by another target in this operation.
    std::vector<IconFrame> generated;
    for (const IconGenerationTarget& target : targets) {
        generated.push_back(generate_icon_target(sources, target, cursor));
    }
    AtlasState result;
    result.kind = kind;
    for (int index = 0; index < count; ++index) {
        if (removed[index]) {
            continue;
        }
        IconFrame frame = sources.frames[index];
        for (std::size_t next = 0; next < targets.size(); ++next) {
            if (targets[next].existing_index == index) {
                frame = generated[next];
                break;
            }
        }
        if (index == atlas.active) {
            result.active = static_cast<int>(result.icons.size());
        }
        result.icons.push_back(std::move(frame));
    }
    for (std::size_t next = 0; next < targets.size(); ++next) {
        if (targets[next].existing_index < 0) {
            result.icons.push_back(std::move(generated[next]));
        }
    }
    if (result.active < 0) {
        result.active = static_cast<int>(result.icons.size()) - 1;
    }
    Image active_image = result.icons[result.active].image;
    checkpoint();
    selection = {};
    path = {};
    curve = {};
    atlas = std::move(result);
    image = std::move(active_image);
    ++atlas_epoch;
}
} // namespace paint

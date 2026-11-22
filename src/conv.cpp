#include "conv.hpp"
#include <stxlib/conv.hpp>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// CONV is computed by stxlib::conv (STXlib), the one canonical CONV of every
// program (owner, 2026-11-22). Plan Paint keeps two things here: the view of
// its straight RGBA pixels as the field's bytes, and the split of the
// destination into row bands over worker threads, which stxlib::conv leaves
// to its caller (one workspace per thread, the compiled field shared).
namespace paint {
namespace {
// Image pixels are four bytes, r g b a, rows contiguous: the field's RGBA8
// layout. A Color is read and written through unsigned char, which may alias
// any object.
static_assert(sizeof(Color) == stxlib::conv::channels && alignof(Color) == 1);
static_assert(std::is_standard_layout_v<Color> && std::is_trivially_copyable_v<Color>);

const stxlib::u8* pixel_bytes(const Image& image) {
    const stxlib::u8* bytes = reinterpret_cast<const stxlib::u8*>(image.pixels.data());
    return bytes;
}
stxlib::u8* pixel_bytes(Image& image) {
    stxlib::u8* bytes = reinterpret_cast<stxlib::u8*>(image.pixels.data());
    return bytes;
}

// The source as the field reads it: a one-pixel side repeated to two, so the
// field across it is constant and along the other axis it is that line's
// curve. Other images are used as they are (`repeated` stays empty).
const Image& field_source(const Image& source, Image& repeated) {
    if (source.width >= 2 && source.height >= 2) {
        return source;
    }
    repeated.reset(std::max(2, source.width), std::max(2, source.height), {0, 0, 0, 0});
    for (int y = 0; y < repeated.height; ++y) {
        for (int x = 0; x < repeated.width; ++x) {
            repeated.set(x, y, source.get(std::min(x, source.width - 1), std::min(y, source.height - 1)));
        }
    }
    return repeated;
}

// One band of destination rows, transferred on one thread with its own
// workspace; `succeeded` is written by that thread only.
struct BandWork {
    const stxlib::conv::Field* field = nullptr;
    stxlib::conv::AxisMap map;
    stxlib::conv::Mode mode = stxlib::conv::Mode::preview;
    stxlib::conv::MutableRgba8Image destination;
    stxlib::conv::Region region;
    stxlib::conv::Workspace* workspace = nullptr;
    bool succeeded = false;
};
void transfer_band(BandWork& work) {
    const stxlib::OperationStatus status =
        stxlib::conv::transfer(*work.field, work.map, work.mode, work.destination, work.region, *work.workspace);
    work.succeeded = status.succeeded();
}

void resize_image(const Image& source, int width, int height, Image& destination, stxlib::conv::Mode mode) {
    if (source.width < 1 || source.height < 1 ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height)) {
        throw std::invalid_argument("Cannot resize an empty image.");
    }
    if (width < 1 || height < 1) {
        throw std::invalid_argument("Cannot resize to an empty image.");
    }
    const std::size_t largest = stxlib::conv::max_side;
    if (static_cast<std::size_t>(source.width) > largest || static_cast<std::size_t>(source.height) > largest ||
        static_cast<std::size_t>(width) > largest || static_cast<std::size_t>(height) > largest) {
        throw std::invalid_argument("CONV resizes images up to 32768 pixels a side.");
    }
    if (width == source.width && height == source.height) {
        destination = source;
        return;
    }
    Image repeated;
    const Image& from = field_source(source, repeated);
    const std::size_t from_width = static_cast<std::size_t>(from.width);
    const std::size_t from_height = static_cast<std::size_t>(from.height);
    stxlib::OperationResult<stxlib::conv::Field> made =
        stxlib::conv::Field::make(from_width, from_height, stxlib::conv::FieldOptions{});
    if (made.failed()) {
        throw std::runtime_error("CONV could not allocate the image's field.");
    }
    stxlib::conv::Field field = std::move(made).take_value();
    const stxlib::conv::Rgba8Image image{stxlib::View<stxlib::u8>(pixel_bytes(from), from.pixels.size() * 4),
                                         from_width, from_height, from_width * 4};
    if (field.compile(image).failed()) {
        throw std::runtime_error("CONV rejected the source image.");
    }
    const std::size_t target_width = static_cast<std::size_t>(width);
    const std::size_t target_height = static_cast<std::size_t>(height);
    Image result;
    result.reset(width, height, {0, 0, 0, 0});
    const stxlib::conv::MutableRgba8Image output{
        stxlib::MutableView<stxlib::u8>(pixel_bytes(result), result.pixels.size() * 4), target_width, target_height,
        target_width * 4};
    // Bands of whole rows; small images take one.
    std::size_t count = std::min<std::size_t>(8, std::max(1u, std::thread::hardware_concurrency()));
    if (target_width * target_height < 65536) {
        count = 1;
    }
    count = std::min(count, target_height);
    std::vector<stxlib::conv::Workspace> workspaces;
    workspaces.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        stxlib::OperationResult<stxlib::conv::Workspace> workspace = stxlib::conv::Workspace::make();
        if (workspace.failed()) {
            throw std::runtime_error("CONV could not allocate a workspace.");
        }
        workspaces.push_back(std::move(workspace).take_value());
    }
    std::vector<BandWork> bands(count);
    for (std::size_t index = 0; index < count; ++index) {
        BandWork& band = bands[index];
        band.field = &field;
        band.map = stxlib::conv::AxisMap{target_width, target_height};
        band.mode = mode;
        band.destination = output;
        band.region = stxlib::conv::Region{0, index * target_height / count, target_width,
                                           (index + 1) * target_height / count};
        band.workspace = &workspaces[index];
    }
    if (count == 1) {
        transfer_band(bands[0]);
    } else {
        std::vector<std::jthread> workers;
        workers.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            workers.emplace_back(transfer_band, std::ref(bands[index]));
        }
        for (std::jthread& worker : workers) {
            worker.join();
        }
    }
    for (const BandWork& band : bands) {
        if (!band.succeeded) {
            throw std::runtime_error("CONV rejected the resize request.");
        }
    }
    destination = std::move(result);
}
} // namespace
void conv_resize(const Image& source, int width, int height, Image& destination) {
    resize_image(source, width, height, destination, stxlib::conv::Mode::preview);
}
void conv_resize_area(const Image& source, int width, int height, Image& destination) {
    resize_image(source, width, height, destination, stxlib::conv::Mode::canon);
}
} // namespace paint

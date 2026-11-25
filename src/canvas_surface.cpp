#include "canvas_surface.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace paint {
namespace {
// Image pixels are four bytes, r g b a, rows contiguous: the field's RGBA8
// layout, read and written through unsigned char.
static_assert(sizeof(Color) == stxlib::conv::channels && alignof(Color) == 1);
static_assert(std::is_standard_layout_v<Color> && std::is_trivially_copyable_v<Color>);
// Areas below this many pixels render on the calling thread.
const std::size_t parallel_threshold = 65536;
// Frame rows go out in chunks of this many, claimed by up to eight workers,
// each with its own workspace.
const int rows_per_chunk = 32;

stxlib::conv::Rgba8Image source_image(const Image& image) {
    const std::size_t width = static_cast<std::size_t>(image.width);
    const std::size_t height = static_cast<std::size_t>(image.height);
    const stxlib::u8* bytes = reinterpret_cast<const stxlib::u8*>(image.pixels.data());
    const stxlib::conv::Rgba8Image source{stxlib::View<stxlib::u8>(bytes, image.pixels.size() * 4), width, height,
                                          width * 4};
    return source;
}
Color encoded(WarpSample value) {
    const double alpha = std::clamp(value.a, 0.0, 1.0);
    if (alpha < 0.5 / 255.0) {
        return {0, 0, 0, 0};
    }
    const Color result{static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.r / alpha, 0.0, 1.0))),
                       static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.g / alpha, 0.0, 1.0))),
                       static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value.b / alpha, 0.0, 1.0))),
                       static_cast<std::uint8_t>(std::round(255.0 * alpha))};
    return result;
}
// Whether frame pixel (x, y) maps inside the footprint, by the position
// formula TargetMap states (sample_components' test on that position).
bool maps_inside(const CanvasSurface::TargetMap& map, int x, int y, int width, int height) {
    const double cx = x + 0.5, cy = y + 0.5;
    const double u = map.ux * cx + map.uy * cy + map.u0;
    const double v = map.vx * cx + map.vy * cy + map.v0;
    const bool inside = u >= -0.5 && v >= -0.5 && u < width - 0.5 && v < height - 0.5;
    return inside;
}
// Narrows [from, to] (real frame x) to where slope * (x + 0.5) + offset lies
// in [low, high).
void narrow(double slope, double offset, double low, double high, double& from, double& to) {
    if (slope == 0.0) {
        if (!(offset >= low && offset < high)) {
            from = 1.0;
            to = 0.0;
        }
        return;
    }
    const double at_low = (low - offset) / slope - 0.5;
    const double at_high = (high - offset) / slope - 0.5;
    from = std::max(from, std::min(at_low, at_high));
    to = std::min(to, std::max(at_low, at_high));
}
struct RenderJob {
    const stxlib::conv::Field& field;
    const CanvasSurface::TargetMap& target;
    stxlib::conv::AffineMap map;
    stxlib::conv::MutableRgba8Image destination;
    Color* pixels = nullptr;
    std::size_t stride = 0;
    int left = 0, top = 0, columns = 0, rows = 0;
    int source_width = 0, source_height = 0;
    std::atomic<int> next_chunk{0};
    std::atomic<bool> failed{false};
};
// Row y of the area: pixels mapping outside the footprint become
// transparent, and so do pixels the transfer left without opacity. The
// inside of a row is one run (the footprint is convex): its ends are found
// from the map, then settled by the exact position test.
void mask_row(const RenderJob& job, int y) {
    const CanvasSurface::TargetMap& map = job.target;
    const int right = job.left + job.columns;
    const double cy = y + 0.5;
    double from = job.left, to = right;
    narrow(map.ux, map.uy * cy + map.u0, -0.5, job.source_width - 0.5, from, to);
    narrow(map.vx, map.vy * cy + map.v0, -0.5, job.source_height - 0.5, from, to);
    int begin = job.left, end = job.left;
    if (from <= to) {
        begin = static_cast<int>(std::clamp(std::floor(from) - 1.0, static_cast<double>(job.left),
                                            static_cast<double>(right)));
        end = static_cast<int>(std::clamp(std::ceil(to) + 2.0, static_cast<double>(job.left),
                                          static_cast<double>(right)));
    }
    while (begin < end && !maps_inside(map, begin, y, job.source_width, job.source_height)) {
        ++begin;
    }
    while (end > begin && !maps_inside(map, end - 1, y, job.source_width, job.source_height)) {
        --end;
    }
    Color* const row = job.pixels + static_cast<std::size_t>(y) * job.stride;
    std::fill(row + job.left, row + begin, Color{0, 0, 0, 0});
    std::fill(row + end, row + right, Color{0, 0, 0, 0});
    for (int x = begin; x < end; ++x) {
        if (row[x].a == 0) {
            row[x] = Color{0, 0, 0, 0};
        }
    }
}
struct RenderWorker {
    RenderJob* job = nullptr;
    stxlib::conv::Workspace workspace;
    static void run(RenderWorker& worker) {
        RenderJob& job = *worker.job;
        for (;;) {
            if (job.failed.load(std::memory_order_relaxed)) {
                return;
            }
            const int chunk = job.next_chunk.fetch_add(1, std::memory_order_relaxed);
            const int first = chunk * rows_per_chunk;
            if (first >= job.rows) {
                return;
            }
            const int y0 = job.top + first;
            const int y1 = job.top + std::min(job.rows, first + rows_per_chunk);
            const stxlib::conv::Region region{static_cast<std::size_t>(job.left), static_cast<std::size_t>(y0),
                                              static_cast<std::size_t>(job.left + job.columns),
                                              static_cast<std::size_t>(y1)};
            const stxlib::OperationStatus status = stxlib::conv::transfer(
                job.field, job.map, stxlib::conv::Mode::preview, job.destination, region, worker.workspace);
            if (status.failed()) {
                job.failed.store(true, std::memory_order_relaxed);
                return;
            }
            for (int y = y0; y < y1; ++y) {
                mask_row(job, y);
            }
        }
    }
};
void fill_transparent(const CanvasSurface::Frame& frame, int left, int top, int columns, int rows) {
    for (int y = top; y < top + rows; ++y) {
        Color* const row = frame.pixels + static_cast<std::size_t>(y) * frame.stride;
        std::fill(row + left, row + left + columns, Color{0, 0, 0, 0});
    }
}
} // namespace

void CanvasSurface::clear() noexcept {
    field_ = stxlib::conv::Field{};
    width_ = 0;
    height_ = 0;
}
void CanvasSurface::build(const Image& source) {
    clear();
    if (source.width < 2 || source.height < 2 ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height) {
        throw std::invalid_argument("The CONV surface needs at least 2 x 2 pixels.");
    }
    if (source.pixels.size() > maximum_pixels || static_cast<std::size_t>(source.width) > stxlib::conv::max_side ||
        static_cast<std::size_t>(source.height) > stxlib::conv::max_side) {
        throw std::length_error("A CONV surface holds 4 megapixels at most; use a smaller picture or selection.");
    }
    stxlib::OperationResult<stxlib::conv::Field> made = stxlib::conv::Field::make(
        static_cast<std::size_t>(source.width), static_cast<std::size_t>(source.height), stxlib::conv::FieldOptions{});
    if (made.failed()) {
        throw std::runtime_error("CONV could not allocate the canvas field.");
    }
    stxlib::conv::Field field = std::move(made).take_value();
    if (field.compile(source_image(source)).failed()) {
        throw std::runtime_error("CONV rejected the canvas.");
    }
    field_ = std::move(field);
    width_ = source.width;
    height_ = source.height;
}
void CanvasSurface::update(const Image& source, Rect damage) {
    if (!ready() || source.width != width_ || source.height != height_ ||
        source.pixels.size() != static_cast<std::size_t>(width_) * height_) {
        throw std::invalid_argument("The CONV surface was built for another image size.");
    }
    const int left = std::max(0, damage.x), top = std::max(0, damage.y);
    const int right = std::min(width_, damage.x + damage.w), bottom = std::min(height_, damage.y + damage.h);
    if (right <= left || bottom <= top) {
        return;
    }
    const stxlib::conv::Region changed{static_cast<std::size_t>(left), static_cast<std::size_t>(top),
                                       static_cast<std::size_t>(right), static_cast<std::size_t>(bottom)};
    const stxlib::conv::Rgba8Image image = source_image(source);
    const stxlib::OperationResult<stxlib::conv::NodeBox> recompiled = field_.recompile(image, changed);
    if (!recompiled.failed()) {
        return;
    }
    // The field is not ready after a refused recompile: compile it whole.
    if (field_.compile(image).failed()) {
        clear();
        throw std::runtime_error("CONV rejected the canvas.");
    }
}
WarpSample CanvasSurface::sample_components(Point position) const {
    WarpSample result;
    if (!ready() || !std::isfinite(position.x) || !std::isfinite(position.y) || position.x < -0.5 ||
        position.y < -0.5 || position.x >= width_ - 0.5 || position.y >= height_ - 0.5) {
        return result;
    }
    std::array<double, stxlib::conv::channels> values{};
    const stxlib::OperationStatus status =
        field_.evaluate(position.x, position.y, stxlib::MutableView<double>(values.data(), values.size()));
    if (status.failed()) {
        return result;
    }
    result = WarpSample{values[0], values[1], values[2], values[3]};
    return result;
}
WarpSample CanvasSurface::sample_premultiplied(Point position) const {
    WarpSample value = sample_components(position);
    value.a = std::clamp(value.a, 0.0, 1.0);
    value.r = std::clamp(value.r, 0.0, value.a);
    value.g = std::clamp(value.g, 0.0, value.a);
    value.b = std::clamp(value.b, 0.0, value.a);
    return value;
}
Color CanvasSurface::sample(Point position) const {
    const Color color = encoded(sample_premultiplied(position));
    return color;
}
void CanvasSurface::render(const TargetMap& map, const Frame& frame, int left, int top, int columns,
                           int rows) const {
    // The area, clipped to the frame.
    const int x0 = std::max(0, left), y0 = std::max(0, top);
    const int x1 = std::min(frame.width, left + columns), y1 = std::min(frame.height, top + rows);
    if (frame.pixels == nullptr || x1 <= x0 || y1 <= y0) {
        return;
    }
    const bool finite = std::isfinite(map.ux) && std::isfinite(map.uy) && std::isfinite(map.u0) &&
                        std::isfinite(map.vx) && std::isfinite(map.vy) && std::isfinite(map.v0);
    if (!ready() || !finite) {
        fill_transparent(frame, x0, y0, x1 - x0, y1 - y0);
        return;
    }
    // The field's map reads destination pixel (x, y) at its integer
    // coordinates; TargetMap reads the pixel's centre (x + 0.5, y + 0.5).
    const stxlib::conv::AffineMap affine{map.ux, map.uy, map.vx, map.vy, map.u0 + 0.5 * (map.ux + map.uy),
                                         map.v0 + 0.5 * (map.vx + map.vy)};
    stxlib::u8* const bytes = reinterpret_cast<stxlib::u8*>(frame.pixels);
    const std::size_t frame_width = static_cast<std::size_t>(frame.width);
    const std::size_t frame_height = static_cast<std::size_t>(frame.height);
    RenderJob job{field_,
                  map,
                  affine,
                  stxlib::conv::MutableRgba8Image{
                      stxlib::MutableView<stxlib::u8>(bytes, frame.stride * frame_height * 4), frame_width,
                      frame_height, frame.stride * 4},
                  frame.pixels,
                  frame.stride,
                  x0,
                  y0,
                  x1 - x0,
                  y1 - y0,
                  width_,
                  height_};
    const int chunks = (job.rows + rows_per_chunk - 1) / rows_per_chunk;
    std::size_t count = std::min<std::size_t>(8, std::max(1u, std::thread::hardware_concurrency()));
    if (static_cast<std::size_t>(job.columns) * static_cast<std::size_t>(job.rows) < parallel_threshold) {
        count = 1;
    }
    count = std::max<std::size_t>(1, std::min(count, static_cast<std::size_t>(chunks)));
    std::vector<RenderWorker> workers(count);
    for (RenderWorker& worker : workers) {
        stxlib::OperationResult<stxlib::conv::Workspace> workspace = stxlib::conv::Workspace::make();
        if (workspace.failed()) {
            throw std::runtime_error("CONV could not allocate a workspace.");
        }
        worker.job = &job;
        worker.workspace = std::move(workspace).take_value();
    }
    if (count == 1) {
        RenderWorker::run(workers[0]);
    } else {
        std::vector<std::jthread> threads;
        threads.reserve(count);
        for (RenderWorker& worker : workers) {
            threads.emplace_back(RenderWorker::run, std::ref(worker));
        }
        for (std::jthread& thread : threads) {
            thread.join();
        }
    }
    if (job.failed.load(std::memory_order_relaxed)) {
        throw std::runtime_error("CONV rejected the view transfer.");
    }
}
} // namespace paint

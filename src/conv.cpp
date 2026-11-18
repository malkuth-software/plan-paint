#include "conv.hpp"
#include <stx_conv/conv.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

// CONV* is computed by stx_conv, the optimized library port of this file's
// former native resampler (same operator: six-source/five-current quintic
// construction, short-line closure, nodal basins and pixel-area footprints,
// binary64, axis order kept). Plan Paint keeps two things here: the
// premultiplied sRGB conversion around the numeric library, and the fan-out of
// independent lines over worker threads, which stx_conv leaves to its caller.
namespace paint {
namespace {
// Premultiplied RGBA: four independent lanes per node.
const std::size_t channels = 4;

// Per-worker stx_conv storage, allocated in the calling thread before any
// worker starts so an allocation failure throws there and not in a thread.
// stx_conv admits one compile workspace per concurrent compile.
struct WorkerStorage {
    stx_conv::Profile profile;
    stx_conv::LineWorkspace workspace;
};

// One axis pass over a width x height interleaved image: every row (across)
// or column (vertical) is resampled to `target` nodes by one shared,
// immutable plan. Workers claim lines from `next`; lines write disjoint nodes.
struct AxisJob {
    std::span<const double> input;
    std::span<double> output;
    const stx_conv::LinePlan& plan;
    std::size_t width = 0, height = 0, target = 0;
    bool vertical = false;
    std::atomic<std::size_t> next{0};
    std::atomic<int> failure{static_cast<int>(stx_conv::Status::ok)};
};
void axis_worker(AxisJob& job, WorkerStorage& storage) {
    const std::size_t lines = job.vertical ? job.width : job.height;
    const int ok = static_cast<int>(stx_conv::Status::ok);
    for (;;) {
        const std::size_t line = job.next.fetch_add(1, std::memory_order_relaxed);
        if (line >= lines || job.failure.load(std::memory_order_relaxed) != ok) {
            return;
        }
        // Columns step by whole rows; rows are contiguous nodes.
        const std::size_t source_offset = job.vertical ? line * channels : line * job.width * channels;
        const std::size_t source_stride = job.vertical ? job.width * channels : channels;
        const std::size_t target_offset = job.vertical ? line * channels : line * job.target * channels;
        const std::size_t target_stride = job.vertical ? job.width * channels : channels;
        const stx_conv::LineInput input{job.input.subspan(source_offset), source_stride};
        const stx_conv::LineOutput output{job.output.subspan(target_offset), target_stride};
        stx_conv::Status status = stx_conv::compile(input, storage.profile, storage.workspace);
        if (status == stx_conv::Status::ok) {
            status = stx_conv::evaluate(storage.profile, job.plan, output);
        }
        if (status != stx_conv::Status::ok) {
            job.failure.store(static_cast<int>(status), std::memory_order_relaxed);
            return;
        }
    }
}
const char* status_message(stx_conv::Status status) {
    if (status == stx_conv::Status::numerical_failure) {
        return "CONV signed-current projection did not admit this image.";
    }
    return "CONV rejected the resize request (invalid image storage).";
}
void resize_axis(const std::vector<double>& input, std::size_t width, std::size_t height, std::size_t target,
                 bool vertical, std::vector<double>& output, bool area) {
    const std::size_t length = vertical ? height : width;
    const std::size_t lines = vertical ? width : height;
    output.resize(lines * target * channels);
    const stx_conv::Sampling sampling = area ? stx_conv::Sampling::pixel_area : stx_conv::Sampling::nodal;
    const stx_conv::LinePlan plan(length, target, sampling);
    unsigned count = std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
    if (width * height < 65536) {
        count = 1;
    }
    std::vector<WorkerStorage> storage;
    storage.reserve(count);
    for (unsigned index = 0; index < count; ++index) {
        storage.push_back(
            WorkerStorage{stx_conv::Profile(length, channels), stx_conv::LineWorkspace(length, channels)});
    }
    AxisJob job{input, output, plan, width, height, target, vertical};
    if (count == 1) {
        axis_worker(job, storage[0]);
    } else {
        std::vector<std::jthread> workers;
        workers.reserve(count);
        for (unsigned index = 0; index < count; ++index) {
            workers.emplace_back(axis_worker, std::ref(job), std::ref(storage[index]));
        }
        for (std::jthread& worker : workers) {
            worker.join();
        }
    }
    const int failure = job.failure.load(std::memory_order_relaxed);
    if (failure != static_cast<int>(stx_conv::Status::ok)) {
        throw std::runtime_error(status_message(static_cast<stx_conv::Status>(failure)));
    }
}
void resize_image(const Image& source, int width, int height, Image& destination, bool area) {
    if (source.width < 1 || source.height < 1) {
        throw std::invalid_argument("Cannot resize an empty image.");
    }
    if (width < 1 || height < 1) {
        throw std::invalid_argument("Cannot resize to an empty image.");
    }
    if (width == source.width && height == source.height) {
        destination = source;
        return;
    }
    const std::size_t source_width = static_cast<std::size_t>(source.width);
    const std::size_t source_height = static_cast<std::size_t>(source.height);
    const std::size_t target_width = static_cast<std::size_t>(width);
    const std::size_t target_height = static_cast<std::size_t>(height);
    Image result;
    result.reset(width, height, {0, 0, 0, 0});
    std::vector<double> input(source.pixels.size() * channels);
    for (std::size_t i = 0; i < source.pixels.size(); ++i) {
        const Color color = source.pixels[i];
        const double alpha = color.a / 255.0;
        input[i * 4] = color.r / 255.0 * alpha;
        input[i * 4 + 1] = color.g / 255.0 * alpha;
        input[i * 4 + 2] = color.b / 255.0 * alpha;
        input[i * 4 + 3] = alpha;
    }
    // Axis order is part of the operator: vertical first when height shrinks.
    std::vector<double> middle, output;
    if (target_width == source_width) {
        resize_axis(input, source_width, source_height, target_height, true, output, area);
    } else if (target_height == source_height) {
        resize_axis(input, source_width, source_height, target_width, false, output, area);
    } else if (target_height < source_height) {
        resize_axis(input, source_width, source_height, target_height, true, middle, area);
        resize_axis(middle, source_width, target_height, target_width, false, output, area);
    } else {
        resize_axis(input, source_width, source_height, target_width, false, middle, area);
        resize_axis(middle, target_width, source_height, target_height, true, output, area);
    }
    // stx_conv may overshoot near sign transitions; the 8-bit format clamps here.
    for (std::size_t i = 0; i < result.pixels.size(); ++i) {
        const double alpha = std::clamp(output[i * 4 + 3], 0.0, 1.0);
        const double inverse = alpha > 1.0 / 65535.0 ? 1.0 / alpha : 0.0;
        const Color color{
            static_cast<std::uint8_t>(std::round(255.0 * std::clamp(output[i * 4] * inverse, 0.0, 1.0))),
            static_cast<std::uint8_t>(std::round(255.0 * std::clamp(output[i * 4 + 1] * inverse, 0.0, 1.0))),
            static_cast<std::uint8_t>(std::round(255.0 * std::clamp(output[i * 4 + 2] * inverse, 0.0, 1.0))),
            static_cast<std::uint8_t>(std::round(255.0 * alpha))};
        result.pixels[i] = color;
    }
    destination = std::move(result);
}
} // namespace
void conv_resize(const Image& source, int width, int height, Image& destination) {
    resize_image(source, width, height, destination, false);
}
void conv_resize_area(const Image& source, int width, int height, Image& destination) {
    resize_image(source, width, height, destination, true);
}
} // namespace paint

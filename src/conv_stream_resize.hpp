// Internal RGBA8 adapter for the public binary64 streaming factor.
#pragma once
#include "image.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <stx_conv/stream.hpp>
#include <thread>
#include <vector>
namespace paint::stream_resize {
using Plan = stx_conv::stream::LinePlan<double>;
using Stream = stx_conv::stream::LineStream<double>;
constexpr int group = 32;
constexpr int band = 64;
inline void decode(Color color, double* values) {
    double alpha = color.a / 255.0;
    values[0] = color.r / 255.0 * alpha;
    values[1] = color.g / 255.0 * alpha;
    values[2] = color.b / 255.0 * alpha;
    values[3] = alpha;
}
inline std::uint8_t byte(double value) {
    return static_cast<std::uint8_t>(std::round(255.0 * std::clamp(value, 0.0, 1.0)));
}
inline Color encode(const double* values) {
    double alpha = std::clamp(values[3], 0.0, 1.0);
    double inverse = alpha > 1.0 / 65535.0 ? 1.0 / alpha : 0.0;
    return {byte(values[0] * inverse), byte(values[1] * inverse), byte(values[2] * inverse), byte(alpha)};
}
struct Slice {
    Plan plan;
    int origin;
};
// Copy globally phased coefficients. Four extra nodes on each side preserve
// every jet, neighboring raw current and bubble read used by a retained cell.
inline Slice slice(const Plan& source, int first, int count) {
    const std::size_t start = source.offsets[first], end = source.offsets[first + count];
    int low = static_cast<int>(source.segments[start].anchor);
    int high = static_cast<int>(source.segments[end - 1].anchor);
    low = std::max(0, low - 4);
    high = std::min(static_cast<int>(source.source_length) - 1, high + 5);
    Slice result{{}, low};
    Plan& plan = result.plan;
    plan.source_length = high - low + 1;
    plan.target_length = count;
    plan.ring = std::min(source.ring, static_cast<std::size_t>(count) + 2);
    plan.bubble_active = source.bubble_active;
    for (int i = first; i <= first + count; ++i) {
        plan.offsets.push_back(source.offsets[i] - start);
    }
    plan.segments.assign(source.segments.begin() + start, source.segments.begin() + end);
    for (stx_conv::stream::Segment<double>& segment : plan.segments) {
        segment.cell = segment.currents ? segment.cell - low : 0;
        segment.anchor -= low;
    }
    return result;
}
struct Job {
    const Image& source;
    Image& result;
    const Plan& first;
    const Plan& second;
    bool bubble;
    bool vertical;
    bool two;
    int first_target;
    int first_count;
    int second_source;
    int tasks;
    std::atomic<int> next{0};
    std::mutex mutex;
    std::exception_ptr failure;
};
// Tasks own strips of the first target axis. The other axis advances in
// groups through the first factor; its finished values feed the second
// factor in node order. Source and output are the only image-sized rasters.
template <bool Vertical> void draw(Job& job, int task) {
    const Image& source = job.source;
    Image& result = job.result;
    const int orthogonal = static_cast<int>(job.second.source_length);
    const int begin = job.two ? task * band : 0;
    const int count = job.two ? std::min(band, job.first_count - begin) : job.first_count;
    Slice partial;
    const bool sliced =
        job.first_target + begin != 0 || static_cast<std::size_t>(count) != job.first.target_length;
    if (sliced) {
        partial = slice(job.first, job.first_target + begin, count);
    }
    const Plan& plan = sliced ? partial.plan : job.first;
    const int origin = sliced ? partial.origin : 0;
    const int first_line = job.two ? 0 : task * group;
    const int last_line = job.two ? orthogonal : std::min(first_line + group, orthogonal);
    Stream first(plan.source_length, group * 4, plan.ring);
    const int lanes = job.two ? count * 4 : 4;
    Stream second(job.second.source_length, lanes, job.two ? job.second.ring : 1);
    if (job.two && !second.begin(job.second, lanes, job.bubble)) {
        throw std::runtime_error("Invalid CONV second-factor workspace.");
    }
    std::vector<double> nodes(8 * group * 4);
    // One complete group is consumed before reuse. Eight additional rows
    // protect second-factor node pointers across successive fills.
    std::vector<double> middle((group + 8) * lanes);
    std::size_t written = 0;
    for (int line = first_line; line < last_line; line += group) {
        const int active = std::min(group, last_line - line);
        if (!first.begin(plan, active * 4, job.bubble)) {
            throw std::runtime_error("Invalid CONV first-factor workspace.");
        }
        std::size_t produced = 0;
        for (std::size_t i = 0; i < plan.source_length; ++i) {
            double* gathered = nodes.data() + (i % 8) * group * 4;
            const int node = static_cast<int>(i) + origin;
            for (int r = 0; r < active; ++r) {
                const int source_line = job.second_source + line + r;
                const std::size_t offset = Vertical ? std::size_t(node) * source.width + source_line
                                                    : std::size_t(source_line) * source.width + node;
                decode(source.pixels[offset], gathered + r * 4);
            }
            first.supply(gathered);
            while (first.advance()) {
                for (; produced < first.completed(); ++produced) {
                    const double* values = first.target(produced);
                    for (int r = 0; r < active; ++r) {
                        if (job.two) {
                            double* dest = middle.data() + ((line + r) % (group + 8)) * lanes + produced * 4;
                            std::copy_n(values + r * 4, 4, dest);
                        } else {
                            const std::size_t pixel = Vertical
                                                          ? produced * result.width + line + r
                                                          : std::size_t(line + r) * result.width + produced;
                            result.pixels[pixel] = encode(values + r * 4);
                        }
                    }
                }
            }
        }
        if (!first.finished() || first.failed() || produced != plan.target_length) {
            throw std::runtime_error("Incomplete first CONV factor.");
        }
        if (!job.two) {
            continue;
        }
        for (int r = 0; r < active; ++r) {
            second.supply(middle.data() + ((line + r) % (group + 8)) * lanes);
            while (second.advance()) {
                for (; written < second.completed(); ++written) {
                    const double* values = second.target(written);
                    for (int k = 0; k < count; ++k) {
                        const std::size_t pixel = Vertical ? std::size_t(begin + k) * result.width + written
                                                           : written * result.width + begin + k;
                        result.pixels[pixel] = encode(values + k * 4);
                    }
                }
            }
        }
    }
    if (job.two && (!second.finished() || second.failed() || written != job.second.target_length)) {
        throw std::runtime_error("Incomplete second CONV factor.");
    }
}
inline void worker(Job& job) {
    try {
        for (int task = job.next.fetch_add(1); task < job.tasks; task = job.next.fetch_add(1)) {
            if (job.vertical) {
                draw<true>(job, task);
            } else {
                draw<false>(job, task);
            }
        }
    } catch (...) {
        job.next.store(job.tasks);
        std::lock_guard<std::mutex> lock(job.mutex);
        job.failure = std::current_exception();
    }
}
inline void resize(const Image& source, Image& result, bool area, bool bubble, int width, int height,
                   Rect region) {
    const bool vertical = height < source.height || width == source.width;
    const bool two = source.width != width && source.height != height;
    const stx_conv::Sampling sampling = area ? stx_conv::Sampling::pixel_area : stx_conv::Sampling::nodal;
    const Plan first = stx_conv::stream::build_line_plan<double>(vertical ? source.height : source.width,
                                                                 vertical ? height : width, sampling);
    const int second_extent = vertical ? width : height;
    const int first_count = vertical ? region.h : region.w;
    const int second_start = vertical ? region.x : region.y;
    const int second_count = vertical ? region.w : region.h;
    Plan second;
    int second_source = second_start;
    if (two) {
        second = stx_conv::stream::build_line_plan<double>(vertical ? source.width : source.height,
                                                           second_extent, sampling);
        second_source = 0;
        if (second_count != second_extent) {
            Slice partial = slice(second, second_start, second_count);
            second_source = partial.origin;
            second = std::move(partial.plan);
        }
    } else {
        // This axis is copied; no plan or target ring is needed.
        second.source_length = second_count;
    }
    const int tasks = two ? (first_count + band - 1) / band : (second_count + group - 1) / group;
    Job job{source,      result,        first, second, bubble, vertical, two, vertical ? region.y : region.x,
            first_count, second_source, tasks, 0,      {},     {}};
    unsigned count =
        std::min({8u, std::max(1u, std::thread::hardware_concurrency()), static_cast<unsigned>(tasks)});
    if (source.pixels.size() < 65536) {
        count = 1;
    }
    std::vector<std::jthread> threads;
    for (unsigned i = 1; i < count; ++i) {
        threads.emplace_back(worker, std::ref(job));
    }
    worker(job);
    for (std::jthread& thread : threads) {
        thread.join();
    }
    if (job.failure) {
        std::rethrow_exception(job.failure);
    }
}
} // namespace paint::stream_resize

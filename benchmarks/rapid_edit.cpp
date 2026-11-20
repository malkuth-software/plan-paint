// One edit per process so the macOS high-water mark belongs to this candidate.
#include "conv.hpp"
#include "document.hpp"
#include "numeric_transform.hpp"
#include "warp_session.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#if defined(__APPLE__)
#include <sys/resource.h>
#endif
namespace {
using Clock = std::chrono::steady_clock;
paint::Image fixture(int width, int height) {
    paint::Image source;
    source.reset(width, height);
    std::uint32_t state = 1;
    for (paint::Color& c : source.pixels) {
        state = state * 1664525u + 1013904223u;
        c = {static_cast<std::uint8_t>(state >> 24), static_cast<std::uint8_t>(state >> 16),
             static_cast<std::uint8_t>(state >> 8), static_cast<std::uint8_t>(state)};
    }
    return source;
}
paint::WarpResult completed(paint::WarpWorker& worker) {
    paint::WarpResult result;
    worker.wait();
    if (!worker.take(result)) {
        throw std::runtime_error("Missing warp result");
    }
    if (!result.error.empty()) {
        throw std::runtime_error(result.error);
    }
    return result;
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr
            << "Usage: paint-rapid-edit "
               "resize|resize-ledger|mesh|mesh-rows|numeric|rotate|rotate-rows|view|view-rows source_width "
               "source_height\n";
        return 2;
    }
    const std::string mode = argv[1];
    const int sw = std::atoi(argv[2]), sh = std::atoi(argv[3]);
    constexpr int width = 2000, height = 2000;
    const bool rows = mode.ends_with("-rows");
    const paint::CanvasSurface::Storage storage =
        rows ? paint::CanvasSurface::Storage::admitted_rows : paint::CanvasSurface::Storage::point_tiles;
    paint::Document document;
    document.image = fixture(sw, sh);
    paint::Image output;
    const Clock::time_point begin = Clock::now();
    if (mode == "resize" || mode == "resize-ledger") {
        document.resize(width, height, true, mode == "resize");
        output = std::move(document.image);
    } else if (mode == "mesh" || mode == "mesh-rows") {
        paint::WarpWorker worker;
        paint::ReshapeMesh mesh = paint::make_reshape_mesh(document.image, 60);
        for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
            if (!mesh.nodes[i].boundary) {
                paint::Point moved = mesh.nodes[i].target;
                moved.x += 5.0;
                if (!paint::move_reshape_node(mesh, i, moved)) {
                    throw std::runtime_error("Rejected benchmark mesh");
                }
                break;
            }
        }
        worker.compile(paint::WarpTask::CompileSelection, document.image, 1, storage);
        paint::WarpResult prepared = completed(worker);
        worker.mesh(paint::WarpTask::CommitMesh, prepared.field, mesh, {0, 0, width, height}, 1,
                    paint::WarpSampling::Point);
        output = std::move(completed(worker).image);
    } else if (mode == "numeric") {
        paint::NumericTransformSettings settings;
        settings.angle = 0.23 * 180 / std::acos(-1.0);
        settings.sampling = paint::WarpSampling::Point;
        const paint::NumericTransformSource captured = paint::capture_numeric_transform(document);
        paint::apply_numeric_transform(document, captured,
                                       paint::render_numeric_transform(captured, settings));
        output = std::move(document.image);
    } else if (mode == "rotate" || mode == "rotate-rows") {
        paint::ConvWarpField field;
        field.compile(document.image, nullptr, storage);
        const double c = std::cos(0.23), s = std::sin(0.23), a = (width - 1) * 0.5;
        paint::render_affine(field, {c, -s, a * (1 - c + s), s, c, a * (1 - c - s)}, width, height, output,
                             paint::WarpSampling::Point);
    } else if (mode == "view" || mode == "view-rows") {
        paint::CanvasSurface field;
        field.build(document.image, storage);
        const double c = std::cos(0.23), s = std::sin(0.23), a = width * 0.5;
        const paint::CanvasSurface::TargetMap map{c, s, a * (1 - c - s) - 0.5, -s, c, a * (1 + s - c) - 0.5};
        output.reset(width, height);
        field.render(map, 0, 0, width, height, output.pixels.data(), width);
    } else {
        return 2;
    }
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    std::cout << mode << " " << sw << "x" << sh << " -> " << width << "x" << height << " ms " << ms;
#if defined(__APPLE__)
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::cout << " peak_bytes " << usage.ru_maxrss;
#endif
    std::uint64_t hash = 0;
    for (paint::Color c : output.pixels) {
        for (std::uint8_t v : {c.r, c.g, c.b, c.a}) {
            hash = hash * 131 + v;
        }
    }
    std::cout << " rgba_hash " << hash << '\n';
}

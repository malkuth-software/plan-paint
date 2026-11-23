// One-off comparison of Plan Paint's CONV paths: the stx_conv implementation
// they replaced (paint::legacy, legacy_*.cpp, Plan Paint 45d8442) against
// stxlib::conv (paint_core). Not a test and not run by CI (owner, 2026-11-22:
// run once on the M4 and report; the bar is new time as good as or better
// than old, with memory expected to be better). Build with
// -DPLAN_PAINT_CONV_COMPARE=ON and run
//
//     paint-conv-compare [repeats]
//
// Each workload runs in its own child process per implementation, so a peak
// is that implementation's alone: the peak resident set after the first call
// less the peak before it (inputs, and for a render its field, already
// built). Time is the median of the repeats after that first call.
#include "canvas_surface.hpp"
#include "conv.hpp"
#include "legacy_canvas_surface.hpp"
#include "legacy_conv.hpp"
#include "legacy_warp.hpp"
#include "warp.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#define PAINT_POPEN _popen
#define PAINT_PCLOSE _pclose
#else
#include <sys/resource.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#define PAINT_POPEN popen
#define PAINT_PCLOSE pclose
#endif

namespace {
using Clock = std::chrono::steady_clock;

// Peak resident set of this process so far, in bytes.
double peak_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
    return static_cast<double>(counters.PeakWorkingSetSize);
#else
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
    return static_cast<double>(usage.ru_maxrss);
#else
    return static_cast<double>(usage.ru_maxrss) * 1024.0;
#endif
#endif
}

#if defined(__APPLE__)
std::string sysctl_text(const char* name) {
    char buffer[256] = {};
    std::size_t size = sizeof(buffer);
    if (sysctlbyname(name, buffer, &size, nullptr, 0) != 0) {
        return "unknown";
    }
    return std::string(buffer);
}
#endif

std::string machine() {
    std::ostringstream text;
#if defined(__APPLE__)
    text << sysctl_text("machdep.cpu.brand_string") << " (" << sysctl_text("hw.model") << ", macOS)";
#elif defined(_WIN32)
    const char* identifier = std::getenv("PROCESSOR_IDENTIFIER");
    text << (identifier != nullptr ? identifier : "unknown") << " (Windows)";
#else
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line, model = "unknown";
    while (std::getline(cpuinfo, line)) {
        if (line.rfind("model name", 0) == 0 && line.find(':') != std::string::npos) {
            model = line.substr(line.find(':') + 2);
            break;
        }
    }
    text << model << " (Linux)";
#endif
    text << ", " << std::thread::hardware_concurrency() << " hardware threads";
#if defined(__clang__)
    text << ", clang " << __clang_major__ << '.' << __clang_minor__;
#elif defined(_MSC_VER)
    text << ", MSVC " << _MSC_VER;
#elif defined(__GNUC__)
    text << ", gcc " << __GNUC__ << '.' << __GNUC_MINOR__;
#endif
#if defined(NDEBUG)
    text << ", release";
#else
    text << ", debug build (timings not representative)";
#endif
    return text.str();
}

// Flat runs, ramps, hard edges and translucency (benchmarks/conv_benchmark.cpp's pattern).
paint::Image fixture(int width, int height) {
    paint::Image image;
    image.reset(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.set(x, y,
                      {static_cast<std::uint8_t>((x * 13 + y * 7) % 256),
                       static_cast<std::uint8_t>((x * 3 + y * 11) % 256),
                       static_cast<std::uint8_t>((x / 11 + y / 17) % 2 * 255),
                       static_cast<std::uint8_t>(128 + (x + 3 * y) % 128)});
        }
    }
    return image;
}

// The two implementations behind one shape. Their types share names and
// fields but live in different namespaces.
struct NewConv {
    using WarpField = paint::ConvWarpField;
    using Surface = paint::CanvasSurface;
    using Map = paint::AffineMap;
    using Mesh = paint::ReshapeMesh;
    static void resize(const paint::Image& source, int width, int height, paint::Image& output) {
        paint::conv_resize(source, width, height, output);
    }
    static void resize_area(const paint::Image& source, int width, int height, paint::Image& output) {
        paint::conv_resize_area(source, width, height, output);
    }
    static paint::Rect bounds(const WarpField& field, const Map& map) {
        return paint::affine_bounds(field, map);
    }
    static void affine(const WarpField& field, const Map& map, int width, int height, paint::Image& output,
                       bool area) {
        paint::render_affine(field, map, width, height, output,
                             area ? paint::WarpSampling::Area : paint::WarpSampling::Minification);
    }
    static Mesh mesh(const paint::Image& source, double spacing) {
        return paint::make_reshape_mesh(source, spacing);
    }
    static bool move(Mesh& mesh, std::size_t node, paint::Point target) {
        return paint::move_reshape_node(mesh, node, target);
    }
    static void render_mesh(const WarpField& field, const Mesh& mesh, int width, int height, paint::Image& output) {
        paint::render_mesh(field, mesh, width, height, output, paint::WarpSampling::Minification);
    }
    static void view(const Surface& surface, const Surface::TargetMap& map, paint::Image& frame) {
        const Surface::Frame target{frame.pixels.data(), frame.width, frame.height,
                                    static_cast<std::size_t>(frame.width)};
        surface.render(map, target, 0, 0, frame.width, frame.height);
    }
};
struct OldConv {
    using WarpField = paint::legacy::ConvWarpField;
    using Surface = paint::legacy::CanvasSurface;
    using Map = paint::legacy::AffineMap;
    using Mesh = paint::legacy::ReshapeMesh;
    static void resize(const paint::Image& source, int width, int height, paint::Image& output) {
        paint::legacy::conv_resize(source, width, height, output);
    }
    static void resize_area(const paint::Image& source, int width, int height, paint::Image& output) {
        paint::legacy::conv_resize_area(source, width, height, output);
    }
    static paint::Rect bounds(const WarpField& field, const Map& map) {
        return paint::legacy::affine_bounds(field, map);
    }
    static void affine(const WarpField& field, const Map& map, int width, int height, paint::Image& output,
                       bool area) {
        paint::legacy::render_affine(
            field, map, width, height, output,
            area ? paint::legacy::WarpSampling::Area : paint::legacy::WarpSampling::Minification);
    }
    static Mesh mesh(const paint::Image& source, double spacing) {
        return paint::legacy::make_reshape_mesh(source, spacing);
    }
    static bool move(Mesh& mesh, std::size_t node, paint::Point target) {
        return paint::legacy::move_reshape_node(mesh, node, target);
    }
    static void render_mesh(const WarpField& field, const Mesh& mesh, int width, int height, paint::Image& output) {
        paint::legacy::render_mesh(field, mesh, width, height, output, paint::legacy::WarpSampling::Minification);
    }
    static void view(const Surface& surface, const Surface::TargetMap& map, paint::Image& frame) {
        surface.render(map, 0, 0, frame.width, frame.height, frame.pixels.data(),
                       static_cast<std::size_t>(frame.width));
    }
};

struct Workload {
    const char* name;
    const char* description;
};
const Workload workloads[] = {
    {"resize_preview", "conv_resize 2500x1600 -> 2000x2000"},
    {"resize_down_4096", "conv_resize 4096x4096 -> 1024x1024"},
    {"resize_area_4096", "conv_resize_area 4096x4096 -> 1024x1024"},
    {"view_build", "rotated view: CanvasSurface::build 2000x2000"},
    {"view_render", "rotated view: render 2000x2000 frame at 17 degrees"},
    {"warp_compile", "transform: ConvWarpField::compile 1600x1600"},
    {"warp_rotate", "transform: render_affine 1600x1600 at 17 degrees, Minification"},
    {"warp_rotate_area", "transform: render_affine 1600x1600 at 17 degrees, Area"},
    {"mesh_warp", "reshape: render_mesh 1600x1600, spacing 48, one node moved"},
};
constexpr int workload_count = static_cast<int>(sizeof(workloads) / sizeof(workloads[0]));
constexpr double degrees_17 = 17.0 * std::numbers::pi / 180.0;

struct Measurement {
    double median_ms = 0.0;
    double peak_mb = 0.0;
};

// Times `operation` (a functor with operator()()): one first call, whose
// peak above `baseline` is the memory figure, then `repeats` timed calls.
template <class Operation>
Measurement measure(Operation& operation, int repeats) {
    Measurement result;
    const double baseline = peak_bytes();
    operation();
    result.peak_mb = std::max(0.0, peak_bytes() - baseline) / (1024.0 * 1024.0);
    std::vector<double> timings;
    for (int i = 0; i < repeats; ++i) {
        const Clock::time_point start = Clock::now();
        operation();
        timings.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    std::sort(timings.begin(), timings.end());
    result.median_ms = timings[timings.size() / 2];
    return result;
}

template <class Impl>
struct ResizeOperation {
    const paint::Image& source;
    int width, height;
    bool area;
    paint::Image output;
    void operator()() {
        if (area) {
            Impl::resize_area(source, width, height, output);
        } else {
            Impl::resize(source, width, height, output);
        }
    }
};
template <class Impl>
struct ViewBuildOperation {
    const paint::Image& source;
    typename Impl::Surface surface;
    void operator()() {
        surface.build(source);
    }
};
template <class Impl>
struct ViewRenderOperation {
    const typename Impl::Surface& surface;
    typename Impl::Surface::TargetMap map;
    paint::Image frame;
    void operator()() {
        Impl::view(surface, map, frame);
    }
};
template <class Impl>
struct WarpCompileOperation {
    const paint::Image& source;
    typename Impl::WarpField field;
    void operator()() {
        field.compile(source);
    }
};
template <class Impl>
struct WarpRotateOperation {
    const typename Impl::WarpField& field;
    typename Impl::Map map;
    int width, height;
    bool area;
    paint::Image output;
    void operator()() {
        Impl::affine(field, map, width, height, output, area);
    }
};
template <class Impl>
struct MeshOperation {
    const typename Impl::WarpField& field;
    const typename Impl::Mesh& mesh;
    int width, height;
    paint::Image output;
    void operator()() {
        Impl::render_mesh(field, mesh, width, height, output);
    }
};

template <class Impl>
Measurement run(int index, int repeats) {
    const std::string name = workloads[index].name;
    if (name == "resize_preview" || name == "resize_down_4096" || name == "resize_area_4096") {
        const bool big = name != "resize_preview";
        const paint::Image source = big ? fixture(4096, 4096) : fixture(2500, 1600);
        ResizeOperation<Impl> operation{source, big ? 1024 : 2000, big ? 1024 : 2000, name == "resize_area_4096", {}};
        return measure(operation, repeats);
    }
    if (name == "view_build") {
        const paint::Image source = fixture(2000, 2000);
        ViewBuildOperation<Impl> operation{source, {}};
        return measure(operation, repeats);
    }
    if (name == "view_render") {
        const paint::Image source = fixture(2000, 2000);
        typename Impl::Surface surface;
        surface.build(source);
        const double c = std::cos(degrees_17), s = std::sin(degrees_17);
        const double half_w = source.width / 2.0, half_h = source.height / 2.0;
        typename Impl::Surface::TargetMap map;
        map.ux = c;
        map.uy = s;
        map.u0 = -c * half_w - s * half_h + (source.width - 1) / 2.0;
        map.vx = -s;
        map.vy = c;
        map.v0 = s * half_w - c * half_h + (source.height - 1) / 2.0;
        ViewRenderOperation<Impl> operation{surface, map, {}};
        operation.frame.reset(source.width, source.height);
        return measure(operation, repeats);
    }
    const paint::Image source = fixture(1600, 1600);
    if (name == "warp_compile") {
        WarpCompileOperation<Impl> operation{source, {}};
        return measure(operation, repeats);
    }
    typename Impl::WarpField field;
    field.compile(source);
    if (name == "warp_rotate" || name == "warp_rotate_area") {
        const double c = std::cos(degrees_17), s = std::sin(degrees_17);
        typename Impl::Map map;
        map.xx = c;
        map.xy = -s;
        map.yx = s;
        map.yy = c;
        const paint::Rect bounds = Impl::bounds(field, map);
        map.tx -= bounds.x;
        map.ty -= bounds.y;
        WarpRotateOperation<Impl> operation{field, map, bounds.w, bounds.h, name == "warp_rotate_area", {}};
        return measure(operation, repeats);
    }
    typename Impl::Mesh mesh = Impl::mesh(source, 48.0);
    // Move the interior node nearest the centre.
    std::size_t chosen = mesh.nodes.size();
    double best = 1e300;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        const double dx = mesh.nodes[i].source.x - source.width / 2.0;
        const double dy = mesh.nodes[i].source.y - source.height / 2.0;
        if (!mesh.nodes[i].boundary && dx * dx + dy * dy < best) {
            best = dx * dx + dy * dy;
            chosen = i;
        }
    }
    if (chosen == mesh.nodes.size() ||
        !Impl::move(mesh, chosen, {mesh.nodes[chosen].target.x + 19.0, mesh.nodes[chosen].target.y - 13.0})) {
        throw std::runtime_error("the mesh node could not be moved");
    }
    MeshOperation<Impl> operation{field, mesh, source.width, source.height, {}};
    return measure(operation, repeats);
}

int child(const char* workload, const char* implementation, int repeats) {
    int index = -1;
    for (int i = 0; i < workload_count; ++i) {
        if (std::strcmp(workloads[i].name, workload) == 0) {
            index = i;
        }
    }
    if (index < 0) {
        std::cerr << "unknown workload " << workload << '\n';
        return 2;
    }
    const bool legacy = std::strcmp(implementation, "old") == 0;
    const Measurement result = legacy ? run<OldConv>(index, repeats) : run<NewConv>(index, repeats);
    std::cout << std::setprecision(17) << result.median_ms << ' ' << result.peak_mb << '\n';
    return 0;
}

bool spawn(const std::string& program, const char* workload, const char* implementation, int repeats,
           Measurement& result) {
    std::ostringstream command;
    command << '"' << program << "\" --child " << workload << ' ' << implementation << ' ' << repeats;
    std::FILE* pipe = PAINT_POPEN(command.str().c_str(), "r");
    if (pipe == nullptr) {
        return false;
    }
    char line[256] = {};
    const bool read = std::fgets(line, sizeof(line), pipe) != nullptr;
    const int status = PAINT_PCLOSE(pipe);
    if (!read || status != 0) {
        return false;
    }
    std::istringstream values(line);
    values >> result.median_ms >> result.peak_mb;
    return !values.fail();
}

std::string ratio(double fresh, double old) {
    if (old <= 0.0) {
        return "-";
    }
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << fresh / old;
    return text.str();
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::strcmp(argv[1], "--child") == 0) {
            return child(argv[2], argv[3], std::clamp(std::atoi(argv[4]), 1, 99));
        }
        const int repeats = argc > 1 ? std::clamp(std::atoi(argv[1]), 1, 99) : 5;
        const std::string program = argv[0];
        std::cout << "Plan Paint CONV: stx_conv (old, Plan Paint 45d8442) against stxlib::conv (new)\n"
                  << "Machine: " << machine() << '\n'
                  << "Median of " << repeats << " runs after a first call; peak is resident memory above the "
                  << "inputs, per child process.\n\n";
        std::cout << std::left << std::setw(18) << "workload" << std::right << std::setw(11) << "old ms"
                  << std::setw(11) << "new ms" << std::setw(9) << "new/old" << std::setw(11) << "old MB"
                  << std::setw(11) << "new MB" << std::setw(9) << "new/old" << "  time\n";
        bool all_good = true;
        for (const Workload& workload : workloads) {
            Measurement old, fresh;
            const bool measured = spawn(program, workload.name, "old", repeats, old) &&
                                  spawn(program, workload.name, "new", repeats, fresh);
            std::cout << std::left << std::setw(18) << workload.name << std::right << std::fixed
                      << std::setprecision(1);
            if (!measured) {
                std::cout << "  failed (run: " << program << " --child " << workload.name << " old|new 1)\n";
                all_good = false;
                continue;
            }
            const bool good = fresh.median_ms <= old.median_ms;
            all_good = all_good && good;
            std::cout << std::setw(11) << old.median_ms << std::setw(11) << fresh.median_ms << std::setw(9)
                      << ratio(fresh.median_ms, old.median_ms) << std::setw(11) << old.peak_mb << std::setw(11)
                      << fresh.peak_mb << std::setw(9) << ratio(fresh.peak_mb, old.peak_mb) << "  "
                      << (good ? "as good or better" : "SLOWER") << '\n';
        }
        std::cout << '\n';
        for (const Workload& workload : workloads) {
            std::cout << std::left << std::setw(18) << workload.name << workload.description << '\n';
        }
        std::cout << "\nBar (owner): new time as good as or better than old on every row; memory expected lower.\n"
                  << (all_good ? "Every row meets the time bar.\n" : "At least one row misses the time bar.\n");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

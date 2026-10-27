#include "codecs.hpp"
#include "fixtures/icon_reference.hpp"
#include "icon_batch.hpp"
#include "import_codecs.hpp"
#include "paths.hpp"
#include "safe_file.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
struct Directory {
    std::filesystem::path path;
    Directory()
        : path(std::filesystem::temp_directory_path() /
               ("paint-icon-batch-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        require(std::filesystem::create_directory(path), "isolated directory");
    }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void cancel_after_one(const paint::IconBatchPlan& plan, void* context) {
    if (plan.entries.front().state == paint::IconExportState::Completed) {
        (*static_cast<std::atomic<bool>*>(context)).store(true);
    }
}
void new_output_folder_contracts() {
    Directory root;
    const std::filesystem::path source_path = root.path / "source.png";
    paint::Image image;
    image.reset(20, 12, {40, 120, 200, 255});
    paint::save_image(image, paint::path_to_utf8(source_path));
    const std::vector<std::string> sources{paint::path_to_utf8(source_path)};
    paint::IconBatchSettings settings;
    settings.sizes = {16, 32};
    std::atomic<bool> cancel{false};
    paint::IconBatchPlan plan = paint::plan_icon_batch(sources, paint::path_to_utf8(root.path), settings, cancel);
    require(plan.create_directory && plan.directory.filename() == "Plan Paint output" &&
                !std::filesystem::exists(plan.directory) && paint::icon_batch_ready(plan),
            "default review reserves a new child name without creating a folder");
    require(paint::icon_batch_report(plan).find("\"createDirectory\":true") != std::string::npos,
            "report records creation of the reviewed folder");
    cancel.store(true);
    paint::run_icon_batch(plan, cancel);
    require(plan.entries[0].state == paint::IconExportState::Cancelled && !std::filesystem::exists(plan.directory),
            "cancelling before publication creates no folder");
    cancel.store(false);
    plan = paint::plan_icon_batch(sources, paint::path_to_utf8(root.path), settings, cancel);
    std::filesystem::create_directory(plan.directory);
    paint::run_icon_batch(plan, cancel);
    require(plan.entries[0].state == paint::IconExportState::Failed && std::filesystem::is_empty(plan.directory),
            "a newly occupied folder is not silently adopted even when empty");
    plan = paint::plan_icon_batch(sources, paint::path_to_utf8(root.path), settings, cancel);
    require(plan.directory.filename() == "Plan Paint output 2" && !std::filesystem::exists(plan.directory),
            "fresh review chooses and discloses the next available child name");
    paint::run_icon_batch(plan, cancel);
    require(plan.entries[0].state == paint::IconExportState::Completed &&
                std::filesystem::is_regular_file(plan.directory / plan.entries[0].filename),
            "execution creates the reviewed folder and publishes the icon inside it");
    const paint::ImageContainer output = paint::load_container(paint::path_to_utf8(plan.directory / plan.entries[0].filename));
    require(output.frames.size() == 2 && output.frames[0].image.width == 16,
            "new-folder mode retains ordinary generation behavior");
    plan = paint::plan_icon_batch(sources, paint::path_to_utf8(root.path), settings, cancel);
    paint::write_file_atomic({'x'}, paint::path_to_utf8(source_path), "changed source fixture");
    paint::run_icon_batch(plan, cancel);
    require(plan.entries[0].state == paint::IconExportState::Failed && !std::filesystem::exists(plan.directory),
            "a stale source fails without leaving an empty output folder");
}
void svg_batch_contracts() {
    Directory root;
    const std::filesystem::path output = root.path / "out";
    std::filesystem::create_directory(output);
    const std::string path = paint::path_to_utf8(root.path / "vector.svg");
    const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="24.5" height="12.25" viewBox="0 0 24.5 12.25"><path d="M0.2 0.1L23.9 11.8M12 0V12" stroke="#e04080" stroke-width="0.7"/><circle cx="17.3" cy="4.2" r="2.7" fill="#20b0d0"/></svg>)";
    const std::vector<std::uint8_t> bytes(svg.begin(), svg.end());
    paint::write_file_atomic(bytes, path, "SVG fixture");
    const paint::ImageContainer captured = paint::load_container(path);
    std::filesystem::remove(paint::path_from_utf8(path));
    require(paint::generate_icon_target(captured, {0, -1, 48, 24, 32}, false).image.width == 48,
            "captured SVG generation does not reread a deleted source path");
    paint::write_file_atomic(bytes, path, "SVG fixture");
    paint::IconBatchSettings settings;
    settings.new_directory = false;
    settings.sizes = {16, 32, 48};
    std::atomic<bool> cancel{false};
    paint::IconBatchPlan plan = paint::plan_icon_batch({path}, paint::path_to_utf8(output), settings, cancel);
    require(paint::icon_batch_ready(plan) && plan.entries[0].summary.find("SVG rasterized independently") != std::string::npos &&
                plan.entries[0].summary.find("UPSCALING") == std::string::npos && std::filesystem::is_empty(output),
            "vector review identifies independent rasterization without a bitmap upscaling warning");
    paint::run_icon_batch(plan, cancel);
    require(plan.entries[0].state == paint::IconExportState::Completed, "SVG conversion publishes its complete family");
    const paint::ImageContainer actual = paint::load_container(paint::path_to_utf8(output / plan.entries[0].filename));
    for (std::size_t index = 0; index < settings.sizes.size(); ++index) {
        const int size = settings.sizes[index];
        const paint::Image direct = paint::rasterize_svg(path, size, size / 2);
        const paint::Image& image = actual.frames[index].image;
        require(image.width == size && image.height == size && image.get(0, 0).a == 0,
                "batch centers vector artwork on a transparent square");
        for (int y = 0; y < direct.height; ++y) {
            for (int x = 0; x < direct.width; ++x) {
                require(paint::equal(image.get(x, y + size / 4), direct.get(x, y)),
                        "each size must rasterize at its own resolution using the fractional intrinsic aspect ratio");
            }
        }
    }
    settings.collision = paint::IconExportCollision::Rename;
    plan = paint::plan_icon_batch({path}, paint::path_to_utf8(output), settings, cancel);
    paint::write_file_atomic({'x'}, path, "changed SVG");
    paint::run_icon_batch(plan, cancel);
    require(plan.entries[0].state == paint::IconExportState::Failed &&
                !std::filesystem::exists(output / plan.entries[0].filename),
            "a changed vector source cannot publish an unreviewed batch output");
}
void shared_palette_batch_contracts() {
    Directory root;
    const std::filesystem::path output = root.path / "out";
    std::filesystem::create_directory(output);
    const std::string path = paint::path_to_utf8(root.path / "shared.gpl");
    const std::vector<paint::Color> colors{{220, 40, 20, 255}, {0, 0, 0, 255}, {20, 40, 220, 255}};
    const std::string text = paint::encode_icon_palette(colors);
    paint::write_file_atomic(std::vector<std::uint8_t>(text.begin(), text.end()), path, "palette fixture");
    paint::IconFrame first, second;
    first.image.reset(16, 16, {210, 45, 25, 255});
    first.image.set(0, 0, {90, 80, 70, 0});
    second.image.reset(32, 32, {25, 45, 210, 255});
    second.hotspot_x = 9;
    const std::string source = paint::path_to_utf8(root.path / "cursor.cur");
    paint::write_file_atomic(paint::encode_icon_container({paint::ContainerKind::Cursor, {first, second}}),
                             source, "cursor fixture");
    paint::IconBatchSettings settings;
    settings.new_directory = false;
    settings.operation = paint::IconBatchOperation::Palette;
    settings.palette_path = path;
    settings.palette.bits_per_pixel = 4;
    std::atomic<bool> cancel{false};
    paint::IconBatchPlan plan =
        paint::plan_icon_batch({source, source}, paint::path_to_utf8(output), settings, cancel);
    require(paint::icon_batch_ready(plan) && std::filesystem::is_empty(output),
            "shared palette review writes nothing");
    paint::write_file_atomic({'c', 'h', 'a', 'n', 'g', 'e', 'd'}, path, "changed palette fixture");
    paint::run_icon_batch(plan, cancel);
    for (const paint::IconBatchEntry& entry : plan.entries) {
        require(entry.state == paint::IconExportState::Completed, "shared batch publishes captured palette");
        const paint::ImageContainer result =
            paint::load_container(paint::path_to_utf8(output / entry.filename));
        require(result.kind == paint::ContainerKind::Cursor && result.frames.size() == 2 &&
                    result.frames[1].hotspot_x == 9,
                "shared conversion retains representations and hotspots");
        for (const paint::IconFrame& frame : result.frames) {
            require(
                paint::has_icon_palette(frame) && (*frame.source).format.bits_per_pixel == 4 &&
                    std::equal(colors.begin(), colors.end(), (*frame.source).palette.begin(), paint::equal),
                "every output representation uses the same exact ordered palette");
        }
        require((*result.frames[0].source).indices[0] == 1 && result.frames[0].image.get(0, 0).a == 0,
                "transparent mask uses retained shared black index");
    }
    const std::string report = paint::icon_batch_report(plan);
    require(report.find("shared-palette") != std::string::npos &&
                report.find("#DC2814") != std::string::npos &&
                report.find("alphaThreshold") != std::string::npos,
            "report records exact palette and conversion policy");
}
void batch_contracts() {
    Directory root;
    const std::filesystem::path output = root.path / "out";
    std::filesystem::create_directory(output);
    const std::string folder = paint::path_to_utf8(output);
    const std::string picture = paint::path_to_utf8(root.path / "wide.png");
    paint::Image image;
    image.reset(40, 20, {30, 90, 170, 255});
    paint::save_image(image, picture);
    std::atomic<bool> cancel{false};
    paint::IconBatchSettings settings;
    settings.new_directory = false;
    settings.sizes = {16, 32};
    paint::IconBatchPlan job = paint::plan_icon_batch({picture}, folder, settings, cancel);
    require(paint::icon_batch_ready(job) && std::filesystem::is_empty(output), "review is write-free");
    require(job.entries[0].summary.find("padding") != std::string::npos, "aspect fit disclosed");
    paint::run_icon_batch(job, cancel);
    require(job.entries[0].state == paint::IconExportState::Completed, "conversion completes");
    const std::string icon = paint::path_to_utf8(output / job.entries[0].filename);
    paint::ImageContainer converted = paint::load_container(icon);
    require(converted.frames.size() == 2 && converted.frames[0].image.width == 16 &&
                converted.frames[0].image.get(8, 0).a == 0 && converted.frames[0].image.get(8, 8).a == 255,
            "conversion fits the rectangle without stretching");
    settings.operation = paint::IconBatchOperation::Normalize;
    settings.sizes = {16, 48};
    job = paint::plan_icon_batch({icon}, folder, settings, cancel);
    require(job.entries[0].summary.find("2 kept, 1 added") != std::string::npos, "add mode preserves sizes");
    paint::run_icon_batch(job, cancel);
    require(paint::load_container(paint::path_to_utf8(output / job.entries[0].filename)).frames.size() == 3,
            "normalization adds missing size");
    settings.sizes_policy = paint::IconSizePolicy::ExactProfile;
    settings.collision = paint::IconExportCollision::Rename;
    job = paint::plan_icon_batch({icon}, folder, settings, cancel);
    require(job.entries[0].summary.find("1 removed") != std::string::npos, "exact removals disclosed");
    paint::run_icon_batch(job, cancel);
    require(paint::load_container(paint::path_to_utf8(output / job.entries[0].filename)).frames.size() == 2,
            "exact profile removes unrequested size");
    settings.operation = paint::IconBatchOperation::States;
    job = paint::plan_icon_batch({icon}, folder, settings, cancel);
    paint::run_icon_batch(job, cancel);
    const paint::ImageContainer disabled =
        paint::load_container(paint::path_to_utf8(output / job.entries[0].filename));
    const paint::Color state = disabled.frames[0].image.get(8, 8);
    require(state.r == state.g && state.g == state.b && state.a == 140,
            "state batch applies existing recipe");
    settings.operation = paint::IconBatchOperation::Representations;
    job = paint::plan_icon_batch({icon, icon}, folder, settings, cancel);
    require(job.entries.size() == 4 && job.entries[0].filename != job.entries[2].filename,
            "duplicate inputs and representation ordinals retain distinct names");
    paint::run_icon_batch(job, cancel, cancel_after_one, &cancel);
    require(job.entries[0].state == paint::IconExportState::Completed &&
                job.entries[1].state == paint::IconExportState::Cancelled &&
                !std::filesystem::exists(output / job.entries[1].filename),
            "cancellation preserves completed output only");
    cancel.store(false);
    job = paint::plan_icon_batch({icon}, folder, settings, cancel);
    const std::filesystem::path occupied = output / job.entries[0].filename;
    paint::write_file_atomic({'s', 'a', 'f', 'e'}, paint::path_to_utf8(occupied), "sentinel");
    paint::run_icon_batch(job, cancel);
    require(job.entries[0].state == paint::IconExportState::Failed &&
                paint::read_image_bytes(paint::path_to_utf8(occupied)) ==
                    std::vector<std::uint8_t>({'s', 'a', 'f', 'e'}),
            "late collision cannot replace reviewed renamed destination");
    settings.operation = paint::IconBatchOperation::Convert;
    job = paint::plan_icon_batch({picture}, folder, settings, cancel);
    image.reset(42, 22, {40, 80, 120, 255});
    paint::save_image(image, picture);
    paint::run_icon_batch(job, cancel);
    require(job.entries[0].state == paint::IconExportState::Failed &&
                job.entries[0].diagnostic.find("Source changed") != std::string::npos,
            "stale source fails");
    job = paint::plan_icon_batch({picture, paint::path_to_utf8(root.path / "missing.ico")}, folder, settings,
                                 cancel);
    require(!paint::icon_batch_ready(job) && job.entries.size() == 2 &&
                job.entries[1].state == paint::IconExportState::Failed,
            "missing input stays visible and blocks Run");
    settings.sizes.clear();
    job = paint::plan_icon_batch({picture}, folder, settings, cancel);
    require(!paint::icon_batch_ready(job), "empty target profile rejected");
    settings.sizes = {16};
    settings.collision = paint::IconExportCollision::Replace;
    const std::filesystem::path alias_folder = root.path / "aliases";
    std::filesystem::create_directory(alias_folder);
    job = paint::plan_icon_batch({picture}, paint::path_to_utf8(alias_folder), settings, cancel);
    const std::filesystem::path alias = alias_folder / job.entries[0].filename;
    std::error_code error;
    std::filesystem::create_hard_link(paint::path_from_utf8(picture), alias, error);
    require(!error, "hardlink test setup");
    job = paint::plan_icon_batch({picture}, paint::path_to_utf8(alias_folder), settings, cancel);
    require(!paint::icon_batch_ready(job) && job.entries[0].diagnostic.find("source") != std::string::npos,
            "output alias cannot overwrite input");
    require(paint::icon_batch_report(job).find("\"Failed\"") != std::string::npos, "report retains failure");
}
void cursor_and_report_contracts() {
    Directory root;
    const std::filesystem::path output = root.path / "out";
    std::filesystem::create_directory(output);
    paint::IconFrame frame;
    frame.image.reset(20, 10, {80, 120, 200, 255});
    frame.hotspot_x = 19;
    frame.hotspot_y = 9;
    const std::string source = paint::path_to_utf8(root.path / "cursor.cur");
    paint::save_container({paint::ContainerKind::Cursor, {frame, frame}}, source);
    const paint::ImageContainer original = paint::load_container(source);
    std::atomic<bool> cancel{false};
    paint::IconBatchSettings settings;
    settings.new_directory = false;
    settings.operation = paint::IconBatchOperation::Normalize;
    settings.sizes = {16};
    paint::IconBatchPlan job =
        paint::plan_icon_batch({source}, paint::path_to_utf8(output), settings, cancel);
    paint::run_icon_batch(job, cancel);
    require(job.entries[0].state == paint::IconExportState::Completed, "cursor normalization completes");
    const paint::ImageContainer result =
        paint::load_container(paint::path_to_utf8(output / job.entries[0].filename));
    require(result.kind == paint::ContainerKind::Cursor && result.frames.size() == 3,
            "normalization retains duplicate cursor images");
    require((*result.frames[0].source).payload == (*original.frames[0].source).payload &&
                (*result.frames[1].source).payload == (*original.frames[1].source).payload,
            "untouched duplicate images keep encoded payloads");
    require(result.frames[2].hotspot_x == 15 && result.frames[2].hotspot_y == 11 &&
                result.frames[2].image.get(0, 0).a == 0,
            "cursor hotspot follows aspect fitting and centered padding");
    const std::string report = paint::icon_batch_report(job);
    require(report.find("\"operation\":\"normalize\"") != std::string::npos &&
                report.find("\"sizes\":[16]") != std::string::npos &&
                report.find("\"sizePolicy\":\"add-missing\"") != std::string::npos &&
                report.find("\"opacityPercent\":55") != std::string::npos &&
                report.find("\"representationOrdinal\":null") != std::string::npos,
            "report records the operation, exact profile, recipe and image identity");
}
void stamp_contracts() {
    paint::Image source, stamp;
    source.reset(3, 3, {0, 0, 255, 128});
    source.set(0, 0, {7, 8, 9, 0});
    stamp.reset(1, 1, {255, 0, 0, 128});
    paint::IconStampSettings settings;
    paint::IconStampResult front = paint::compose_icon_stamp(source, stamp, settings);
    require(front.placement.x == 1 && front.placement.y == 1 &&
                paint::equal(front.image.get(1, 1), {170, 0, 85, 192}),
            "centered front stamp uses Paint source-over");
    settings.behind = true;
    const paint::IconStampResult behind = paint::compose_icon_stamp(source, stamp, settings);
    require(paint::equal(behind.image.get(1, 1), {85, 0, 170, 192}), "behind order reverses source-over");
    settings.opacity = 0;
    const paint::IconStampResult empty = paint::compose_icon_stamp(source, stamp, settings);
    for (std::size_t index = 0; index < source.pixels.size(); ++index) {
        require(paint::equal(empty.image.pixels[index], source.pixels[index]),
                "zero opacity preserves hidden RGB and alpha");
    }
    settings.opacity = 100;
    settings.behind = false;
    settings.anchor = 0;
    settings.offset_x = -1;
    stamp.reset(2, 2, {255, 0, 0, 255});
    front = paint::compose_icon_stamp(source, stamp, settings);
    require(front.clipped_pixels == 2 && paint::equal(front.image.get(0, 0), {255, 0, 0, 255}),
            "clipping counts nontransparent stamp pixels");
    settings.offset_x = 0;
    settings.flatten = true;
    settings.matte = {0, 255, 0, 255};
    front = paint::compose_icon_stamp(source, stamp, settings);
    require(front.image.get(2, 2).a == 255 && front.image.get(2, 2).g == 127,
            "matte flattens the complete canvas");
    settings.flatten = false;
    settings.scale = paint::IconStampScale::Fit;
    source.reset(8, 4, {0, 0, 0, 0});
    settings.anchor = 8;
    front = paint::compose_icon_stamp(source, stamp, settings);
    require(front.placement.w == 4 && front.placement.h == 4 && front.placement.x == 4 && front.upscaled,
            "fit preserves stamp aspect and applies anchor");
    settings.percent = std::numeric_limits<double>::quiet_NaN();
    bool rejected = false;
    try {
        static_cast<void>(paint::compose_icon_stamp(source, stamp, settings));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "nonfinite stamp parameters rejected before resizing");
    Directory root;
    const std::filesystem::path output = root.path / "out";
    std::filesystem::create_directory(output);
    const std::string input = paint::path_to_utf8(root.path / "base.png");
    const std::string fixed = paint::path_to_utf8(root.path / "stamp.png");
    paint::save_image(source, input);
    paint::save_image(stamp, fixed);
    paint::IconBatchSettings batch;
    batch.new_directory = false;
    batch.operation = paint::IconBatchOperation::Stamp;
    batch.stamp_path = fixed;
    std::atomic<bool> cancel{false};
    paint::IconBatchPlan job = paint::plan_icon_batch({input}, paint::path_to_utf8(output), batch, cancel);
    require(paint::icon_batch_ready(job) && job.preview_before.width == 8 && job.preview_after.width == 8 &&
                std::filesystem::is_empty(output),
            "stamp preflight produces bounded before/after proofs without writing");
    paint::run_icon_batch(job, cancel);
    const paint::Image stamped = paint::load_image(paint::path_to_utf8(output / job.entries[0].filename));
    require(stamped.width == 8 && stamped.height == 4 && stamped.get(3, 1).r == 255,
            "ordinary source exports stamped PNG with unchanged dimensions");
    batch.collision = paint::IconExportCollision::Rename;
    job = paint::plan_icon_batch({input}, paint::path_to_utf8(output), batch, cancel);
    stamp.reset(3, 3, {0, 255, 0, 255});
    paint::save_image(stamp, fixed);
    paint::run_icon_batch(job, cancel);
    require(job.entries[0].state == paint::IconExportState::Failed &&
                !std::filesystem::exists(output / job.entries[0].filename),
            "changed fixed image invalidates reviewed stamp outputs");
    require(paint::icon_batch_report(job).find("fixed-image-stamp") != std::string::npos,
            "report identifies stamping operation");
}
void stamp_container_contracts() {
    Directory root;
    const std::filesystem::path output = root.path / "out";
    std::filesystem::create_directory(output);
    paint::IconFrame first, second;
    first.image.reset(20, 10, {40, 60, 80, 128});
    first.hotspot_x = 19;
    first.hotspot_y = 8;
    second.image.reset(16, 16, {10, 20, 30, 255});
    second.hotspot_x = 3;
    second.hotspot_y = 5;
    const std::string source = paint::path_to_utf8(root.path / "family.cur");
    paint::save_container({paint::ContainerKind::Cursor, {first, second, first}}, source);
    const paint::ImageContainer original = paint::load_container(source);
    paint::Image stamp;
    stamp.reset(2, 2, {255, 0, 0, 255});
    const std::string fixed = paint::path_to_utf8(root.path / "badge.png");
    paint::save_image(stamp, fixed);
    paint::IconBatchSettings settings;
    settings.new_directory = false;
    settings.operation = paint::IconBatchOperation::Stamp;
    settings.stamp_path = fixed;
    settings.stamp.anchor = 8;
    std::atomic<bool> cancel{false};
    paint::IconBatchPlan job =
        paint::plan_icon_batch({source}, paint::path_to_utf8(output), settings, cancel);
    require(job.entries[0].summary.find("image #3") != std::string::npos,
            "stamp manifest identifies every representation, including duplicates");
    paint::run_icon_batch(job, cancel);
    const paint::ImageContainer stamped =
        paint::load_container(paint::path_to_utf8(output / job.entries[0].filename));
    require(stamped.kind == paint::ContainerKind::Cursor && stamped.frames.size() == 3,
            "stamp retains cursor kind and duplicate representations");
    for (std::size_t index = 0; index < original.frames.size(); ++index) {
        const paint::IconFrame& before = original.frames[index];
        const paint::IconFrame& after = stamped.frames[index];
        require(after.image.width == before.image.width && after.image.height == before.image.height &&
                    after.hotspot_x == before.hotspot_x && after.hotspot_y == before.hotspot_y &&
                    paint::equal(after.image.get(after.image.width - 1, after.image.height - 1),
                                 {255, 0, 0, 255}),
                "stamp applies independently at each size without moving cursor hotspots");
    }
    settings.collision = paint::IconExportCollision::Rename;
    settings.stamp.opacity = 0;
    job = paint::plan_icon_batch({source}, paint::path_to_utf8(output), settings, cancel);
    paint::run_icon_batch(job, cancel);
    const paint::ImageContainer unchanged =
        paint::load_container(paint::path_to_utf8(output / job.entries[0].filename));
    for (std::size_t index = 0; index < original.frames.size(); ++index) {
        require((*unchanged.frames[index].source).payload == (*original.frames[index].source).payload,
                "no-op stamping preserves original encoded representation payloads");
    }
    settings.stamp.opacity = 100;
    job = paint::plan_icon_batch({source}, paint::path_to_utf8(output), settings, cancel);
    cancel.store(true);
    paint::run_icon_batch(job, cancel);
    require(job.entries[0].state == paint::IconExportState::Cancelled &&
                !std::filesystem::exists(output / job.entries[0].filename),
            "cancelled stamp job publishes no unstarted output");
    cancel.store(false);
    const std::string base = paint::path_to_utf8(root.path / "base.png");
    paint::save_image(first.image, base);
    settings.stamp_path = paint::path_to_utf8(output / "base-1-stamped.png");
    paint::save_image(stamp, settings.stamp_path);
    settings.collision = paint::IconExportCollision::Replace;
    job = paint::plan_icon_batch({base}, paint::path_to_utf8(output), settings, cancel);
    require(!paint::icon_batch_ready(job) && job.entries[0].state == paint::IconExportState::Failed,
            "review cannot replace the fixed stamp even under Replace policy");
    settings.stamp_path = fixed;
    settings.collision = paint::IconExportCollision::Rename;
    job = paint::plan_icon_batch({base}, paint::path_to_utf8(output), settings, cancel);
    std::filesystem::create_hard_link(paint::path_from_utf8(fixed), output / job.entries[0].filename);
    paint::run_icon_batch(job, cancel);
    require(job.entries[0].state == paint::IconExportState::Failed && paint::load_image(fixed).width == 2,
            "execution protects a late hard-link alias of the fixed image");
    paint::Image small;
    small.reset(3, 3, {0, 0, 0, 0});
    stamp.reset(4, 4, {255, 0, 0, 255});
    const paint::IconStampResult centered = paint::compose_icon_stamp(small, stamp, {});
    require(centered.placement.x == -1 && centered.placement.y == -1 && centered.clipped_pixels == 7,
            "oversized odd center differences consistently round toward negative infinity");
    const std::string report = paint::icon_batch_report(job);
    require(report.find("\"anchor\":8") != std::string::npos &&
                report.find("\"scaleMode\":\"natural\"") != std::string::npos &&
                report.find("\"sourceImageOrdinal\":1") != std::string::npos,
            "report records stamp source identity, anchor and scale contract");
    const paint::ImageContainer indexed =
        paint::decode_icon_container(paint::test::indexed_icon, sizeof(paint::test::indexed_icon));
    const std::string indexed_path = paint::path_to_utf8(root.path / "indexed.ico");
    paint::save_container(indexed, indexed_path);
    job = paint::plan_icon_batch({indexed_path}, paint::path_to_utf8(output), settings, cancel);
    paint::run_icon_batch(job, cancel);
    const paint::ImageContainer rgba =
        paint::load_container(paint::path_to_utf8(output / job.entries[0].filename));
    require(paint::icon_output_format(rgba.frames[0], rgba.kind).bits_per_pixel == 32 &&
                paint::equal(rgba.frames[0].image.get(0, 0), {255, 0, 0, 255}),
            "edited indexed input produces RGBA without silently reusing the old palette");
    const paint::ImageContainer legacy =
        paint::decode_icon_container(paint::test::legacy_cursor, sizeof(paint::test::legacy_cursor));
    const std::string legacy_path = paint::path_to_utf8(root.path / "legacy.cur");
    paint::save_container(legacy, legacy_path);
    job = paint::plan_icon_batch({legacy_path}, paint::path_to_utf8(output), settings, cancel);
    require(!paint::icon_batch_ready(job) && job.entries[0].state == paint::IconExportState::Failed,
            "background-dependent source fails as a complete family");
    settings.stamp_path = legacy_path;
    bool rejected = false;
    try {
        static_cast<void>(paint::plan_icon_batch({source}, paint::path_to_utf8(output), settings, cancel));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "background-dependent fixed stamp is rejected before review");
}
} // namespace
int main() {
    try {
        batch_contracts();
        new_output_folder_contracts();
        svg_batch_contracts();
        shared_palette_batch_contracts();
        cursor_and_report_contracts();
        stamp_contracts();
        stamp_container_contracts();
        std::cout << "Icon batch: conversion, normalization, state, PNG, cancellation, stale input and "
                     "collisions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

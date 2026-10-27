#include "codecs.hpp"
#include "fixtures/icon_reference.hpp"
#include "icon_export.hpp"
#include "paths.hpp"
#include "safe_file.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
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
               ("paint-icon-export-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        require(std::filesystem::create_directory(path), "isolated export directory creation failed");
    }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
std::shared_ptr<const paint::ImageContainer> source_images() {
    paint::ImageContainer source;
    source.kind = paint::ContainerKind::Icon;
    paint::IconFrame first, second;
    first.image.reset(16, 16, {20, 90, 180, 128});
    first.image.set(0, 0, {70, 60, 50, 0});
    second.image.reset(16, 16, {230, 100, 30, 255});
    source.frames = {first, second};
    return std::make_shared<const paint::ImageContainer>(std::move(source));
}
paint::IconExportPlan plan(const Directory& directory,
                           paint::IconExportCollision policy = paint::IconExportCollision::Skip) {
    return paint::plan_icon_export(source_images(), {0, 1}, paint::path_to_utf8(directory.path), "sample",
                                   policy);
}
void write_sentinel(const std::filesystem::path& target) {
    paint::write_file_atomic({'s', 'a', 'f', 'e'}, paint::path_to_utf8(target), "sentinel write failed");
}
bool is_sentinel(const std::filesystem::path& target) {
    return paint::read_image_bytes(paint::path_to_utf8(target)) ==
           std::vector<std::uint8_t>{'s', 'a', 'f', 'e'};
}
void cancel_after_one(const paint::IconExportPlan& report, void* context) {
    if (report.entries[0].state == paint::IconExportState::Completed) {
        (*static_cast<std::atomic<bool>*>(context)).store(true);
    }
}
void export_contracts() {
    Directory directory;
    paint::IconExportPlan job = plan(directory);
    require(paint::icon_export_ready(job) && std::filesystem::is_empty(directory.path),
            "preflight wrote files");
    require(job.entries[0].filename != job.entries[1].filename &&
                job.entries[0].filename.find("16x16-rgba-001") != std::string::npos,
            "same-size identity missing from filenames");
    const paint::IconExportPlan repeated =
        paint::plan_icon_export(job.source, {0, 0, 1}, paint::path_to_utf8(directory.path), "sample",
                                paint::IconExportCollision::Skip);
    require(repeated.entries[0].filename != repeated.entries[1].filename,
            "repeated sequence overwrites itself");
    std::atomic<bool> cancel{false};
    paint::run_icon_export(job, cancel);
    for (const paint::IconExportEntry& entry : job.entries) {
        require(entry.state == paint::IconExportState::Completed, "PNG export did not complete");
        const paint::Image actual = paint::load_image(paint::path_to_utf8(directory.path / entry.filename));
        const paint::Image& expected = (*job.source).frames[entry.source_index].image;
        require(actual.width == expected.width && actual.height == expected.height, "PNG dimensions changed");
        for (std::size_t index = 0; index < actual.pixels.size(); ++index) {
            require(paint::equal(actual.pixels[index], expected.pixels[index]),
                    "RGBA or hidden RGB changed on export");
        }
    }
    paint::IconExportPlan skipped = plan(directory);
    require(!paint::icon_export_ready(skipped) && skipped.entries[0].state == paint::IconExportState::Skipped,
            "skip preflight allowed overwrite");
    paint::IconExportPlan renamed = plan(directory, paint::IconExportCollision::Rename);
    require(renamed.entries[0].filename != job.entries[0].filename,
            "rename did not resolve collisions in review");
    write_sentinel(directory.path / renamed.entries[0].filename);
    paint::run_icon_export(renamed, cancel);
    require(renamed.entries[0].state == paint::IconExportState::Failed &&
                renamed.entries[1].state == paint::IconExportState::Completed &&
                is_sentinel(directory.path / renamed.entries[0].filename),
            "late rename collision changed a reviewed path or destroyed another file");
    paint::IconExportPlan replacement = plan(directory, paint::IconExportCollision::Replace);
    require(replacement.entries[0].replaces_existing, "replacement not shown in manifest");
    write_sentinel(directory.path / replacement.entries[0].filename);
    paint::run_icon_export(replacement, cancel);
    require(replacement.entries[0].state == paint::IconExportState::Completed &&
                !is_sentinel(directory.path / replacement.entries[0].filename),
            "explicit replacement failed");
    const std::string report = paint::icon_export_report(replacement);
    require(report.find("\"sourceOrdinal\": 2") != std::string::npos &&
                report.find("\"state\": \"Completed\"") != std::string::npos,
            "report lost identity or completion");
    replacement.entries[0].diagnostic = "quoted \"message\"\nnext line";
    require(paint::icon_export_report(replacement).find("quoted \\\"message\\\"\\u000anext line") !=
                std::string::npos,
            "report diagnostics not escaped as JSON");
}
void cancellation_and_errors() {
    Directory directory;
    paint::IconExportPlan job = plan(directory);
    std::atomic<bool> cancel{false};
    paint::run_icon_export(job, cancel, &cancel_after_one, &cancel);
    require(job.entries[0].state == paint::IconExportState::Completed &&
                job.entries[1].state == paint::IconExportState::Cancelled &&
                !std::filesystem::exists(directory.path / job.entries[1].filename),
            "cancellation misreported partial job");
    paint::IconExportPlan late = plan(directory);
    write_sentinel(directory.path / late.entries[1].filename);
    cancel.store(false);
    paint::run_icon_export(late, cancel);
    require(late.entries[1].state == paint::IconExportState::Skipped &&
                is_sentinel(directory.path / late.entries[1].filename),
            "late skip collision overwritten");
    paint::IconExportPlan failure =
        paint::plan_icon_export(source_images(), {0, 1}, paint::path_to_utf8(directory.path), "blocked",
                                paint::IconExportCollision::Replace);
    std::filesystem::create_directory(directory.path / failure.entries[0].filename);
    paint::run_icon_export(failure, cancel);
    require(failure.entries[0].state == paint::IconExportState::Failed &&
                failure.entries[1].state == paint::IconExportState::Completed,
            "one filesystem failure hid other output results");
    const std::shared_ptr<const paint::ImageContainer> cursor = std::make_shared<const paint::ImageContainer>(
        paint::decode_icon_container(paint::test::legacy_cursor, sizeof(paint::test::legacy_cursor)));
    const paint::IconExportPlan xor_plan = paint::plan_icon_export(
        cursor, {0}, paint::path_to_utf8(directory.path), "cursor", paint::IconExportCollision::Skip);
    require(!paint::icon_export_ready(xor_plan) &&
                xor_plan.entries[0].state == paint::IconExportState::Failed,
            "XOR was silently exported to ordinary alpha");
    bool rejected = false;
    try {
        paint::plan_icon_export(source_images(), {0}, paint::path_to_utf8(directory.path), "../escape",
                                paint::IconExportCollision::Skip);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "export prefix escaped destination");
    rejected = false;
    try {
        paint::plan_icon_export(source_images(), {}, paint::path_to_utf8(directory.path), "empty",
                                paint::IconExportCollision::Skip);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "empty selection accepted");
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory.path)) {
        require(paint::path_to_utf8(entry.path().filename()).find(".rainstar-") == std::string::npos,
                "job left an incomplete temporary behind");
    }
}
struct Publisher {
    std::string path;
    std::uint8_t value;
    std::atomic<int>* ready;
    std::atomic<int>* successes;
    void operator()() const {
        (*ready).fetch_add(1);
        while ((*ready).load() < 2) {
            std::this_thread::yield();
        }
        try {
            paint::write_file_atomic({value}, path, "competing publication", false);
            (*successes).fetch_add(1);
        } catch (const std::exception&) {
        }
    }
};
void exclusive_publication() {
    Directory directory;
    const std::filesystem::path target = directory.path / "same.png";
    std::atomic<int> ready{0}, successes{0};
    std::thread first(Publisher{paint::path_to_utf8(target), 31, &ready, &successes});
    std::thread second(Publisher{paint::path_to_utf8(target), 72, &ready, &successes});
    first.join();
    second.join();
    require(successes.load() == 1, "exclusive atomic publication allowed two writers");
    const std::vector<std::uint8_t> bytes = paint::read_image_bytes(paint::path_to_utf8(target));
    require(bytes == std::vector<std::uint8_t>{31} || bytes == std::vector<std::uint8_t>{72},
            "winner output corrupted");
#ifndef _WIN32
    const std::filesystem::path sentinel = directory.path / "sentinel";
    write_sentinel(sentinel);
    const std::filesystem::path link = directory.path / "link.png";
    std::filesystem::create_symlink(sentinel, link);
    bool rejected = false;
    try {
        paint::write_file_atomic({90}, paint::path_to_utf8(link), "exclusive symbolic link", false);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && is_sentinel(sentinel) && std::filesystem::is_symlink(link),
            "exclusive write followed a symlink");
#endif
}
} // namespace
int main() {
    try {
        export_contracts();
        cancellation_and_errors();
        exclusive_publication();
        std::cout
            << "Representation exports, collision policies, partial results and atomic no-replace passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "paths.hpp"
#include "recovery.hpp"
#include "safe_file.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
paint::RecoveryRecord sample() {
    paint::RecoveryRecord record;
    record.opened_path = "/art/leaf 🍃\noriginal.png";
    record.source_path = "/art/saved leaf.png";
    record.created_ms = 100;
    record.captured_ms = 200;
    record.saved_ms = 150;
    record.revision = 12;
    paint::Image image;
    image.reset(7, 5, {30, 60, 90, 127});
    image.set(3, 4, {230, 140, 80, 0});
    record.image.frames.push_back({image, 0, 0, {}});
    return record;
}
void exercise(const std::filesystem::path& root) {
    std::string id;
    {
        paint::RecoverySession session(root);
        id = session.id();
        require(id.size() == 36 && id[14] == '4', "UUID v4 identity");
        bool locked = false;
        try {
            paint::RecoverySession duplicate(root, id);
        } catch (const std::exception&) {
            locked = true;
        }
        require(locked, "second session cannot acquire the live lease");
        paint::RecoveryRecord record = sample();
        session.write(record);
        require(paint::RecoverySession::candidates(root).empty(),
                "running documents are not offered for recovery");
        paint::RecoveryRecord read = session.read();
        require(read.opened_path == record.opened_path && read.source_path == record.source_path &&
                    read.created_ms == 100 && read.saved_ms == 150 && read.captured_ms == 200 &&
                    read.revision == 12,
                "Unicode, newlines and provenance metadata round trip");
        require(paint::equal(read.image.frames[0].image.get(3, 4), {230, 140, 80, 0}),
                "transparent pixel channels survive recovery");
        record.captured_ms = 300;
        record.image.frames[0].image.set(1, 1, {255, 0, 0, 255});
        session.write(record);
        require(session.read().captured_ms == 300, "newest completed snapshot wins");
        const std::filesystem::path newest = root / id / "1.rpr";
        paint::write_file_atomic({1, 2, 3}, paint::path_to_utf8(newest), "corruption fixture");
        require(session.read().captured_ms == 200,
                "torn or corrupted newest snapshot falls back to older copy");
        record.captured_ms = 400;
        session.write(record);
        require(session.read().captured_ms == 400, "writing after fallback replaces damaged slot");
        record.captured_ms = 50;
        session.write(record);
        require(session.read().captured_ms == 50,
                "generation order survives a backwards wall-clock adjustment");
    }
    require(paint::RecoverySession::candidates(root) == std::vector<std::string>{id},
            "released lease exposes abandoned artwork");
    {
        paint::RecoverySession claimed(root, id);
        paint::RecoveryRecord record = claimed.read();
        require(record.captured_ms == 50, "relaunch claims and restores latest intact generation");
        paint::write_file_atomic({8}, paint::path_to_utf8(root / id / "keep.txt"), "unrelated file fixture");
        claimed.discard();
        require(std::filesystem::exists(root / id / "keep.txt"), "cleanup never removes unrecognized files");
        require(paint::RecoverySession::candidates(root).empty(),
                "explicit completion removes recovery candidates");
    }
    {
        paint::RecoverySession cursor(root);
        paint::RecoveryRecord record = sample();
        record.image.kind = paint::ContainerKind::Cursor;
        record.image.frames[0].hotspot_x = 3;
        record.image.frames[0].hotspot_y = 4;
        record.image.frames.push_back(record.image.frames[0]);
        record.image.frames[1].image.reset(16, 16, {50, 100, 150, 255});
        cursor.write(record);
        paint::RecoveryRecord restored = cursor.read();
        require(restored.image.kind == paint::ContainerKind::Cursor && restored.image.frames.size() == 2 &&
                    restored.image.frames[0].hotspot_x == 3 && restored.image.frames[0].hotspot_y == 4,
                "cursor frames and hotspots survive recovery");
        cursor.discard();
    }
    bool invalid = false;
    try {
        paint::RecoverySession bad(root, "../elsewhere");
    } catch (const std::exception&) {
        invalid = true;
    }
    require(invalid, "recovery IDs cannot escape their root");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--crash") {
            paint::RecoverySession session(paint::path_from_utf8(argv[2]));
            session.write(sample());
            // Deliberately skip C++ destructors, as a process crash would.
            std::_Exit(23);
        }
        if (argc == 3 && std::string(argv[1]) == "--after-crash") {
            const std::filesystem::path root = paint::path_from_utf8(argv[2]);
            const std::vector<std::string> ids = paint::RecoverySession::candidates(root);
            require(ids.size() == 1, "OS releases the lease after abrupt process termination");
            paint::RecoverySession session(root, ids.front());
            require(session.read().captured_ms == 200, "abrupt process exit preserves completed pixels");
            session.discard();
            return 0;
        }
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() /
            ("rainstar-recovery-test-" + std::to_string(paint::recovery_time_ms()));
        exercise(root);
        std::filesystem::remove_all(root);
        std::cout << "Recovery: leases, metadata, transparent pixels, two generations, corruption, cleanup "
                     "and cursor frames passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

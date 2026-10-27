#include "codecs.hpp"
#include "fixtures/icon_reference.hpp"
#include "icon_palette.hpp"
#include "icon_validation.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
paint::ImageContainer fixture() {
    paint::IconFrame frame;
    frame.image.reset(16, 16, {80, 130, 210, 128});
    paint::IconPaletteOptions palette;
    palette.bits_per_pixel = 4;
    paint::ImageContainer source;
    source.kind = paint::ContainerKind::Icon;
    source.frames = {frame, frame, paint::convert_icon_palette(frame, palette).frame};
    return source;
}
void contracts() {
    paint::ImageContainer source = fixture();
    paint::IconValidationProfile profile;
    profile.sizes = {16, 32};
    paint::IconValidation result = paint::validate_icon_output(source, profile);
    require(result.missing == std::vector<int>({32}) &&
                result.duplicates == std::vector<std::vector<int>>({{0, 1}}),
            "duplicates use saved size and depth; different indexed depth remains distinct");
    require(result.frames[0].current_alpha[1] == 256 && result.frames[0].saved_alpha[1] == 256 &&
                result.frames[0].changed_alpha == 0,
            "actual encoded RGBA preserves partial transparency");
    profile.depth = 4;
    result = paint::validate_icon_output(source, profile);
    require(result.missing == std::vector<int>({32}) && result.frames[2].saved.bits_per_pixel == 4,
            "retained indexed payload satisfies indexed profile");
    source.frames[2].image.set(0, 0, {1, 2, 3, 127});
    result = paint::validate_icon_output(source, profile);
    require(result.missing == std::vector<int>({16, 32}) && result.frames[2].imported.bits_per_pixel == 4 &&
                result.frames[2].saved.bits_per_pixel == 32,
            "editing indexed pixels changes saved depth and profile coverage");
    require(result.frames[2].source_alpha[1] == 0 && result.frames[2].current_alpha[1] == 1,
            "source and edited alpha distributions remain separate");
    paint::IconFrame large;
    large.image.reset(256, 256, {40, 70, 130, 255});
    source.frames = {large};
    result = paint::validate_icon_output(source, {});
    require(result.frames[0].valid && result.frames[0].saved.storage == paint::IconStorage::Png,
            "256-square ICO audit follows actual PNG writer");
    source.kind = paint::ContainerKind::Cursor;
    result = paint::validate_icon_output(source, {});
    require(result.frames[0].saved.storage == paint::IconStorage::Dib, "CUR follows DIB writer");
    source.frames[0].hotspot_x = 256;
    result = paint::validate_icon_output(source, {});
    require(!result.frames[0].valid && !result.frames[0].error.empty(),
            "invalid hotspot is reported per image");
    source = paint::decode_icon_container(paint::test::legacy_cursor, sizeof(paint::test::legacy_cursor));
    profile.sizes = {2};
    profile.depth = 0;
    result = paint::validate_icon_output(source, profile);
    require(result.frames[0].valid && result.frames[0].xor_pixels == 1 && result.missing.empty(),
            "legacy XOR is counted without rejecting preserved CUR payload");
    source = paint::decode_icon_container(paint::test::indexed_icon, sizeof(paint::test::indexed_icon));
    result = paint::validate_icon_output(source, profile);
    require(result.frames[0].saved.bits_per_pixel == 2 && result.frames[0].saved_alpha[1] > 0,
            "imported indexed PNG is audited at its actual saved depth");
    source = fixture();
    source.frames.resize(1);
    paint::ImageContainer opaque = source;
    opaque.frames[0].image.reset(16, 16, {20, 40, 60, 255});
    const std::vector<std::uint8_t> bytes = paint::encode_icon_container(opaque);
    const paint::ImageContainer decoded = paint::decode_icon_container(bytes.data(), bytes.size());
    std::shared_ptr<paint::IconSource> mismatched =
        std::make_shared<paint::IconSource>(*decoded.frames[0].source);
    (*mismatched).image = source.frames[0].image;
    source.frames[0].source = mismatched;
    result = paint::validate_icon_output(source, {});
    require(result.frames[0].changed_alpha == 256 && result.frames[0].saved_alpha[2] == 256,
            "audit decodes actual retained bytes instead of assuming alpha matches current pixels");
    source.frames[0].image.pixels.pop_back();
    result = paint::validate_icon_output(source, {});
    require(!result.frames[0].valid, "malformed dimensions do not access missing pixels");
    require(paint::parse_validation_sizes(" 16,24, 256 ") == std::vector<int>({16, 24, 256}),
            "custom profile parser accepts bounded explicit sizes");
    for (const std::string& text : {"", "16,16", "16,", "0", "257", "16px"}) {
        bool rejected = false;
        try {
            static_cast<void>(paint::parse_validation_sizes(text));
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "invalid profile rejected");
    }
    std::atomic<bool> cancel{true};
    bool rejected = false;
    try {
        static_cast<void>(paint::validate_icon_output(fixture(), {}, &cancel));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "cancelled audit publishes no result");
}
} // namespace
int main(int argc, char**) {
    try {
        contracts();
        if (argc > 1) {
            paint::IconValidation result = paint::validate_icon_output(fixture(), {});
            result.frames[0].error = "quote\" newline\n slash\\";
            std::cout << paint::icon_validation_json(result);
        } else {
            std::cout << "Icon validation contracts passed\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

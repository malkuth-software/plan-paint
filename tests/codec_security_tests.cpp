#include "codecs.hpp"
#include "safe_file.hpp"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::uint32_t random_word(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
void mutate(std::vector<std::uint8_t>& bytes, std::uint32_t& seed, int trial) {
    const int changes = 1 + static_cast<int>(random_word(seed) % 8);
    for (int change = 0; change < changes && !bytes.empty(); ++change) {
        const std::size_t offset = random_word(seed) % bytes.size();
        bytes[offset] ^= static_cast<std::uint8_t>(1U << (random_word(seed) % 8));
    }
    if (trial % 4 == 0 && bytes.size() > 1) {
        bytes.resize(1 + random_word(seed) % (bytes.size() - 1));
    }
}
void exercise_raster_mutations(const std::vector<std::uint8_t>& original, std::uint32_t seed, int trials) {
    for (int trial = 0; trial < trials; ++trial) {
        std::vector<std::uint8_t> bytes = original;
        mutate(bytes, seed, trial);
        try {
            static_cast<void>(paint::decode_image(bytes.data(), bytes.size()));
        } catch (const std::exception&) {
        }
    }
}
void exercise_icon_mutations(const std::vector<std::uint8_t>& original, std::uint32_t seed, int trials) {
    for (int trial = 0; trial < trials; ++trial) {
        std::vector<std::uint8_t> bytes = original;
        mutate(bytes, seed, trial);
        try {
            static_cast<void>(paint::decode_icon_container(bytes.data(), bytes.size()));
        } catch (const std::exception&) {
        }
    }
}
void exercise_file_mutations(const std::vector<std::uint8_t>& original, const std::filesystem::path& path,
                             std::uint32_t seed, int trials) {
    for (int trial = 0; trial < trials; ++trial) {
        std::vector<std::uint8_t> bytes = original;
        mutate(bytes, seed, trial);
        paint::write_file_atomic(bytes, path.string(), "Could not write a codec mutation.");
        try {
            static_cast<void>(paint::load_image(path.string()));
        } catch (const std::exception&) {
        }
    }
}
std::vector<std::uint8_t> save_fixture(const paint::Image& image, const std::filesystem::path& directory,
                                       const std::string& extension) {
    const std::filesystem::path path = directory / ("seed." + extension);
    paint::save_image(image, path.string());
    return paint::read_image_bytes(path.string());
}
} // namespace

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "rainstar-codec-security-corpus";
    try {
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        paint::Image image;
        image.reset(19, 13, {27, 83, 179, 255});
        image.set(3, 5, {241, 37, 91, 113});

        const char* raster_formats[] = {"png", "jpg", "bmp", "tga"};
        const std::uint32_t raster_seeds[] = {0x31415926U, 0x27182818U, 0x16180339U, 0x14142135U};
        for (std::size_t index = 0; index < std::size(raster_formats); ++index) {
            const std::vector<std::uint8_t> bytes = save_fixture(image, directory, raster_formats[index]);
            exercise_raster_mutations(bytes, raster_seeds[index], 48);
        }

        const std::string root = FORMAT_FIXTURES;
        const std::vector<std::uint8_t> webp = paint::read_image_bytes(root + "/quadrants.webp");
        exercise_raster_mutations(webp, 0x17320508U, 48);
        const std::vector<std::uint8_t> gif = paint::read_image_bytes(root + "/animated.gif");
        exercise_raster_mutations(gif, 0x22360679U, 48);
        const std::vector<std::uint8_t> avif = paint::read_image_bytes(root + "/quadrants.avif");
        exercise_raster_mutations(avif, 0x70710678U, 72);

        const std::vector<std::uint8_t> tiff = save_fixture(image, directory, "tiff");
        exercise_file_mutations(tiff, directory / "mutated.tiff", 0x24494897U, 48);
        const std::vector<std::uint8_t> svg = paint::read_image_bytes(root + "/import.svg");
        exercise_file_mutations(svg, directory / "mutated.svg", 0x26457513U, 48);
        const std::vector<std::uint8_t> heif = paint::read_image_bytes(root + "/quadrants.heic");
        exercise_file_mutations(heif, directory / "mutated.heic", 0x31622776U, 24);

        paint::ImageContainer icon;
        icon.kind = paint::ContainerKind::Icon;
        icon.frames.push_back({image, 0, 0, {}});
        const std::vector<std::uint8_t> icon_bytes = paint::encode_icon_container(icon);
        exercise_icon_mutations(icon_bytes, 0x33166247U, 72);

        std::filesystem::remove_all(directory);
        std::cout << "Malformed PNG, JPEG, BMP, TGA, WebP, GIF, AVIF, TIFF, SVG, HEIF and ICO corpus "
                     "completed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(directory);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

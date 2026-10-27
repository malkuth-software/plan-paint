#pragma once
#include "atlas.hpp"
#include <array>
#include <atomic>
namespace paint {
struct IconValidationProfile {
    std::vector<int> sizes{16, 24, 32, 48, 64, 128, 256};
    unsigned depth = 32; // Zero accepts any saved depth.
};
struct IconValidationFrame {
    int index = 0, width = 0, height = 0;
    IconFormat imported, saved;
    bool has_imported = false, valid = false;
    std::array<std::uint64_t, 3> source_alpha{}, current_alpha{}, saved_alpha{};
    std::uint64_t changed_alpha = 0, xor_pixels = 0;
    std::string error;
};
struct IconValidation {
    ContainerKind kind = ContainerKind::Icon;
    IconValidationProfile profile;
    std::vector<IconValidationFrame> frames;
    std::vector<int> missing;
    std::vector<std::vector<int>> duplicates;
};
std::vector<int> parse_validation_sizes(const std::string& text);
IconValidation validate_icon_output(const ImageContainer& source, const IconValidationProfile& profile,
                                    const std::atomic<bool>* cancel = nullptr);
std::string icon_validation_json(const IconValidation& validation);
} // namespace paint

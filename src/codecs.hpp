#pragma once
#include "atlas.hpp"
#include "image.hpp"
namespace paint {
ImageContainer decode_icon_container(const void* data, std::size_t size);
std::vector<std::uint8_t> encode_icon_container(const ImageContainer& container);
ImageContainer load_container(const std::string& path);
void save_container(const ImageContainer& container, const std::string& path);
std::string image_extension(const std::string& path);
bool writable_image_path(const std::string& path);
std::vector<std::uint8_t> read_image_bytes(const std::string& path, std::size_t maximum = 256000000);
void save_encoded_bytes(const std::vector<std::uint8_t>& bytes, const std::string& path);
Image load_image(const std::string& path);
void save_image(const Image& image, const std::string& path, int quality = 95);
std::vector<std::uint8_t> encode_bmp(const Image& image);
std::vector<std::uint8_t> encode_png(const Image& image);
Image decode_image(const void* data, std::size_t size);
} // namespace paint

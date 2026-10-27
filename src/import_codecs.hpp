#pragma once
#include "atlas.hpp"
namespace paint {
void reject_animation(const std::uint8_t* data, std::size_t size);
bool is_avif(const void* data, std::size_t size);
Image decode_avif(const void* data, std::size_t size);
class SvgSource {
  public:
    double width = 0, height = 0;
    Image original;

  private:
    std::string xml_;
    friend std::shared_ptr<const SvgSource> load_svg_source(const std::string&, bool);
    friend Image rasterize_svg(const SvgSource&, int, int);
};
std::shared_ptr<const SvgSource> load_svg_source(const std::string& path, bool retain_raster = true);
bool svg_source_matches(const IconFrame& frame);
Image rasterize_svg(const SvgSource& source, int width = 0, int height = 0);
Image rasterize_svg(const std::string& path, int width = 0, int height = 0);
Image decode_native_heif(const void* data, std::size_t size);
} // namespace paint

#pragma once
#include "image.hpp"
#include <vector>
namespace paint::forms {
struct LinuxPrintSettings {
    double width_mm = 210, height_mm = 297, margin_mm = 12;
    int copies = 1;
    std::string printer;
};
LinuxPrintSettings& linux_print_settings();
std::vector<std::string> linux_printers();
void linux_print_image(const Image& image, const LinuxPrintSettings& settings);
void write_print_postscript(const Image& image, const LinuxPrintSettings& settings, const std::string& path);
} // namespace paint::forms

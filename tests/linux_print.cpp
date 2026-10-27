#include "forms/linux_desktop.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Supply a PostScript output path.");
        }
        paint::Image image;
        image.reset(2, 2);
        image.pixels = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 0, 255, 0}};
        paint::forms::LinuxPrintSettings settings;
        settings.width_mm = 200;
        settings.height_mm = 100;
        settings.margin_mm = 10;
        paint::forms::write_print_postscript(image, settings, argv[1]);
        if (image.pixels[3].a != 0 || image.pixels[3].g != 0) {
            throw std::runtime_error("Printing modified source transparency.");
        }
        settings.margin_mm = 60;
        bool rejected = false;
        try {
            paint::forms::write_print_postscript(image, settings, std::string(argv[1]) + ".invalid");
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        if (!rejected) {
            throw std::runtime_error("Invalid page margins accepted.");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "carpet.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    if (argc > 1) {
        std::filesystem::create_directories(argv[1]);
    }
    for (int preset : {0, 3, 4}) {
        for (bool hillshade : {false, true}) {
            paint::CarpetParameters p = paint::carpet_preset(preset);
            p.hillshade = hillshade;
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            const paint::Image image = paint::render_carpet_tile(p, 512);
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const std::string name = std::to_string(preset) + (hillshade ? "-relief" : "-plain");
            if (argc > 1) {
                std::ofstream out(std::string(argv[1]) + "/" + name + ".rgba", std::ios::binary);
                out.write(reinterpret_cast<const char*>(image.pixels.data()),
                          image.pixels.size() * sizeof(paint::Color));
            }
            std::uint64_t hash = 1469598103934665603ULL;
            for (paint::Color color : image.pixels) {
                for (std::uint8_t channel : {color.r, color.g, color.b, color.a}) {
                    hash = (hash ^ channel) * 1099511628211ULL;
                }
            }
            std::cout << name << ',' << seconds << ',' << hash << '\n';
        }
    }
}

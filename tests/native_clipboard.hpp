#pragma once
#include "forms/editor.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gui_forms/timer.hpp>
#include <stdexcept>
namespace paint::tests {
// Two application instances exchange through the real desktop clipboard.
// The Python coordinator permits this only on an explicitly isolated desktop.
class NativeClipboardExchange {
    forms::Editor& editor_;
    gui_forms::ApplicationWindowHandle handle_;
    std::filesystem::path directory_;
    bool producer_, alpha_sent_ = false, success_ = false;
    std::string error_;
    std::unique_ptr<gui_forms::Timer> timer_;
    gui_forms::SubscriptionToken tick_;
    std::chrono::steady_clock::time_point deadline_;
    static Image specimen() {
        Image image;
        image.reset(4, 1);
        image.pixels = {{231, 7, 91, 0}, {10, 90, 200, 37}, {80, 120, 160, 128}, {255, 1, 33, 255}};
        return image;
    }
    void signal(const char* name) {
        std::ofstream file(directory_ / name);
        file << "ready\n";
        file.close();
        if (!file) {
            throw std::runtime_error("Could not signal native clipboard peer");
        }
    }
    bool signaled(const char* name) const {
        return std::filesystem::exists(directory_ / name);
    }
    void finish() {
        (*timer_).stop();
        editor_.document.selection = {};
        editor_.document.saved_revision = editor_.document.revision;
        if (!handle_.request_close().accepted()) {
            success_ = false;
            error_ = "Native clipboard window refused to close";
        }
    }
    void tick() {
        try {
            if (std::chrono::steady_clock::now() > deadline_) {
                throw std::runtime_error("Native clipboard peer timed out");
            }
            if (producer_) {
                if (!alpha_sent_ && signaled("request-alpha")) {
                    editor_.copy_alpha_image(specimen());
                    signal("alpha-ready");
                    alpha_sent_ = true;
                }
                if (signaled("done")) {
                    success_ = true;
                    finish();
                }
            } else if (signaled("alpha-ready")) {
                const Image mask = editor_.read_alpha_mask();
                const Image source = specimen();
                if (mask.width != 4 || mask.height != 1) {
                    throw std::runtime_error("Native alpha clipboard dimensions changed");
                }
                for (int x = 0; x < 4; ++x) {
                    const std::uint8_t value = source.get(x, 0).a;
                    if (!equal(mask.get(x, 0), {value, value, value, 255})) {
                        throw std::runtime_error("Native alpha clipboard lost its opaque grayscale mask");
                    }
                }
                signal("done");
                success_ = true;
                finish();
            }
        } catch (const std::exception& error) {
            error_ = error.what();
            finish();
        }
    }

  public:
    NativeClipboardExchange(forms::Editor& editor, gui_forms::Window& window,
                            gui_forms::ApplicationWindowHandle handle, const std::filesystem::path& directory,
                            bool producer)
        : editor_(editor), handle_(handle), directory_(directory), producer_(producer),
          deadline_(std::chrono::steady_clock::now() + std::chrono::seconds(30)) {
        timer_ = std::make_unique<gui_forms::Timer>(window, std::chrono::milliseconds(20));
        tick_ = (*timer_).tick().subscribe(std::bind(&NativeClipboardExchange::tick, std::ref(*this)));
        if (producer_) {
            editor_.document.new_image(4, 1);
            editor_.document.image = specimen();
            editor_.execute("copy");
            signal("image-ready");
        } else {
            editor_.execute("paste");
            const Image expected = specimen();
            const Image actual = editor_.document.selection.image;
            if (!editor_.document.selection.active || actual.width != expected.width ||
                actual.height != expected.height ||
                !std::equal(expected.pixels.begin(), expected.pixels.end(), actual.pixels.begin(), equal)) {
                throw std::runtime_error(
                    "Cross-process Paint clipboard lost RGBA bytes, including hidden RGB");
            }
            signal("request-alpha");
        }
        (*timer_).start();
    }
    bool success() const {
        return success_;
    }
    const std::string& error() const {
        return error_;
    }
};
} // namespace paint::tests

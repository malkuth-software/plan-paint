#include "application.hpp"
#include "codecs.hpp"
#include "idle_render.hpp"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "renderer.hpp"
#include "safe_file.hpp"
#include <SDL3/SDL_main.h>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <sstream>

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    SDL_Window* window = SDL_CreateWindow("Untitled - Plan Paint", 1280, 850,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::cerr << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 1160, 480);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = nullptr;
    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    const ImWchar interface_ranges[] = {0x20, 0x024F, 0x2000, 0x2199, 0x25B2, 0x25C0, 0};
    (*io.Fonts).AddFontFromMemoryTTF(const_cast<unsigned char*>(paint::embedded_font),
                                     paint::embedded_font_size, 18.0f, &font_config, interface_ranges);
    paint::configure_interface_style();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    int exit_code = 0;
    try {
        paint::Application app(window, renderer);
        paint::MouseCursorState mouse_cursor;
        bool trace = false;
        bool demo_reshape = false;
        bool demo_rotation = false;
        double idle_report_seconds = 0;
        std::string idle_report_path;
        for (int i = 1; i < argc; ++i) {
            std::string argument = argv[i];
            if (argument == "--idle-report" && i + 2 < argc) {
                idle_report_seconds = std::clamp(std::stod(argv[++i]), 1.0, 120.0);
                idle_report_path = argv[++i];
            } else if (argument == "--trace") {
                trace = true;
            } else if (argument == "--screenshot" && i + 1 < argc) {
                app.screenshot_path = argv[++i];
                app.screenshot_frame = 8;
            } else if (argument == "--demo-reshape") {
                demo_reshape = true;
            } else if (argument == "--demo-rotation") {
                demo_rotation = true;
            } else if (argument == "--view-tab") {
                app.view_tab = true;
            } else if (argument == "--about") {
                app.about_dialog = true;
            } else if (argument == "--help-sidebar") {
                app.show_help = true;
            } else if (argument == "--edit-colors") {
                app.begin_color();
            } else if (argument == "--demo") {
                app.document.new_image(960, 640);
                paint::Ink ink;
                ink.primary = {79, 129, 189, 255};
                ink.secondary = {219, 229, 241, 255};
                ink.size = 3;
                paint::draw_shape(app.document.image, paint::Shape::RoundedRectangle, {75, 85}, {390, 325},
                                  ink, true, true);
                ink.primary = {155, 187, 89, 255};
                ink.secondary = {235, 241, 222, 255};
                paint::draw_shape(app.document.image, paint::Shape::Oval, {445, 120}, {745, 420}, ink, true,
                                  true);
                ink.primary = {192, 80, 77, 255};
                ink.secondary = {242, 220, 219, 255};
                paint::draw_shape(app.document.image, paint::Shape::Star5, {235, 310}, {535, 570}, ink, true,
                                  true);
                paint::TextStyle text_style;
                text_style.size = 30;
                paint::draw_text(app.document.image, {80, 25}, "A little room for a big imagination.",
                                 text_style, {31, 73, 125, 255}, {}, "");
                app.texture_dirty = true;
            } else if (!argument.starts_with("--")) {
                app.open_image(argument);
            }
        }
        if (demo_rotation) {
            app.document.new_image(960, 640);
            paint::TextStyle text_style;
            text_style.size = 30;
            paint::draw_text(app.document.image, {80, 25}, "A little freedom to turn things around.",
                             text_style, {31, 73, 125, 255}, {}, "");
            paint::Image material;
            material.reset(326, 251, {0, 0, 0, 0});
            paint::Ink ink;
            ink.primary = {79, 129, 189, 255};
            ink.secondary = {219, 229, 241, 255};
            ink.size = 3;
            paint::draw_shape(material, paint::Shape::RoundedRectangle, {6, 6}, {320, 245}, ink, true, true);
            app.document.paste(material, 270, 200);
            app.request_rotation(-25);
        }
        if (demo_reshape) {
            std::vector<paint::Point> outline;
            for (int i = 0; i < 24; ++i) {
                double angle = i * 2.0 * std::numbers::pi / 24;
                outline.push_back({595.0 + 154 * std::cos(angle), 270.0 + 154 * std::sin(angle)});
            }
            app.document.select({440, 115, 310, 310}, outline);
            app.start_reshape();
            app.document.tool = paint::Tool::Reshape;
            std::size_t rightmost = 0;
            for (std::size_t i = 1; i < app.reshape_mesh.nodes.size(); ++i) {
                if (app.reshape_mesh.nodes[i].target.x > app.reshape_mesh.nodes[rightmost].target.x) {
                    rightmost = i;
                }
            }
            paint::Point target = app.reshape_mesh.nodes[rightmost].target;
            if (paint::move_reshape_node(app.reshape_mesh, rightmost, {target.x + 85, target.y - 25})) {
                ++app.mesh_generation;
                app.reshape_render_pending = true;
            }
        }
        paint::IdleRender idle;
        bool input_held = false;
        bool force_present = true;
        bool measuring = false;
        Uint64 measurement_start = 0;
        int measured_frames = 0, measured_presents = 0, measured_visible = 0, measured_focused = 0;
        const Uint64 warmup_end = SDL_GetTicks() + 3000;
        while (app.running) {
            SDL_Event event{};
            bool received =
                SDL_WaitEventTimeout(&event, idle.wait_timeout(input_held, !app.screenshot_path.empty()));
            while (received) {
                if (trace &&
                    (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                     event.type == SDL_EVENT_MOUSE_MOTION)) {
                    std::cerr << "input " << event.type << " " << event.button.x << "," << event.button.y
                              << "\n";
                }
                ImGui_ImplSDL3_ProcessEvent(&event);
                app.event(event);
                idle.activity();
                if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
                    force_present = true;
                }
                if (event.type == SDL_EVENT_RENDER_TARGETS_RESET ||
                    event.type == SDL_EVENT_RENDER_DEVICE_RESET) {
                    force_present = true;
                    app.texture_dirty = true;
                }
                received = SDL_PollEvent(&event);
            }
            idle.framed();
            app.prepare_text_font();
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            app.frame();
            ImGui::Render();
            mouse_cursor.update(ImGui::GetMouseCursor());
            ImDrawData& draw_data = *ImGui::GetDrawData();
            SDL_WindowFlags flags = SDL_GetWindowFlags(window);
            bool visible = !(flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED));
            bool present = (visible || !app.screenshot_path.empty()) &&
                           (force_present || !app.screenshot_path.empty() ||
                            idle.changed(draw_data, app.texture_generation));
            if (present) {
                paint::render_interface(renderer, &draw_data);
                idle.submitted(draw_data, app.texture_generation);
                force_present = false;
            }
            input_held = false;
            for (int button = 0; button < ImGuiMouseButton_COUNT; ++button) {
                input_held = input_held || io.MouseDown[button];
            }
            for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
                input_held = input_held || ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
            }
            ++app.rendered_frames;
            if (!app.screenshot_path.empty() && app.rendered_frames >= app.screenshot_frame &&
                (!demo_reshape || (!app.warp_worker.busy() && !app.reshape_render_pending)) &&
                (!demo_rotation || (!app.warp_worker.busy() && !app.rotation_active))) {
                std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
                    SDL_RenderReadPixels(renderer, nullptr), &SDL_DestroySurface);
                if (surface) {
                    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
                        SDL_ConvertSurface(surface.get(), SDL_PIXELFORMAT_RGBA32), &SDL_DestroySurface);
                    if (rgba) {
                        paint::Image image;
                        image.reset((*rgba).w, (*rgba).h);
                        const std::uint8_t* pixels = static_cast<const std::uint8_t*>((*rgba).pixels);
                        for (int y = 0; y < image.height; ++y) {
                            std::memcpy(image.pixels.data() + static_cast<std::size_t>(y) * image.width,
                                        pixels + y * (*rgba).pitch, image.width * 4);
                        }
                        paint::save_image(image, app.screenshot_path);
                    }
                }
                app.running = false;
            }
            if (present) {
                SDL_RenderPresent(renderer);
            }
            if (idle_report_seconds > 0 && SDL_GetTicks() >= warmup_end) {
                if (!measuring) {
                    measuring = true;
                    measurement_start = SDL_GetTicks();
                } else {
                    ++measured_frames;
                    measured_presents += present ? 1 : 0;
                    measured_visible += visible ? 1 : 0;
                    measured_focused += flags & SDL_WINDOW_INPUT_FOCUS ? 1 : 0;
                    double elapsed = (SDL_GetTicks() - measurement_start) / 1000.0;
                    if (elapsed >= idle_report_seconds) {
                        std::ostringstream report;
                        report << "{\n  \"wall_seconds\": " << elapsed
                               << ",\n  \"gui_frames\": " << measured_frames
                               << ",\n  \"gpu_presentations\": " << measured_presents
                               << ",\n  \"visible_frames\": " << measured_visible
                               << ",\n  \"focused_frames\": " << measured_focused
                               << ",\n  \"framebuffer_scale_x\": " << draw_data.FramebufferScale.x
                               << ",\n  \"framebuffer_scale_y\": " << draw_data.FramebufferScale.y << "\n}\n";
                        const std::string encoded = report.str();
                        const std::vector<std::uint8_t> bytes(encoded.begin(), encoded.end());
                        paint::write_file_atomic(bytes, idle_report_path,
                                                 "Could not write the idle measurement report.");
                        app.running = false;
                    }
                }
            }
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        exit_code = 1;
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}

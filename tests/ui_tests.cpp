#include "application.hpp"
#include "codecs.hpp"
#include "idle_render.hpp"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_internal.h"
#include "paths.hpp"
#include "renderer.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
class UiFixture {
  public:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::unique_ptr<paint::Application> app;
    explicit UiFixture(bool native = false) {
        if (!native) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        }
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(SDL_GetError());
        }
        window = SDL_CreateWindow("Rainstar isolated interaction tests", 1280, 850, SDL_WINDOW_HIDDEN);
        if (!window) {
            throw std::runtime_error(SDL_GetError());
        }
        renderer = SDL_CreateRenderer(window, native ? nullptr : "software");
        if (!renderer) {
            throw std::runtime_error(SDL_GetError());
        }
        ImGui::CreateContext();
        paint::configure_interface_style();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.DisplaySize = {1280, 850};
        io.DeltaTime = 1.0f / 60;
        io.IniFilename = nullptr;
        static const ImWchar ranges[] = {0x20, 0x024f, 0x2000, 0x2199, 0x25b2, 0x25c0, 0};
        ImFontConfig font_config;
        font_config.FontDataOwnedByAtlas = false;
        ImFont* font =
            (*io.Fonts).AddFontFromMemoryTTF(const_cast<unsigned char*>(paint::embedded_font),
                                             paint::embedded_font_size, 18.0f, &font_config, ranges);
        unsigned char* atlas = nullptr;
        int width = 0, height = 0;
        (*io.Fonts).GetTexDataAsRGBA32(&atlas, &width, &height);
        require((*font).FindGlyphNoFallback(0x25bc) && (*font).FindGlyphNoFallback(0x25b2) &&
                    (*font).FindGlyphNoFallback(0x25b6) && (*font).FindGlyphNoFallback(0x25c0),
                "Portsmouth navigation glyphs are missing from embedded UI font");
        ImGui_ImplSDLRenderer3_Init(renderer);
        app = std::make_unique<paint::Application>(window, renderer, false);
        frame();
        frame();
    }
    ~UiFixture() {
        app.reset();
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    void frame() {
        (*app).prepare_text_font();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();
        (*app).frame();
        ImGui::Render();
    }
    void move(float x, float y) {
        ImGui::GetIO().AddMousePosEvent(x, y);
        frame();
    }
    void click(float x, float y, int button = 0) {
        move(x, y);
        ImGui::GetIO().AddMouseButtonEvent(button, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(button, false);
        frame();
    }
    void drag(float x, float y, float end_x, float end_y) {
        move(x, y);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame();
        for (int i = 1; i <= 10; ++i) {
            move(x + (end_x - x) * i / 10, y + (end_y - y) * i / 10);
        }
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
    }
    void key(ImGuiKey key) {
        ImGui::GetIO().AddKeyEvent(key, true);
        frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        frame();
    }
    void wait_work() {
        std::chrono::steady_clock::time_point limit =
            std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while ((*app).warp_worker.busy() || (*app).stamp_render_pending || (*app).reshape_render_pending ||
               (*app).rotation_render_pending || (*app).rotation_commit_pending) {
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            require(std::chrono::steady_clock::now() < limit, "asynchronous transform timed out");
        }
    }
};
paint::Image capture_framebuffer(UiFixture& ui, int width, int height, float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(width), static_cast<float>(height)};
    io.DisplayFramebufferScale = {scale, scale};
    ui.frame();
    ui.frame();
    int pixel_width = static_cast<int>(width * scale);
    int pixel_height = static_cast<int>(height * scale);
    SDL_Texture* target = SDL_CreateTexture(ui.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                            pixel_width, pixel_height);
    require(target != nullptr, SDL_GetError());
    require(SDL_SetRenderTarget(ui.renderer, target), SDL_GetError());
    paint::render_interface(ui.renderer, ImGui::GetDrawData());
    SDL_Surface* surface = SDL_RenderReadPixels(ui.renderer, nullptr);
    require(surface != nullptr, SDL_GetError());
    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    require(rgba != nullptr, SDL_GetError());
    paint::Image capture;
    capture.reset((*rgba).w, (*rgba).h);
    const std::uint8_t* pixels = static_cast<const std::uint8_t*>((*rgba).pixels);
    for (int y = 0; y < capture.height; ++y) {
        std::memcpy(capture.pixels.data() + static_cast<std::size_t>(y) * capture.width,
                    pixels + y * (*rgba).pitch, capture.width * 4);
    }
    SDL_DestroySurface(rgba);
    require(SDL_SetRenderTarget(ui.renderer, nullptr), SDL_GetError());
    SDL_DestroyTexture(target);
    return capture;
}
void retina_rendering(UiFixture& ui, const std::string& screenshot = "") {
    paint::Application& app = *ui.app;
    app.document.new_image(960, 1600);
    app.texture_dirty = true;
    app.show_help = false;
    app.show_status = true;
    app.zoom = 1;
    ui.move(-100, -100);
    const int sizes[3][2] = {{1280, 850}, {1160, 600}, {1440, 900}};
    const float densities[] = {2.0f, 1.5f, 1.0f};
    for (int size = 0; size < 3; ++size) {
        int width = sizes[size][0], height = sizes[size][1];
        paint::Image reference = capture_framebuffer(ui, width, height, 1);
        const int points[5][2] = {
            {width - 30, 300}, {80, 400}, {80, height - 16}, {width - 30, 140}, {500, 100}};
        require(!paint::equal(reference.get(width - 30, 300), {240, 240, 240, 255}),
                "reference frame does not fill the window");
        require(reference.get(80, 400).r == 255, "reference canvas is not visible");
        require(reference.get(80, height - 16).r != 255,
                "reference canvas is not clipped above the status bar");
        for (float density : densities) {
            paint::Image capture = capture_framebuffer(ui, width, height, density);
            if (size == 0 && density == 2 && !screenshot.empty()) {
                paint::save_image(capture, screenshot);
            }
            require(capture.width == static_cast<int>(width * density) &&
                        capture.height == static_cast<int>(height * density),
                    "physical framebuffer dimensions do not match density");
            for (int point = 0; point < 5; ++point) {
                int x = points[point][0], y = points[point][1];
                int physical_x = static_cast<int>((x + 0.5f) * density);
                int physical_y = static_cast<int>((y + 0.5f) * density);
                require(paint::equal(reference.get(x, y), capture.get(physical_x, physical_y)),
                        "Retina pixels differ: window coverage, ribbon or canvas clipping is incorrect");
            }
        }
    }
    ImGui::GetIO().DisplaySize = {1280, 850};
    ImGui::GetIO().DisplayFramebufferScale = {1, 1};
}
void idle_rendering(UiFixture& ui) {
    paint::Application& app = *ui.app;
    app.document.new_image(960, 640);
    app.texture_dirty = true;
    app.choose_tool(paint::Tool::Pencil);
    app.text_active = false;
    app.text_tab = false;
    ui.move(-100, -100);
    for (int frame = 0; frame < 4; ++frame) {
        ui.frame();
    }
    paint::IdleRender idle;
    require(idle.changed(*ImGui::GetDrawData(), app.texture_generation), "first frame must render");
    idle.submitted(*ImGui::GetDrawData(), app.texture_generation);
    ImGui::GetIO().DeltaTime = 0.25f;
    for (int frame = 0; frame < 20; ++frame) {
        ui.frame();
        require(!idle.changed(*ImGui::GetDrawData(), app.texture_generation),
                "idle interface incorrectly requires a GPU submission");
    }
    // Same image geometry and texture ID; only the uploaded texture pixels change.
    app.document.image.set(400, 400, {192, 80, 77, 255});
    app.texture_dirty = true;
    ui.frame();
    require(idle.changed(*ImGui::GetDrawData(), app.texture_generation),
            "texture-only painting change was suppressed by idle renderer");
    idle.submitted(*ImGui::GetDrawData(), app.texture_generation);
    ImDrawData& data = *ImGui::GetDrawData();
    data.FramebufferScale = {2, 2};
    require(idle.changed(data, app.texture_generation), "display-density change was not rendered");
    data.FramebufferScale = {1, 1};
    ImDrawCmd& command = (*data.CmdLists[0]).CmdBuffer[0];
    command.ClipRect.x += 1;
    require(idle.changed(data, app.texture_generation), "clip change was not rendered");
    command.ClipRect.x -= 1;
    command.UserCallback = ImDrawCallback_ResetRenderState;
    require(idle.changed(data, app.texture_generation), "callback frame must never be suppressed");
    command.UserCallback = nullptr;
    ui.move(273, 48);
    int initial_lists = (*ImGui::GetDrawData()).CmdListsCount;
    bool tooltip = false;
    for (int frame = 0; frame < 8; ++frame) {
        ui.frame();
        tooltip = tooltip || (*ImGui::GetDrawData()).CmdListsCount > initial_lists;
    }
    require(tooltip, "tooltip timer stopped while the mouse was stationary");
    ui.move(-100, -100);
    app.choose_tool(paint::Tool::Text);
    ui.click(150, 273);
    ui.frame();
    idle.submitted(*ImGui::GetDrawData(), app.texture_generation);
    int caret_changes = 0;
    for (int frame = 0; frame < 12; ++frame) {
        ui.frame();
        if (idle.changed(*ImGui::GetDrawData(), app.texture_generation)) {
            ++caret_changes;
            idle.submitted(*ImGui::GetDrawData(), app.texture_generation);
        }
    }
    require(caret_changes >= 2 && caret_changes < 12, "text caret did not blink at idle cadence");
    ui.key(ImGuiKey_Escape);
    for (int frame = 0; frame < 3; ++frame) {
        idle.framed();
    }
    require(idle.wait_timeout(false, false) > 200, "idle loop does not wait for input");
    require(idle.wait_timeout(true, false) <= 16, "held input is throttled to idle cadence");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_A, true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_A, false);
    require(idle.wait_timeout(false, false) <= 16, "queued ImGui input was delayed by idle wait");
    ui.frame();
    ui.frame();
    idle.activity();
    require(idle.wait_timeout(false, false) <= 16, "input event did not wake interactive cadence");
    SDL_FlushEvent(SDL_EVENT_USER);
    paint::Image source;
    source.reset(16, 16, {255, 0, 0, 255});
    paint::WarpWorker worker;
    worker.set_completion(paint::wake_event_loop);
    worker.compile(paint::WarpTask::CompileStamp, source);
    SDL_Event event{};
    bool woke = false;
    Uint64 deadline = SDL_GetTicks() + 3000;
    while (!woke && SDL_GetTicks() < deadline) {
        if (SDL_WaitEventTimeout(&event, 100)) {
            woke = event.type == SDL_EVENT_USER;
        }
    }
    require(woke, "completed transform did not wake the event loop");
    paint::WarpResult result;
    require(worker.take(result) && result.field, "worker result was not ready at its wake event");
    ImGui::GetIO().DeltaTime = 1.0f / 60;
}
void drawing_and_controls(UiFixture& ui) {
    paint::Application& app = *ui.app;
    ui.click(273, 48);
    ui.drag(107, 238, 207, 338);
    require(app.document.dirty(), "canvas pointer stroke did not start");
    require(app.document.image.get(100, 100).r == 0, "stroke start pixel missing");
    require(app.document.image.get(200, 200).r == 0, "stroke end pixel missing");
    ui.click(87, 79);
    require(app.document.image.get(100, 100).r == 255, "ribbon Undo failed");
    ui.click(87, 99);
    require(app.document.image.get(100, 100).r == 0, "ribbon Redo failed");
    ui.key(ImGuiKey_F1);
    require(app.show_help, "F1 did not open help");
    ui.key(ImGuiKey_F1);
    require(!app.show_help, "F1 did not close help");
    ui.click(146, 12);
    require(app.view_tab, "View tab failed");
    ui.click(86, 12);
    require(!app.view_tab, "Home tab failed");
    // The second palette row's first Office blue pastel is #C6D9F1.
    ui.click(921, 66);
    require(paint::equal(app.document.ink.primary, {198, 217, 241, 255}),
            "Office palette hit testing failed");
}
void path_and_selection(UiFixture& ui) {
    paint::Application& app = *ui.app;
    app.execute(paint::Command::New);
    app.choose_tool(paint::Tool::Path);
    app.document.continuous_path = true;
    ui.click(100, 213);
    ui.click(220, 213);
    ui.click(220, 323);
    ui.click(101, 214);
    require(app.document.path.nodes.size() == 4, "continuous path terminated at first junction");
    require(app.document.path.nodes[0].x == app.document.path.nodes[3].x &&
                app.document.path.nodes[0].y == app.document.path.nodes[3].y,
            "junction did not snap exactly");
    ui.click(340, 343);
    ui.key(ImGuiKey_Escape);
    require(app.document.path.nodes.empty(), "Escape did not commit continuous path");
    require(app.document.image.get(93, 75).r != 255, "path was not rasterized");
    app.choose_tool(paint::Tool::Select);
    ui.drag(90, 198, 230, 343);
    require(app.document.selection.active, "rectangle selection was not created");
    int previous_x = app.document.selection.x;
    ui.drag(150, 253, 270, 293);
    require(app.document.selection.x == previous_x + 120, "floating selection did not move");
    ui.key(ImGuiKey_Escape);
    require(!app.document.selection.active, "Escape did not place selection");
    app.execute(paint::Command::Undo);
    require(app.document.image.get(93, 75).r != 255, "selection undo did not restore source");
    app.execute(paint::Command::Redo);
    require(app.document.image.get(213, 115).r != 255, "selection redo lost floating pixels");
}
void stamp_and_reshape(UiFixture& ui) {
    paint::Application& app = *ui.app;
    app.execute(paint::Command::New);
    paint::Ink ink;
    ink.primary = {192, 80, 77, 255};
    ink.secondary = {242, 220, 219, 255};
    paint::draw_shape(app.document.image, paint::Shape::Oval, {50, 50}, {140, 140}, ink, true, true);
    app.texture_dirty = true;
    app.stamp_width = 90;
    app.stamp_height = 90;
    app.choose_tool(paint::Tool::Stamp);
    ui.click(102, 233);
    ui.wait_work();
    require(!app.stamp_preview.pixels.empty(), "stamp was not compiled");
    ui.click(307, 338);
    require(app.document.image.get(300, 200).r == 242, "stamp did not place a copy");
    ui.key(ImGuiKey_R);
    ui.key(ImGuiKey_Equal);
    ui.wait_work();
    require(app.stamp_preview.width > 90, "stamp rotation/scale keys failed");
    app.choose_tool(paint::Tool::Select);
    app.document.select({40, 40, 120, 120});
    app.texture_dirty = true;
    app.choose_tool(paint::Tool::Reshape);
    ui.wait_work();
    require(app.reshape_active && app.reshape_field && app.reshape_mesh.nodes.size() > 4,
            "reshape field/mesh missing");
    paint::Point first = app.reshape_mesh.nodes[0].target;
    bool moved = paint::move_reshape_node(app.reshape_mesh, 0, {first.x - 8, first.y - 5});
    require(moved, "reasonable reshape knob move rejected");
    ++app.mesh_generation;
    app.reshape_render_pending = true;
    ui.wait_work();
    ui.key(ImGuiKey_Escape);
    ui.wait_work();
    require(!app.reshape_active && !app.document.selection.active, "Escape did not commit reshape");
    require(app.error.empty(), app.error.c_str());
}
void path_session_and_escape(UiFixture& ui, const std::string& screenshot = "") {
    paint::Application& app = *ui.app;
    app.document.new_image();
    app.zoom = 1;
    app.patterns_tab = app.view_tab = app.text_tab = app.atlas_tab = false;
    app.document.ink = {};
    app.document.shape_outline = true;
    app.document.shape_fill = false;
    app.choose_tool(paint::Tool::Path);
    app.document.continuous_path = true;
    ui.click(100, 220);
    ui.click(260, 220);
    ui.click(260, 380);
    ui.click(420, 380);
    std::size_t history = app.document.undo_history.size();
    ui.click(440, 480, ImGuiMouseButton_Right);
    require(!app.document.path.extending && app.document.path.nodes.size() == 4 &&
                app.document.undo_history.size() == history,
            "right-click added a path node, discarded junctions, or created an undo step");
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    ui.key(ImGuiKey_Z);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    ui.frame();
    require(app.document.path.nodes.size() == 3 && app.document.path.extending &&
                app.document.image.get(330, 242).r == 255 && app.document.image.get(253, 170).r == 0,
            "keyboard Undo did not remove just the last segment and retain the editable path");
    app.command(paint::Command::Redo);
    ui.frame();
    require(app.document.path.nodes.size() == 4 && !app.document.path.extending &&
                app.document.image.get(330, 242).r == 0,
            "Redo lost the stopped path or its last segment");
    app.choose_tool(paint::Tool::Path);
    require(app.document.path.nodes.size() == 4, "reselecting the active tool discarded its junctions");
    ui.click(101, 221);
    require(app.document.path.start == 4 && app.document.path.nodes.back().x == 93 &&
                app.document.path.nodes.back().y == 82 && app.document.image.get(170, 120).r == 255,
            "branch did not snap to an old junction or inserted a connecting segment");
    ui.click(100, 500);
    std::filesystem::path saved = std::filesystem::temp_directory_path() / "rainstar-live-path.png";
    app.save_to(saved.string());
    ui.frame();
    require(app.document.path.nodes.size() == 6 && !app.document.dirty() &&
                paint::load_image(saved.string()).get(93, 300).r == 0,
            "saving lost active nodes, omitted path pixels, or left the saved picture dirty");
    std::filesystem::remove(saved);
    ui.key(ImGuiKey_Escape);
    require(app.document.path.session == 0 && app.document.path.nodes.empty() &&
                app.document.image.get(93, 300).r == 0,
            "Escape failed to release the path while preserving the drawing");
    app.command(paint::Command::Undo);
    ui.frame();
    require(app.document.path.nodes.empty() && app.document.image.get(93, 300).r == 255 &&
                app.document.image.get(330, 242).r == 0,
            "Undo after Escape restored handles or erased older segments");
    app.command(paint::Command::Redo);
    ui.frame();
    require(app.document.path.nodes.empty() && app.document.image.get(93, 300).r == 0,
            "Redo after Escape restored handles or lost the segment");

    // Polygon closure retains its junctions too; a later polygon closes against
    // its own first node, including when that node is shared with an older run.
    app.document.new_image();
    app.document.continuous_path = false;
    ui.click(100, 220);
    ui.click(260, 220);
    ui.click(260, 380);
    ui.click(100, 220);
    require(!app.document.path.extending && app.document.path.nodes.size() == 4,
            "polygon closure discarded the closed polygon's junctions");
    ui.click(100, 220);
    ui.click(100, 500);
    ui.click(400, 500);
    ui.click(100, 220);
    require(!app.document.path.extending && app.document.path.nodes.size() == 8,
            "a second polygon sharing an old junction failed to close");
    app.choose_tool(paint::Tool::Pencil);
    require(app.document.path.nodes.empty(), "a different tool did not release the path session");
    app.command(paint::Command::Undo);
    require(app.document.path.nodes.empty(), "undo after a tool change resurrected path handles");

    // Reset during compilation, lift a replacement before the old result arrives,
    // and confirm only the replacement is ever rendered.
    app.document.new_image();
    app.document.image.reset(960, 640, {220, 30, 40, 255});
    app.choose_tool(paint::Tool::Stamp);
    app.stamp_width = 32;
    app.reset_stamp();
    app.begin_gesture({100, 100}, false);
    require(app.warp_worker.busy(), "stamp compilation was not queued");
    app.begin_gesture({100, 100}, true);
    require(app.document.stamp.pixels.empty() && !app.stamp_field && app.stamp_preview.pixels.empty(),
            "right-click did not immediately clear a compiling stamp");
    app.document.image.reset(960, 640, {30, 80, 220, 255});
    app.begin_gesture({100, 100}, false);
    ui.wait_work();
    require(!app.stamp_preview.pixels.empty() && app.stamp_preview.get(16, 16).b == 220,
            "obsolete compilation replaced the newly lifted stamp");
    ui.click(450, 450, ImGuiMouseButton_Right);
    require(app.document.stamp.pixels.empty() && app.stamp_texture == nullptr &&
                app.document.undo_history.empty(),
            "right-click placed a stamp instead of returning to sample selection");
    app.begin_gesture({100, 100}, false);
    ui.key(ImGuiKey_Escape);
    ui.wait_work();
    require(app.document.stamp.pixels.empty() && app.stamp_preview.pixels.empty() && !app.stamp_field &&
                !app.stamp_render_pending && app.stamp_texture == nullptr,
            "Escape failed to release a stamp or an obsolete background result revived it");

    app.choose_tool(paint::Tool::Shape);
    app.document.shape = paint::Shape::Rectangle;
    ui.move(200, 300);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    ui.frame();
    ui.move(400, 500);
    require(app.dragging && app.preview_active, "shape drag did not start");
    app.hotspot_pick = true;
    ui.key(ImGuiKey_Escape);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    ui.frame();
    require(!app.dragging && !app.preview_active && !app.hotspot_pick && app.document.undo_history.empty(),
            "Escape did not free the pending shape or hotspot operation");

    app.choose_tool(paint::Tool::Path);
    app.document.continuous_path = true;
    ui.click(100, 220);
    ui.click(260, 220);
    const paint::Command modal_commands[] = {paint::Command::Resize, paint::Command::Properties,
                                             paint::Command::PrintPreview, paint::Command::About,
                                             paint::Command::Quit};
    for (paint::Command command : modal_commands) {
        app.command(command);
        ui.frame();
        ui.frame();
        require(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
                "test dialog did not open");
        ui.key(ImGuiKey_Escape);
        require(!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                    app.running && app.document.path.nodes.size() == 2,
                "Escape did not cancel the dialog alone, or changed the active picture");
    }
    app.atlas_grid_dialog = true;
    ui.frame();
    ui.key(ImGuiKey_Escape);
    require(app.document.atlas.kind == paint::AtlasKind::None &&
                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
            "Escape did not dismiss sprite grid setup without applying it");
    app.icon_sizes_dialog = true;
    app.deferred_after_save = true;
    ui.frame();
    ui.key(ImGuiKey_Escape);
    require(!app.deferred_after_save &&
                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
            "Escape did not cancel icon export and its deferred command");
    app.error = "Dismiss this test message.";
    ui.frame();
    ui.key(ImGuiKey_Escape);
    require(app.error.empty() &&
                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
            "Escape did not dismiss the message dialog");
    ui.key(ImGuiKey_Escape);
    require(app.document.path.nodes.empty(), "Escape did not release the tool after dismissing dialogs");

    if (!screenshot.empty()) {
        app.document.new_image();
        app.choose_tool(paint::Tool::Path);
        app.document.continuous_path = true;
        app.document.ink.primary = {25, 70, 145, 255};
        app.document.ink.size = 3;
        ui.click(180, 280);
        ui.click(440, 280);
        ui.click(440, 530);
        ui.click(180, 280);
        ui.click(180, 280, ImGuiMouseButton_Right);
        ui.click(440, 280);
        ui.click(700, 430);
        ui.click(440, 530);
        ui.click(440, 530, ImGuiMouseButton_Right);
        ui.move(440, 280);
        paint::save_image(capture_framebuffer(ui, 1280, 850, 2), screenshot);
    }
    app.document.new_image();
    app.choose_tool(paint::Tool::Pencil);
    app.texture_dirty = true;
    ui.frame();
}
void text_and_stale_transform(UiFixture& ui) {
    paint::Application& app = *ui.app;
    app.execute(paint::Command::New);
    app.choose_tool(paint::Tool::Text);
    ui.click(120, 233);
    ui.frame();
    ImGui::GetIO().AddInputCharactersUTF8("Hello Portsmouth");
    ui.frame();
    require(std::string(app.text_buffer) == "Hello Portsmouth",
            "text did not receive initial keyboard focus");
    app.text_style.bold = true;
    app.text_style.size = 36;
    ui.frame();
    require(app.text_layout.line_height >= 36 && !app.text_preview.pixels.empty(),
            "live text raster did not update");
    app.finish_text();
    ui.frame();
    unsigned marks = 0;
    for (paint::Color pixel : app.document.image.pixels) {
        if (pixel.r < 250) {
            ++marks;
        }
    }
    require(marks > 80, "styled text was not rasterized");
    app.document.select({50, 50, 80, 80});
    app.start_reshape();
    app.command(paint::Command::Undo);
    ui.wait_work();
    require(!app.reshape_active && !app.reshape_field, "completed background compilation survived Undo");
    require(app.error.empty(), app.error.c_str());
}
void text_object_controls(UiFixture& ui, const std::string& screenshot = "") {
    paint::Application& app = *ui.app;
    app.execute(paint::Command::New);
    app.patterns_tab = app.view_tab = false;
    app.zoom = 1;
    app.choose_tool(paint::Tool::Text);
    ui.click(120, 233);
    ImGui::GetIO().AddInputCharactersUTF8(
        "Watercolor words wrap across the page. Café and a bright new day.");
    ui.frame();
    std::string content(app.text_buffer);
    require(content.find("Café") != std::string::npos, "UTF-8 text input failed");
    app.text_width = 175;
    app.text_style.size = 28;
    app.text_style.word_wrap = true;
    ui.frame();
    int wrapped_height = app.text_layout.height;
    require(wrapped_height > app.text_layout.line_height * 2, "text did not wrap at its box width");
    ui.click(447, 77);
    require(!app.text_style.word_wrap && app.text_layout.height == app.text_layout.line_height,
            "ribbon word wrap toggle did not change live layout");
    require(std::string(app.text_buffer) == content, "soft wrapping modified text content");
    ui.click(447, 77);
    ui.drag(155, 215, 215, 255);
    require(std::abs(app.text_origin.x - 173) < 1 && std::abs(app.text_origin.y - 135) < 1,
            "floating Move text control did not drag the text object");
    ui.frame();
    float right = 7 + static_cast<float>(app.text_origin.x) + app.text_width;
    float bottom = 138 + static_cast<float>(app.text_origin.y) + app.text_height;
    int old_width = app.text_width, old_height = app.text_height;
    ui.drag(right, bottom, right + 220, bottom + 70);
    require(app.text_width == old_width + 220 && app.text_height == old_height + 70,
            "text corner handle did not enlarge the box");
    require(app.text_layout.height < wrapped_height, "resizing the text box did not reflow words");
    app.text_style.bold = app.text_style.italic = app.text_style.underline = app.text_style.strikeout = false;
    ui.frame();
    const float format_x[4] = {224, 300, 224, 349};
    const float format_y[4] = {46, 46, 76, 76};
    for (int style = 0; style < 4; ++style) {
        paint::Image before = app.text_preview;
        ui.click(format_x[style], format_y[style]);
        require(std::memcmp(before.pixels.data(), app.text_preview.pixels.data(), before.pixels.size() * 4) !=
                    0,
                "text formatting button did not change the preview pixels");
    }
    if (!screenshot.empty()) {
        paint::save_image(capture_framebuffer(ui, 1280, 850, 1), screenshot);
    }
    paint::Image expected = app.document.image;
    paint::composite(expected, app.text_preview, static_cast<int>(app.text_origin.x),
                     static_cast<int>(app.text_origin.y));
    float toolbar_x = 7 + static_cast<float>(app.text_origin.x) - 5;
    float toolbar_y = 138 + static_cast<float>(app.text_origin.y) - 32;
    ui.click(toolbar_x + 130, toolbar_y + 15);
    require(!app.text_active, "floating Place text button did not commit");
    require(std::memcmp(expected.pixels.data(), app.document.image.pixels.data(),
                        expected.pixels.size() * 4) == 0,
            "placed text differs from the live preview raster");
    app.choose_tool(paint::Tool::Text);
    ui.click(120, 233);
    ImGui::GetIO().AddInputCharactersUTF8("Cancel this text");
    ui.frame();
    ui.click(115 + 205, 201 + 15);
    require(!app.text_active, "floating Cancel button did not dismiss text");
    require(std::memcmp(expected.pixels.data(), app.document.image.pixels.data(),
                        expected.pixels.size() * 4) == 0,
            "cancelling text modified the picture");
    app.choose_tool(paint::Tool::Text);
    ui.click(120, 233);
    ImGui::GetIO().AddInputCharactersUTF8("café");
    ui.frame();
    ui.key(ImGuiKey_Backspace);
    require(std::string(app.text_buffer) == "caf", "backspace split a UTF-8 character");
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    ui.key(ImGuiKey_Z);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    ui.frame();
    require(std::string(app.text_buffer) == "café", "text undo did not restore the removed character");
    ui.key(ImGuiKey_Escape);
    require(!app.text_active, "Escape did not cancel the text box");
    require(app.error.empty(), app.error.c_str());
}
void material_ribbon_and_circle(UiFixture& ui, const std::string& screenshot = "") {
    paint::Application& app = *ui.app;
    app.execute(paint::Command::New);
    app.patterns_tab = app.view_tab = app.text_tab = false;
    app.zoom = 1;
    app.document.ink.primary = {0, 0, 0, 255};
    app.document.ink.brush = paint::Brush::Round;
    app.document.ink.size = 1;
    app.document.shape_fill = false;
    app.document.shape_outline = true;
    ui.click(581, 91);
    require(app.document.shape == paint::Shape::Circle, "dedicated Circle ribbon option is missing");
    ui.drag(120, 243, 260, 293);
    int min_x = 960, max_x = 0, min_y = 640, max_y = 0;
    for (int y = 0; y < 640; ++y) {
        for (int x = 0; x < 960; ++x) {
            if (app.document.image.get(x, y).r < 100) {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
    }
    require(max_x - min_x == max_y - min_y && max_x - min_x >= 138,
            "unequal drag drew an oval with Circle selected");
    ui.click(253, 13);
    require(app.patterns_tab &&
                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
            "Patterns and tools opened a menu instead of a ribbon");
    ui.click(40, 103);
    require(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
            "stamp dropdown is not beneath Stamp");
    ui.key(ImGuiKey_Escape);
    ui.click(110, 103);
    require(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
            "mesh spacing dropdown is not beneath Mesh");
    ui.key(ImGuiKey_Escape);
    if (!screenshot.empty()) {
        app.document.new_image();
        const paint::Brush media[8] = {paint::Brush::Watercolor, paint::Brush::Oil,     paint::Brush::Bristle,
                                       paint::Brush::Crayon,     paint::Brush::Pencil,  paint::Brush::Marker,
                                       paint::Brush::Pastel,     paint::Brush::Charcoal};
        for (int i = 0; i < 8; ++i) {
            int x = 12 + (i % 4) * 237, y = 10 + (i / 4) * 305;
            paint::TextStyle label;
            label.size = 23;
            paint::draw_text(app.document.image, {static_cast<double>(x), static_cast<double>(y)},
                             paint::brush_names[static_cast<int>(media[i])], label, {34, 55, 82, 255}, {},
                             "");
            paint::Ink ink;
            ink.primary = ink.secondary = {43, 93, 141, 255};
            ink.brush = media[i];
            ink.paper_roughness = 0.85;
            ink.grain_scale = 1.35;
            ink.pigment_load = 0.72;
            ink.size = 22;
            paint::draw_shape(app.document.image, paint::Shape::RoundedRectangle, {x + 3.0, y + 40.0},
                              {x + 218.0, y + 204.0}, ink, false, true, media[i]);
            paint::MaterialStroke coat;
            const paint::Point points[4] = {
                {x + 10.0, y + 255.0}, {x + 73.0, y + 235.0}, {x + 132.0, y + 262.0}, {x + 211.0, y + 241.0}};
            for (int j = 1; j < 4; ++j) {
                coat.segment(app.document.image, points[j - 1], points[j], ink);
            }
        }
        app.document.ink.primary = {40, 88, 140, 255};
        app.document.ink.secondary = {143, 183, 216, 255};
        app.document.ink.brush = paint::Brush::Oil;
        app.document.shape_fill_brush = paint::Brush::Watercolor;
        app.texture_dirty = true;
        paint::save_image(capture_framebuffer(ui, 1280, 850, 1), screenshot);
        paint::save_image(app.document.image, screenshot + ".swatches.png");
    }
    ui.click(85, 13);
    require(!app.patterns_tab, "Home did not restore the main ribbon");
    require(app.error.empty(), app.error.c_str());
}
void geometry_preview(UiFixture& ui, const std::string& screenshot = "") {
    paint::Application& app = *ui.app;
    app.execute(paint::Command::New);
    app.zoom = 1;
    app.patterns_tab = app.view_tab = app.text_tab = false;
    app.document.ink.primary = {25, 70, 145, 170};
    app.document.ink.secondary = {90, 160, 200, 100};
    app.document.ink.brush = paint::Brush::Round;
    app.document.ink.size = 3;
    app.document.shape_outline = true;
    app.document.shape_fill = false;
    app.choose_tool(paint::Tool::Path);
    app.document.continuous_path = true;
    ui.click(120, 240);
    ui.click(440, 280);
    ui.click(250, 520);
    ui.click(120, 240);
    ui.move(1100, 500);
    paint::Image pending = capture_framebuffer(ui, 1280, 850, 1);
    ui.key(ImGuiKey_Escape);
    paint::Image placed = capture_framebuffer(ui, 1280, 850, 1);
    for (int y = 160; y < 700; ++y) {
        for (int x = 40; x < 940; ++x) {
            require(paint::equal(pending.get(x, y), placed.get(x, y)),
                    "path preview differs from committed stroke coverage");
        }
    }
    if (!screenshot.empty()) {
        app.document.new_image();
        const int widths[3] = {1, 3, 7};
        for (int row = 0; row < 3; ++row) {
            paint::Ink ink;
            ink.size = widths[row];
            ink.primary = {30, 65, 105, 255};
            ink.secondary = {120, 175, 200, 150};
            int y = 40 + row * 195;
            std::vector<paint::Point> points = {
                {30.0, double(y)}, {215.0, y + 22.0}, {350.0, y + 135.0}, {510.0, y + 15.0}};
            paint::polygon(app.document.image, points, ink, true, false, false);
            paint::draw_shape(app.document.image, paint::Shape::Triangle, {550.0, double(y)},
                              {710.0, y + 150.0}, ink, true, true);
            paint::draw_shape(app.document.image, paint::Shape::Circle, {765.0, double(y)}, {915.0, y + 85.0},
                              ink, true, true);
        }
        app.texture_dirty = true;
        paint::save_image(capture_framebuffer(ui, 1280, 850, 1), screenshot);
    }
}
void pointed_tools(UiFixture& ui, const std::string& screenshot = "") {
    paint::Application& app = *ui.app;
    app.execute(paint::Command::New);
    app.document.new_image(96, 64);
    app.zoom = 16;
    app.show_rulers = false;
    app.patterns_tab = app.view_tab = app.text_tab = false;
    app.choose_tool(paint::Tool::Pencil);
    app.document.ink.primary = {28, 100, 185, 255};
    app.document.ink.pattern = paint::Pattern::Solid;
    ui.move(185, 315);
    int px = int(app.hover.x), py = int(app.hover.y);
    paint::Image original = app.document.image;
    paint::Image pencil = capture_framebuffer(ui, 1280, 850, 1);
    require(std::equal(app.document.image.pixels.begin(), app.document.image.pixels.end(),
                       original.pixels.begin(), paint::equal),
            "pencil hover modified the document");
    int sample_x = 185 + int((px + .5 - app.hover.x) * app.zoom);
    int sample_y = 315 + int((py + .5 - app.hover.y) * app.zoom);
    require(pencil.get(sample_x, sample_y).b > pencil.get(sample_x, sample_y).r,
            "pencil preview did not fill the hovered pixel");
    ui.click(185, 315);
    require(paint::equal(app.document.image.get(px, py), app.document.ink.primary),
            "pencil click missed previewed pixel");
    app.choose_tool(paint::Tool::Eraser);
    app.document.ink.size = 8;
    ui.move(185, 315);
    paint::Image erased_preview = capture_framebuffer(ui, 1280, 850, 1);
    require(erased_preview.get(180, 300).r > erased_preview.get(180, 300).g,
            "eraser preview is not visibly pink");
    if (!screenshot.empty()) {
        paint::save_image(erased_preview, screenshot + ".eraser.png");
    }
    ui.click(185, 315);
    require(app.document.image.get(px, py).a == 0, "hard eraser did not clear pointed pixel");
    app.command(paint::Command::Undo);
    require(paint::equal(app.document.image.get(px, py), app.document.ink.primary), "eraser Undo failed");
    ui.click(280, 75);
    require(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
            "eraser has no settings menu");
    ui.key(ImGuiKey_Escape);
    app.zoom = 1;
    app.document.new_image(960, 640);
    for (int y = 0; y < 640; ++y) {
        for (int x = 0; x < 960; ++x) {
            app.document.image.set(
                x, y,
                {std::uint8_t((x / 16) % 2 ? 20 : 230), std::uint8_t((y / 16) % 2 ? 60 : 210), 150, 255});
        }
    }
    app.texture_dirty = true;
    app.choose_tool(paint::Tool::Magnifier);
    ui.move(400, 360);
    ui.frame();
    ui.frame();
    paint::Point pointed = app.hover;
    original = app.document.image;
    if (!screenshot.empty()) {
        paint::save_image(capture_framebuffer(ui, 1280, 850, 1), screenshot + ".magnifier.png");
    }
    require(std::equal(app.document.image.pixels.begin(), app.document.image.pixels.end(),
                       original.pixels.begin(), paint::equal),
            "magnifier hover modified the image");
    ui.click(400, 360);
    ui.frame();
    ui.frame();
    ui.frame();
    require(app.zoom == 2 && std::abs(app.hover.x - pointed.x) < 1 && std::abs(app.hover.y - pointed.y) < 1,
            "magnifier did not preserve the pointed canvas location");
    app.zoom = 8;
    ui.frame();
    ui.click(400, 360);
    ui.frame();
    ui.frame();
    require(app.zoom == 16, "extra 1600-percent zoom level is unavailable");
    app.zoom = 1;
    app.execute(paint::Command::New);
    ui.frame();
    ui.frame();
}
void recent_files_and_desktop_layouts(UiFixture& ui) {
    paint::Application& app = *ui.app;
    app.zoom = 1;
    ui.click(1258, 837);
    require(app.zoom == 2, "status-bar zoom-in button failed");
    ui.click(1088, 837);
    require(app.zoom == 1, "status-bar zoom-out button failed");
    paint::Image material;
    material.reset(2, 2, {10, 30, 60, 255});
    paint::Image tiled = paint::wallpaper_image(material, 7, 5, paint::WallpaperLayout::Tile);
    require(tiled.width == 7 && tiled.height == 5 && paint::equal(tiled.pixels.back(), material.pixels[0]),
            "tiled wallpaper left an unpainted edge");
    paint::Image centered = paint::wallpaper_image(material, 6, 6, paint::WallpaperLayout::Center);
    require(centered.pixels[0].r == 255 && paint::equal(centered.pixels[14], material.pixels[0]),
            "centered wallpaper did not preserve native size and center");
    paint::Image filled = paint::wallpaper_image(material, 7, 5, paint::WallpaperLayout::Fill);
    require(paint::equal(filled.pixels.front(), material.pixels[0]) &&
                paint::equal(filled.pixels.back(), material.pixels[0]),
            "CONV wallpaper fill did not cover the desktop");
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("rainstar-recent-test-" + std::to_string(SDL_GetTicksNS()));
    std::filesystem::create_directory(directory);
    std::u8string encoded = (directory / "recent.bin").u8string();
    paint::RecentFiles recent;
    recent.storage_path = std::string(encoded.begin(), encoded.end());
    recent.remember("a\\b\nimage.png");
    recent.remember("日本語 image.png");
    recent.remember("a\\b\nimage.png");
    paint::RecentFiles reloaded;
    reloaded.storage_path = recent.storage_path;
    reloaded.load();
    require(reloaded.paths == recent.paths && reloaded.paths.size() == 2,
            "recent files lost Unicode/newlines or retained duplicates");
    for (int index = 0; index < 20; ++index) {
        recent.remember(std::to_string(index));
    }
    require(recent.paths.size() == 12 && recent.paths[0] == "19", "recent files did not bound history");
    app.execute(paint::Command::New);
    encoded = (directory / paint::path_from_utf8("日本語.png")).u8string();
    const std::string picture_path(encoded.begin(), encoded.end());
    app.save_to(picture_path);
    require(app.recent_files.paths.front() == picture_path && app.status.find("日本語") != std::string::npos,
            "saving did not publish the Unicode recent-file entry");
    ui.frame();
    app.document.checkpoint();
    app.document.image.pixels[0] = {20, 40, 60, 255};
    app.recent_to_open = picture_path;
    app.command(paint::Command::OpenRecent);
    require(app.unsaved_dialog && app.deferred_command == paint::Command::OpenRecent,
            "opening recent picture bypassed the unsaved-work prompt");
    app.unsaved_dialog = false;
    app.execute(paint::Command::OpenRecent);
    require(!app.document.dirty() && app.document.filename == picture_path,
            "recent-picture command did not load the selected document");
    std::filesystem::remove_all(directory);
}
void custom_colors_and_cursor(UiFixture& ui) {
    paint::Application& app = *ui.app;
    app.execute(paint::Command::New);
    app.choose_tool(paint::Tool::Pencil);
    ui.move(400, 323);
    require(ImGui::GetMouseCursor() == ImGuiMouseCursor_None,
            "painting cursor did not use its canvas preview");
    ui.move(90, 13);
    for (int frame = 0; frame < 20; ++frame) {
        ui.frame();
        require(ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow, "menu cursor request was unstable");
    }
    app.begin_color();
    ui.frame();
    ui.frame();
    app.edit_color({18, 52, 86, 255});
    ImGuiWindow* dialog = ImGui::FindWindowByName("Edit Colors");
    require(dialog != nullptr, "Edit Colors dialog did not open");
    const ImVec2 position = (*dialog).Pos;
    ui.click(position.x + 136, position.y + 289);
    require((app.custom_colors.occupied & 1) && paint::equal(app.custom_colors.colors[0], {18, 52, 86, 255}),
            "Add to custom colors did not retain the edited color");
    app.edit_color({200, 20, 50, 255});
    ui.click(position.x + 26, position.y + 221);
    require(std::string(app.edited_hex) == "#123456", "saved custom swatch did not restore its color");
    ui.click(position.x + 64, position.y + (*dialog).Size.y - 26);
    require(paint::equal(app.document.ink.primary, {18, 52, 86, 255}),
            "Edit Colors OK did not apply the saved color");
    app.begin_color();
    ui.frame();
    app.edit_color({240, 20, 40, 255});
    ui.key(ImGuiKey_Escape);
    require(paint::equal(app.document.ink.primary, {18, 52, 86, 255}), "Cancel changed the drawing color");
    std::filesystem::path file = std::filesystem::temp_directory_path() /
                                 ("rainstar-colors-" + std::to_string(SDL_GetTicksNS()) + ".bin");
    const std::u8string encoded = file.u8string();
    paint::CustomColors saved;
    saved.storage_path = std::string(encoded.begin(), encoded.end());
    saved.store(0, {18, 52, 86, 255});
    saved.store(15, {207, 121, 52, 255});
    paint::CustomColors loaded;
    loaded.storage_path = saved.storage_path;
    loaded.load();
    require(loaded.occupied == 0x8001 && paint::equal(loaded.colors[0], saved.colors[0]) &&
                paint::equal(loaded.colors[15], saved.colors[15]),
            "custom colors did not survive reload");
    std::filesystem::remove(file);
    app.stamp_angle = 90;
    app.stamp_scale = 2;
    app.reset_stamp();
    require(app.document.stamp.pixels.empty() && app.stamp_preview.pixels.empty() && !app.stamp_field &&
                app.stamp_angle == 0 && app.stamp_scale == 1 && app.document.tool == paint::Tool::Stamp,
            "Lift a new stamp did not reset the material, rotation and scale");
}
void arbitrary_rotation(UiFixture& ui) {
    paint::Application& app = *ui.app;
    app.execute(paint::Command::New);
    app.choose_tool(paint::Tool::Select);
    for (int y = 40; y < 80; ++y) {
        for (int x = 40; x < 120; ++x) {
            app.document.image.set(x, y, {static_cast<std::uint8_t>(x), 100, 200, 255});
        }
    }
    app.document.select({40, 40, 80, 40});
    app.texture_dirty = true;
    ui.frame();
    ui.drag(145, 160, 155, 212);
    ui.wait_work();
    require(!app.rotation_active && app.document.selection.active &&
                app.document.selection.image.height > 60 && app.rotation_angle > 30 &&
                app.rotation_angle < 60,
            "selection corner handle did not perform an arbitrary CONV rotation");
    require(app.document.selection.image.pixels.front().a == 0, "rotation lost its transparent corners");
    ui.key(ImGuiKey_Escape);
    require(!app.document.selection.active, "Escape did not place the rotated selection");
    app.command(paint::Command::Undo);
    require(app.document.image.get(40, 40).r == 40, "Undo did not restore the pre-rotation object");
    app.document.new_image(64, 32);
    app.request_rotation(37);
    ui.wait_work();
    require(!app.rotation_active && !app.document.selection.active && app.document.image.width > 64 &&
                app.document.image.height > 32,
            "precise whole-picture rotation did not expand its canvas");
    app.document.new_image();
    app.document.select({40, 40, 80, 40});
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, true);
    ui.frame();
    ui.drag(145, 160, 150, 223);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);
    ui.wait_work();
    require(std::abs(std::remainder(app.rotation_angle, 15.0)) < 1e-8,
            "Shift did not constrain the rotation handle to 15-degree steps");
    ui.key(ImGuiKey_Escape);
    app.document.select({8, 8, 24, 24});
    app.start_rotation();
    app.command(paint::Command::Undo);
    ui.wait_work();
    require(!app.rotation_active && !app.rotation_field && !app.document.selection.active,
            "obsolete rotation compilation survived Undo");
    require(app.error.empty(), app.error.c_str());
}
void curve_interactions(UiFixture& ui, const std::string& screenshot = "") {
    paint::Application& app = *ui.app;
    app.execute(paint::Command::New);
    app.document.new_image(960, 640);
    app.zoom = 1;
    app.show_rulers = false;
    app.atlas_tab = app.patterns_tab = app.view_tab = app.text_tab = false;
    app.document.ink.primary = {20, 75, 145, 170};
    app.document.ink.size = 5;
    app.document.ink.brush = paint::Brush::Round;
    app.document.ink.pattern = paint::Pattern::Solid;
    app.choose_shape(paint::Shape::Bezier);
    ui.frame();
    ui.move(300, 350);
    float ox = 300 - static_cast<float>(app.hover.x), oy = 350 - static_cast<float>(app.hover.y);
    paint::Image blank = app.document.image;
    ui.click(ox + 100, oy + 150);
    ui.move(ox + 450, oy + 250);
    require(app.document.curve.base && !app.document.curve.line_set && app.document.undo_history.empty() &&
                !app.document.dirty(),
            "first curve anchor modified the document");
    ui.key(ImGuiKey_Escape);
    require(!app.document.curve.base && !app.preview_active &&
                std::equal(blank.pixels.begin(), blank.pixels.end(), app.document.image.pixels.begin(),
                           paint::equal),
            "Escape preserved a lone curve anchor or its hover line");
    ui.move(ox + 100, oy + 150);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    ui.frame();
    ui.move(ox + 450, oy + 150);
    ui.key(ImGuiKey_Escape);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    ui.frame();
    require(!app.document.curve.base && app.document.undo_history.empty() && !app.document.dirty(),
            "Escape during an unset baseline drag preserved a line");
    ui.click(ox + 100, oy + 200);
    ui.click(ox + 700, oy + 200);
    require(app.document.curve.line_set && app.document.curve.geometry.handle_count() == 2,
            "two endpoint clicks did not establish a Bézier with two handles");
    paint::Image baseline = app.document.image;
    ui.drag(ox + 300, oy + 200, ox + 150, oy + 50);
    require(std::abs(app.document.curve.geometry.first_control.x - 150) < .01 &&
                std::abs(app.document.curve.geometry.first_control.y - 50) < .01,
            "first Bézier control did not follow its drag");
    ui.drag(ox + 500, oy + 200, ox + 650, oy + 350);
    require(std::abs(app.document.curve.geometry.second_control.y - 350) < .01,
            "second Bézier control did not follow its drag");
    require(app.document.undo_history.size() == 3, "handle movements were not one undo step per drag");
    app.command(paint::Command::Undo);
    ui.frame();
    require(app.document.curve.line_set && app.document.curve.geometry.second_control.y == 200,
            "Bézier Undo did not restore an editable handle");
    app.command(paint::Command::Redo);
    ui.frame();
    require(app.document.curve.geometry.second_control.y == 350, "Bézier Redo lost its control");
    ui.drag(ox + 150, oy + 50, ox + 180, oy + 80);
    require(app.document.curve.geometry.first_control.x == 180,
            "Bézier handle stopped accepting repeated drags");
    std::filesystem::path path = std::filesystem::temp_directory_path() / "rainstar-editable-curve.png";
    app.save_to(path.string());
    require(app.error.empty(), app.error.c_str());
    require(app.document.curve.line_set && !app.document.dirty(), "Save released the editable curve");
    paint::Image saved = paint::load_image(path.string());
    require(
        std::equal(saved.pixels.begin(), saved.pixels.end(), app.document.image.pixels.begin(), paint::equal),
        "Save differs from accepted curve pixels");
    std::filesystem::remove(path);
    if (!screenshot.empty()) {
        ui.move(1100, 600);
        paint::save_image(capture_framebuffer(ui, 1280, 850, 1), screenshot + ".bezier.png");
    }
    paint::Image accepted = app.document.image;
    ui.key(ImGuiKey_Escape);
    require(!app.document.curve.base && !app.preview_active &&
                std::equal(accepted.pixels.begin(), accepted.pixels.end(), app.document.image.pixels.begin(),
                           paint::equal),
            "Escape changed an accepted curve or retained its handles");
    app.choose_shape(paint::Shape::Arc);
    ui.drag(ox + 100, oy + 420, ox + 700, oy + 420);
    require(app.document.curve.line_set && app.document.curve.geometry.handle_count() == 1,
            "dragged baseline did not establish an arc with one handle");
    ui.drag(ox + 400, oy + 420, ox + 480, oy + 570);
    paint::Point middle = app.document.curve.geometry.handle(0);
    require(std::abs(middle.x - 400) < .01 && std::abs(middle.y - 570) < .01,
            "arc handle did not constrain to the chord midpoint normal");
    require(app.document.image.get(400, 570).b < 245, "arc missed its middle control");
    ui.drag(ox + 400, oy + 570, ox + 400, oy + 510);
    require(app.document.curve.geometry.bulge == 90, "arc handle stopped after one adjustment");
    if (!screenshot.empty()) {
        ui.move(1100, 600);
        paint::save_image(capture_framebuffer(ui, 1280, 850, 1), screenshot);
    }
    accepted = app.document.image;
    app.choose_shape(paint::Shape::Bezier);
    require(!app.document.curve.base && std::equal(accepted.pixels.begin(), accepted.pixels.end(),
                                                   app.document.image.pixels.begin(), paint::equal),
            "switching curve forms did not preserve the accepted arc");
    ui.click(ox + 100, oy + 100);
    app.choose_tool(paint::Tool::Pencil);
    require(!app.document.curve.base && std::equal(accepted.pixels.begin(), accepted.pixels.end(),
                                                   app.document.image.pixels.begin(), paint::equal),
            "changing tools preserved an unset curve anchor");
    app.document.new_image();
    app.texture_dirty = true;
    ui.frame();
}
void atlas_interactions(UiFixture& ui, const std::string& screenshot = "") {
    paint::Application& app = *ui.app;
    app.document.new_image(96, 96);
    app.zoom = 8;
    app.texture_dirty = true;
    app.choose_tool(paint::Tool::Pencil);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            for (int y = 2; y < 30; ++y) {
                for (int x = 2; x < 30; ++x) {
                    if ((x - 16) * (x - 16) + (y - 16) * (y - 16) < 150) {
                        app.document.image.set(column * 32 + x, row * 32 + y,
                                               {static_cast<std::uint8_t>(70 + column * 60),
                                                static_cast<std::uint8_t>(60 + row * 70), 190, 255});
                    }
                }
            }
        }
    }
    paint::AtlasGrid grid;
    grid.rows = 3;
    grid.columns = 3;
    app.document.configure_atlas(grid);
    ui.click(367, 13);
    require(app.atlas_tab, "Atlas tab did not activate");
    ui.click(210, 85);
    require(app.document.atlas.active == 1, "ribbon thumbnail did not select a frame");
    ui.key(ImGuiKey_RightArrow);
    require(app.document.atlas.active == 2, "right arrow did not step Atlas");
    ui.key(ImGuiKey_LeftArrow);
    require(app.document.atlas.active == 1, "left arrow did not step Atlas");
    ui.click(90, 13);
    require(!app.atlas_tab, "Home did not leave Atlas ribbon");
    app.document.ink.primary = {10, 20, 30, 255};
    ui.click(44, 174);
    ui.key(ImGuiKey_RightArrow);
    require(app.document.atlas.active == 2, "Atlas keys failed while painting in Home");
    ui.key(ImGuiKey_LeftArrow);
    require(app.document.image.get(4, 4).r == 10, "paint edit lost while switching frames");
    ui.click(367, 13);
    ui.click(265, 44);
    ui.frame();
    require(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
            "Atlas gallery did not expand");
    ImGuiContext& context = *ImGui::GetCurrentContext();
    ImGuiWindow* grid_window = nullptr;
    for (int i = 0; i < context.Windows.Size; ++i) {
        if (std::strstr((*context.Windows[i]).Name, "Atlas grid gallery")) {
            grid_window = context.Windows[i];
        }
    }
    require(grid_window != nullptr, "expanded 2D gallery missing");
    ImVec2 start = (*grid_window).Pos;
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    ui.frame();
    ui.click(start.x + 80, start.y + 85);
    ui.click(start.x + 80, start.y + 155);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    ui.frame();
    require(app.document.atlas.sequence == std::vector<int>({1, 4, 7}),
            "Ctrl-click did not select a vertical sequence");
    if (!screenshot.empty()) {
        paint::save_image(capture_framebuffer(ui, 1280, 850, 1), screenshot + ".gallery.png");
    }
    ui.key(ImGuiKey_RightArrow);
    require(app.document.atlas.active == 1, "selected vertical sequence did not wrap");
    ui.click(start.x + 20, start.y + 255);
    ui.frame();
    if (!screenshot.empty()) {
        paint::save_image(capture_framebuffer(ui, 1280, 850, 1), screenshot);
    }
    std::filesystem::path path = std::filesystem::temp_directory_path() / "rainstar-atlas-save.png";
    app.save_to(path.string());
    paint::Image saved = paint::load_image(path.string());
    require(saved.width == 96 && saved.height == 96 && saved.get(36, 4).r == 10,
            "Save wrote one sprite instead of the sheet");
    std::filesystem::remove(path);
    require(app.error.empty(), app.error.c_str());
    app.document.make_icon_sizes({16, 32, 48}, true);
    app.document.atlas_select(1, false);
    app.texture_dirty = true;
    ui.frame();
    ui.click(1140, 122);
    require(app.hotspot_pick, "cursor hotspot picker did not activate");
    ui.click(51, 221);
    require(app.document.atlas.icons[1].hotspot_x == 5 && app.document.atlas.icons[1].hotspot_y == 7,
            "canvas cursor hotspot coordinate differs");
    if (!screenshot.empty()) {
        paint::save_image(capture_framebuffer(ui, 1280, 850, 1), screenshot + ".cursor.png");
    }
    path = std::filesystem::temp_directory_path() / "rainstar-atlas-save.cur";
    app.save_to(path.string());
    paint::ImageContainer saved_cursor = paint::load_container(path.string());
    require(saved_cursor.frames.size() == 3 && saved_cursor.frames[1].hotspot_x == 5 &&
                saved_cursor.frames[1].hotspot_y == 7,
            "UI save lost cursor sizes or hotspot");
    std::filesystem::remove(path);
    app.document.new_image();
    app.atlas_tab = false;
    app.zoom = 1;
    app.texture_dirty = true;
    ui.frame();
}
} // namespace
int main(int argc, char** argv) {
    try {
        bool curves_render = argc >= 2 && std::string(argv[1]) == "--curves-render-test";
        bool path_render = argc >= 2 && std::string(argv[1]) == "--path-render-test";
        bool atlas_render = argc >= 2 && std::string(argv[1]) == "--atlas-render-test";
        bool text_render = argc >= 2 && std::string(argv[1]) == "--text-render-test";
        bool geometry_render = argc >= 2 && std::string(argv[1]) == "--geometry-render-test";
        bool pointed_render = argc >= 2 && std::string(argv[1]) == "--pointed-render-test";
        bool material_render = argc >= 2 && std::string(argv[1]) == "--materials-render-test";
        bool native = curves_render || path_render || atlas_render || geometry_render || pointed_render ||
                      material_render || text_render ||
                      (argc >= 2 && std::string(argv[1]) == "--native-render-test");
        UiFixture ui(native);
        if (native) {
            std::string screenshot = argc >= 3 ? argv[2] : "";
            if (curves_render) {
                curve_interactions(ui, screenshot);
            } else if (path_render) {
                path_session_and_escape(ui, screenshot);
            } else if (atlas_render) {
                atlas_interactions(ui, screenshot);
            } else if (geometry_render) {
                geometry_preview(ui, screenshot);
            } else if (pointed_render) {
                pointed_tools(ui, screenshot);
            } else if (material_render) {
                material_ribbon_and_circle(ui, screenshot);
            } else if (text_render) {
                text_object_controls(ui, screenshot);
            } else {
                retina_rendering(ui, screenshot);
            }
            std::cout << (curves_render ? "Native editable Bézier and circular Arc controls passed with "
                          : path_render
                              ? "Native retained paths, segment history, stamp reset and Escape passed with "
                          : atlas_render
                              ? "Native Atlas sequence, gallery, painting and cursor controls passed with "
                          : geometry_render ? "Native path preview/commit and geometry coverage passed with "
                          : pointed_render
                              ? "Native pencil preview, pink eraser and anchored magnifier passed with "
                          : material_render ? "Native material ribbon, Circle and swatches passed with "
                          : text_render
                              ? "Native text editing, formatting and floating controls passed with "
                              : "Native framebuffer coverage and clipping passed at 1x, 1.5x and 2x with ")
                      << SDL_GetRendererName(ui.renderer) << ".\n";
            return 0;
        }
        drawing_and_controls(ui);
        path_and_selection(ui);
        path_session_and_escape(ui);
        curve_interactions(ui);
        stamp_and_reshape(ui);
        text_and_stale_transform(ui);
        text_object_controls(ui);
        material_ribbon_and_circle(ui);
        geometry_preview(ui);
        pointed_tools(ui);
        recent_files_and_desktop_layouts(ui);
        custom_colors_and_cursor(ui);
        arbitrary_rotation(ui);
        retina_rendering(ui);
        idle_rendering(ui);
        atlas_interactions(ui);
        std::cout << "Ribbon/canvas, palettes, cursor requests, paths, selections, stamps, free rotation and "
                     "reshape passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

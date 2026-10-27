#include "application.hpp"
#include "codecs.hpp"
#include "gui_scope.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace paint {
void Application::open_image(const std::string& path) {
    document.replace_container(load_container(path), path);
    text_active = false;
    clear_curve_controls();
    hotspot_pick = false;
    texture_dirty = true;
    atlas_grid_dialog = document.atlas.kind == AtlasKind::None && sprite_sheet_filename(path);
    atlas_grid = {};
    if (document.atlas.kind != AtlasKind::None || atlas_grid_dialog) {
        atlas_tab = true;
        view_tab = false;
        text_tab = false;
        patterns_tab = false;
    }
}
void Application::select_atlas_frame(int index, bool control) {
    if (dragging || reshape_active || rotation_active || transform_active) {
        status = "Finish the current gesture before switching Atlas images.";
        return;
    }
    finish_text();
    finish_curve();
    document.atlas_select(index, control);
    texture_dirty = true;
}
void Application::step_atlas(int direction) {
    if (dragging || reshape_active || rotation_active || transform_active) {
        return;
    }
    finish_text();
    finish_curve();
    document.atlas_step(direction);
    texture_dirty = true;
}
void Application::refresh_atlas_texture() {
    if (atlas_texture_epoch == document.atlas_epoch && atlas_texture_generation == texture_generation) {
        return;
    }
    document.sync_atlas();
    if (atlas_texture_epoch == document.atlas_epoch) {
        atlas_texture_generation = texture_generation;
        return;
    }
    int count = document.atlas.count();
    if (!count) {
        return;
    }
    atlas_texture_height = ((count + 15) / 16) * 48;
    Image thumbnails;
    thumbnails.reset(16 * 48, atlas_texture_height, {0, 0, 0, 0});
    for (int i = 0; i < count; ++i) {
        Image frame = document.atlas.frame_image(i);
        double scale = std::min(44.0 / frame.width, 44.0 / frame.height);
        int width = std::max(1, static_cast<int>(frame.width * scale));
        int height = std::max(1, static_cast<int>(frame.height * scale));
        int left = (i % 16) * 48 + (48 - width) / 2, top = (i / 16) * 48 + (48 - height) / 2;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                thumbnails.set(left + x, top + y,
                               frame.get(std::min(frame.width - 1, static_cast<int>(x / scale)),
                                         std::min(frame.height - 1, static_cast<int>(y / scale))));
            }
        }
    }
    // Reuse the texture while dimensions match, so ribbon draw commands never retain freed textures.
    float width = 0, height = 0;
    if (atlas_texture) {
        SDL_GetTextureSize(atlas_texture, &width, &height);
    }
    if (width != thumbnails.width || height != thumbnails.height) {
        SDL_DestroyTexture(atlas_texture);
        atlas_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                          thumbnails.width, thumbnails.height);
    }
    if (!atlas_texture) {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_SetTextureScaleMode(atlas_texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(atlas_texture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(atlas_texture, nullptr, thumbnails.pixels.data(), thumbnails.width * 4);
    atlas_texture_epoch = document.atlas_epoch;
    atlas_texture_generation = texture_generation;
}
void Application::atlas_thumbnail(int index) {
    ImGui::PushID(index);
    ImVec2 position = ImGui::GetCursorScreenPos();
    bool selected = document.atlas.active == index ||
                    std::find(document.atlas.sequence.begin(), document.atlas.sequence.end(), index) !=
                        document.atlas.sequence.end();
    bool control = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
    if (ImGui::InvisibleButton("Sprite", {58, 69},
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight) &&
        (ImGui::IsMouseReleased(0) || control)) {
        select_atlas_frame(index, control);
    }
    if (!ImGui::IsRectVisible(position, {position.x + 58, position.y + 69})) {
        ImGui::PopID();
        return;
    }
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    draw.AddRectFilled(position, {position.x + 56, position.y + 67},
                       selected ? IM_COL32(255, 222, 155, 255) : IM_COL32(240, 245, 250, 255));
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            draw.AddRectFilled({position.x + 4 + x * 8, position.y + 2 + y * 8},
                               {position.x + 12 + x * 8, position.y + 10 + y * 8},
                               (x + y) % 2 ? IM_COL32(205, 211, 218, 255) : IM_COL32(250, 250, 250, 255));
        }
    }
    draw.AddImage(reinterpret_cast<ImTextureID>(atlas_texture), {position.x + 4, position.y + 2},
                  {position.x + 52, position.y + 50},
                  {float(index % 16) / 16, float((index / 16) * 48) / atlas_texture_height},
                  {float(index % 16 + 1) / 16, float((index / 16 + 1) * 48) / atlas_texture_height});
    std::string label;
    if (document.atlas.kind == AtlasKind::Sheet) {
        label = std::to_string(index + 1);
    } else {
        const Image& image = document.atlas.icons[index].image;
        label = std::to_string(image.width) + "x" + std::to_string(image.height);
    }
    draw.AddText(ImGui::GetFont(), 12, {position.x + 4, position.y + 52}, IM_COL32(32, 48, 65, 255),
                 label.c_str());
    draw.AddRect(position, {position.x + 56, position.y + 67},
                 document.atlas.active == index ? IM_COL32(188, 115, 20, 255) : IM_COL32(148, 169, 190, 255),
                 0, 0, document.atlas.active == index ? 2 : 1);
    if (document.atlas.kind == AtlasKind::Sheet) {
        ImGui::SetItemTooltip("Sprite %d · row %d, column %d\nCtrl-click to select a sequence", index + 1,
                              index / document.atlas.grid.columns + 1,
                              index % document.atlas.grid.columns + 1);
    } else {
        ImGui::SetItemTooltip("Image %d · %s\nCtrl-click to select a sequence", index + 1, label.c_str());
    }
    ImGui::PopID();
}
void Application::atlas_ribbon(float width) {
    ImDrawList& background = *ImGui::GetWindowDrawList();
    ImVec2 base = ImGui::GetWindowPos();
    background.AddRectFilled({base.x, base.y + 130}, {base.x + width, base.y + 153},
                             IM_COL32(216, 228, 241, 255));
    bool sheet = document.atlas.kind == AtlasKind::Sheet;
    bool active = document.atlas.kind != AtlasKind::None;
    ImGui::SetCursorPos({10, 33});
    ImGui::BeginDisabled(active && !sheet);
    if (ImGui::Button(sheet ? "Grid..." : "Set up grid...", {112, 27})) {
        atlas_grid = sheet ? document.atlas.grid : AtlasGrid{};
        atlas_grid_dialog = true;
    }
    ImGui::EndDisabled();
    ImGui::SetCursorPos({10, 64});
    ImGui::BeginDisabled(!sheet);
    if (ImGui::Button("Whole sheet", {112, 27})) {
        select_atlas_frame(-1);
    }
    ImGui::EndDisabled();
    ImGui::SetCursorPos({10, 95});
    ImGui::BeginDisabled(!active);
    if (ImGui::Button(sheet ? "Leave grid" : "Keep image", {112, 25})) {
        finish_text();
        finish_curve();
        document.leave_atlas();
        texture_dirty = true;
    }
    ImGui::EndDisabled();
    if (!active) {
        ImGui::SetCursorPos({138, 45});
        ImGui::TextUnformatted("Choose rows and columns to edit a sprite sheet.");
        ImGui::SetCursorPos({138, 75});
        ImGui::TextDisabled(
            "ICO and CUR files show their separate sizes here. Each image is one flat canvas.");
        return;
    }
    refresh_atlas_texture();
    ImGui::SetCursorPos({132, 32});
    if (ImGui::Button("<", {30, 25})) {
        step_atlas(-1);
    }
    ImGui::SameLine();
    if (ImGui::Button(">", {30, 25})) {
        step_atlas(1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Expand gallery", {124, 25})) {
        ImGui::OpenPopup("Atlas gallery");
    }
    ImGui::SameLine();
    ImGui::Text("%d / %d · Left / Right steps the sequence", document.atlas.active + 1,
                document.atlas.count());
    if (!document.atlas.sequence.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("All frames")) {
            document.atlas.sequence.clear();
        }
    }
    ImGui::SetCursorPos({132, 61});
    float controls = document.atlas.kind == AtlasKind::Cursor ? 210 : 0;
    ImGui::BeginChild("Atlas sequence", {width - 145 - controls, 87}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const std::vector<int>& sequence = document.atlas.sequence;
    // Copy indices: Ctrl-click may change the selection while the tray is being drawn.
    std::vector<int> indices = sequence;
    if (indices.empty()) {
        for (int i = 0; i < document.atlas.count(); ++i) {
            indices.push_back(i);
        }
    }
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (i) {
            ImGui::SameLine(0, 3);
        }
        atlas_thumbnail(indices[i]);
    }
    ImGui::EndChild();
    atlas_gallery_active = false;
    if (ImGui::BeginPopup("Atlas gallery")) {
        atlas_gallery_active = true;
        GuiScope scope(GuiEnd::Popup);
        ImGui::TextUnformatted("Ctrl-click frames to hold a sequence. Left / Right steps through it.");
        int columns = sheet ? document.atlas.grid.columns : std::min(8, document.atlas.count());
        ImGui::BeginChild("Atlas grid gallery",
                          {std::min(950.0f, columns * 61.0f + 18),
                           std::min(540.0f, ((document.atlas.count() + columns - 1) / columns) * 73.0f + 20)},
                          ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        for (int i = 0; i < document.atlas.count(); ++i) {
            if (i % columns) {
                ImGui::SameLine(0, 3);
            }
            atlas_thumbnail(i);
        }
        ImGui::EndChild();
        if (ImGui::Button("Done")) {
            ImGui::CloseCurrentPopup();
        }
    }
    if (document.atlas.kind == AtlasKind::Cursor && document.atlas.active >= 0) {
        IconFrame& frame = document.atlas.icons[document.atlas.active];
        int x = frame.hotspot_x, y = frame.hotspot_y;
        ImGui::SetCursorPos({width - 202, 33});
        ImGui::TextUnformatted("Cursor hotspot");
        ImGui::SetCursorPos({width - 202, 59});
        ImGui::SetNextItemWidth(142);
        if (ImGui::InputInt("X", &x)) {
            document.set_hotspot(x, y);
        }
        ImGui::SetCursorPos({width - 202, 85});
        ImGui::SetNextItemWidth(142);
        if (ImGui::InputInt("Y", &y)) {
            document.set_hotspot(x, y);
        }
        ImGui::SetCursorPos({width - 202, 111});
        if (ImGui::Button(hotspot_pick ? "Click canvas..." : "Pick on canvas", {157, 23})) {
            hotspot_pick = true;
        }
    }
}
void Application::atlas_dialogs() {
    if (atlas_grid_dialog) {
        ImGui::OpenPopup("Sprite sheet grid");
        atlas_grid_dialog = false;
    }
    if (ImGui::BeginPopupModal("Sprite sheet grid", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        GuiScope scope(GuiEnd::Popup);
        ImGui::TextUnformatted("Choose the arrangement of sprites in this picture.");
        ImGui::InputInt("Rows", &atlas_grid.rows);
        ImGui::InputInt("Columns", &atlas_grid.columns);
        ImGui::Separator();
        ImGui::InputInt("Horizontal margin", &atlas_grid.margin_x);
        ImGui::InputInt("Vertical margin", &atlas_grid.margin_y);
        ImGui::InputInt("Horizontal spacing", &atlas_grid.spacing_x);
        ImGui::InputInt("Vertical spacing", &atlas_grid.spacing_y);
        const Image& sheet = document.atlas.kind == AtlasKind::Sheet ? document.atlas.sheet : document.image;
        bool valid = true;
        try {
            Rect rect = atlas_grid.frame(sheet, 0);
            ImGui::Text("%d sprites · %d x %d pixels each", atlas_grid.rows * atlas_grid.columns, rect.w,
                        rect.h);
            int remainder_x = sheet.width - 2 * atlas_grid.margin_x -
                              (atlas_grid.columns - 1) * atlas_grid.spacing_x - rect.w * atlas_grid.columns;
            int remainder_y = sheet.height - 2 * atlas_grid.margin_y -
                              (atlas_grid.rows - 1) * atlas_grid.spacing_y - rect.h * atlas_grid.rows;
            if (remainder_x || remainder_y) {
                ImGui::Text("Unused edge: %d px right, %d px bottom (preserved)", remainder_x, remainder_y);
            }
        } catch (const std::exception& exception) {
            valid = false;
            ImGui::TextWrapped("%s", exception.what());
        }
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Use grid", {110, 0})) {
            finish_text();
            finish_curve();
            document.configure_atlas(atlas_grid);
            texture_dirty = true;
            atlas_tab = true;
            view_tab = false;
            text_tab = false;
            patterns_tab = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Open as picture", {140, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
    }
    if (icon_sizes_dialog) {
        ImGui::OpenPopup("Icon sizes");
        icon_sizes_dialog = false;
    }
    if (ImGui::BeginPopupModal("Icon sizes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        GuiScope scope(GuiEnd::Popup);
        ImGui::TextUnformatted("Choose the sizes to include in the file.");
        ImGui::TextDisabled("The current canvas is resampled for each selected square size.");
        const int sizes[8] = {16, 24, 32, 48, 64, 96, 128, 256};
        for (int i = 0; i < 8; ++i) {
            std::string label = std::to_string(sizes[i]) + " x " + std::to_string(sizes[i]);
            ImGui::Checkbox(label.c_str(), &icon_sizes[i]);
        }
        if (ImGui::Button("Create and save", {140, 0})) {
            std::vector<int> selected;
            for (int i = 0; i < 8; ++i) {
                if (icon_sizes[i]) {
                    selected.push_back(sizes[i]);
                }
            }
            document.make_icon_sizes(selected, image_extension(icon_save_path) == ".cur");
            texture_dirty = true;
            atlas_tab = true;
            view_tab = false;
            text_tab = false;
            patterns_tab = false;
            save_to(icon_save_path);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {90, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            deferred_after_save = false;
            ImGui::CloseCurrentPopup();
        }
    }
}
void Application::atlas_overlay(ImDrawList& draw, ImVec2 origin) {
    if (document.atlas.kind != AtlasKind::Cursor && document.atlas.kind != AtlasKind::Icon) {
        return;
    }
    if (document.atlas.active < 0) {
        return;
    }
    const IconFrame& frame = document.atlas.icons[document.atlas.active];
    if (frame.image.width == document.image.width && frame.image.height == document.image.height) {
        for (std::size_t i = 0; i < frame.xor_pixels.size(); ++i) {
            if (!legacy_xor_pixel(frame, i) || !equal(document.image.pixels[i], {255, 255, 255, 0})) {
                continue;
            }
            int x = static_cast<int>(i % frame.image.width), y = static_cast<int>(i / frame.image.width);
            Color c = frame.xor_pixels[i];
            // Legacy XOR acts on the displayed checker background; metadata is retained on save.
            float left = x * zoom, top = y * zoom, right = (x + 1) * zoom, bottom = (y + 1) * zoom;
            for (int by = static_cast<int>(top / 12) * 12; by < bottom; by += 12) {
                for (int bx = static_cast<int>(left / 12) * 12; bx < right; bx += 12) {
                    int background = ((bx / 12 + by / 12) % 2) ? 255 : 240;
                    Color shown = {static_cast<std::uint8_t>(c.r ^ background),
                                   static_cast<std::uint8_t>(c.g ^ background),
                                   static_cast<std::uint8_t>(c.b ^ background), 255};
                    draw.AddRectFilled(
                        {origin.x + std::max(left, float(bx)), origin.y + std::max(top, float(by))},
                        {origin.x + std::min(right, float(bx + 12)),
                         origin.y + std::min(bottom, float(by + 12))},
                        packed(shown));
                }
            }
        }
    }
    if (atlas_tab && document.atlas.kind == AtlasKind::Cursor) {
        ImVec2 p(origin.x + (frame.hotspot_x + 0.5f) * zoom, origin.y + (frame.hotspot_y + 0.5f) * zoom);
        draw.AddCircle(p, 7, IM_COL32(255, 255, 255, 255), 16, 3);
        draw.AddLine({p.x - 10, p.y}, {p.x + 10, p.y}, IM_COL32(204, 40, 83, 255), 1.5f);
        draw.AddLine({p.x, p.y - 10}, {p.x, p.y + 10}, IM_COL32(204, 40, 83, 255), 1.5f);
    }
}
} // namespace paint

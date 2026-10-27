#include "application.hpp"
#include "gui_scope.hpp"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace paint {
void Application::prepare_text_font() {
    // The preview is the same pixel raster that will be committed to the image.
    // Keep the interface atlas stable when changing a text object's face or size.
    if (text_active) {
        refresh_text_preview();
    }
}
void Application::refresh_text_preview() {
    std::string signature =
        std::string(text_buffer) + "\n" + text_style.face_path + "@" + std::to_string(text_style.size) + ":" +
        std::to_string(text_width) + ":" + std::to_string(text_height) + (text_style.bold ? "b" : "-") +
        (text_style.italic ? "i" : "-") + (text_style.underline ? "u" : "-") +
        (text_style.strikeout ? "s" : "-") + (text_style.mono ? "m" : "-") + (text_style.opaque ? "o" : "-") +
        (text_style.word_wrap ? "w" : "-") + std::to_string(packed(document.ink.primary)) + ":" +
        std::to_string(packed(document.ink.secondary));
    if (signature == text_preview_signature) {
        return;
    }
    text_layout = layout_text(text_buffer, text_style, text_width);
    text_preview.reset(text_width, text_height,
                       text_style.opaque ? document.ink.secondary : Color{0, 0, 0, 0});
    render_text(text_preview, {0, 0}, text_layout, text_style, document.ink.primary, document.ink.secondary);
    SDL_DestroyTexture(text_texture);
    text_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, text_width,
                                     text_height);
    if (!text_texture) {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_SetTextureBlendMode(text_texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(text_texture, SDL_SCALEMODE_NEAREST);
    SDL_UpdateTexture(text_texture, nullptr, text_preview.pixels.data(), text_width * 4);
    ++texture_generation;
    text_preview_signature = signature;
    text_caret = std::min(text_caret, std::strlen(text_buffer));
    text_anchor = std::min(text_anchor, std::strlen(text_buffer));
}
void Application::cancel_text() {
    text_active = false;
    text_tab = false;
    text_focus = false;
    text_editing = false;
    ImGui::ClearActiveID();
    status = "Text cancelled. The picture is unchanged.";
}
void Application::finish_text() {
    if (!text_active) {
        return;
    }
    if (text_buffer[0]) {
        refresh_text_preview();
        document.checkpoint();
        composite(document.image, text_preview, static_cast<int>(text_origin.x),
                  static_cast<int>(text_origin.y));
    }
    text_active = false;
    text_tab = false;
    text_focus = false;
    text_editing = false;
    texture_dirty = true;
    ImGui::ClearActiveID();
    status = "Text placed. Undo restores the picture.";
}
void Application::replace_text_selection(const std::string& inserted) {
    std::string content(text_buffer);
    std::size_t start = std::min(text_caret, text_anchor), end = std::max(text_caret, text_anchor);
    std::string value = inserted;
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    std::size_t available = sizeof(text_buffer) - 1 - (content.size() - (end - start));
    if (value.size() > available) {
        std::size_t length = available;
        while (length > 0 && (static_cast<unsigned char>(value[length]) & 192) == 128) {
            --length;
        }
        value.resize(length);
        status = "Text limit reached (8191 UTF-8 bytes). Place this box to begin another.";
    }
    if (start == end && value.empty()) {
        return;
    }
    text_undo.push_back({content, text_caret, text_anchor});
    if (text_undo.size() > 64) {
        text_undo.erase(text_undo.begin());
    }
    text_redo.clear();
    content.replace(start, end - start, value);
    std::memcpy(text_buffer, content.c_str(), content.size() + 1);
    text_caret = text_anchor = start + value.size();
    text_caret_epoch = ImGui::GetTime();
}
void Application::text_history(bool redo) {
    std::vector<TextEditSnapshot>& source = redo ? text_redo : text_undo;
    std::vector<TextEditSnapshot>& destination = redo ? text_undo : text_redo;
    if (source.empty()) {
        return;
    }
    destination.push_back({text_buffer, text_caret, text_anchor});
    TextEditSnapshot restored = source.back();
    source.pop_back();
    std::memcpy(text_buffer, restored.content.c_str(), restored.content.size() + 1);
    text_caret = restored.caret;
    text_anchor = restored.anchor;
    text_caret_epoch = ImGui::GetTime();
}
static std::size_t caret_at(const TextLayout& layout, Point point) {
    double best = 1e100;
    std::size_t result = 0;
    // Only UTF-8 boundaries appear as glyph starts; continuation bytes are never selected.
    for (std::size_t i = 0; i <= layout.glyphs.size(); ++i) {
        std::size_t offset = i == layout.glyphs.size() ? layout.carets.size() - 1 : layout.glyphs[i].begin;
        Point caret = layout.carets[offset];
        double row = std::abs(std::floor(point.y / layout.line_height) - caret.y / layout.line_height);
        double distance = row * 100000 + std::abs(point.x - caret.x);
        if (distance < best) {
            best = distance;
            result = offset;
        }
    }
    return result;
}
static std::string utf8_character(unsigned int value) {
    std::string result;
    if (value < 128) {
        result += static_cast<char>(value);
    } else if (value < 2048) {
        result += static_cast<char>(192 | (value >> 6));
        result += static_cast<char>(128 | (value & 63));
    } else if (value < 65536) {
        result += static_cast<char>(224 | (value >> 12));
        result += static_cast<char>(128 | ((value >> 6) & 63));
        result += static_cast<char>(128 | (value & 63));
    } else {
        result += static_cast<char>(240 | (value >> 18));
        result += static_cast<char>(128 | ((value >> 12) & 63));
        result += static_cast<char>(128 | ((value >> 6) & 63));
        result += static_cast<char>(128 | (value & 63));
    }
    return result;
}
void Application::text_canvas(ImVec2 canvas_origin) {
    refresh_text_preview();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 top_left(canvas_origin.x + static_cast<float>(text_origin.x) * zoom,
                    canvas_origin.y + static_cast<float>(text_origin.y) * zoom);
    ImGui::SetNextWindowPos({top_left.x - 5, top_left.y - 32});
    ImGui::SetNextWindowSize({std::max(256.0f, text_width * zoom + 10), text_height * zoom + 38});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("Text object", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoScrollWithMouse);
    GuiScope scope(GuiEnd::Window, 1, 0);
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    ImVec2 base = ImGui::GetWindowPos();
    draw.AddRectFilled(base, {base.x + 256, base.y + 28}, IM_COL32(227, 239, 251, 255), 3);
    ImGui::SetCursorPos({4, 3});
    ImGui::Button("Move text", {83, 23});
    ImGui::SetItemTooltip("Drag to move the text box");
    if (ImGui::IsItemActivated()) {
        text_drag_mouse = io.MousePos;
        text_drag_bounds = {static_cast<int>(text_origin.x), static_cast<int>(text_origin.y), text_width,
                            text_height};
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        text_origin = {text_drag_bounds.x + std::round((io.MousePos.x - text_drag_mouse.x) / zoom),
                       text_drag_bounds.y + std::round((io.MousePos.y - text_drag_mouse.y) / zoom)};
    }
    ImGui::SetCursorPos({91, 3});
    if (ImGui::Button("Place text", {87, 23})) {
        finish_text();
        return;
    }
    ImGui::SetCursorPos({182, 3});
    if (ImGui::Button("Cancel", {69, 23})) {
        cancel_text();
        return;
    }
    ImVec2 content(base.x + 5, base.y + 32);
    ImVec2 end(content.x + text_width * zoom, content.y + text_height * zoom);
    ImGui::SetCursorPos({10, 37});
    ImGui::InvisibleButton("Text contents",
                           {std::max(1.0f, text_width * zoom - 10), std::max(1.0f, text_height * zoom - 10)});
    ImGuiID input_id = ImGui::GetItemID();
    bool editing_item_active = ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    }
    if (ImGui::IsItemClicked()) {
        text_editing = true;
        text_caret =
            caret_at(text_layout, {(io.MousePos.x - content.x) / zoom, (io.MousePos.y - content.y) / zoom});
        if (!io.KeyShift) {
            text_anchor = text_caret;
        }
        text_caret_epoch = ImGui::GetTime();
    }
    if (editing_item_active && ImGui::IsMouseDragging(0)) {
        text_caret =
            caret_at(text_layout, {(io.MousePos.x - content.x) / zoom, (io.MousePos.y - content.y) / zoom});
    }
    if (text_focus) {
        text_focus = false;
        text_editing = true;
        ImGui::SetWindowFocus();
    }
    const double handle_x[8] = {0, 0.5, 1, 1, 1, 0.5, 0, 0};
    const double handle_y[8] = {0, 0, 0, 0.5, 1, 1, 1, 0.5};
    for (int i = 0; i < 8; ++i) {
        ImVec2 position(content.x + static_cast<float>(handle_x[i]) * text_width * zoom,
                        content.y + static_cast<float>(handle_y[i]) * text_height * zoom);
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos({position.x - 5, position.y - 5});
        ImGui::InvisibleButton("Text resize handle", {10, 10});
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(i == 1 || i == 5   ? ImGuiMouseCursor_ResizeNS
                                  : i == 3 || i == 7 ? ImGuiMouseCursor_ResizeEW
                                  : i == 0 || i == 4 ? ImGuiMouseCursor_ResizeNWSE
                                                     : ImGuiMouseCursor_ResizeNESW);
        }
        if (ImGui::IsItemActivated()) {
            text_drag_mouse = io.MousePos;
            text_drag_bounds = {static_cast<int>(text_origin.x), static_cast<int>(text_origin.y), text_width,
                                text_height};
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            int dx = static_cast<int>(std::round((io.MousePos.x - text_drag_mouse.x) / zoom));
            int dy = static_cast<int>(std::round((io.MousePos.y - text_drag_mouse.y) / zoom));
            Rect bounds = text_drag_bounds;
            if (i == 0 || i == 6 || i == 7) {
                bounds.x += dx;
                bounds.w -= dx;
            }
            if (i == 2 || i == 3 || i == 4) {
                bounds.w += dx;
            }
            if (i == 0 || i == 1 || i == 2) {
                bounds.y += dy;
                bounds.h -= dy;
            }
            if (i == 4 || i == 5 || i == 6) {
                bounds.h += dy;
            }
            text_width = std::clamp(bounds.w, 24, 8192);
            text_height = std::clamp(bounds.h, 24, std::min(8192, 16000000 / text_width));
            if (i == 0 || i == 6 || i == 7) {
                bounds.x = text_drag_bounds.x + text_drag_bounds.w - text_width;
            }
            if (i == 0 || i == 1 || i == 2) {
                bounds.y = text_drag_bounds.y + text_drag_bounds.h - text_height;
            }
            text_origin = {static_cast<double>(bounds.x), static_cast<double>(bounds.y)};
        }
        ImGui::PopID();
    }
    ImGuiContext& context = *ImGui::GetCurrentContext();
    bool own_keyboard =
        text_editing && (context.ActiveId == 0 || context.ActiveId == input_id) &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (own_keyboard) {
        context.WantTextInputNextFrame = 1;
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        std::string value(text_buffer);
        bool shortcut = io.KeyCtrl || io.KeySuper;
        if (shortcut && ImGui::IsKeyPressed(ImGuiKey_A)) {
            text_anchor = 0;
            text_caret = value.size();
        }
        if (shortcut && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            text_history(io.KeyShift);
        }
        if (shortcut && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            text_history(true);
        }
        if (shortcut && (ImGui::IsKeyPressed(ImGuiKey_C) || ImGui::IsKeyPressed(ImGuiKey_X))) {
            std::size_t a = std::min(text_anchor, text_caret), b = std::max(text_anchor, text_caret);
            if (a != b) {
                SDL_SetClipboardText(value.substr(a, b - a).c_str());
                if (ImGui::IsKeyPressed(ImGuiKey_X)) {
                    replace_text_selection("");
                }
            }
        }
        if (shortcut && ImGui::IsKeyPressed(ImGuiKey_V)) {
            char* clipboard = SDL_GetClipboardText();
            if (clipboard) {
                replace_text_selection(clipboard);
                SDL_free(clipboard);
            }
        }
        bool left = ImGui::IsKeyPressed(ImGuiKey_LeftArrow), right = ImGui::IsKeyPressed(ImGuiKey_RightArrow);
        bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow), down_key = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
        bool home = ImGui::IsKeyPressed(ImGuiKey_Home), end_key = ImGui::IsKeyPressed(ImGuiKey_End);
        if (left || right || up || down_key || home || end_key) {
            if (left) {
                text_caret = !io.KeyShift && text_caret != text_anchor
                                 ? std::min(text_caret, text_anchor)
                                 : previous_text_boundary(value, text_caret);
            }
            if (right) {
                text_caret = !io.KeyShift && text_caret != text_anchor
                                 ? std::max(text_caret, text_anchor)
                                 : next_text_boundary(value, text_caret);
            }
            Point caret = text_layout.carets[std::min(text_caret, text_layout.carets.size() - 1)];
            if (up || down_key) {
                text_caret =
                    caret_at(text_layout, {caret.x, caret.y + (up ? -1 : 1) * text_layout.line_height});
            }
            if (home || end_key) {
                text_caret = shortcut ? (home ? 0 : value.size())
                                      : caret_at(text_layout, {home ? -1.0 : 100000.0, caret.y});
            }
            if (!io.KeyShift) {
                text_anchor = text_caret;
            }
            text_caret_epoch = ImGui::GetTime();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) || ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (text_caret == text_anchor) {
                text_anchor = ImGui::IsKeyPressed(ImGuiKey_Backspace)
                                  ? previous_text_boundary(value, text_caret)
                                  : next_text_boundary(value, text_caret);
            }
            replace_text_selection("");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !shortcut) {
            replace_text_selection("\n");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !shortcut) {
            replace_text_selection("\t");
        }
        if (!shortcut) {
            std::string inserted;
            for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
                unsigned int cp = io.InputQueueCharacters[i];
                if (cp >= 32 && cp != 127) {
                    inserted += utf8_character(cp);
                }
            }
            if (!inserted.empty()) {
                replace_text_selection(inserted);
            }
        }
    }
    refresh_text_preview();
    draw.AddImage(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(text_texture)), content, end);
    draw.AddRect(content, end, IM_COL32(0, 105, 205, 255), 0, 0, 1);
    for (int i = 0; i < 8; ++i) {
        ImVec2 position(content.x + static_cast<float>(handle_x[i]) * text_width * zoom,
                        content.y + static_cast<float>(handle_y[i]) * text_height * zoom);
        draw.AddRectFilled({position.x - 3, position.y - 3}, {position.x + 3, position.y + 3},
                           IM_COL32(255, 255, 255, 255));
        draw.AddRect({position.x - 3, position.y - 3}, {position.x + 3, position.y + 3},
                     IM_COL32(0, 105, 205, 255));
    }
    draw.PushClipRect(content, end, true);
    std::size_t first = std::min(text_caret, text_anchor), last_byte = std::max(text_caret, text_anchor);
    for (std::size_t i = 0; i < text_layout.glyphs.size(); ++i) {
        const TextGlyph& glyph = text_layout.glyphs[i];
        if (glyph.begin >= first && glyph.begin < last_byte) {
            ImVec2 a(content.x + glyph.x * zoom, content.y + glyph.y * zoom);
            draw.AddRectFilled(
                a, {a.x + std::max(2, glyph.advance) * zoom, a.y + text_layout.line_height * zoom},
                IM_COL32(60, 140, 240, 70));
        }
    }
    if (own_keyboard && std::fmod(ImGui::GetTime() - text_caret_epoch, 1.0) < 0.55) {
        Point caret = text_layout.carets[text_caret];
        ImVec2 a(content.x + static_cast<float>(caret.x) * zoom,
                 content.y + static_cast<float>(caret.y) * zoom);
        draw.AddLine(a, {a.x, a.y + text_layout.line_height * zoom}, IM_COL32(20, 50, 80, 255), 1);
    }
    draw.PopClipRect();
    context.PlatformImeData.WantVisible = own_keyboard;
    if (own_keyboard) {
        Point caret = text_layout.carets[text_caret];
        context.PlatformImeData.InputPos = {content.x + static_cast<float>(caret.x) * zoom,
                                            content.y + static_cast<float>(caret.y) * zoom};
        context.PlatformImeData.InputLineHeight = text_layout.line_height * zoom;
    }
}
} // namespace paint

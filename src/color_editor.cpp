#include "application.hpp"
#include "gui_scope.hpp"
#include <algorithm>
#include <cmath>
namespace paint {
static Color edited_color(const float* rgb) {
    return {static_cast<std::uint8_t>(std::round(std::clamp(rgb[0], 0.0f, 1.0f) * 255)),
            static_cast<std::uint8_t>(std::round(std::clamp(rgb[1], 0.0f, 1.0f) * 255)),
            static_cast<std::uint8_t>(std::round(std::clamp(rgb[2], 0.0f, 1.0f) * 255)), 255};
}
static ImVec4 color_vector(Color color) {
    return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, 1};
}
static bool color_swatch(const char* id, Color color, ImVec2 size, bool selected = false) {
    bool clicked = ImGui::ColorButton(id, color_vector(color),
                                      ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, size);
    if (selected) {
        ImVec2 first = ImGui::GetItemRectMin(), last = ImGui::GetItemRectMax();
        (*ImGui::GetWindowDrawList())
            .AddRect({first.x - 2, first.y - 2}, {last.x + 2, last.y + 2}, IM_COL32(25, 103, 184, 255), 0, 0,
                     2);
    }
    return clicked;
}
void Application::color_editor() {
    if (color_dialog) {
        ImGui::OpenPopup("Edit Colors");
        color_dialog = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14, 12});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(244, 247, 250, 255));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, IM_COL32(214, 229, 246, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(126, 150, 178, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 255));
    ImGui::SetNextWindowSize({640, 0}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Edit Colors", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
        return;
    }
    GuiScope popup_scope(GuiEnd::Popup, 1, 4);
    if (ImGui::RadioButton("Color 1", primary_slot)) {
        primary_slot = true;
        original_color = document.ink.primary;
        edit_color(original_color);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Color 2", !primary_slot)) {
        primary_slot = false;
        original_color = document.ink.secondary;
        edit_color(original_color);
    }
    ImGui::Separator();
    if (ImGui::BeginTable("Color editor layout", 2,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
        GuiScope table_scope(GuiEnd::Table);
        ImGui::TableSetupColumn("Swatches", ImGuiTableColumnFlags_WidthFixed, 264);
        ImGui::TableSetupColumn("Color values", ImGuiTableColumnFlags_WidthFixed, 326);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Basic colors");
        ImVec2 start = ImGui::GetCursorPos();
        for (int index = 0; index < 30; ++index) {
            ImGui::SetCursorPos({start.x + (index % 10) * 25.0f, start.y + (index / 10) * 28.0f});
            ImGui::PushID(index);
            Color color = office_color(index);
            bool clicked = color_swatch("Basic color", color, {22, 24});
            ImGui::SetItemTooltip("%s", to_hex(color).c_str());
            ImGui::PopID();
            if (clicked) {
                edit_color(color);
            }
        }
        ImGui::SetCursorPos({start.x, start.y + 94});
        ImGui::TextUnformatted("Custom colors");
        start = ImGui::GetCursorPos();
        for (int index = 0; index < 16; ++index) {
            ImGui::SetCursorPos({start.x + (index % 8) * 31.0f, start.y + (index / 8) * 31.0f});
            ImGui::PushID(100 + index);
            const Color color = custom_colors.colors[index];
            bool clicked = color_swatch("Custom color", color, {26, 26}, index == custom_color_slot);
            if (custom_colors.occupied & (1u << index)) {
                ImGui::SetItemTooltip("Custom %d: %s", index + 1, to_hex(color).c_str());
            } else {
                ImGui::SetItemTooltip("Empty slot %d. Choose a color, then Add to custom colors.", index + 1);
            }
            ImGui::PopID();
            if (clicked) {
                custom_color_slot = index;
                if (custom_colors.occupied & (1u << index)) {
                    edit_color(color);
                }
            }
        }
        ImGui::SetCursorPos({start.x, start.y + 67});
        if (ImGui::Button("Add to custom colors", {245, 28})) {
            custom_colors.store(custom_color_slot, edited_color(edited_rgb));
            custom_color_slot = (custom_color_slot + 1) % 16;
        }
        ImGui::Spacing();
        const float preview_left = ImGui::GetCursorPosX();
        ImGui::TextUnformatted("Original");
        ImGui::SameLine(preview_left + 130);
        ImGui::TextUnformatted("New");
        if (color_swatch("Original color", original_color, {115, 48})) {
            edit_color(original_color);
        }
        ImGui::SameLine(0, 15);
        color_swatch("New color", edited_color(edited_rgb), {115, 48});
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(312);
        if (ImGui::ColorPicker3("##Picker", edited_rgb,
                                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoSidePreview |
                                    ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_PickerHueBar)) {
            edit_color(edited_color(edited_rgb));
        }
        int rgb[3] = {static_cast<int>(std::round(edited_rgb[0] * 255)),
                      static_cast<int>(std::round(edited_rgb[1] * 255)),
                      static_cast<int>(std::round(edited_rgb[2] * 255))};
        bool rgb_changed = false;
        const char* names[] = {"R", "G", "B"};
        for (int index = 0; index < 3; ++index) {
            if (index) {
                ImGui::SameLine();
            }
            ImGui::SetNextItemWidth(77);
            rgb_changed |= ImGui::InputInt(names[index], &rgb[index], 0, 0);
        }
        if (rgb_changed) {
            edit_color({static_cast<std::uint8_t>(std::clamp(rgb[0], 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(rgb[1], 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(rgb[2], 0, 255)), 255});
        }
        ImGui::SetNextItemWidth(257);
        if (ImGui::InputText("Hex", edited_hex, sizeof(edited_hex))) {
            Color color;
            if (from_hex(edited_hex, color)) {
                edited_lab = to_oklab(color);
                edited_rgb[0] = color.r / 255.0f;
                edited_rgb[1] = color.g / 255.0f;
                edited_rgb[2] = color.b / 255.0f;
            }
        }
        ImGui::TextUnformatted("OKLab");
        bool lab_changed = false;
        ImGui::SetNextItemWidth(77);
        lab_changed |= ImGui::InputDouble("L", &edited_lab.l, 0, 0, "%.4f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(77);
        lab_changed |= ImGui::InputDouble("a", &edited_lab.a, 0, 0, "%.4f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(77);
        lab_changed |= ImGui::InputDouble("b", &edited_lab.b, 0, 0, "%.4f");
        if (lab_changed && std::isfinite(edited_lab.l) && std::isfinite(edited_lab.a) &&
            std::isfinite(edited_lab.b)) {
            edited_lab.l = std::clamp(edited_lab.l, 0.0, 1.0);
            edited_lab.a = std::clamp(edited_lab.a, -1.0, 1.0);
            edited_lab.b = std::clamp(edited_lab.b, -1.0, 1.0);
            const Lab requested = edited_lab;
            edit_color(from_oklab(requested));
            edited_lab = requested;
        }
        ImGui::TextDisabled("L: 0 to 1. Outside sRGB is clipped.");
    }
    ImGui::Separator();
    if (ImGui::Button("OK", {100, 28})) {
        if (primary_slot) {
            document.ink.primary = edited_color(edited_rgb);
        } else {
            document.ink.secondary = edited_color(edited_rgb);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {100, 28}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
}
} // namespace paint

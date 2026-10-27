#pragma once
#include "imgui.h"
namespace paint {
enum class GuiEnd { Window, Child, Popup, Menu, Table };
// Balances the foreign immediate-mode UI stack when an editing operation fails.
class GuiScope {
  public:
    explicit GuiScope(GuiEnd end, int variables = 0, int colors = 0)
        : end_(end), variables_(variables), colors_(colors) {}
    GuiScope(const GuiScope&) = delete;
    GuiScope& operator=(const GuiScope&) = delete;
    ~GuiScope() {
        if (end_ == GuiEnd::Window) {
            ImGui::End();
        } else if (end_ == GuiEnd::Child) {
            ImGui::EndChild();
        } else if (end_ == GuiEnd::Menu) {
            ImGui::EndMenu();
        } else if (end_ == GuiEnd::Table) {
            ImGui::EndTable();
        } else {
            ImGui::EndPopup();
        }
        if (variables_ > 0) {
            ImGui::PopStyleVar(variables_);
        }
        if (colors_ > 0) {
            ImGui::PopStyleColor(colors_);
        }
    }

  private:
    GuiEnd end_;
    int variables_ = 0, colors_ = 0;
};
} // namespace paint

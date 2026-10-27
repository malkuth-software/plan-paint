#include "application.hpp"
#include "gui_scope.hpp"
#include "help_content.hpp"
namespace paint {
static void help_topic(const char* title, const char* body, bool open = false) {
    if (ImGui::CollapsingHeader(title, open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None)) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(body);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
}
void Application::help(float x, float y, float width, float height) {
    ImGui::SetCursorPos({x, y});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 211, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(238, 234, 168, 255));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(230, 225, 150, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    ImGui::BeginChild("Plan Paint Help", {width, height}, true, ImGuiWindowFlags_AlwaysUseWindowPadding);
    GuiScope help_scope(GuiEnd::Child, 1, 4);
    ImGui::TextUnformatted("Plan Paint Help");
    ImGui::Separator();
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(help_welcome.data());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    for (const HelpTopic& topic : help_topics) {
        help_topic(topic.title.data(), topic.body.data(), topic.open);
    }
}
} // namespace paint

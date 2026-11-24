#pragma once

// Plan Paint for a model (GUI.Forms MCP, round 2 of the pilot): what a
// program launched with --mcp tells the automation about itself.
//
// - Commands are tools already (the ribbon's and the File menu's Commands,
//   by stable ID). The ones that discard work, replace the picture, write
//   files or change the desktop are destructive: the first of a launch asks
//   the user once (mark_command).
// - Commands that open a dialog are marked so a model knows a user must
//   answer it; where Plan Paint can do the same with arguments, a tool does
//   it without the dialog (EditorModelTools).

#include <gui_forms/automation/tools/model_tools.hpp>

#include <string_view>

namespace gui_forms {
class Command;
}

namespace paint::forms {

class Editor;

// Applies Plan Paint's model policy to a Command as it is made: destructive
// for the commands listed in model_tools.cpp, by ID with or without the
// ribbon's "choice-" row prefix.
void mark_command(gui_forms::Command& command, std::string_view id);

// Registers the editor's dialog-free tools and the dialog marks for the
// editor's lifetime; unregisters them on destruction.
class EditorModelTools final {
  public:
    explicit EditorModelTools(Editor& editor);
    ~EditorModelTools();
    EditorModelTools(const EditorModelTools&) = delete;
    EditorModelTools& operator=(const EditorModelTools&) = delete;

  private:
    static void image_info(void* context, const gui_forms::automation::ModelToolCall& call,
                           gui_forms::automation::ModelToolReply& reply);
    static void resize_image(void* context, const gui_forms::automation::ModelToolCall& call,
                             gui_forms::automation::ModelToolReply& reply);
    static void new_image(void* context, const gui_forms::automation::ModelToolCall& call,
                          gui_forms::automation::ModelToolReply& reply);

    Editor& editor_;
};

} // namespace paint::forms

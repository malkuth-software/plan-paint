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
// - Tools draw in picture pixels (draw, path, stroke, fill, gradient, text,
//   set_colour), read colours back (sample), convert picture points to
//   window points (to_window), and image_info names what floats.

#include <gui_forms/automation/tools/model_tools.hpp>

#include <string>
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

// Names what the editor leaves floating (a shape still being placed, a live
// curve or path, a paste not yet on the canvas) as the automation's pending
// state, so every reply to a model says it and how to settle it; clears it
// once nothing floats. Cheap when nothing changed.
void note_model_pending(const Editor& editor);

// Notes the pending state when a scope ends: Editor::pointer's events leave
// a shape floating without a refresh.
class PendingStateNote final {
  public:
    explicit PendingStateNote(const Editor& editor) noexcept : editor_(editor) {}
    ~PendingStateNote();
    PendingStateNote(const PendingStateNote&) = delete;
    PendingStateNote& operator=(const PendingStateNote&) = delete;

  private:
    const Editor& editor_;
};

// Registers the editor's tools, its note for a model and the dialog marks
// for the editor's lifetime; unregisters them on destruction.
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
    static void draw(void* context, const gui_forms::automation::ModelToolCall& call,
                     gui_forms::automation::ModelToolReply& reply);
    static void stroke(void* context, const gui_forms::automation::ModelToolCall& call,
                       gui_forms::automation::ModelToolReply& reply);
    static void set_colour(void* context, const gui_forms::automation::ModelToolCall& call,
                           gui_forms::automation::ModelToolReply& reply);
    static void to_window(void* context, const gui_forms::automation::ModelToolCall& call,
                          gui_forms::automation::ModelToolReply& reply);
    static void path(void* context, const gui_forms::automation::ModelToolCall& call,
                     gui_forms::automation::ModelToolReply& reply);
    static void fill(void* context, const gui_forms::automation::ModelToolCall& call,
                     gui_forms::automation::ModelToolReply& reply);
    static void gradient(void* context, const gui_forms::automation::ModelToolCall& call,
                         gui_forms::automation::ModelToolReply& reply);
    static void text(void* context, const gui_forms::automation::ModelToolCall& call,
                     gui_forms::automation::ModelToolReply& reply);
    static void sample(void* context, const gui_forms::automation::ModelToolCall& call,
                       gui_forms::automation::ModelToolReply& reply);
    static void save(void* context, const gui_forms::automation::ModelToolCall& call,
                     gui_forms::automation::ModelToolReply& reply);

    Editor& editor_;
    // The draw tool's schema names every shape it draws, so it is built.
    std::string draw_schema_;
};

} // namespace paint::forms

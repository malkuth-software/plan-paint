#pragma once
#include "image.hpp"
struct SDL_Window;
struct SDL_Cursor;
#include <mutex>
namespace paint {
enum class FileAction { None, Open, Paste, Save };
struct FileResult {
    FileAction action = FileAction::None;
    std::string path;
    std::string error;
    bool completed = false;
};
// SDL may invoke the dialog callback on another thread. The mailbox owns results.
struct FileDialog {
    std::mutex mutex;
    FileResult result;
    FileAction requested = FileAction::None;
    bool pending = false;
    void show(SDL_Window* window, FileAction action, const std::string& initial);
    FileResult take();
};
// Apply native cursor changes only on transitions, after the current UI frame.
class MouseCursorState {
  public:
    MouseCursorState();
    ~MouseCursorState();
    MouseCursorState(const MouseCursorState&) = delete;
    MouseCursorState& operator=(const MouseCursorState&) = delete;
    void update(int requested);

  private:
    std::vector<SDL_Cursor*> cursors_;
    int previous_ = -2;
    int visibility_ = -1;
    SDL_Window* focus_ = nullptr;
};
void copy_to_clipboard(const Image& image);
bool paste_from_clipboard(Image& image);
bool print_image(const Image& image);
void page_setup();
std::vector<std::string> installed_fonts();
std::string default_font_path(bool mono = false, bool bold = false, bool italic = false);
} // namespace paint

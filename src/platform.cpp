#include "platform.hpp"
#include "codecs.hpp"
#include "idle_render.hpp"
#include "imgui.h"
#include "paths.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
namespace paint {
MouseCursorState::MouseCursorState() : cursors_(ImGuiMouseCursor_COUNT, nullptr) {}
MouseCursorState::~MouseCursorState() {
    for (SDL_Cursor* cursor : cursors_) {
        SDL_DestroyCursor(cursor);
    }
}
void MouseCursorState::update(int requested) {
    SDL_Window* focus = SDL_GetMouseFocus();
    if (focus != focus_) {
        focus_ = focus;
        previous_ = -2;
        visibility_ = -1;
    }
    if (requested == ImGuiMouseCursor_None) {
        if (visibility_ != 0) {
            SDL_HideCursor();
            visibility_ = 0;
        }
        return;
    }
    requested = std::clamp(requested, 0, ImGuiMouseCursor_COUNT - 1);
    if (requested != previous_) {
        static const SDL_SystemCursor shapes[ImGuiMouseCursor_COUNT] = {
            SDL_SYSTEM_CURSOR_DEFAULT,     SDL_SYSTEM_CURSOR_TEXT,       SDL_SYSTEM_CURSOR_MOVE,
            SDL_SYSTEM_CURSOR_NS_RESIZE,   SDL_SYSTEM_CURSOR_EW_RESIZE,  SDL_SYSTEM_CURSOR_NESW_RESIZE,
            SDL_SYSTEM_CURSOR_NWSE_RESIZE, SDL_SYSTEM_CURSOR_POINTER,    SDL_SYSTEM_CURSOR_WAIT,
            SDL_SYSTEM_CURSOR_PROGRESS,    SDL_SYSTEM_CURSOR_NOT_ALLOWED};
        if (!cursors_[requested]) {
            cursors_[requested] = SDL_CreateSystemCursor(shapes[requested]);
        }
        if (cursors_[requested]) {
            SDL_SetCursor(cursors_[requested]);
        }
        previous_ = requested;
    }
    if (visibility_ != 1) {
        SDL_ShowCursor();
        visibility_ = 1;
    }
}
static const SDL_DialogFileFilter image_filters[] = {
    {"All supported images", "png;jpg;jpeg;bmp;tif;tiff;tga;webp;avif;svg;ico;cur;heic;heif"}};
static const SDL_DialogFileFilter save_filters[] = {{"PNG image", "png"},      {"JPEG picture", "jpg;jpeg"},
                                                    {"Bitmap picture", "bmp"}, {"TIFF picture", "tiff;tif"},
                                                    {"Targa image", "tga"},    {"Windows icon", "ico"},
                                                    {"Windows cursor", "cur"}};
static const char* save_extensions[] = {".png", ".jpg", ".bmp", ".tiff", ".tga", ".ico", ".cur"};
static void file_dialog_result(void* userdata, const char* const* files, int filter) {
    FileDialog& dialog = *static_cast<FileDialog*>(userdata);
    std::lock_guard<std::mutex> lock(dialog.mutex);
    try {
        if (!files) {
            dialog.result = {FileAction::None, "", SDL_GetError()};
        } else if (files[0]) {
            std::string path = files[0];
            if (dialog.requested == FileAction::Save && path_from_utf8(path).extension().empty()) {
                path += save_extensions[filter >= 0 && filter < 7 ? filter : 0];
            }
            dialog.result = {dialog.requested, path, ""};
        }
    } catch (...) {
        dialog.result.action = FileAction::None;
    }
    dialog.result.completed = true;
    dialog.pending = false;
    wake_event_loop();
}
void FileDialog::show(SDL_Window* window, FileAction action, const std::string& initial) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (pending) {
            return;
        }
        requested = action;
        pending = true;
    }
    if (action == FileAction::Save) {
        SDL_ShowSaveFileDialog(file_dialog_result, this, window, save_filters, 7, initial.c_str());
    } else {
        SDL_ShowOpenFileDialog(file_dialog_result, this, window, image_filters, 1,
                               initial.empty() ? nullptr : initial.c_str(), false);
    }
}
FileResult FileDialog::take() {
    std::lock_guard<std::mutex> lock(mutex);
    FileResult taken = std::move(result);
    result = {};
    return taken;
}
#ifndef __APPLE__
struct ClipboardImages {
    std::vector<std::uint8_t> png;
    std::vector<std::uint8_t> bmp;
};
static const void* clipboard_data(void* userdata, const char* type, std::size_t* size) {
    const ClipboardImages& images = *static_cast<ClipboardImages*>(userdata);
    if (type && std::strcmp(type, "image/png") == 0) {
        *size = images.png.size();
        return images.png.data();
    }
    if (type && std::strcmp(type, "image/bmp") == 0) {
        *size = images.bmp.size();
        return images.bmp.data();
    }
    *size = 0;
    return nullptr;
}
static void clipboard_cleanup(void* userdata) {
    delete static_cast<ClipboardImages*>(userdata);
}
void copy_to_clipboard(const Image& image) {
    std::unique_ptr<ClipboardImages> images = std::make_unique<ClipboardImages>();
    (*images).png = encode_png(image);
    (*images).bmp = encode_bmp(image);
    const char* types[] = {"image/png", "image/bmp"};
    if (!SDL_SetClipboardData(clipboard_data, clipboard_cleanup, images.get(), types, 2)) {
        throw std::runtime_error(SDL_GetError());
    }
    images.release();
}
bool paste_from_clipboard(Image& image) {
    const char* types[] = {"image/png", "image/bmp", "image/jpeg", "image/webp"};
    for (const char* type : types) {
        std::size_t size = 0;
        void* bytes = SDL_GetClipboardData(type, &size);
        if (!bytes) {
            continue;
        }
        try {
            image = decode_image(bytes, size);
            SDL_free(bytes);
            return true;
        } catch (...) {
            SDL_free(bytes);
            throw;
        }
    }
    return false;
}
std::string default_font_path(bool mono, bool bold, bool italic) {
#ifdef _WIN32
    std::string face = mono ? "consola" : "arial";
    if (bold && italic) {
        face += mono ? "z" : "bi";
    } else if (bold) {
        face += mono ? "b" : "bd";
    } else if (italic) {
        face += mono ? "i" : "i";
    }
    return "C:/Windows/Fonts/" + face + ".ttf";
#else
    std::string face = mono ? "DejaVuSansMono" : "DejaVuSans";
    if (bold && italic) {
        face += "-BoldOblique";
    } else if (bold) {
        face += "-Bold";
    } else if (italic) {
        face += "-Oblique";
    }
    return "/usr/share/fonts/truetype/dejavu/" + face + ".ttf";
#endif
}
#endif
} // namespace paint

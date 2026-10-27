#pragma once
#include "text.hpp"
#include "sticky_note.hpp"
#include <optional>
namespace paint {
struct TextEditState {
    std::string content;
    std::size_t caret = 0, anchor = 0;
};
// The editor stores UTF-8 byte boundaries; the same raster layout supplies both
// hit testing and the pixels eventually composited into the document.
class TextSession {
  public:
    bool active = false;
    Rect bounds{0, 0, 440, 160};
    TextStyle style;
    TextEditState edit;
    TextLayout layout;
    Image preview;
    std::optional<StickyNoteSettings> note;
    int inset() const;
    void begin(Point origin);
    void refresh(Color foreground, Color background);
    bool replace(const std::string& inserted);
    void history(bool redo);
    std::size_t caret_at(Point point) const;
    Point display_point(Point point) const;
    Point source_point(Point point) const;
    void resize(Rect rectangle);
    void clear();

  private:
    std::vector<TextEditState> undo_, redo_;
    std::string signature_;
    std::string paper_signature_;
    Image paper_;
};
} // namespace paint

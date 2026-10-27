#include "sticky_note.hpp"
#include "text_session.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
bool same(const paint::Image& a, const paint::Image& b) {
    if (a.width != b.width || a.height != b.height) {
        return false;
    }
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        if (!paint::equal(a.pixels[i], b.pixels[i])) {
            return false;
        }
    }
    return true;
}
void paper_and_text() {
    paint::StickyNoteSettings settings;
    settings.width = 96;
    settings.height = 80;
    settings.padding = 8;
    settings.font.size = 16;
    for (int paper = 0; paper < 10; ++paper) {
        settings.paper = paper;
        const paint::StickyNoteResult blank = paint::render_sticky_note(settings);
        require(blank.image.width == 96 && blank.image.height == 80 && !blank.text_clipped,
                "paper dimensions or blank text extent changed");
        require(same(blank.image, paint::render_sticky_note(settings).image),
                "paper seed is not deterministic");
        ++settings.seed;
        require(!same(blank.image, paint::render_sticky_note(settings).image),
                "paper seed does not vary texture");
        --settings.seed;
        for (paint::Color color : blank.image.pixels) {
            require(color.a == 255, "paper must remain opaque");
        }
        settings.text = "Plan\nPaint";
        const paint::StickyNoteResult text = paint::render_sticky_note(settings);
        require(!text.text_clipped && !same(blank.image, text.image), "fitting note text did not render");
        for (int y = 0; y < 80; ++y) {
            for (int x = 0; x < 96; ++x) {
                if (x < 8 || x >= 88 || y < 8 || y >= 72) {
                    require(paint::equal(blank.image.get(x, y), text.image.get(x, y)),
                            "text escaped padding");
                }
            }
        }
        settings.text.clear();
    }
    settings.text = "A long note with enough words to overflow this small sheet of paper.";
    require(paint::render_sticky_note(settings).text_clipped, "overflow must be reported before placement");
    settings.height = 320;
    require(!paint::render_sticky_note(settings).text_clipped, "word wrapping must use the enlarged note");
    settings.text = "W";
    settings.width = 16;
    settings.height = 32;
    settings.padding = 7;
    require(paint::render_sticky_note(settings).text_clipped, "a single wide glyph must report overflow");
}
void invalid_and_cancelled() {
    for (int invalid = 0; invalid < 7; ++invalid) {
        paint::StickyNoteSettings settings;
        if (invalid == 0) {
            settings.width = 4097;
        }
        if (invalid == 1) {
            settings.height = 15;
        }
        if (invalid == 2) {
            settings.padding = 96;
        }
        if (invalid == 3) {
            settings.font.size = 257;
        }
        if (invalid == 4) {
            settings.paper = 10;
        }
        if (invalid == 5) {
            settings.paper_color.a = 254;
        }
        if (invalid == 6) {
            settings.text.assign(8192, 'x');
        }
        bool rejected = false;
        try {
            static_cast<void>(paint::render_sticky_note(settings));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "invalid note settings were accepted");
    }
    const std::atomic<bool> cancel{true};
    bool cancelled = false;
    try {
        static_cast<void>(paint::render_sticky_note({}, &cancel));
    } catch (const std::runtime_error&) {
        cancelled = true;
    }
    require(cancelled, "cancelled note generation must not publish an image");
}
void inline_editing() {
    paint::TextSession text;
    text.begin({10, 20});
    text.note = paint::StickyNoteSettings{};
    text.resize({10, 20, 160, 120});
    text.refresh({20, 30, 40, 255}, {255, 255, 255, 255});
    const paint::Image blank = text.preview;
    require(blank.get(0, 0).a == 255, "an empty inline note must still display paper");
    text.replace("Plan\nPaint");
    text.refresh({20, 30, 40, 255}, {255, 255, 255, 255});
    require(!same(blank, text.preview) && paint::equal(blank.get(2, 2), text.preview.get(2, 2)),
            "typing must update text while preserving the paper and inset");
    const paint::Point caret = text.display_point({0, 0});
    require(caret.x == 16 && caret.y == 16 && text.source_point(caret).x == 0 &&
                text.source_point(caret).y == 0,
            "paper padding must also offset caret and hit testing");
    text.history(false);
    text.refresh({20, 30, 40, 255}, {255, 255, 255, 255});
    require(text.edit.content.empty() && same(blank, text.preview),
            "inline Undo must preserve the note background");
    text.history(true);
    text.resize({40, 50, 180, 150});
    text.refresh({20, 30, 40, 255}, {255, 255, 255, 255});
    require(text.preview.width == 180 && text.preview.height == 150 && text.edit.content == "Plan\nPaint",
            "moving and resizing must retain editable text and resize the paper");
    text.clear();
    text.begin({0, 0});
    text.refresh({0, 0, 0, 255}, {255, 255, 255, 255});
    require(!text.note && text.preview.get(0, 0).a == 0, "ordinary text must not inherit note paper");
}
} // namespace
int main() {
    try {
        paper_and_text();
        invalid_and_cancelled();
        inline_editing();
        std::cout << "Sticky paper, text, overflow and cancellation contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

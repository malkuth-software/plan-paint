#include "localization.hpp"
#include "forms/atlas.hpp"
#include "forms/editor.hpp"
#include <algorithm>
#include <cmath>
namespace paint::forms {
namespace gf = gui_forms;
std::string icon_format_name(IconFormat format) {
    if (format.storage == IconStorage::Unknown) {
        return "RGBA";
    }
    return std::to_string(format.bits_per_pixel) + " bpp " +
           (format.storage == IconStorage::Png ? "PNG" : "DIB");
}
namespace {
std::string icon_source_name(const IconFrame& frame) {
    return icon_format_name(frame.source ? (*frame.source).format : IconFormat{});
}
} // namespace
AtlasThumbnail::AtlasThumbnail(gf::StableId id, std::weak_ptr<Editor> editor, int index)
    : Button(std::move(id), ""), editor_(std::move(editor)), index_(index) {
    set_theme_override(ribbon_theme());
    set_requested_bounds({0, 0, 82, 72});
    set_margin({0, 0, 0, 0});
    click_ = Button::clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<AtlasThumbnail, &AtlasThumbnail::clicked>(*this));
}
void AtlasThumbnail::clicked(gf::ButtonBase&) {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (editor) {
        (*editor).select_frame(index_, sequence_);
    }
    sequence_ = false;
}
void AtlasThumbnail::on_pointer(gf::PointerEvent& event) {
    if (event.button == gf::PointerButton::secondary) {
        if (event.action == gf::PointerAction::down) {
            const std::shared_ptr<Editor> editor = editor_.lock();
            if (editor) {
                (*editor).set_reference_frame(index_);
            }
        }
        event.handled = true;
        return;
    }
    sequence_ = gf::has_modifier(event.modifiers, gf::Modifier::control) ||
                gf::has_modifier(event.modifiers, gf::Modifier::meta);
    Button::on_pointer(event);
}
void AtlasThumbnail::on_key(gf::KeyEvent& event) {
    sequence_ = gf::has_modifier(event.modifiers, gf::Modifier::control) ||
                gf::has_modifier(event.modifiers, gf::Modifier::meta);
    Button::on_key(event);
}
void AtlasThumbnail::on_detaching_from_window(gf::Window& window) noexcept {
    if (image_.value) {
        static_cast<void>(window.remove_image(image_));
    }
    image_ = {};
    Button::on_detaching_from_window(window);
}
void AtlasThumbnail::synchronize() {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    const AtlasState& atlas = (*editor).document.atlas;
    const bool icon = atlas.kind == AtlasKind::Icon || atlas.kind == AtlasKind::Cursor;
    set_requested_bounds({0, 0, icon ? 110.0 : 82.0, 72});
    bool selected = atlas.active == index_ ||
                    std::find(atlas.sequence.begin(), atlas.sequence.end(), index_) != atlas.sequence.end();
    set_selected(selected);
    set_accessible_name(tr("Frame ") + std::to_string(index_ + 1) +
                        (atlas.active == index_ ? tr(", current") : "") + (selected ? tr(", selected") : "") +
                        (icon && index_ < atlas.count()
                             ? "; " + tr("Source: ") + icon_source_name(atlas.icons[index_])
                             : ""));
    update_image();
    invalidate(gf::Dirty::paint);
}
void AtlasThumbnail::arrange(gf::Rect bounds) {
    Button::arrange(bounds);
    update_image();
}
void AtlasThumbnail::update_image() {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !window()) {
        return;
    }
    std::shared_ptr<gf::FlowLayoutPanelCompat> strip = std::dynamic_pointer_cast<gf::FlowLayoutPanelCompat>(parent());
    if (!strip) {
        return;
    }
    gf::Rect bounds = committed_arranged_bounds();
    gf::Rect viewport = (*strip).scroll_snapshot().viewport_rectangle;
    bool displayed = bounds.width > 0 && bounds.height > 0 && bounds.x < viewport.right() &&
                     bounds.right() > viewport.x && bounds.y < viewport.bottom() &&
                     bounds.bottom() > viewport.y;
    if (!displayed) {
        if (image_.value) {
            static_cast<void>((*window()).remove_image(image_));
            image_ = {};
        }
        return;
    }
    const Document& document = (*editor).document;
    const AtlasState& atlas = document.atlas;
    if (index_ >= atlas.count()) {
        return;
    }
    Rect source = atlas.kind == AtlasKind::Sheet
                      ? atlas.grid.frame(atlas.sheet, index_)
                      : Rect{0, 0, atlas.icons[index_].image.width, atlas.icons[index_].image.height};
    if (!image_.value || epoch_ != document.atlas_epoch) {
        const Image& original = atlas.kind == AtlasKind::Sheet ? atlas.sheet : atlas.icons[index_].image;
        double scale = std::min(44.0 / source.w, 44.0 / source.h);
        int width = std::max(1, static_cast<int>(source.w * scale)),
            height = std::max(1, static_cast<int>(source.h * scale));
        std::vector<std::byte> pixels(44 * 44 * 4);
        for (int y = 0; y < 44; ++y) {
            for (int x = 0; x < 44; ++x) {
                int checker = ((x / 6 + y / 6) % 2) ? 215 : 249;
                Color color{static_cast<std::uint8_t>(checker), static_cast<std::uint8_t>(checker),
                            static_cast<std::uint8_t>(checker), 255};
                int dx = x - (44 - width) / 2, dy = y - (44 - height) / 2;
                if (dx >= 0 && dx < width && dy >= 0 && dy < height) {
                    int sx = source.x + std::min(source.w - 1, dx * source.w / width);
                    int sy = source.y + std::min(source.h - 1, dy * source.h / height);
                    std::size_t offset = static_cast<std::size_t>(sy) * original.width + sx;
                    Color sample = original.pixels[offset];
                    if (atlas.kind != AtlasKind::Sheet && legacy_xor_pixel(atlas.icons[index_], offset)) {
                        Color mask = atlas.icons[index_].xor_pixels[offset];
                        color = {static_cast<std::uint8_t>(checker ^ mask.r),
                                 static_cast<std::uint8_t>(checker ^ mask.g),
                                 static_cast<std::uint8_t>(checker ^ mask.b), 255};
                    } else {
                        color = {static_cast<std::uint8_t>(
                                     (sample.r * sample.a + checker * (255 - sample.a) + 127) / 255),
                                 static_cast<std::uint8_t>(
                                     (sample.g * sample.a + checker * (255 - sample.a) + 127) / 255),
                                 static_cast<std::uint8_t>(
                                     (sample.b * sample.a + checker * (255 - sample.a) + 127) / 255),
                                 255};
                    }
                }
                std::size_t i = static_cast<std::size_t>(y * 44 + x) * 4;
                pixels[i] = static_cast<std::byte>(color.b);
                pixels[i + 1] = static_cast<std::byte>(color.g);
                pixels[i + 2] = static_cast<std::byte>(color.r);
                pixels[i + 3] = std::byte{255};
            }
        }
        gf::ImageLoadResult result =
            image_.value ? (*window()).replace_bgra32_premultiplied(image_, 44, 44, 176, pixels, *this)
                         : (*window()).load_bgra32_premultiplied(44, 44, 176, pixels);
        if (result) {
            image_ = result.image;
            epoch_ = document.atlas_epoch;
        }
    }
}
void AtlasThumbnail::on_paint(gf::Painter& painter, gf::Rect damage) {
    Button::on_paint(painter, damage);
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    const AtlasState& atlas = (*editor).document.atlas;
    if (index_ >= atlas.count()) {
        return;
    }
    Rect source = atlas.kind == AtlasKind::Sheet
                      ? atlas.grid.frame(atlas.sheet, index_)
                      : Rect{0, 0, atlas.icons[index_].image.width, atlas.icons[index_].image.height};
    const bool icon = atlas.kind == AtlasKind::Icon || atlas.kind == AtlasKind::Cursor;
    if (image_.value) {
        painter.draw_image(image_, {icon ? 37.0 : 19.0, 5, icon ? 36.0 : 44.0, icon ? 36.0 : 44.0});
    }
    painter.draw_text_utf8({5, icon ? 54.0 : 67.0},
                           std::to_string(index_ + 1) + ": " + std::to_string(source.w) + "×" +
                               std::to_string(source.h),
                           {gf::FontRole::control, 10, 400, false}, gf::Color::rgba(42, 65, 91));
    if (icon) {
        painter.draw_text_utf8({5, 68}, icon_source_name(atlas.icons[index_]),
                               {gf::FontRole::control, 10, 400, false}, gf::Color::rgba(42, 65, 91));
    }
}
AtlasPanel::AtlasPanel(gf::StableId id, std::weak_ptr<Editor> editor, bool gallery)
    : Control(std::move(id)), editor_(std::move(editor)), gallery_(gallery) {}
void AtlasPanel::initialize_control_tree() {
    const char* ids[] = {"atlas-grid",    "atlas-icons",    "atlas-cursors", "atlas-whole",
                         "atlas-leave",   "atlas-previous", "atlas-next",    "atlas-hotspot",
                         "atlas-gallery", "atlas-compare",  "atlas-export",  "atlas-alpha-tools",
                         "atlas-palette", "atlas-states"};
    const std::string labels[] = {
        tr("Sprite grid…"), tr("Icon sizes…"),  tr("Cursor sizes…"), tr("Whole sheet"), tr("Leave atlas"),
        tr("Previous"),     tr("Next"),         tr("Hotspot…"),      tr("Gallery…"),    tr("Compare…"),
        tr("Export…"),      tr("Alpha tools…"), tr("Palette…"),      tr("States…")};
    for (int i = 0; i < 14; ++i) {
        std::shared_ptr<gf::Button> control = gf::make_control<gf::Button>(
            gf::StableId(std::string(gallery_ ? "gallery-" : "") + ids[i]), labels[i]);
        (*control).set_theme_override(ribbon_theme());
        (*control).set_font({gf::FontRole::control, 11, 400, false});
        const int slot = i >= 11 ? i - 3 : i;
        (*control).set_requested_bounds({6 + (slot % 3) * 106.0, 5 + (slot / 3) * 26.0, 102, 24});
        subscriptions_.push_back((*control).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<AtlasPanel, &AtlasPanel::clicked>(*this)));
        buttons_.push_back(control);
        add_child(control);
    }
    strip_ =
        gf::make_control<gf::FlowLayoutPanelCompat>(gf::StableId(gallery_ ? "gallery-frames" : "atlas-frames"));
    (*strip_).set_auto_scroll(true);
    (*strip_).set_padding({0, 0, 0, 0});
    (*strip_).set_wrap_contents(gallery_);
    (*strip_).set_item_spacing({4, 0});
    add_child(strip_);
    status_ = gf::make_control<gf::Label>(gf::StableId(gallery_ ? "gallery-status" : "atlas-status"));
    (*status_).set_font({gf::FontRole::control, 11, 400, false});
    add_child(status_);
    wrap_ = gf::make_control<gf::CheckBox>(gf::StableId(gallery_ ? "gallery-atlas-wrap" : "atlas-wrap"),
                                           tr("Warp edges"));
    alpha_ = gf::make_control<gf::CheckBox>(gf::StableId(gallery_ ? "gallery-atlas-alpha" : "atlas-alpha"),
                                            tr("Preserve transparency"));
    clear_reference_ = gf::make_control<gf::Button>(
        gf::StableId(gallery_ ? "gallery-atlas-reference-clear" : "atlas-reference-clear"),
        tr("Dismiss reference"));
    for (const std::shared_ptr<gf::CheckBox>& check : {wrap_, alpha_}) {
        (*check).set_font({gf::FontRole::control, 11, 400, false});
        subscriptions_.push_back((*check).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<AtlasPanel, &AtlasPanel::clicked>(*this)));
        add_child(check);
    }
    (*clear_reference_).set_font({gf::FontRole::control, 11, 400, false});
    subscriptions_.push_back(
        (*clear_reference_)
            .clicked()
            .subscribe(*this, gf::Delegate<gf::ButtonBase&>::bind<AtlasPanel, &AtlasPanel::clicked>(*this)));
    add_child(clear_reference_);
}
void AtlasPanel::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        set_child_layout(buttons_[i], (*buttons_[i]).requested_bounds());
    }
    set_child_layout(buttons_[8], {bounds.width - 96, bounds.height - 22, 88, 20});
    set_child_layout(buttons_[9], {bounds.width - 192, bounds.height - 22, 88, 20});
    set_child_layout(buttons_[10], {bounds.width - 288, bounds.height - 22, 88, 20});
    const double controls = std::min(324.0, bounds.width * 0.42);
    const double options = std::min(190.0, bounds.width * 0.22);
    set_child_layout(
        strip_, {controls + 6, 4, std::max(1.0, bounds.width - controls - options - 18), bounds.height - 26});
    set_child_layout(status_,
                     {controls + 6, bounds.height - 22, std::max(1.0, bounds.width - controls - 304), 20});
    set_child_layout(wrap_, {bounds.width - options - 6, 5, options, 25});
    set_child_layout(alpha_, {bounds.width - options - 6, 34, options, 25});
    set_child_layout(clear_reference_, {bounds.width - options - 6, 65, options, 25});
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        if (i >= 8 && i <= 10) {
            continue;
        }
        gf::Rect rectangle = (*buttons_[i]).requested_bounds();
        rectangle.x *= controls / 324;
        rectangle.width *= controls / 324;
        set_child_layout(buttons_[i], rectangle);
    }
}
void AtlasPanel::clicked(gf::ButtonBase& control) {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (editor) {
        std::string command(control.stable_id().value());
        (*editor).execute(command.starts_with("gallery-") ? command.substr(8) : command);
    }
}
void AtlasPanel::synchronize() {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    Document& document = (*editor).document;
    document.sync_atlas();
    AtlasState& atlas = document.atlas;
    if (kind_ != atlas.kind || count_ != atlas.count()) {
        for (std::size_t i = 0; i < thumbnails_.size(); ++i) {
            static_cast<void>((*strip_).remove_child((*thumbnails_[i]).runtime_id()));
        }
        thumbnails_.clear();
        kind_ = atlas.kind;
        count_ = atlas.count();
        active_ = -2;
        for (int i = 0; i < count_; ++i) {
            std::shared_ptr<AtlasThumbnail> thumbnail = gf::make_control<AtlasThumbnail>(
                gf::StableId(std::string(gallery_ ? "gallery-frame-" : "atlas-frame-") + std::to_string(i)),
                editor_, i);
            thumbnails_.push_back(thumbnail);
            (*strip_).add_child(thumbnail);
        }
    }
    for (std::size_t i = 0; i < thumbnails_.size(); ++i) {
        (*thumbnails_[i]).synchronize();
    }
    if (active_ != atlas.active && atlas.active >= 0 && atlas.active < count_) {
        active_ = atlas.active;
        reveal_current();
    }
    (*wrap_).set_checked((*editor).atlas_wrap);
    (*alpha_).set_checked((*editor).atlas_preserve_alpha);
    (*wrap_).set_enabled(atlas.active >= 0);
    (*alpha_).set_enabled(atlas.active >= 0);
    (*clear_reference_).set_enabled(!(*editor).atlas_reference.pixels.empty());
    (*buttons_[8]).set_visible(!gallery_);
    (*buttons_[9]).set_enabled(atlas.kind == AtlasKind::Icon || atlas.kind == AtlasKind::Cursor);
    (*buttons_[10]).set_enabled(count_ > 0);
    (*buttons_[10]).set_text(atlas.kind == AtlasKind::Sheet ? tr("Toolbar strip") : tr("Export…"));
    (*buttons_[11]).set_enabled(atlas.kind == AtlasKind::Icon || atlas.kind == AtlasKind::Cursor);
    (*buttons_[12]).set_enabled(atlas.kind == AtlasKind::Icon || atlas.kind == AtlasKind::Cursor);
    (*buttons_[13]).set_enabled(atlas.kind == AtlasKind::Icon || atlas.kind == AtlasKind::Cursor);
    (*buttons_[3]).set_enabled(atlas.kind == AtlasKind::Sheet);
    (*buttons_[4]).set_enabled(atlas.kind != AtlasKind::None);
    (*buttons_[5]).set_enabled(count_ > 0);
    (*buttons_[6]).set_enabled(count_ > 0);
    (*buttons_[7]).set_enabled(atlas.kind == AtlasKind::Cursor);
    (*status_).set_text(
        count_ == 0
            ? tr("Split a sprite sheet or create a set of icon or cursor sizes.")
            : (atlas.active < 0
                   ? tr("Whole sheet")
                   : tr("Frame ") + std::to_string(atlas.active + 1) + tr(" of ") + std::to_string(count_)) +
                  tr(" · Ctrl-click frames to select a sequence; use Previous / Next to step through it."));
    if ((atlas.kind == AtlasKind::Icon || atlas.kind == AtlasKind::Cursor) && atlas.active >= 0 &&
        atlas.active < count_) {
        const IconFrame& frame = atlas.icons[atlas.active];
        const ContainerKind kind =
            atlas.kind == AtlasKind::Icon ? ContainerKind::Icon : ContainerKind::Cursor;
        (*status_).set_text(tr("Frame ") + std::to_string(atlas.active + 1) + tr(" of ") +
                            std::to_string(count_) + " · " + tr("Source: ") + icon_source_name(frame) +
                            " · " + tr("Save: ") + icon_format_name(icon_output_format(frame, kind)));
    }
}
void AtlasPanel::reveal_current() {
    if (!window() || active_ < 0 || active_ >= count_) {
        return;
    }
    (*window()).perform_layout();
    (*strip_).scroll_control_into_view(thumbnails_[static_cast<std::size_t>(active_)]);
}
void Editor::paint_atlas_overlay(gf::Painter& painter) {
    if (std::abs(canvas().view_angle) > 1e-10) {
        return;
    }
    if ((document.atlas.kind != AtlasKind::Icon && document.atlas.kind != AtlasKind::Cursor) ||
        document.atlas.active < 0) {
        return;
    }
    const IconFrame& frame = document.atlas.icons[document.atlas.active];
    if (frame.image.width != document.image.width || frame.image.height != document.image.height) {
        return;
    }
    const double zoom = canvas().zoom();
    const gf::Point origin = screen({0, 0});
    const gf::Rect viewport = canvas().committed_arranged_bounds();
    for (std::size_t i = 0; i < frame.xor_pixels.size(); ++i) {
        if (!legacy_xor_pixel(frame, i) || !equal(document.image.pixels[i], {255, 255, 255, 0})) {
            continue;
        }
        int x = static_cast<int>(i % frame.image.width), y = static_cast<int>(i / frame.image.width);
        double left = x * zoom, top = y * zoom, right = (x + 1) * zoom, bottom = (y + 1) * zoom;
        left = std::max(left, -origin.x);
        top = std::max(top, -origin.y);
        right = std::min(right, viewport.width - origin.x);
        bottom = std::min(bottom, viewport.height - origin.y);
        if (right <= left || bottom <= top) {
            continue;
        }
        Color mask = frame.xor_pixels[i];
        for (int row = static_cast<int>(std::floor(top / 12)); row * 12 < bottom; ++row) {
            for (int column = static_cast<int>(std::floor(left / 12)); column * 12 < right; ++column) {
                int background = (row + column) % 2 ? 255 : 240;
                double cell_left = std::max(left, column * 12.0), cell_top = std::max(top, row * 12.0);
                painter.fill_rect(
                    {origin.x + cell_left, origin.y + cell_top,
                     std::min(right, (column + 1) * 12.0) - cell_left,
                     std::min(bottom, (row + 1) * 12.0) - cell_top},
                    gf::Color::rgba(mask.r ^ background, mask.g ^ background, mask.b ^ background));
            }
        }
    }
}
void Editor::set_reference_frame(int index) {
    if (!atlas_painting() || index < 0 || index >= document.atlas.count()) {
        return;
    }
    document.sync_atlas();
    reference_frame = index;
    atlas_reference = document.atlas.frame_image(index);
    refresh();
}
void Editor::select_frame(int index, bool sequence) {
    unset_guide();
    finish_controls();
    document.atlas_select(index, sequence);
    refresh();
}
} // namespace paint::forms

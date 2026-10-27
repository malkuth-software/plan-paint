#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "localization.hpp"
#include <algorithm>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
Image effect_preview(const IconFrame& frame, int zoom) {
    if (zoom <= 1) {
        return render_asset_preview(frame, 355, 192);
    }
    IconFrame enlarged;
    enlarged.image.reset(frame.image.width * zoom, frame.image.height * zoom, {0, 0, 0, 0});
    for (int y = 0; y < enlarged.image.height; ++y) {
        for (int x = 0; x < enlarged.image.width; ++x) {
            enlarged.image.set(x, y, frame.image.get(x / zoom, y / zoom));
        }
    }
    return render_asset_preview(enlarged, 355, 192);
}
} // namespace
void EditorDialog::initialize_icon_effects() {
    panel_ = {0, 0, 780, 510};
    const std::shared_ptr<Editor> editor = editor_.lock();
    Document& document = (*editor).document;
    document.sync_atlas();
    effect_source_ = document.image;
    effect_revision_ = document.revision;
    effect_epoch_ = document.atlas_epoch;
    effect_current_ = document.atlas.active;
    label("effect-scope",
          tr("Current image") + " · " + std::to_string(effect_source_.width) + " × " +
              std::to_string(effect_source_.height),
          {20, 48, 195, 28});
    effect_advanced_ = gf::make_control<gf::CheckBox>(gf::StableId("effect-advanced"), tr("Advanced"));
    put(effect_advanced_, {220, 48, 120, 28});
    subscriptions_.push_back(
        (*effect_advanced_)
            .clicked()
            .subscribe(
                *this,
                gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::effect_advanced_changed>(
                    *this)));
    effect_choice_ = gf::make_control<gf::ComboBox>(gf::StableId("effect-choice"));
    (*effect_choice_)
        .set_items({tr("Brightness / contrast"), tr("Hue / saturation"), tr("Grayscale"), tr("Opacity %"),
                    tr("Blur"), tr("Sharpen"), tr("Shadow"), tr("Fit artwork within image")});
    (*effect_choice_).set_selected_index(0);
    (*effect_choice_).set_accessible_name(tr("Effects"));
    put(effect_choice_, {350, 48, 410, 28});
    label("effect-before-label", tr("Original"), {20, 84, 355, 24});
    label("effect-after-label", tr("Preview"), {405, 84, 355, 24});
    effect_before_ = gf::make_control<gf::RasterCanvas>(gf::StableId("effect-before"));
    effect_after_ = gf::make_control<gf::RasterCanvas>(gf::StableId("effect-after"));
    (*effect_before_).set_accessible_name(tr("Original"));
    (*effect_after_).set_accessible_name(tr("Preview"));
    put(effect_before_, {20, 112, 355, 192});
    put(effect_after_, {405, 112, 355, 192});
    for (int index = 0; index < 5; ++index) {
        const std::shared_ptr<gf::Label> caption =
            gf::make_control<gf::Label>(gf::StableId("effect-label-" + std::to_string(index)));
        const std::shared_ptr<gf::NumericUpDown> value =
            gf::make_control<gf::NumericUpDown>(gf::StableId("effect-value-" + std::to_string(index)));
        put(caption, {20 + index * 148.0, 312, 138, 24});
        put(value, {20 + index * 148.0, 340, 138, 28});
        effect_labels_.push_back(caption);
        effect_values_.push_back(value);
        subscriptions_.push_back((*value).value_changed().subscribe(
            *this, gf::Delegate<double>::bind<EditorDialog, &EditorDialog::effect_number_changed>(*this)));
    }
    label("effect-color-label", tr("Color"), {20, 378, 80, 28});
    effect_color_ = gf::make_control<gf::TextBox>(gf::StableId("effect-color"));
    (*effect_color_).set_text("#000000");
    (*effect_color_).set_accessible_name(tr("Color"));
    put(effect_color_, {110, 378, 140, 28});
    effect_status_ = gf::make_control<gf::TextBox>(gf::StableId("effect-status"));
    (*effect_status_).set_read_only(true);
    (*effect_status_).set_multiline(true);
    (*effect_status_).set_word_wrap(true);
    (*effect_status_).set_accessible_name(tr("Preview"));
    put(effect_status_, {270, 378, 490, 64});
    error_ = gf::make_control<gf::Label>(gf::StableId("dialog-error"));
    (*error_).set_text_wrapping(gf::TextWrapping::word);
    put(error_, {20, 444, 740, 26});
    const std::shared_ptr<gf::Button> reset = button("effect-reset", tr("Reset"), {20, 474, 125, 28});
    const std::shared_ptr<gf::Button> repeat =
        button("effect-repeat", tr("Repeat effect…"), {155, 474, 180, 28});
    (*repeat).set_enabled((*editor).last_icon_effect.has_value());
    effect_apply_ = button("effect-apply", tr("Apply"), {550, 474, 100, 28});
    button("dialog-cancel", tr("Close"), {660, 474, 100, 28});
    for (const std::shared_ptr<gf::Button>& control : {reset, repeat, effect_apply_}) {
        subscriptions_.push_back((*control).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::effect_clicked>(*this)));
    }
    subscriptions_.push_back(
        (*effect_choice_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::effect_choice_changed>(*this)));
    subscriptions_.push_back(
        (*effect_color_)
            .text_changed()
            .subscribe(
                *this,
                gf::Delegate<const std::string&>::bind<EditorDialog, &EditorDialog::effect_text_changed>(
                    *this)));
    effect_choice_changed(0);
}
void EditorDialog::effect_choice_changed(std::optional<std::size_t>) {
    if (synchronizing_) {
        return;
    }
    synchronizing_ = true;
    const std::size_t choice = (*effect_choice_).selected_index().value_or(0);
    const char* names[15][5] = {{"Brightness %", "Contrast %", "", "", ""},
                                {"Hue", "Saturation %", "", "", ""},
                                {"", "", "", "", ""},
                                {"Opacity %", "", "", "", ""},
                                {"Radius (px)", "", "", "", ""},
                                {"Radius (px)", "Strength %", "", "", ""},
                                {"Radius (px)", "Offset X", "Offset Y", "Opacity %", ""},
                                {"Padding", "", "", "", ""},
                                {"Gamma", "", "", "", ""},
                                {"Input black", "Input white", "Gamma", "Output black", "Output white"},
                                {"", "", "", "", ""},
                                {"Strength %", "", "", "", ""},
                                {"Strength %", "", "", "", ""},
                                {"Strength %", "", "", "", ""},
                                {"Strength %", "", "", "", ""}};
    const double minimum[15][5] = {{-100, -100},      {-180, 0}, {}, {}, {}, {}, {0, -128, -128}, {}, {0.1},
                                   {0, 1, 0.1, 0, 0}, {},        {}, {}, {}, {}};
    const double maximum[15][5] = {{100, 100},
                                   {180, 200},
                                   {},
                                   {100},
                                   {32},
                                   {32, 300},
                                   {32, 128, 128, 100},
                                   {127},
                                   {10},
                                   {254, 255, 10, 255, 255},
                                   {},
                                   {300},
                                   {300},
                                   {300},
                                   {300}};
    const double defaults[15][5] = {{},       {0, 100},      {},    {100}, {2},
                                    {2, 100}, {2, 2, 2, 55}, {2},   {1},   {0, 255, 1, 0, 255},
                                    {},       {100},         {100}, {100}, {100}};
    for (int index = 0; index < 5; ++index) {
        const bool visible = names[choice][index][0] != 0;
        (*effect_labels_[index]).set_text(tr(names[choice][index]));
        (*effect_labels_[index]).set_visible(visible);
        (*effect_values_[index]).set_visible(visible);
        (*effect_values_[index]).set_accessible_name(tr(names[choice][index]));
        const bool fractional = (choice == 8 && index == 0) || (choice == 9 && index == 2);
        (*effect_values_[index]).set_decimal_places(fractional ? 2 : 0);
        (*effect_values_[index]).set_increment(fractional ? 0.01 : 1);
        (*effect_values_[index]).set_range(minimum[choice][index], maximum[choice][index]);
        (*effect_values_[index]).set_value(defaults[choice][index]);
    }
    (*effect_color_).set_enabled(choice == 6);
    (*effect_color_).set_text("#000000");
    synchronizing_ = false;
    render_icon_effects();
}
void EditorDialog::effect_advanced_changed(gf::ButtonBase&) {
    const std::size_t previous = (*effect_choice_).selected_index().value_or(0);
    std::vector<std::string> choices = {tr("Brightness / contrast"),
                                        tr("Hue / saturation"),
                                        tr("Grayscale"),
                                        tr("Opacity %"),
                                        tr("Blur"),
                                        tr("Sharpen"),
                                        tr("Shadow"),
                                        tr("Fit artwork within image")};
    if ((*effect_advanced_).checked()) {
        const std::vector<std::string> advanced = {tr("Gamma"),      tr("Levels"),        tr("Invert"),
                                                   tr("Find edges"), tr("Enhance edges"), tr("Contour"),
                                                   tr("Emboss")};
        choices.insert(choices.end(), advanced.begin(), advanced.end());
    }
    const std::size_t selected = previous < choices.size() ? previous : 0;
    synchronizing_ = true;
    (*effect_choice_).set_items(std::move(choices));
    (*effect_choice_).set_selected_index(selected);
    synchronizing_ = false;
    if (selected != previous) {
        effect_choice_changed(selected);
    }
}
void EditorDialog::restore_icon_effect(const IconEffectSettings& settings) {
    (*effect_advanced_).set_checked(settings.effect >= IconEffect::Gamma);
    effect_advanced_changed(*effect_advanced_);
    (*effect_choice_).set_selected_index(static_cast<std::size_t>(settings.effect));
    effect_choice_changed({});
    std::array<double, 5> values{};
    switch (settings.effect) {
    case IconEffect::BrightnessContrast:
        values = {settings.brightness, settings.contrast};
        break;
    case IconEffect::HueSaturation:
        values = {settings.hue, settings.saturation};
        break;
    case IconEffect::Opacity:
        values = {settings.opacity};
        break;
    case IconEffect::Blur:
        values = {double(settings.radius)};
        break;
    case IconEffect::Sharpen:
        values = {double(settings.radius), settings.strength};
        break;
    case IconEffect::Shadow:
        values = {double(settings.radius), double(settings.offset_x), double(settings.offset_y),
                  settings.opacity};
        break;
    case IconEffect::Fit:
        values = {double(settings.padding)};
        break;
    case IconEffect::Gamma:
        values = {settings.gamma};
        break;
    case IconEffect::Levels:
        values = {double(settings.input_black), double(settings.input_white), settings.gamma,
                  double(settings.output_black), double(settings.output_white)};
        break;
    case IconEffect::EdgeFind:
    case IconEffect::EdgeEnhance:
    case IconEffect::Contour:
    case IconEffect::Emboss:
        values = {settings.strength};
        break;
    case IconEffect::Grayscale:
    case IconEffect::Invert:
        break;
    }
    synchronizing_ = true;
    for (std::size_t index = 0; index < values.size(); ++index) {
        (*effect_values_[index]).set_value(values[index]);
    }
    (*effect_color_).set_text(to_hex(settings.shadow));
    synchronizing_ = false;
    render_icon_effects();
}
void EditorDialog::effect_number_changed(double) {
    if (!synchronizing_) {
        render_icon_effects();
    }
}
void EditorDialog::effect_text_changed(const std::string&) {
    if (!synchronizing_) {
        render_icon_effects();
    }
}
void EditorDialog::render_icon_effects() {
    if (!attached_window()) {
        return;
    }
    (*effect_apply_).set_enabled(false);
    (*error_).set_text("");
    effect_result_ = {};
    try {
        IconEffectSettings settings;
        settings.effect = static_cast<IconEffect>((*effect_choice_).selected_index().value_or(0));
        const double first = (*effect_values_[0]).value(), second = (*effect_values_[1]).value();
        switch (settings.effect) {
        case IconEffect::BrightnessContrast:
            settings.brightness = first;
            settings.contrast = second;
            break;
        case IconEffect::HueSaturation:
            settings.hue = first;
            settings.saturation = second;
            break;
        case IconEffect::Opacity:
            settings.opacity = first;
            break;
        case IconEffect::Blur:
            settings.radius = static_cast<int>(first);
            break;
        case IconEffect::Sharpen:
            settings.radius = static_cast<int>(first);
            settings.strength = second;
            break;
        case IconEffect::Shadow:
            settings.radius = static_cast<int>(first);
            settings.offset_x = static_cast<int>(second);
            settings.offset_y = static_cast<int>((*effect_values_[2]).value());
            settings.opacity = (*effect_values_[3]).value();
            if (!from_hex(std::string((*effect_color_).text()), settings.shadow)) {
                throw std::runtime_error(tr("Invalid color"));
            }
            break;
        case IconEffect::Fit:
            settings.padding = static_cast<int>(first);
            break;
        case IconEffect::Gamma:
            settings.gamma = first;
            break;
        case IconEffect::Levels:
            settings.input_black = static_cast<int>(first);
            settings.input_white = static_cast<int>(second);
            settings.gamma = (*effect_values_[2]).value();
            settings.output_black = static_cast<int>((*effect_values_[3]).value());
            settings.output_white = static_cast<int>((*effect_values_[4]).value());
            break;
        case IconEffect::EdgeFind:
        case IconEffect::EdgeEnhance:
        case IconEffect::Contour:
        case IconEffect::Emboss:
            settings.strength = first;
            break;
        case IconEffect::Invert:
        case IconEffect::Grayscale:
            break;
        }
        effect_result_ = render_icon_effect(effect_source_, settings);
        effect_settings_ = settings;
        (*effect_apply_)
            .set_enabled(!std::equal(effect_result_.image.pixels.begin(), effect_result_.image.pixels.end(),
                                     effect_source_.pixels.begin(), equal));
    } catch (const std::exception& exception) {
        (*error_).set_text(exception.what());
    }
    IconFrame before, after;
    before.image = effect_source_;
    after.image = effect_result_.image.pixels.empty() ? effect_source_ : effect_result_.image;
    if (!effect_result_.overflow.pixels.empty()) {
        after.image = effect_result_.overflow;
        const Rect bounds = effect_result_.bounds;
        for (int x = bounds.x; x < bounds.x + bounds.w; ++x) {
            after.image.set(x, bounds.y, {235, 40, 100, 255});
            after.image.set(x, bounds.y + bounds.h - 1, {235, 40, 100, 255});
        }
        for (int y = bounds.y; y < bounds.y + bounds.h; ++y) {
            after.image.set(bounds.x, y, {235, 40, 100, 255});
            after.image.set(bounds.x + bounds.w - 1, y, {235, 40, 100, 255});
        }
    }
    const int zoom = std::max(1, std::min(355 / std::max(before.image.width, after.image.width),
                                          192 / std::max(before.image.height, after.image.height)));
    publish_image(effect_preview(before, zoom), *effect_before_);
    publish_image(effect_preview(after, zoom), *effect_after_);
    (*effect_status_)
        .set_text(effect_result_.overflow.pixels.empty()
                      ? tr("Current image") + " · " + std::to_string(effect_source_.width) + " × " +
                            std::to_string(effect_source_.height)
                      : tr("Pink border: image bounds. Pixels outside are clipped.") + " " +
                            std::to_string(effect_result_.clipped_pixels));
}
void EditorDialog::effect_clicked(gf::ButtonBase& control) {
    const std::shared_ptr<EditorDialog> keep_alive =
        std::static_pointer_cast<EditorDialog>(shared_from_this());
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    if (control.stable_id().value() == "effect-repeat") {
        if ((*editor).last_icon_effect) {
            restore_icon_effect(*(*editor).last_icon_effect);
        }
        return;
    }
    if (control.stable_id().value() == "effect-reset") {
        effect_choice_changed({});
        return;
    }
    if (!(*effect_apply_).enabled()) {
        return;
    }
    try {
        Document& document = (*editor).document;
        if (document.revision != effect_revision_ || document.atlas_epoch != effect_epoch_ ||
            document.atlas.active != effect_current_ || document.image.width != effect_source_.width ||
            document.image.height != effect_source_.height ||
            !std::equal(document.image.pixels.begin(), document.image.pixels.end(),
                        effect_source_.pixels.begin(), effect_source_.pixels.end(), equal)) {
            throw std::runtime_error(tr("Image changed. Reopen Effects."));
        }
        document.require_rgba_transform();
        document.checkpoint();
        document.image = std::move(effect_result_.image);
        (*editor).last_icon_effect = effect_settings_;
        (*editor).close_editor_dialog();
        (*editor).refresh();
    } catch (const std::exception& exception) {
        (*error_).set_text(exception.what());
    }
}
} // namespace paint::forms

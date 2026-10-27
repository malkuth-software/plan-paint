#include "application.hpp"
#include "gui_scope.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
namespace paint {
void Application::choose_shape(Shape shape) {
    if (shape != document.shape) {
        finish_curve();
    }
    if (document.continuous_path) {
        document.commit_path();
    }
    choose_tool(shape == Shape::Polygon ? Tool::Path : Tool::Shape);
    document.continuous_path = false;
    document.shape = shape;
}
namespace {
// Ribbon coordinates begin at the tabs; the native window owns the only title bar.
void ribbon_cursor(ImVec2 position) {
    ImGui::SetCursorPos({position.x, position.y - 27});
}
} // namespace

void Application::reset_stamp() {
    document.stamp = {};
    stamp_field.reset();
    stamp_preview = {};
    stamp_render_pending = false;
    stamp_scale = 1;
    stamp_angle = 0;
    ++stamp_generation;
    ++stamp_source_generation;
    SDL_DestroyTexture(stamp_texture);
    stamp_texture = nullptr;
    choose_tool(Tool::Stamp);
    status = "Click the picture to lift a new stamp. The old stamp has been cleared.";
}
void Application::stamp_controls() {
    bool reset = ImGui::Button("Lift a new stamp", {245, 28});
    if (reset) {
        reset_stamp();
        ImGui::CloseCurrentPopup();
    }
    ImGui::TextDisabled("Next click picks up a fresh sample.");
    ImGui::Separator();
    int stamp = static_cast<int>(document.stamp_shape);
    const char* stamp_names[] = {"Circle", "Pill", "Square", "Rectangle"};
    if (ImGui::Combo("Stamp outline", &stamp, stamp_names, 4)) {
        document.stamp_shape = static_cast<StampShape>(stamp);
    }
    ImGui::SliderInt("Stamp width", &stamp_width, 4, 500);
    if (document.stamp_shape == StampShape::Pill || document.stamp_shape == StampShape::Rectangle) {
        ImGui::SliderInt("Stamp height", &stamp_height, 4, 500);
    }
    ImGui::Checkbox("Transparent stamp (skip Color 2)", &document.stamp_transparent);
    ImGui::TextDisabled("R rotates; Shift+R turns back; +/- scales.");
}
void Application::material_preview(ImVec2 position) {
    std::string signature =
        std::to_string(static_cast<int>(document.ink.brush)) + ":" +
        std::to_string(static_cast<int>(document.shape_fill_brush)) + ":" +
        std::to_string(static_cast<int>(document.ink.pattern)) + ":" + std::to_string(document.ink.noise) +
        ":" + std::to_string(document.ink.grain_scale) + ":" + std::to_string(document.ink.paper_roughness) +
        ":" + std::to_string(document.ink.pigment_load) + ":" + std::to_string(document.ink.material_angle) +
        ":" + std::to_string(packed(document.ink.primary)) + ":" +
        std::to_string(packed(document.ink.secondary)) + (document.ink.transparent_pattern ? "t" : "o");
    if (signature != material_preview_signature) {
        Image sample;
        sample.reset(144, 78, {255, 255, 255, 255});
        Ink ink = document.ink;
        ink.size = 14;
        draw_shape(sample, Shape::RoundedRectangle, {12, 12}, {132, 65}, ink, true, true,
                   document.shape_fill_brush);
        SDL_DestroyTexture(material_texture);
        material_texture =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 144, 78);
        if (!material_texture) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_UpdateTexture(material_texture, nullptr, sample.pixels.data(), 144 * 4);
        material_preview_signature = signature;
        ++texture_generation;
    }
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    draw.AddImage(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(material_texture)), position,
                  {position.x + 144, position.y + 78});
    draw.AddRect(position, {position.x + 144, position.y + 78}, IM_COL32(150, 175, 200, 255));
}
void classic_icon(ImDrawList& draw, int icon, ImVec2 position, float size, ImU32 color) {
    float x = position.x, y = position.y, s = size;
    ImU32 blue = IM_COL32(60, 135, 203, 255), gold = IM_COL32(233, 174, 55, 255),
          edge = IM_COL32(76, 88, 101, 255);
    if (icon >= 100 && icon < 100 + shape_count) {
        Shape shape = static_cast<Shape>(icon - 100);
        if (shape == Shape::Bezier) {
            draw.AddBezierCubic({x + 1, y + s - 3}, {x + s * .35f, y - s * .4f}, {x + s * .65f, y + s * 1.4f},
                                {x + s - 1, y + 3}, color, 1.3f);
            return;
        }
        if (shape == Shape::Arc) {
            draw.PathArcTo({x + s * .5f, y + s * .75f}, s * .43f, 3.14159265f, 6.2831853f, 16);
            draw.PathStroke(color, 0, 1.3f);
            draw.AddCircleFilled({x + s * .5f, y + s * .32f}, 2, blue);
            return;
        }
        std::vector<Point> points = shape_points(shape, {x + 2, y + (shape == Shape::Oval ? s * 0.24f : 2)},
                                                 {x + s - 2, y + s - (shape == Shape::Oval ? s * 0.24f : 2)});
        for (std::size_t i = 1; i < points.size(); ++i) {
            draw.AddLine(ImVec2(static_cast<float>(points[i - 1].x), static_cast<float>(points[i - 1].y)),
                         ImVec2(static_cast<float>(points[i].x), static_cast<float>(points[i].y)), color,
                         1.1f);
        }
        if (points.size() > 2) {
            draw.AddLine(ImVec2(static_cast<float>(points.back().x), static_cast<float>(points.back().y)),
                         ImVec2(static_cast<float>(points.front().x), static_cast<float>(points.front().y)),
                         color, 1.1f);
        }
        return;
    }
    switch (icon) {
    case 0: // Clipboard with bevelled clip and ruled paper.
        draw.AddRectFilled({x + s * .16f, y + s * .12f}, {x + s * .87f, y + s * .96f},
                           IM_COL32(120, 78, 44, 255), 2);
        draw.AddRectFilledMultiColor({x + s * .20f, y + s * .15f}, {x + s * .83f, y + s * .91f},
                                     IM_COL32(225, 179, 105, 255), IM_COL32(190, 135, 71, 255),
                                     IM_COL32(149, 97, 51, 255), IM_COL32(202, 154, 89, 255));
        draw.AddRectFilled({x + s * .27f, y + s * .24f}, {x + s * .77f, y + s * .85f},
                           IM_COL32(250, 253, 255, 255));
        draw.AddRect({x + s * .27f, y + s * .24f}, {x + s * .77f, y + s * .85f},
                     IM_COL32(107, 136, 157, 255));
        for (int line = 0; line < 6; ++line) {
            float ly = y + s * (.34f + line * .075f);
            draw.AddLine({x + s * .33f, ly}, {x + s * .7f, ly}, IM_COL32(184, 204, 219, 255));
        }
        draw.AddRectFilledMultiColor({x + s * .35f, y + s * .08f}, {x + s * .68f, y + s * .26f},
                                     IM_COL32(250, 251, 247, 255), IM_COL32(236, 241, 242, 255),
                                     IM_COL32(130, 148, 158, 255), IM_COL32(187, 200, 206, 255));
        draw.AddRect({x + s * .35f, y + s * .08f}, {x + s * .68f, y + s * .26f}, edge, 1);
        draw.AddCircleFilled({x + s * .51f, y + s * .08f}, s * .065f, IM_COL32(150, 164, 174, 255));
        break;
    case 1: // scissors
        draw.AddCircle(ImVec2(x + s * .2f, y + s * .75f), s * .13f, blue, 10, 1.7f);
        draw.AddCircle(ImVec2(x + s * .52f, y + s * .82f), s * .13f, blue, 10, 1.7f);
        draw.AddLine(ImVec2(x + s * .28f, y + s * .67f), ImVec2(x + s * .84f, y + s * .08f), edge, 1.5f);
        draw.AddLine(ImVec2(x + s * .5f, y + s * .69f), ImVec2(x + s * .24f, y + s * .08f), edge, 1.5f);
        break;
    case 2: // copy
        draw.AddRectFilled(ImVec2(x + s * .1f, y + s * .1f), ImVec2(x + s * .68f, y + s * .73f),
                           IM_COL32(255, 255, 255, 255));
        draw.AddRect(ImVec2(x + s * .1f, y + s * .1f), ImVec2(x + s * .68f, y + s * .73f), blue);
        draw.AddRectFilled(ImVec2(x + s * .34f, y + s * .3f), ImVec2(x + s * .91f, y + s * .96f),
                           IM_COL32(255, 255, 255, 255));
        draw.AddRect(ImVec2(x + s * .34f, y + s * .3f), ImVec2(x + s * .91f, y + s * .96f), blue);
        break;
    case 3: // select
        for (int i = 0; i < 4; ++i) {
            float t = s * (.1f + i * .22f);
            draw.AddLine(ImVec2(x + t, y + s * .15f), ImVec2(x + t + s * .1f, y + s * .15f), blue);
            draw.AddLine(ImVec2(x + t, y + s * .85f), ImVec2(x + t + s * .1f, y + s * .85f), blue);
            draw.AddLine(ImVec2(x + s * .1f, y + t), ImVec2(x + s * .1f, y + t + s * .1f), blue);
            draw.AddLine(ImVec2(x + s * .9f, y + t), ImVec2(x + s * .9f, y + t + s * .1f), blue);
        }
        break;
    case 4: // crop
        draw.AddLine(ImVec2(x + s * .3f, y), ImVec2(x + s * .3f, y + s * .75f), edge, 2);
        draw.AddLine(ImVec2(x, y + s * .25f), ImVec2(x + s * .8f, y + s * .25f), edge, 2);
        draw.AddLine(ImVec2(x + s * .3f, y + s * .75f), ImVec2(x + s, y + s * .75f), edge, 2);
        draw.AddLine(ImVec2(x + s * .8f, y + s * .25f), ImVec2(x + s * .8f, y + s), edge, 2);
        break;
    case 5: // resize
        draw.AddRect(ImVec2(x + s * .12f, y + s * .15f), ImVec2(x + s * .88f, y + s * .85f), blue);
        draw.AddRectFilled(ImVec2(x + s * .12f, y + s * .5f), ImVec2(x + s * .48f, y + s * .85f),
                           IM_COL32(188, 221, 245, 255));
        draw.AddLine(ImVec2(x + s * .4f, y + s * .6f), ImVec2(x + s * .85f, y + s * .15f), blue, 2);
        break;
    case 6: // rotate
        draw.AddRectFilled(ImVec2(x + s * .2f, y + s * .35f), ImVec2(x + s * .65f, y + s * .85f),
                           IM_COL32(183, 214, 236, 255));
        draw.PathArcTo(ImVec2(x + s * .5f, y + s * .45f), s * .33f, 3.4f, 6.6f, 14);
        draw.PathStroke(blue, 0, 2);
        draw.AddTriangleFilled(ImVec2(x + s * .98f, y + s * .45f), ImVec2(x + s * .67f, y + s * .42f),
                               ImVec2(x + s * .86f, y + s * .64f), blue);
        break;
    case 7: // pencil
        draw.AddLine(ImVec2(x + s * .2f, y + s * .8f), ImVec2(x + s * .82f, y + s * .18f), gold, s * .19f);
        draw.AddTriangleFilled(ImVec2(x + s * .08f, y + s * .94f), ImVec2(x + s * .14f, y + s * .68f),
                               ImVec2(x + s * .34f, y + s * .87f), edge);
        draw.AddLine(ImVec2(x + s * .76f, y + s * .24f), ImVec2(x + s * .87f, y + s * .13f),
                     IM_COL32(218, 121, 110, 255), s * .19f);
        break;
    case 8: // fill
        draw.AddQuadFilled(ImVec2(x + s * .18f, y + s * .4f), ImVec2(x + s * .48f, y + s * .13f),
                           ImVec2(x + s * .8f, y + s * .45f), ImVec2(x + s * .5f, y + s * .75f),
                           IM_COL32(169, 196, 216, 255));
        draw.AddQuad(ImVec2(x + s * .18f, y + s * .4f), ImVec2(x + s * .48f, y + s * .13f),
                     ImVec2(x + s * .8f, y + s * .45f), ImVec2(x + s * .5f, y + s * .75f), edge);
        draw.AddTriangleFilled(ImVec2(x + s * .81f, y + s * .55f), ImVec2(x + s * .68f, y + s * .93f),
                               ImVec2(x + s * .96f, y + s * .93f), blue);
        break;
    case 9:
        draw.AddText(ImVec2(x + s * .18f, y - s * .1f), edge, "A");
        break;
    case 10:
        draw.AddQuadFilled(ImVec2(x + s * .12f, y + s * .63f), ImVec2(x + s * .53f, y + s * .16f),
                           ImVec2(x + s * .91f, y + s * .47f), ImVec2(x + s * .48f, y + s * .94f),
                           IM_COL32(233, 147, 155, 255));
        draw.AddLine(ImVec2(x + s * .13f, y + s * .65f), ImVec2(x + s * .48f, y + s * .93f), edge, 2);
        break;
    case 11:
        draw.AddLine(ImVec2(x + s * .18f, y + s * .84f), ImVec2(x + s * .76f, y + s * .24f), blue, s * .16f);
        draw.AddLine(ImVec2(x + s * .62f, y + s * .08f), ImVec2(x + s * .92f, y + s * .38f), edge, s * .2f);
        break;
    case 12:
        draw.AddCircle(ImVec2(x + s * .4f, y + s * .4f), s * .28f, blue, 20, 2);
        draw.AddLine(ImVec2(x + s * .61f, y + s * .61f), ImVec2(x + s * .91f, y + s * .91f), edge, 3);
        break;
    case 13: // Paint stroke, varnished handle, metal ferrule and bristles.
        draw.AddBezierCubic({x + s * .4f, y + s * .16f}, {x - s * .17f, y + s * .30f},
                            {x + s * .08f, y + s * .94f}, {x + s * .68f, y + s * .83f},
                            IM_COL32(232, 119, 27, 255), s * .11f);
        draw.AddQuadFilled({x + s * .73f, y + s * .02f}, {x + s * .91f, y + s * .16f},
                           {x + s * .59f, y + s * .61f}, {x + s * .39f, y + s * .46f},
                           IM_COL32(99, 65, 41, 255));
        draw.AddQuadFilled({x + s * .75f, y + s * .05f}, {x + s * .85f, y + s * .13f},
                           {x + s * .54f, y + s * .54f}, {x + s * .46f, y + s * .47f},
                           IM_COL32(213, 158, 93, 255));
        draw.AddQuadFilled({x + s * .46f, y + s * .39f}, {x + s * .68f, y + s * .55f},
                           {x + s * .57f, y + s * .71f}, {x + s * .33f, y + s * .54f},
                           IM_COL32(117, 147, 169, 255));
        draw.AddLine({x + s * .45f, y + s * .44f}, {x + s * .61f, y + s * .56f}, IM_COL32(235, 247, 255, 255),
                     s * .07f);
        draw.AddQuadFilled({x + s * .32f, y + s * .53f}, {x + s * .58f, y + s * .71f},
                           {x + s * .39f, y + s * .96f}, {x + s * .13f, y + s * .79f},
                           IM_COL32(141, 92, 51, 255));
        for (int line = 0; line < 4; ++line) {
            float shift = line * s * .052f;
            draw.AddLine({x + s * .30f + shift, y + s * .57f + shift * .6f},
                         {x + s * .18f + shift, y + s * .78f + shift * .6f}, IM_COL32(220, 173, 103, 255), 1);
        }
        break;
    case 14:
        draw.AddRectFilled(ImVec2(x + s * .08f, y + s * .05f), ImVec2(x + s * .92f, y + s * .95f),
                           IM_COL32(113, 132, 191, 255));
        draw.AddRectFilled(ImVec2(x + s * .25f, y + s * .06f), ImVec2(x + s * .71f, y + s * .4f),
                           IM_COL32(225, 231, 237, 255));
        draw.AddRectFilled(ImVec2(x + s * .24f, y + s * .58f), ImVec2(x + s * .77f, y + s * .93f),
                           IM_COL32(250, 250, 252, 255));
        break;
    case 15:
    case 16: {
        float direction = icon == 15 ? 1.0f : -1.0f;
        draw.PathArcTo(ImVec2(x + s * .5f, y + s * .52f), s * .32f, -2.6f, 1.0f, 16);
        draw.PathStroke(blue, 0, 2);
        float tip = x + (icon == 15 ? s * .08f : s * .92f);
        draw.AddTriangleFilled(ImVec2(tip, y + s * .16f), ImVec2(tip + direction * s * .38f, y + s * .1f),
                               ImVec2(tip + direction * s * .08f, y + s * .43f), blue);
        break;
    }
    case 17:
        draw.AddRectFilled(ImVec2(x + s * .2f, y + s * .1f), ImVec2(x + s * .8f, y + s * .94f),
                           IM_COL32(255, 255, 255, 255));
        draw.AddRect(ImVec2(x + s * .2f, y + s * .1f), ImVec2(x + s * .8f, y + s * .94f), blue);
        break;
    case 18:
        draw.AddRectFilled(ImVec2(x + s * .1f, y + s * .3f), ImVec2(x + s * .94f, y + s * .85f), gold, 2);
        draw.AddRectFilled(ImVec2(x + s * .15f, y + s * .15f), ImVec2(x + s * .5f, y + s * .4f), gold, 2);
        break;
    case 19:
        draw.AddRectFilled(ImVec2(x + s * .18f, y + s * .36f), ImVec2(x + s * .85f, y + s * .7f),
                           IM_COL32(176, 187, 197, 255), 2);
        draw.AddRectFilled(ImVec2(x + s * .28f, y + s * .65f), ImVec2(x + s * .73f, y + s * .97f),
                           IM_COL32(255, 255, 255, 255));
        draw.AddRect(ImVec2(x + s * .28f, y + s * .02f), ImVec2(x + s * .73f, y + s * .38f), blue);
        break;
    case 20:
        draw.AddCircleFilled(ImVec2(x + s * .5f, y + s * .3f), s * .2f, IM_COL32(157, 96, 58, 255));
        draw.AddRectFilled(ImVec2(x + s * .38f, y + s * .3f), ImVec2(x + s * .63f, y + s * .65f),
                           IM_COL32(157, 96, 58, 255));
        draw.AddRectFilled(ImVec2(x + s * .12f, y + s * .65f), ImVec2(x + s * .88f, y + s * .9f), edge);
        break;
    case 21:
        draw.AddLine(ImVec2(x + s * .1f, y + s * .8f), ImVec2(x + s * .45f, y + s * .12f), blue, 1.5f);
        draw.AddLine(ImVec2(x + s * .45f, y + s * .12f), ImVec2(x + s * .9f, y + s * .7f), blue, 1.5f);
        draw.AddLine(ImVec2(x + s * .9f, y + s * .7f), ImVec2(x + s * .1f, y + s * .8f), blue, 1.5f);
        draw.AddCircleFilled(ImVec2(x + s * .45f, y + s * .12f), 3, blue);
        draw.AddCircleFilled(ImVec2(x + s * .9f, y + s * .7f), 3, blue);
        break;
    case 22:
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                ImVec2 p(x + s * (.12f + .36f * col), y + s * (.12f + .36f * row));
                if (col < 2) {
                    draw.AddLine(p, ImVec2(p.x + s * .36f, p.y), blue);
                }
                if (row < 2) {
                    draw.AddLine(p, ImVec2(p.x, p.y + s * .36f), blue);
                }
                draw.AddCircleFilled(p, 2, blue);
            }
        }
        break;
    default:
        draw.AddText(ImVec2(x + 3, y + 1), color, "?");
        break;
    }
}
bool ribbon_button(const char* id, const char* label, int icon, ImVec2 position, ImVec2 size, bool selected,
                   const char* tooltip) {
    ribbon_cursor(position);
    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##button", size);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    if (hovered || selected) {
        ImU32 top = selected ? IM_COL32(255, 237, 186, 255) : IM_COL32(255, 249, 228, 255);
        ImU32 bottom = selected ? IM_COL32(255, 207, 102, 255) : IM_COL32(255, 228, 157, 255);
        draw.AddRectFilledMultiColor(a, b, top, top, bottom, bottom);
        draw.AddRect(a, b, selected ? IM_COL32(196, 147, 50, 255) : IM_COL32(219, 182, 109, 255), 2);
        draw.AddRect({a.x + 1, a.y + 1}, {b.x - 1, b.y - 1}, IM_COL32(255, 255, 238, 215), 1);
    }
    bool tall = size.y > 40;
    float icon_size = tall ? 32.0f : 16.0f;
    if (icon >= 0) {
        classic_icon(
            draw, icon,
            ImVec2(a.x + (tall ? (size.x - icon_size) / 2 : 4), a.y + (tall ? 5 : (size.y - icon_size) / 2)),
            icon_size);
    }
    if (label && label[0]) {
        const char* line = label;
        float text_y = a.y + (tall ? 38.0f : (size.y - ImGui::GetFontSize()) / 2);
        while (*line) {
            const char* end = line;
            while (*end && *end != '\n') {
                ++end;
            }
            ImVec2 text = ImGui::CalcTextSize(line, end);
            float text_x = a.x + ((tall || icon < 0) ? (size.x - text.x) / 2 : 25);
            draw.AddText({std::floor(text_x), std::floor(text_y)}, IM_COL32(35, 49, 64, 255), line, end);
            text_y += 15.0f;
            line = *end ? end + 1 : end;
        }
    }
    if (hovered && tooltip) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::PopID();
    return clicked;
}
static bool ribbon_tab(const char* id, const char* label, float left, float width, bool active) {
    ribbon_cursor({left, 27});
    bool clicked = ImGui::InvisibleButton(id, {width, 26});
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    if (active) {
        draw.AddRectFilled({a.x + 2, a.y - 1}, {b.x + 2, b.y}, IM_COL32(129, 151, 175, 95), 3);
        draw.AddRectFilledMultiColor({a.x, a.y - 1}, {b.x, b.y + 1}, IM_COL32(255, 255, 255, 255),
                                     IM_COL32(255, 255, 255, 255), IM_COL32(247, 251, 255, 255),
                                     IM_COL32(247, 251, 255, 255));
        draw.AddLine({a.x, a.y + 2}, {a.x, b.y}, IM_COL32(125, 157, 192, 255));
        draw.AddLine({b.x, a.y + 2}, {b.x, b.y}, IM_COL32(125, 157, 192, 255));
        draw.AddLine({a.x + 1, a.y}, {b.x - 1, a.y}, IM_COL32(131, 161, 193, 255));
        draw.AddLine({a.x + 1, b.y}, {b.x - 1, b.y}, IM_COL32(247, 251, 255, 255), 2);
    } else if (ImGui::IsItemHovered()) {
        draw.AddRectFilled({a.x, a.y + 1}, b, IM_COL32(226, 239, 252, 255), 2);
        draw.AddRect({a.x, a.y + 1}, b, IM_COL32(158, 184, 211, 255), 2);
    }
    ImVec2 text = ImGui::CalcTextSize(label);
    draw.AddText({std::floor(a.x + (width - text.x) / 2), a.y + 4}, IM_COL32(29, 51, 77, 255), label);
    return clicked;
}
static void group_label(float left, float right, const char* label) {
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    ImVec2 base = ImGui::GetWindowPos();
    base.y -= 27;
    draw.AddLine(ImVec2(base.x + right, base.y + 57), ImVec2(base.x + right, base.y + 148),
                 IM_COL32(170, 191, 213, 255));
    draw.AddLine(ImVec2(base.x + right + 1, base.y + 57), ImVec2(base.x + right + 1, base.y + 148),
                 IM_COL32(255, 255, 255, 235));
    ImVec2 size = ImGui::CalcTextSize(label);
    draw.AddText(ImVec2(base.x + (left + right - size.x) / 2, base.y + 137), IM_COL32(88, 103, 121, 255),
                 label);
}
static bool command_menu(const char* name, const char* shortcut, Application& app, Command command,
                         bool enabled = true) {
    bool chosen = ImGui::MenuItem(name, shortcut, false, enabled);
    if (chosen) {
        app.command(command);
    }
    return chosen;
}
void Application::ribbon(float width) {
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    ImVec2 base = ImGui::GetWindowPos();
    base.y -= 27;
    draw.AddRectFilledMultiColor(ImVec2(base.x, base.y + 27), ImVec2(base.x + width, base.y + 53),
                                 IM_COL32(191, 211, 234, 255), IM_COL32(208, 224, 242, 255),
                                 IM_COL32(181, 203, 227, 255), IM_COL32(178, 202, 228, 255));
    draw.AddLine({base.x, base.y + 52}, {base.x + width, base.y + 52}, IM_COL32(132, 158, 188, 255));
    draw.AddRectFilledMultiColor(ImVec2(base.x, base.y + 52), ImVec2(base.x + width, base.y + 157),
                                 IM_COL32(247, 251, 255, 255), IM_COL32(247, 251, 255, 255),
                                 IM_COL32(216, 228, 241, 255), IM_COL32(216, 228, 241, 255));
    draw.AddLine(ImVec2(base.x, base.y + 157), ImVec2(base.x + width, base.y + 157),
                 IM_COL32(168, 184, 204, 255));
    draw.AddRectFilled(ImVec2(base.x, base.y + 27), ImVec2(base.x + 56, base.y + 51),
                       IM_COL32(40, 116, 183, 255));
    ribbon_cursor({0, 27});
    if (ImGui::InvisibleButton("File tab", {56, 24})) {
        ImGui::OpenPopup("File");
    }
    draw.AddText(ImVec2(base.x + 16, base.y + 32), IM_COL32(255, 255, 255, 255), "File");
    if (ribbon_tab("Home tab", "Home", 59, 58, !view_tab && !text_tab && !patterns_tab && !atlas_tab)) {
        atlas_tab = false;
        view_tab = false;
        text_tab = false;
        patterns_tab = false;
    }
    if (ribbon_tab("View tab", "View", 118, 56, view_tab)) {
        atlas_tab = false;
        view_tab = true;
        text_tab = false;
        patterns_tab = false;
    }
    if (text_active && ribbon_tab("Text tab", "Text", 175, 56, text_tab)) {
        text_tab = true;
        atlas_tab = false;
        view_tab = false;
        patterns_tab = false;
    }
    if (ribbon_tab("Patterns and tools tab", "Patterns & tools", text_active ? 235.0f : 175.0f, 162,
                   patterns_tab)) {
        patterns_tab = true;
        atlas_tab = false;
        view_tab = false;
        text_tab = false;
    }
    if (ribbon_tab("Atlas tab", "Atlas", text_active ? 399.0f : 339.0f, 64, atlas_tab)) {
        atlas_tab = true;
        view_tab = false;
        text_tab = false;
        patterns_tab = false;
    }
    if (ribbon_button("Help", "?", -1, {width - 29, 28}, {25, 23}, show_help, "Help (F1)")) {
        show_help = !show_help;
    }
    if (ImGui::BeginPopup("File")) {
        GuiScope popup_scope(GuiEnd::Popup);
        command_menu("New", "Ctrl+N", *this, Command::New);
        command_menu("Open...", "Ctrl+O", *this, Command::Open);
        if (ImGui::BeginMenu("Recent pictures", !recent_files.paths.empty())) {
            GuiScope menu_scope(GuiEnd::Menu);
            for (const std::string& path : recent_files.paths) {
                if (ImGui::MenuItem(path.c_str())) {
                    recent_to_open = path;
                    command(Command::OpenRecent);
                    break;
                }
            }
        }
        command_menu("Save", "Ctrl+S", *this, Command::Save);
        command_menu("Save as...", "F12", *this, Command::SaveAs);
        ImGui::Separator();
        command_menu("Print...", "Ctrl+P", *this, Command::Print);
        command_menu("Page setup...", nullptr, *this, Command::PageSetup);
        command_menu("Print preview", nullptr, *this, Command::PrintPreview);
        command_menu("From scanner or camera...", nullptr, *this, Command::Acquire);
        if (ImGui::BeginMenu("Set as desktop background")) {
            GuiScope menu_scope(GuiEnd::Menu);
            command_menu("Fill", nullptr, *this, Command::WallpaperFill);
            command_menu("Tile", nullptr, *this, Command::WallpaperTile);
            command_menu("Center", nullptr, *this, Command::WallpaperCenter);
        }
        command_menu("Properties", "Ctrl+E", *this, Command::Properties);
        ImGui::Separator();
        command_menu("About Plan Paint", nullptr, *this, Command::About);
        command_menu("Exit", nullptr, *this, Command::Quit);
    }
    if (atlas_tab) {
        atlas_ribbon(width);
        return;
    }
    if (patterns_tab) {
        if (ribbon_button("Ribbon stamp", "Stamp", 20, {8, 58}, {66, 65}, document.tool == Tool::Stamp)) {
            choose_tool(Tool::Stamp);
        }
        if (ribbon_button("Ribbon stamp options", "▼", -1, {8, 124}, {66, 14}, false, "Stamp options")) {
            ImGui::OpenPopup("Ribbon stamp options");
        }
        if (ImGui::BeginPopup("Ribbon stamp options")) {
            GuiScope popup_scope(GuiEnd::Popup);
            stamp_controls();
        }
        ImGui::BeginDisabled(!document.selection.active && !reshape_active);
        if (ribbon_button("Ribbon mesh", "Mesh", 21, {82, 58}, {66, 65}, document.tool == Tool::Reshape,
                          "Reshape the selected object")) {
            choose_tool(Tool::Reshape);
        }
        ImGui::EndDisabled();
        if (ribbon_button("Mesh options", "▼", -1, {82, 124}, {66, 14}, false, "Mesh spacing")) {
            ImGui::OpenPopup("Mesh options");
        }
        if (ImGui::BeginPopup("Mesh options")) {
            GuiScope popup_scope(GuiEnd::Popup);
            const double minimum = 20, maximum = 140;
            ImGui::SliderScalar("Mesh spacing", ImGuiDataType_Double, &mesh_spacing, &minimum, &maximum,
                                "%.0f px");
            ImGui::TextDisabled("Spacing applies to the next mesh.");
        }
        if (ribbon_button("Ribbon path", "Path", 21, {156, 58}, {66, 80},
                          document.tool == Tool::Path && document.continuous_path)) {
            if (!document.continuous_path) {
                document.commit_path();
            }
            choose_tool(Tool::Path);
            document.continuous_path = true;
        }
        group_label(0, 232, "Tools");
        for (int i = 0; i < 18; ++i) {
            ImGui::PushID(i);
            ribbon_cursor({242.0f + (i % 9) * 33, 59.0f + (i / 9) * 32});
            if (ImGui::InvisibleButton("Pattern swatch", {29, 28})) {
                document.ink.pattern = static_cast<Pattern>(i);
            }
            ImVec2 a = ImGui::GetItemRectMin();
            Ink swatch = document.ink;
            swatch.pattern = static_cast<Pattern>(i);
            swatch.primary = {40, 80, 120, 255};
            swatch.secondary = {245, 248, 252, 255};
            swatch.transparent_pattern = false;
            for (int y = 0; y < 24; ++y) {
                for (int x = 0; x < 25; ++x) {
                    draw.AddRectFilled({a.x + 2 + x, a.y + 2 + y}, {a.x + 3 + x, a.y + 3 + y},
                                       packed(patterned(swatch, x, y)));
                }
            }
            draw.AddRect(a, {a.x + 29, a.y + 28},
                         document.ink.pattern == swatch.pattern ? IM_COL32(225, 155, 30, 255)
                                                                : IM_COL32(155, 178, 201, 255),
                         0, 0, document.ink.pattern == swatch.pattern ? 2 : 1);
            ImGui::SetItemTooltip("%s", pattern_names[i]);
            ImGui::PopID();
        }
        ribbon_cursor({242, 123});
        ImGui::Checkbox("Transparent second color", &document.ink.transparent_pattern);
        group_label(232, 548, "Patterns");
        ribbon_cursor({560, 59});
        ImGui::SetNextItemWidth(156);
        const double small = 0.3, large = 4.0, zero = 0.0, one = 1.0, minus = -180, plus = 180;
        ImGui::SliderScalar("Grain scale", ImGuiDataType_Double, &document.ink.grain_scale, &small, &large,
                            "%.2fx");
        ribbon_cursor({560, 88});
        ImGui::SetNextItemWidth(156);
        ImGui::SliderScalar("Paper tooth", ImGuiDataType_Double, &document.ink.paper_roughness, &zero, &one,
                            "%.2f");
        ribbon_cursor({560, 117});
        ImGui::SetNextItemWidth(156);
        ImGui::SliderScalar("Paint load", ImGuiDataType_Double, &document.ink.pigment_load, &zero, &one,
                            "%.2f");
        group_label(548, 846, "Material");
        ribbon_cursor({858, 60});
        ImGui::TextUnformatted("Outline");
        ribbon_cursor({927, 57});
        ImGui::SetNextItemWidth(159);
        int outline_medium = static_cast<int>(document.ink.brush);
        if (ImGui::Combo("##Outline medium", &outline_medium, brush_names, brush_count)) {
            document.ink.brush = static_cast<Brush>(outline_medium);
            document.shape_outline = true;
        }
        ImGui::SetItemTooltip("Brush and outline material");
        ribbon_cursor({858, 90});
        ImGui::TextUnformatted("Fill");
        ribbon_cursor({927, 87});
        ImGui::SetNextItemWidth(159);
        int fill_medium = static_cast<int>(document.shape_fill_brush);
        if (ImGui::Combo("##Fill medium", &fill_medium, brush_names, brush_count)) {
            document.shape_fill_brush = static_cast<Brush>(fill_medium);
            document.shape_fill = true;
        }
        ribbon_cursor({858, 119});
        ImGui::TextUnformatted("Angle");
        ribbon_cursor({917, 117});
        ImGui::SetNextItemWidth(94);
        ImGui::SliderScalar("##Grain angle", ImGuiDataType_Double, &document.ink.material_angle, &minus,
                            &plus, "%.0f deg");
        ribbon_cursor({1017, 117});
        if (ImGui::Button("New grain", {93, 23})) {
            ++document.ink.noise;
        }
        if (width >= 1230) {
            material_preview({base.x + 1122, base.y + 59});
        }
        group_label(846, width - 3, "Media");
        return;
    }
    if (view_tab) {
        if (ribbon_button("Zoom in", "Zoom in", 12, {8, 57}, {65, 70})) {
            zoom = std::min(16.0f, zoom * 2);
        }
        if (ribbon_button("Zoom out", "Zoom out", 12, {76, 57}, {65, 70})) {
            zoom = std::max(0.125f, zoom / 2);
        }
        if (ribbon_button("100 percent", "100%", 5, {144, 57}, {65, 70})) {
            zoom = 1;
        }
        group_label(0, 220, "Zoom");
        ribbon_cursor({232, 61});
        ImGui::Checkbox("Rulers", &show_rulers);
        ribbon_cursor({232, 86});
        ImGui::Checkbox("Gridlines", &show_grid);
        ribbon_cursor({232, 111});
        ImGui::Checkbox("Status bar", &show_status);
        group_label(220, 366, "Show or hide");
        if (ribbon_button("Full screen", "Full screen", 5, {376, 57}, {82, 70})) {
            full_screen = !full_screen;
            SDL_SetWindowFullscreen(window, full_screen);
        }
        if (ribbon_button("Fit view", "Fit window", 5, {461, 57}, {85, 70})) {
            ImVec2 size = ImGui::GetIO().DisplaySize;
            zoom = std::min((size.x - 30) / document.image.width, (size.y - 178) / document.image.height);
        }
        group_label(366, 557, "Display");
        return;
    }
    if (text_tab && text_active) {
        ribbon_cursor({12, 63});
        if (font_paths.empty()) {
            font_paths = installed_fonts();
        }
        std::string font_name = text_style.face_path.empty()
                                    ? (text_style.mono ? "Portsmouth Mono" : "Portsmouth")
                                    : std::filesystem::path(text_style.face_path).stem().string();
        ImGui::SetNextItemWidth(185);
        if (ImGui::BeginCombo("##Font", font_name.c_str())) {
            if (ImGui::Selectable("Portsmouth")) {
                text_style.face_path.clear();
                text_style.mono = false;
            }
            if (ImGui::Selectable("Portsmouth Mono")) {
                text_style.face_path.clear();
                text_style.mono = true;
            }
            for (const std::string& path : font_paths) {
                std::string name = std::filesystem::path(path).stem().string();
                ImGui::PushID(path.c_str());
                if (ImGui::Selectable(name.c_str(), text_style.face_path == path)) {
                    text_style.face_path = path;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ribbon_cursor({12, 96});
        ImGui::SetNextItemWidth(90);
        ImGui::InputInt("Size", &text_style.size);
        text_style.size = std::clamp(text_style.size, 6, 300);
        ribbon_cursor({215, 64});
        ImGui::Checkbox("Bold", &text_style.bold);
        ImGui::SameLine();
        ImGui::Checkbox("Italic", &text_style.italic);
        ribbon_cursor({215, 94});
        ImGui::Checkbox("Underline", &text_style.underline);
        ImGui::SameLine();
        ImGui::Checkbox("Strikeout", &text_style.strikeout);
        group_label(0, 427, "Font");
        ribbon_cursor({438, 64});
        ImGui::Checkbox("Opaque background", &text_style.opaque);
        ribbon_cursor({438, 95});
        ImGui::Checkbox("Word wrap", &text_style.word_wrap);
        group_label(427, 655, "Text box");
        if (ribbon_button("Place text ribbon", "Place text", -1, {666, 61}, {104, 30})) {
            finish_text();
        }
        if (ribbon_button("Cancel text ribbon", "Cancel", -1, {666, 98}, {104, 30})) {
            cancel_text();
        }
        if (ribbon_button("Text color", "Edit colors", 8, {783, 57}, {83, 72})) {
            begin_color();
        }
        if (ribbon_button("Fit text height", "Fit height", -1, {880, 61}, {106, 30})) {
            refresh_text_preview();
            text_height = std::clamp(text_layout.height + 4, 24, std::min(8192, 16000000 / text_width));
        }
        group_label(655, 995, "Finish text");
        return;
    }
    if (ribbon_button("Save above Paste", "Save", 14, {1, 57}, {55, 22}, false,
                      "Save (Ctrl+S / Command+S)")) {
        command(Command::Save);
    }
    if (ribbon_button("Paste", "Paste", 0, {1, 80}, {52, 57}, false, "Paste an image from the clipboard")) {
        command(Command::Paste);
    }
    if (ribbon_button("Paste dropdown", "▼", -1, {48, 120}, {12, 18})) {
        ImGui::OpenPopup("Paste choices");
    }
    if (ImGui::BeginPopup("Paste choices")) {
        GuiScope popup_scope(GuiEnd::Popup);
        command_menu("Paste", "Ctrl+V", *this, Command::Paste);
        command_menu("Paste from...", nullptr, *this, Command::PasteFrom);
    }
    if (ribbon_button("Cut", "Cut", 1, {60, 57}, {63, 18})) {
        command(Command::Cut);
    }
    if (ribbon_button("Copy", "Copy", 2, {60, 77}, {63, 18})) {
        command(Command::Copy);
    }
    if (ribbon_button("Undo below Copy", "Undo", 15, {60, 97}, {63, 18}, false,
                      "Undo (Ctrl+Z / Command+Z)")) {
        command(Command::Undo);
    }
    if (ribbon_button("Redo below Undo", "Redo", 16, {60, 117}, {63, 18}, false,
                      "Redo (Ctrl+Y / Command+Shift+Z)")) {
        command(Command::Redo);
    }
    group_label(0, 123, "Clipboard");
    if (ribbon_button("Select", "Select", 3, {128, 57}, {48, 67},
                      document.tool == Tool::Select || document.tool == Tool::Lasso)) {
        choose_tool(Tool::Select);
    }
    if (ribbon_button("Selection menu", "▼", -1, {130, 126}, {44, 13})) {
        ImGui::OpenPopup("Selection");
    }
    if (ImGui::BeginPopup("Selection")) {
        GuiScope popup_scope(GuiEnd::Popup);
        if (ImGui::MenuItem("Rectangular selection", nullptr, document.tool == Tool::Select)) {
            choose_tool(Tool::Select);
        }
        if (ImGui::MenuItem("Free-form selection", nullptr, document.tool == Tool::Lasso)) {
            choose_tool(Tool::Lasso);
        }
        ImGui::Separator();
        command_menu("Select all", "Ctrl+A", *this, Command::SelectAll);
        command_menu("Invert selection", nullptr, *this, Command::InvertSelection);
        command_menu("Delete", "Del", *this, Command::Delete);
        ImGui::MenuItem("Transparent selection", nullptr, &document.transparent_selection);
    }
    if (ribbon_button("Crop", "Crop", 4, {183, 58}, {72, 23})) {
        command(Command::Crop);
    }
    if (ribbon_button("Resize", "Resize", 5, {183, 84}, {72, 23})) {
        command(Command::Resize);
    }
    if (ribbon_button("Rotate", "Rotate", 6, {183, 110}, {80, 23})) {
        ImGui::OpenPopup("Rotate");
    }
    draw.AddTriangleFilled({base.x + 257, base.y + 119}, {base.x + 263, base.y + 119},
                           {base.x + 260, base.y + 122}, IM_COL32(40, 60, 85, 255));
    if (ImGui::BeginPopup("Rotate")) {
        GuiScope popup_scope(GuiEnd::Popup);
        ImGui::SetNextItemWidth(150);
        ImGui::InputDouble("Angle (degrees)", &requested_rotation, 1, 15, "%.2f");
        ImGui::BeginDisabled(rotation_active || warp_worker.busy());
        bool rotate = ImGui::Button("Rotate with CONV");
        ImGui::EndDisabled();
        if (rotate) {
            request_rotation(requested_rotation);
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextDisabled("Positive turns clockwise. Drag a selection's round handle for free rotation.");
        ImGui::Separator();
        command_menu("Rotate right 90 degrees", nullptr, *this, Command::RotateRight);
        command_menu("Rotate left 90 degrees", nullptr, *this, Command::RotateLeft);
        command_menu("Rotate 180 degrees", nullptr, *this, Command::Rotate180);
        ImGui::Separator();
        command_menu("Flip vertical", nullptr, *this, Command::FlipVertical);
        command_menu("Flip horizontal", nullptr, *this, Command::FlipHorizontal);
    }
    group_label(123, 265, "Image");
    const Tool tools[6] = {Tool::Pencil, Tool::Fill, Tool::Text, Tool::Eraser, Tool::Picker, Tool::Magnifier};
    const char* names[6] = {"Pencil", "Fill with color", "Text", "Eraser", "Color picker", "Magnifier"};
    for (int i = 0; i < 6; ++i) {
        ImVec2 tile(base.x + 269.0f + (i % 3) * 22, base.y + 63.0f + (i / 3) * 28);
        draw.AddRectFilledMultiColor(tile, {tile.x + 21, tile.y + 25}, IM_COL32(253, 255, 255, 255),
                                     IM_COL32(253, 255, 255, 255), IM_COL32(225, 234, 243, 255),
                                     IM_COL32(225, 234, 243, 255));
        draw.AddRect(tile, {tile.x + 21, tile.y + 25}, IM_COL32(173, 194, 215, 255));
        if (ribbon_button(names[i], nullptr, 7 + i, {269.0f + (i % 3) * 22.0f, 63.0f + (i / 3) * 28.0f},
                          {21, 25}, document.tool == tools[i], names[i])) {
            choose_tool(tools[i]);
            if (tools[i] == Tool::Eraser) {
                ImGui::OpenPopup("Eraser settings");
            }
        }
    }
    if (ImGui::BeginPopup("Eraser settings")) {
        GuiScope popup_scope(GuiEnd::Popup);
        ImGui::TextUnformatted("Round eraser");
        if (ImGui::RadioButton("Hard — erase every touched pixel", !eraser_soft)) {
            eraser_soft = false;
        }
        if (ImGui::RadioButton("Soft — strongest at the center", eraser_soft)) {
            eraser_soft = true;
        }
        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Diameter", &document.ink.size, 1, 100);
    }
    group_label(265, 341, "Tools");
    if (ribbon_button("Brushes", "Brushes\n▼", 13, {346, 57}, {58, 68}, document.tool == Tool::Brush)) {
        choose_tool(Tool::Brush);
        ImGui::OpenPopup("Brushes");
    }
    if (ImGui::BeginPopup("Brushes")) {
        GuiScope popup_scope(GuiEnd::Popup);
        for (int i = 0; i < brush_count; ++i) {
            if (ImGui::MenuItem(brush_names[i], nullptr, static_cast<int>(document.ink.brush) == i)) {
                document.ink.brush = static_cast<Brush>(i);
                choose_tool(Tool::Brush);
            }
        }
    }
    group_label(341, 410, "");
    draw.AddRectFilled(ImVec2(base.x + 418, base.y + 60), ImVec2(base.x + 597, base.y + 132),
                       IM_COL32(255, 255, 255, 255));
    draw.AddRect(ImVec2(base.x + 418, base.y + 60), ImVec2(base.x + 597, base.y + 132),
                 IM_COL32(177, 186, 198, 255));
    const Shape home_shapes[21] = {Shape::Line,       Shape::Bezier,         Shape::Arc,
                                   Shape::Oval,       Shape::Rectangle,      Shape::RoundedRectangle,
                                   Shape::Polygon,    Shape::Triangle,       Shape::RightTriangle,
                                   Shape::Diamond,    Shape::Pentagon,       Shape::Hexagon,
                                   Shape::RightArrow, Shape::LeftArrow,      Shape::UpArrow,
                                   Shape::DownArrow,  Shape::Star4,          Shape::Star5,
                                   Shape::Star6,      Shape::RoundedCallout, Shape::Circle};
    for (int i = 0; i < 21; ++i) {
        int shape_index = static_cast<int>(home_shapes[i]);
        if (ribbon_button(shape_names[shape_index], nullptr, 100 + shape_index,
                          {419.0f + (i % 7) * 25.0f, 61.0f + (i / 7) * 23.0f}, {25, 23},
                          document.tool == Tool::Shape && static_cast<int>(document.shape) == shape_index,
                          shape_names[shape_index])) {
            choose_shape(home_shapes[i]);
        }
    }
    if (ribbon_button("More shapes", "▼", -1, {596, 60}, {15, 72})) {
        ImGui::OpenPopup("Shape gallery");
    }
    if (ImGui::BeginPopup("Shape gallery")) {
        GuiScope popup_scope(GuiEnd::Popup);
        for (int i = 0; i < shape_count; ++i) {
            if (i % 6) {
                ImGui::SameLine();
            }
            ImGui::PushID(i);
            if (ImGui::Selectable("##Shape",
                                  document.tool == Tool::Shape && static_cast<int>(document.shape) == i,
                                  ImGuiSelectableFlags_None, {104, 70})) {
                choose_shape(static_cast<Shape>(i));
            }
            ImVec2 a = ImGui::GetItemRectMin();
            ImDrawList& gallery = *ImGui::GetWindowDrawList();
            classic_icon(gallery, 100 + i, {a.x + 38, a.y + 3}, 28);
            gallery.AddText(ImGui::GetFont(), 14, {a.x + 4, a.y + 36}, IM_COL32(35, 49, 64, 255),
                            shape_names[i], nullptr, 96);
            ImGui::SetItemTooltip("%s", shape_names[i]);
            ImGui::PopID();
        }
    }
    if (ribbon_button("Outline", "Outline ▼", -1, {614, 61}, {78, 25})) {
        ImGui::OpenPopup("Outline style");
    }
    if (ImGui::BeginPopup("Outline style")) {
        GuiScope popup_scope(GuiEnd::Popup);
        if (ImGui::MenuItem("No outline", nullptr, !document.shape_outline)) {
            document.shape_outline = false;
        }
        if (ImGui::MenuItem("Solid color", nullptr, document.shape_outline)) {
            document.shape_outline = true;
            document.ink.brush = Brush::Round;
        }
        for (int i = 4; i < brush_count; ++i) {
            if (ImGui::MenuItem(brush_names[i])) {
                document.shape_outline = true;
                document.ink.brush = static_cast<Brush>(i);
            }
        }
    }
    if (ribbon_button("Fill", "Fill ▼", -1, {614, 88}, {78, 25})) {
        ImGui::OpenPopup("Fill style");
    }
    if (ImGui::BeginPopup("Fill style")) {
        GuiScope popup_scope(GuiEnd::Popup);
        if (ImGui::MenuItem("No fill", nullptr, !document.shape_fill)) {
            document.shape_fill = false;
        }
        if (ImGui::MenuItem("Solid color", nullptr,
                            document.shape_fill && document.ink.pattern == Pattern::Solid &&
                                document.shape_fill_brush == Brush::Round)) {
            document.shape_fill = true;
            document.ink.pattern = Pattern::Solid;
            document.shape_fill_brush = Brush::Round;
        }
        for (int i = 4; i < brush_count; ++i) {
            if (ImGui::MenuItem(brush_names[i], nullptr,
                                document.shape_fill && static_cast<int>(document.shape_fill_brush) == i)) {
                document.shape_fill = true;
                document.shape_fill_brush = static_cast<Brush>(i);
                document.ink.pattern = Pattern::Solid;
            }
        }
        ImGui::Separator();
        for (int i = 1; i < 18; ++i) {
            if (ImGui::MenuItem(pattern_names[i], nullptr,
                                document.shape_fill && static_cast<int>(document.ink.pattern) == i)) {
                document.shape_fill = true;
                document.ink.pattern = static_cast<Pattern>(i);
                document.shape_fill_brush = Brush::Round;
            }
        }
    }
    group_label(410, 701, "Shapes");
    if (ribbon_button("Size", "Size\n▼", -1, {708, 57}, {49, 76})) {
        ImGui::OpenPopup("Stroke size");
    }
    for (int i = 0; i < 4; ++i) {
        draw.AddLine(ImVec2(base.x + 717, base.y + 65 + i * 8), ImVec2(base.x + 748, base.y + 65 + i * 8),
                     IM_COL32(72, 99, 130, 255), static_cast<float>(i + 1));
    }
    if (ImGui::BeginPopup("Stroke size")) {
        GuiScope popup_scope(GuiEnd::Popup);
        const int sizes[] = {1, 3, 5, 8, 12, 20, 32, 48};
        for (int size : sizes) {
            char label[32] = {};
            std::snprintf(label, sizeof(label), "%d px", size);
            if (ImGui::MenuItem(label, nullptr, document.ink.size == size)) {
                document.ink.size = size;
            }
        }
        ImGui::SliderInt("Custom", &document.ink.size, 1, 100);
    }
    group_label(701, 765, "");
    for (int slot = 0; slot < 2; ++slot) {
        float x = 771.0f + slot * 47;
        if (ribbon_button(slot == 0 ? "Color1" : "Color2", slot == 0 ? "Color\n1" : "Color\n2", -1, {x, 57},
                          {44, 75}, primary_slot == (slot == 0))) {
            primary_slot = slot == 0;
        }
        Color color = slot == 0 ? document.ink.primary : document.ink.secondary;
        draw.AddRectFilled(ImVec2(base.x + x + 8, base.y + 64), ImVec2(base.x + x + 36, base.y + 93),
                           packed(color));
        draw.AddRect(ImVec2(base.x + x + 7, base.y + 63), ImVec2(base.x + x + 37, base.y + 94),
                     IM_COL32(128, 143, 160, 255));
    }
    // Office 2007/2010 theme accents and their light tints, in the classic Paint grid.
    for (int i = 0; i < 30; ++i) {
        Color color = office_color(i);
        ImVec2 pos(871.0f + (i % 10) * 21.0f, 60.0f + (i / 10) * 23.0f);
        ribbon_cursor(pos);
        ImGui::PushID(i);
        ImGui::InvisibleButton("Office color", {19, 21},
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemClicked(0)) {
            if (primary_slot) {
                document.ink.primary = color;
            } else {
                document.ink.secondary = color;
            }
        }
        if (ImGui::IsItemClicked(1)) {
            document.ink.secondary = color;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\nLeft: selected color. Right: Color 2.", to_hex(color).c_str());
        }
        draw.AddRectFilled(ImVec2(base.x + pos.x + 2, base.y + pos.y + 2),
                           ImVec2(base.x + pos.x + 17, base.y + pos.y + 19), packed(color));
        draw.AddRect(ImVec2(base.x + pos.x + 1, base.y + pos.y + 1),
                     ImVec2(base.x + pos.x + 18, base.y + pos.y + 20), IM_COL32(160, 170, 181, 255));
        ImGui::PopID();
    }
    if (ribbon_button("Edit colors", "Edit\ncolors", -1, {1085, 57}, {48, 76}, false,
                      "Edit RGB, OKLab and hexadecimal colors")) {
        begin_color();
    }
    for (int x = 0; x < 20; ++x) {
        float hue = x / 20.0f;
        float r = 0, g = 0, b = 0;
        ImGui::ColorConvertHSVtoRGB(hue, 0.85f, 0.95f, r, g, b);
        ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1));
        draw.AddRectFilled(ImVec2(base.x + 1098 + x, base.y + 63), ImVec2(base.x + 1099 + x, base.y + 89),
                           color);
    }
    group_label(765, 1138, "Colors");
    if (width > 1210) {
        if (ribbon_button("Stamp shortcut", "Stamp", 20, {1145, 58}, {58, 66},
                          document.tool == Tool::Stamp)) {
            choose_tool(Tool::Stamp);
        }
        if (ribbon_button("Stamp options", "▼", -1, {1148, 124}, {52, 14}, false,
                          "Stamp outline, dimensions, transparency, and Lift a new stamp")) {
            ImGui::OpenPopup("Stamp options");
        }
        if (ImGui::BeginPopup("Stamp options")) {
            GuiScope popup_scope(GuiEnd::Popup);
            stamp_controls();
        }
        if (ribbon_button("Path shortcut", "Path", 21, {1206, 58}, {58, 66},
                          document.tool == Tool::Path && document.continuous_path)) {
            if (!document.continuous_path) {
                document.commit_path();
            }
            choose_tool(Tool::Path);
            document.continuous_path = true;
        }
        group_label(1138, width - 3, "Extra tools");
    }
}
} // namespace paint

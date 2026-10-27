// Offline exporter for the original ribbon artwork in src/ribbon.cpp.
// This is an asset authoring tool, never linked into either application frontend.
#pragma once
#include "codecs.hpp"
#include "import_codecs.hpp"
#include "raster.hpp"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
struct IconPoint {
    float x, y;
    IconPoint(float px, float py) : x(px), y(py) {}
};
using IconColor = std::uint32_t;
constexpr IconColor icon_rgba(int r, int g, int b, int a) {
    return r | (g << 8) | (b << 16) | (a << 24);
}
class SvgIcon {
    std::ostringstream out_;
    std::vector<IconPoint> path_;
    int gradient_ = 0;
    std::string color(IconColor c) {
        std::ostringstream out;
        out << "#" << std::hex << std::setfill('0') << std::setw(2) << (c & 255) << std::setw(2)
            << ((c >> 8) & 255) << std::setw(2) << ((c >> 16) & 255);
        return out.str();
    }
    void polygon(const std::vector<IconPoint>& points, IconColor c, bool fill, float width = 1) {
        out_ << "<polygon points='";
        for (std::size_t i = 0; i < points.size(); ++i) {
            out_ << points[i].x << "," << points[i].y << " ";
        }
        out_ << "' fill='" << (fill ? color(c) : "none") << "' stroke='" << (fill ? "none" : color(c))
             << "' stroke-width='" << width << "'/>";
    }

  public:
    explicit SvgIcon(int size = 32) {
        out_ << "<svg xmlns='http://www.w3.org/2000/svg' width='" << size << "' height='" << size
             << "' viewBox='0 0 " << size << " " << size << "'>";
    }
    void ShapePath(const std::string& path, IconColor color_value, float width = 1.25f, float scale = 1) {
        out_ << "<path d='" << path << "' fill='none' stroke='" << color(color_value) << "' stroke-width='"
             << width << "' stroke-linecap='round' stroke-linejoin='round' transform='scale(" << scale
             << ")'/>";
    }
    std::string svg() {
        return out_.str() + "</svg>";
    }
    void AddLine(IconPoint a, IconPoint b, IconColor c, float w = 1) {
        out_ << "<path d='M" << a.x << " " << a.y << " L" << b.x << " " << b.y << "' fill='none' stroke='"
             << color(c) << "' stroke-width='" << w << "' stroke-linecap='round'/>";
    }
    void AddRectFilled(IconPoint a, IconPoint b, IconColor c, float r = 0) {
        out_ << "<rect x='" << a.x << "' y='" << a.y << "' width='" << b.x - a.x << "' height='" << b.y - a.y
             << "' rx='" << r << "' fill='" << color(c) << "'/>";
    }
    void AddRect(IconPoint a, IconPoint b, IconColor c, float r = 0) {
        out_ << "<rect x='" << a.x << "' y='" << a.y << "' width='" << b.x - a.x << "' height='" << b.y - a.y
             << "' rx='" << r << "' fill='none' stroke='" << color(c) << "'/>";
    }
    void AddRectFilledMultiColor(IconPoint a, IconPoint b, IconColor c1, IconColor, IconColor c3, IconColor) {
        ++gradient_;
        out_ << "<defs><linearGradient id='g" << gradient_ << "' x2='0' y2='1'><stop stop-color='"
             << color(c1) << "'/><stop offset='1' stop-color='" << color(c3)
             << "'/></linearGradient></defs><rect x='" << a.x << "' y='" << a.y << "' width='" << b.x - a.x
             << "' height='" << b.y - a.y << "' fill='url(#g" << gradient_ << ")'/>";
    }
    void AddCircle(IconPoint p, float r, IconColor c, int = 0, float w = 1) {
        out_ << "<circle cx='" << p.x << "' cy='" << p.y << "' r='" << r << "' fill='none' stroke='"
             << color(c) << "' stroke-width='" << w << "'/>";
    }
    void AddCircleFilled(IconPoint p, float r, IconColor c) {
        out_ << "<circle cx='" << p.x << "' cy='" << p.y << "' r='" << r << "' fill='" << color(c) << "'/>";
    }
    void AddQuadFilled(IconPoint a, IconPoint b, IconPoint c, IconPoint d, IconColor color) {
        polygon({a, b, c, d}, color, true);
    }
    void AddQuad(IconPoint a, IconPoint b, IconPoint c, IconPoint d, IconColor color) {
        polygon({a, b, c, d}, color, false);
    }
    void AddTriangleFilled(IconPoint a, IconPoint b, IconPoint c, IconColor color) {
        polygon({a, b, c}, color, true);
    }
    void AddBezierCubic(IconPoint a, IconPoint b, IconPoint c, IconPoint d, IconColor color_value,
                        float width) {
        out_ << "<path d='M" << a.x << " " << a.y << " C" << b.x << " " << b.y << " " << c.x << " " << c.y
             << " " << d.x << " " << d.y << "' fill='none' stroke='" << color(color_value)
             << "' stroke-width='" << width << "' stroke-linecap='round'/>";
    }
    void PathArcTo(IconPoint p, float r, float a, float b, int n) {
        path_.clear();
        for (int i = 0; i <= n; ++i) {
            float t = a + (b - a) * i / n;
            path_.emplace_back(p.x + r * std::cos(t), p.y + r * std::sin(t));
        }
    }
    void PathStroke(IconColor c, int, float w) {
        for (std::size_t i = 1; i < path_.size(); ++i) {
            AddLine(path_[i - 1], path_[i], c, w);
        }
    }
    void AddText(IconPoint p, IconColor c, const char*) {
        AddLine({p.x, p.y + 27}, {p.x + 9, p.y + 3}, c, 2);
        AddLine({p.x + 9, p.y + 3}, {p.x + 18, p.y + 27}, c, 2);
        AddLine({p.x + 4, p.y + 18}, {p.x + 14, p.y + 18}, c, 2);
    }
};

#include "raster.hpp"
#include "material.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace paint {
const char* pattern_names[pattern_count] = {
    "Solid",        "Dither 12.5%", "Dither 25%", "Dither 37.5%", "Dither 50%", "Dither 62.5%", "Dither 75%",
    "Dither 87.5%", "Horizontal",   "Vertical",   "Diagonal",     "Crosshatch", "Checkerboard", "Bricks",
    "Woven cloth",  "Houndstooth",  "Polka dots", "Waves",        "No color",
    "Buttons", "Cargo Net", "Circuits", "Cobblestones", "Straw Mat", "Custom pattern"};
const char* brush_names[brush_count] = {
    "Brush",  "Calligraphy brush 1", "Calligraphy brush 2", "Airbrush",      "Oil brush",   "Crayon",
    "Marker", "Natural pencil",      "Watercolor brush",    "Bristle brush", "Soft pastel", "Charcoal",
    "Gel pen"};
const char* shape_names[shape_count] = {"Line",
                                        "Bézier",
                                        "Oval",
                                        "Rectangle",
                                        "Rounded rectangle",
                                        "Polygon",
                                        "Triangle",
                                        "Right triangle",
                                        "Diamond",
                                        "Pentagon",
                                        "Hexagon",
                                        "Right arrow",
                                        "Left arrow",
                                        "Up arrow",
                                        "Down arrow",
                                        "Four-point star",
                                        "Five-point star",
                                        "Six-point star",
                                        "Rounded callout",
                                        "Oval callout",
                                        "Cloud callout",
                                        "Heart",
                                        "Lightning",
                                        "Circle",
                                        "Octagon",
                                        "Trapezoid",
                                        "Parallelogram",
                                        "Chevron",
                                        "Double arrow",
                                        "Cross",
                                        "Gear",
                                        "Crescent",
                                        "Teardrop",
                                        "Leaf",
                                        "Eight-point star",
                                        "Burst",
                                        "Arc"};
bool solid_material(const Ink& ink) {
    return ink.pattern == Pattern::Solid && ink.brush == Brush::Round;
}
void select_brush(Ink& ink, Brush brush) {
    ink.brush = brush;
    ink.pattern = Pattern::Solid;
    ink.transparent_pattern = false;
}
void select_pattern(Ink& ink, Pattern pattern) {
    ink.pattern = pattern;
    ink.brush = Brush::Round;
    ink.transparent_pattern = false;
}
Ink pencil_ink(Ink ink) {
    ink.alternate.reset();
    ink.size = 1;
    ink.brush = Brush::Round;
    // The pixel pencil uses the selected color, independently of the retained
    // brush/shape pattern. Explicit No color remains an empty material.
    if (ink.pattern != Pattern::None) {
        ink.pattern = Pattern::Solid;
    }
    ink.transparent_pattern = false;
    return ink;
}
Color patterned(const Ink& ink, int x, int y) {
    if (ink.pattern == Pattern::None) {
        return ink.alternate ? MaterialSurface(*ink.alternate, (*ink.alternate).brush).sample(x, y, 32)
                             : Color{0, 0, 0, 0};
    }
    const int bayer[8][8] = {{0, 48, 12, 60, 3, 51, 15, 63}, {32, 16, 44, 28, 35, 19, 47, 31},
                             {8, 56, 4, 52, 11, 59, 7, 55},  {40, 24, 36, 20, 43, 27, 39, 23},
                             {2, 50, 14, 62, 1, 49, 13, 61}, {34, 18, 46, 30, 33, 17, 45, 29},
                             {10, 58, 6, 54, 9, 57, 5, 53},  {42, 26, 38, 22, 41, 25, 37, 21}};
    int px = x & 7, py = y & 7;
    bool front = true;
    int choice = static_cast<int>(ink.pattern);
    if (choice >= 1 && choice <= 7) {
        front = bayer[py][px] < choice * 8;
    } else {
        switch (ink.pattern) {
        case Pattern::Horizontal:
            front = (py & 3) == 0;
            break;
        case Pattern::Vertical:
            front = (px & 3) == 0;
            break;
        case Pattern::Diagonal:
            front = ((px + py) & 3) == 0;
            break;
        case Pattern::Crosshatch:
            front = ((px + py) & 3) == 0 || ((px - py) & 3) == 0;
            break;
        case Pattern::Checker:
            front = ((px / 4 + py / 4) & 1) == 0;
            break;
        case Pattern::Bricks:
            front = py == 0 || py == 4 || ((px + (py < 4 ? 0 : 4)) & 7) == 0;
            break;
        case Pattern::Weave:
            front = ((px < 4 && py < 4) || (px >= 4 && py >= 4)) ? (px & 1) == 0 : (py & 1) == 0;
            break;
        case Pattern::Houndstooth: {
            const unsigned rows[8] = {0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0xe1, 0xc3, 0x87};
            front = (rows[py] & (1u << px)) != 0;
            break;
        }
        case Pattern::Dots:
            front = (px - 3) * (px - 3) + (py - 3) * (py - 3) <= 3;
            break;
        case Pattern::Buttons:
            front = px == 6 || py == 6 || ((px == 1 || py == 1) && px > 0 && py > 0 && px < 6 && py < 6);
            break;
        case Pattern::CargoNet:
            front = ((x + y) & 15) < 2 || ((x - y) & 15) < 2;
            break;
        case Pattern::Circuits: {
            const unsigned rows[16] = {0x0000, 0x003c, 0x0024, 0xffe4, 0x8004, 0x8007, 0x8004, 0x8004,
                                       0x8004, 0x8fe4, 0x8824, 0x883c, 0x8800, 0x0ffc, 0x0000, 0x0000};
            front = (rows[y & 15] & (1u << (x & 15))) != 0;
            break;
        }
        case Pattern::Cobblestones: {
            const unsigned rows[16] = {0x7fff, 0xc081, 0x8081, 0x8101, 0xc102, 0x7ffe, 0x0823, 0x0821,
                                       0x1041, 0x1041, 0x1ffe, 0xf083, 0x8081, 0x8101, 0x8101, 0xc102};
            front = (rows[y & 15] & (1u << (x & 15))) != 0;
            break;
        }
        case Pattern::StrawMat:
            front = ((x / 8 + y / 8) & 1) == 0 ? (px == 0 || (py & 1) == 0)
                                                       : (py == 0 || (px & 1) == 0);
            break;
        case Pattern::Custom:
            front = false;
            if (ink.custom_pattern && (*ink.custom_pattern).width > 0 && (*ink.custom_pattern).height > 0) {
                const Image& tile = *ink.custom_pattern;
                front = tile.get((x % tile.width + tile.width) % tile.width,
                                 (y % tile.height + tile.height) % tile.height).r < 128;
            }
            break;
        case Pattern::Waves:
            front = (py == ((px < 4 ? px : 7 - px) + 2));
            break;
        default:
            break;
        }
    }
    if (front) {
        return ink.primary;
    }
    if (ink.alternate) {
        return MaterialSurface(*ink.alternate, (*ink.alternate).brush).sample(x, y, 32);
    }
    if (ink.transparent_pattern) {
        return {0, 0, 0, 0};
    }
    return ink.secondary;
}
static std::uint32_t noise_at(int x, int y, std::uint32_t seed) {
    std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393u +
                          static_cast<std::uint32_t>(y) * 668265263u + seed * 1274126177u;
    value = (value ^ (value >> 13)) * 1274126177u;
    return value ^ (value >> 16);
}
int RasterSpace::paper_x(int value) const {
    return x + static_cast<int>(std::floor(static_cast<double>(value) / scale));
}
int RasterSpace::paper_y(int value) const {
    return y + static_cast<int>(std::floor(static_cast<double>(value) / scale));
}
void dab(Image& image, Point point, const Ink& ink, RasterSpace space) {
    if (textured_brush(ink.brush)) {
        MaterialStroke coat;
        coat.segment(image, point, point, ink, space);
        return;
    }
    if (ink.size == 1) {
        point.x = std::round(point.x);
        point.y = std::round(point.y);
    }
    double radius = std::max(0.5, ink.size * 0.5);
    const MaterialSurface material(ink, ink.brush, space);
    int left = std::max(0, static_cast<int>(std::floor(point.x - radius)));
    int top = std::max(0, static_cast<int>(std::floor(point.y - radius)));
    int right = std::min(image.width - 1, static_cast<int>(std::ceil(point.x + radius)));
    int bottom = std::min(image.height - 1, static_cast<int>(std::ceil(point.y + radius)));
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            double dx = x - point.x, dy = y - point.y;
            bool hit = dx * dx + dy * dy <= radius * radius;
            if (ink.brush == Brush::Calligraphy) {
                hit = std::abs(dx + dy) < std::max(double(space.scale), radius * 0.3) && std::abs(dx - dy) <= radius * 1.4;
            } else if (ink.brush == Brush::CalligraphyLeft) {
                hit = std::abs(dx - dy) < std::max(double(space.scale), radius * 0.3) && std::abs(dx + dy) <= radius * 1.4;
            } else if (ink.brush == Brush::Marker) {
                hit = std::abs(dx) < radius && std::abs(dy) < radius;
            }
            if (!hit) {
                continue;
            }
            std::uint32_t noise = noise_at(space.paper_x(x), space.paper_y(y), ink.noise);
            if (ink.brush == Brush::Airbrush && noise % 100 > 18) {
                continue;
            }
            Color color = space.scale == 1 ? patterned(ink, x, y)
                                          : material.sample(x, y, 32.0 * space.scale);
            image.blend(x, y, color);
        }
    }
}
void stroke(Image& image, Point start, Point end, const Ink& ink, RasterSpace space) {
    if (ink.brush == Brush::Round || textured_brush(ink.brush)) {
        MaterialStroke coat;
        coat.segment(image, start, end, ink, space);
        return;
    }
    double dx = end.x - start.x, dy = end.y - start.y;
    int steps = std::max(1, static_cast<int>(std::ceil(std::hypot(dx, dy) * 1.5)));
    for (int i = 0; i <= steps; ++i) {
        Point point{start.x + dx * i / steps, start.y + dy * i / steps};
        dab(image, point, ink, space);
    }
}
void EraserStroke::clear() {
    pixels_.clear();
}
static double segment_distance(Point p, Point a, Point b) {
    double dx = b.x - a.x, dy = b.y - a.y, squared = dx * dx + dy * dy;
    double t = squared > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / squared, 0.0, 1.0) : 0;
    return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}
static double square_distance(Point a, Point b, int x, int y) {
    // Distance from a swept center line to the whole pixel square, including corner contact.
    double enter = 0, leave = 1;
    const double origin[2] = {a.x, a.y}, delta[2] = {b.x - a.x, b.y - a.y}, low[2] = {double(x), double(y)};
    bool intersects = true;
    for (int axis = 0; axis < 2; ++axis) {
        if (std::abs(delta[axis]) < 1e-12) {
            if (origin[axis] < low[axis] || origin[axis] > low[axis] + 1) {
                intersects = false;
            }
        } else {
            double first = (low[axis] - origin[axis]) / delta[axis],
                   last = (low[axis] + 1 - origin[axis]) / delta[axis];
            if (first > last) {
                std::swap(first, last);
            }
            enter = std::max(enter, first);
            leave = std::min(leave, last);
        }
    }
    if (intersects && enter <= leave) {
        return 0;
    }
    double distance = 1e30;
    const Point ends[2] = {a, b};
    for (int i = 0; i < 2; ++i) {
        distance =
            std::min(distance, std::hypot(ends[i].x - std::clamp(ends[i].x, double(x), double(x + 1)),
                                          ends[i].y - std::clamp(ends[i].y, double(y), double(y + 1))));
    }
    for (int corner = 0; corner < 4; ++corner) {
        distance =
            std::min(distance, segment_distance({double(x + (corner & 1)), double(y + (corner >> 1))}, a, b));
    }
    return distance;
}
void EraserStroke::segment(Image& image, Point start, Point end, double diameter, bool soft) {
    double radius = std::max(.5, diameter * .5);
    int left = std::max(0, int(std::floor(std::min(start.x, end.x) - radius)));
    int right = std::min(image.width - 1, int(std::floor(std::max(start.x, end.x) + radius)));
    int top = std::max(0, int(std::floor(std::min(start.y, end.y) - radius)));
    int bottom = std::min(image.height - 1, int(std::floor(std::max(start.y, end.y) + radius)));
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            double distance =
                soft ? segment_distance({x + .5, y + .5}, start, end) : square_distance(start, end, x, y);
            if (distance >= radius) {
                continue;
            }
            double strength =
                soft ? std::sqrt(std::max(0.0, 1 - distance * distance / (radius * radius))) : 1;
            int index = y * image.width + x;
            std::unordered_map<int, Pixel>::iterator found = pixels_.find(index);
            if (found == pixels_.end()) {
                found = pixels_.emplace(index, Pixel{image.get(x, y), 0}).first;
            }
            Pixel& pixel = (*found).second;
            pixel.strength = std::max(pixel.strength, strength);
            Color result = pixel.original;
            result.a = static_cast<std::uint8_t>(std::lround(result.a * (1 - pixel.strength)));
            if (result.a == 0) {
                result = {0, 0, 0, 0};
            }
            image.set(x, y, result);
        }
    }
}
void pixel_line(Image& image, Point start, Point end, const Ink& ink) {
    int x = static_cast<int>(std::floor(start.x)), y = static_cast<int>(std::floor(start.y));
    int ex = static_cast<int>(std::floor(end.x)), ey = static_cast<int>(std::floor(end.y));
    int dx = std::abs(ex - x), dy = -std::abs(ey - y), sx = x < ex ? 1 : -1, sy = y < ey ? 1 : -1;
    int error = dx + dy;
    while (true) {
        image.blend(x, y, patterned(ink, x, y));
        if (x == ex && y == ey) {
            break;
        }
        int doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y += sy;
        }
    }
}
void flood(Image& image, int x, int y, const Ink& ink) {
    if (!image.contains(x, y)) {
        return;
    }
    Color target = image.get(x, y);
    // Discover against immutable source; a patterned fill may contain the target color.
    std::vector<std::uint8_t> visited(image.pixels.size(), 0);
    std::vector<int> queue;
    queue.reserve(std::min<std::size_t>(image.pixels.size(), 65536));
    int first = y * image.width + x;
    queue.push_back(first);
    visited[first] = 1;
    for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
        int index = queue[cursor], px = index % image.width, py = index / image.width;
        const int nx[4] = {px - 1, px + 1, px, px};
        const int ny[4] = {py, py, py - 1, py + 1};
        for (int direction = 0; direction < 4; ++direction) {
            if (!image.contains(nx[direction], ny[direction])) {
                continue;
            }
            int next = ny[direction] * image.width + nx[direction];
            if (!visited[next] && equal(image.pixels[next], target)) {
                visited[next] = 1;
                queue.push_back(next);
            }
        }
    }
    const MaterialSurface material(ink, ink.brush);
    for (int index : queue) {
        image.blend(index % image.width, index / image.width,
                    material.sample(index % image.width, index / image.width, 32));
    }
}
bool inside_polygon(const std::vector<Point>& points, double x, double y) {
    if (points.size() < 3) {
        return false;
    }
    bool inside = false;
    std::size_t previous = points.size() - 1;
    for (std::size_t i = 0; i < points.size(); ++i) {
        Point a = points[i], b = points[previous];
        if ((a.y > y) != (b.y > y)) {
            double crossing = (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x;
            if (x < crossing) {
                inside = !inside;
            }
        }
        previous = i;
    }
    return inside;
}
static void polygon_native(Image& image, const std::vector<Point>& points, const Ink& ink,
                           bool outline, bool fill, Brush fill_brush,
                           const Ink* fill_material, RasterSpace space,
                           const std::vector<StrokeSegment>& segments) {
    if (points.empty()) {
        return;
    }
    FillBoundary boundary;
    if (fill && points.size() > 2) {
        Ink fill_ink = ink;
        fill_ink.primary = ink.secondary;
        fill_ink.secondary = ink.primary;
        if (fill_material) {
            fill_ink = *fill_material;
        }
        material_fill(image, points, fill_ink, fill_brush, outline ? &boundary : nullptr, space);
    }
    if (outline) {
        if (ink.brush == Brush::Round || textured_brush(ink.brush)) {
            MaterialStroke coat(&boundary);
            for (const StrokeSegment& segment : segments) {
                coat.segment(image, segment.first, segment.last, ink, space);
            }
        } else {
            for (const StrokeSegment& segment : segments) {
                stroke(image, segment.first, segment.last, ink, space);
            }
        }
    }
}
static Ink enlarged_ink(const Ink& ink) {
    Ink result = ink;
    result.size *= 4;
    result.supersample = false;
    if (ink.alternate) {
        result.alternate = std::make_shared<const Ink>(enlarged_ink(*ink.alternate));
    }
    return result;
}
void polygon(Image& image, const std::vector<Point>& vertices, const Ink& ink, bool outline, bool fill,
             bool closed, Brush fill_brush, const Ink* fill_material) {
    const std::vector<Point> rounded = ink.geometry.corner_radius > 0
        ? soften_corners(vertices, ink.geometry.corner_radius, closed, ink.supersample ? 0.03125 : 0.125)
        : std::vector<Point>{};
    const std::vector<Point>& corners = ink.geometry.corner_radius > 0 ? rounded : vertices;
    const std::vector<Point> varied = ink.geometry.organic > 0
        ? organic_contour(corners, ink.geometry.organic, ink.geometry.seed, closed)
        : std::vector<Point>{};
    const std::vector<Point>& points = ink.geometry.organic > 0 ? varied : corners;
    const std::vector<StrokeSegment> segments = outline
        ? geometry_outline(points, ink.geometry, std::max(1, ink.size), closed,
                           ink.supersample ? 0.03125 : 0.125)
        : std::vector<StrokeSegment>{};
    if (!ink.smooth || !ink.supersample || points.empty()) {
        polygon_native(image, points, ink, outline, fill, fill_brush, fill_material, {}, segments);
        return;
    }
    double left = points.front().x, right = left, top = points.front().y, bottom = top;
    for (const Point point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return;
        }
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    for (const StrokeSegment& segment : segments) {
        for (Point point : {segment.first, segment.last}) {
            left = std::min(left, point.x); right = std::max(right, point.x);
            top = std::min(top, point.y); bottom = std::max(bottom, point.y);
        }
    }
    const double padding = outline ? ink.size + 2.0 : 2.0;
    const int x0 = static_cast<int>(std::clamp(std::floor(left - padding), 0.0, double(image.width)));
    const int y0 = static_cast<int>(std::clamp(std::floor(top - padding), 0.0, double(image.height)));
    const int x1 = static_cast<int>(std::clamp(std::ceil(right + padding), 0.0, double(image.width)));
    const int y1 = static_cast<int>(std::clamp(std::ceil(bottom + padding), 0.0, double(image.height)));
    const Ink enlarged = enlarged_ink(ink);
    const Ink body = enlarged_ink(fill_material ? *fill_material : ink);
    // The halo exceeds the bounded material-distance field. Each tile therefore
    // sees the same pigment depth as an uninterrupted high-resolution render.
    constexpr int tile_size = 96, halo = 32;
    std::vector<Point> local(points.size());
    std::vector<StrokeSegment> local_segments(segments.size());
    for (int y = y0; y < y1; y += tile_size) {
        for (int x = x0; x < x1; x += tile_size) {
            const int width = std::min(tile_size, x1 - x), height = std::min(tile_size, y1 - y);
            const RasterSpace space{4, x - halo, y - halo};
            Image tile;
            tile.reset((width + halo * 2) * 4, (height + halo * 2) * 4, {0, 0, 0, 0});
            for (int ty = 0; ty < tile.height; ++ty) {
                for (int tx = 0; tx < tile.width; ++tx) {
                    tile.set(tx, ty, image.get(space.paper_x(tx), space.paper_y(ty)));
                }
            }
            for (std::size_t i = 0; i < points.size(); ++i) {
                local[i] = {(points[i].x - space.x) * 4 + 1.5,
                            (points[i].y - space.y) * 4 + 1.5};
            }
            for (std::size_t i = 0; i < segments.size(); ++i) {
                local_segments[i] = {{(segments[i].first.x - space.x) * 4 + 1.5,
                                      (segments[i].first.y - space.y) * 4 + 1.5},
                                     {(segments[i].last.x - space.x) * 4 + 1.5,
                                      (segments[i].last.y - space.y) * 4 + 1.5}};
            }
            polygon_native(tile, local, enlarged, outline, fill, fill_brush,
                           fill_material ? &body : nullptr, space, local_segments);
            for (int py = 0; py < height; ++py) {
                for (int px = 0; px < width; ++px) {
                    const Color original = image.get(x + px, y + py);
                    int alpha = 0, red = 0, green = 0, blue = 0;
                    bool changed = false;
                    for (int sy = 0; sy < 4; ++sy) {
                        for (int sx = 0; sx < 4; ++sx) {
                            const Color sample = tile.get((px + halo) * 4 + sx, (py + halo) * 4 + sy);
                            changed = changed || !equal(sample, original);
                            alpha += sample.a;
                            red += sample.r * sample.a;
                            green += sample.g * sample.a;
                            blue += sample.b * sample.a;
                        }
                    }
                    if (changed && alpha > 0) {
                        image.set(x + px, y + py,
                                  {static_cast<std::uint8_t>((red + alpha / 2) / alpha),
                                   static_cast<std::uint8_t>((green + alpha / 2) / alpha),
                                   static_cast<std::uint8_t>((blue + alpha / 2) / alpha),
                                   static_cast<std::uint8_t>((alpha + 8) / 16)});
                    }
                }
            }
        }
    }
}
std::vector<Point> shape_points(Shape shape, Point start, Point end) {
    double left = std::min(start.x, end.x), top = std::min(start.y, end.y), width = std::abs(end.x - start.x),
           height = std::abs(end.y - start.y);
    if (shape == Shape::Circle) {
        double diameter = std::max(width, height);
        width = height = diameter;
        left = end.x < start.x ? start.x - diameter : start.x;
        top = end.y < start.y ? start.y - diameter : start.y;
    }
    std::vector<Point> normalized;
    if (shape == Shape::Line || shape == Shape::Bezier || shape == Shape::Arc) {
        return {start, end};
    }
    switch (shape) {
    case Shape::Trapezoid:
        normalized = {{0.23, 0}, {0.77, 0}, {1, 1}, {0, 1}};
        break;
    case Shape::Parallelogram:
        normalized = {{0.25, 0}, {1, 0}, {0.75, 1}, {0, 1}};
        break;
    case Shape::Chevron:
        normalized = {{0, 0}, {0.6, 0}, {1, 0.5}, {0.6, 1}, {0, 1}, {0.4, 0.5}};
        break;
    case Shape::DoubleArrow:
        normalized = {{0, 0.5}, {0.3, 0}, {0.3, 0.3}, {0.7, 0.3}, {0.7, 0},
                      {1, 0.5}, {0.7, 1}, {0.7, 0.7}, {0.3, 0.7}, {0.3, 1}};
        break;
    case Shape::Cross:
        normalized = {{0.34, 0}, {0.66, 0}, {0.66, 0.34}, {1, 0.34}, {1, 0.66}, {0.66, 0.66},
                      {0.66, 1}, {0.34, 1}, {0.34, 0.66}, {0, 0.66}, {0, 0.34}, {0.34, 0.34}};
        break;
    case Shape::Crescent:
    case Shape::Leaf:
    case Shape::Teardrop: {
        Point control[7];
        if (shape == Shape::Crescent) {
            const Point curve[7] = {{0.75, 0.02}, {-0.23, 0.02}, {-0.23, 0.98}, {0.75, 0.98},
                                    {0.25, 0.8},  {0.25, 0.2},   {0.75, 0.02}};
            std::copy(curve, curve + 7, control);
        } else if (shape == Shape::Leaf) {
            const Point curve[7] = {{1, 0}, {0.85, 0.9}, {0.1, 1}, {0, 1}, {0, 0.1}, {0.15, 0}, {1, 0}};
            std::copy(curve, curve + 7, control);
        } else {
            const Point curve[7] = {{0.5, 0},  {0.28, 0.4}, {-0.42, 1}, {0.5, 1},
                                    {1.42, 1}, {0.72, 0.4}, {0.5, 0}};
            std::copy(curve, curve + 7, control);
        }
        for (int segment = 0; segment < 2; ++segment) {
            for (int i = 0; i < 48; ++i) {
                double t = i / 48.0, u = 1 - t;
                Point a = control[segment * 3], b = control[segment * 3 + 1], c = control[segment * 3 + 2],
                      d = control[segment * 3 + 3];
                normalized.push_back(
                    {u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                     u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y});
            }
        }
        break;
    }
    case Shape::Rectangle:
        normalized = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        break;
    case Shape::Triangle:
        normalized = {{0.5, 0}, {1, 1}, {0, 1}};
        break;
    case Shape::RightTriangle:
        normalized = {{0, 0}, {1, 1}, {0, 1}};
        break;
    case Shape::Diamond:
        normalized = {{0.5, 0}, {1, 0.5}, {0.5, 1}, {0, 0.5}};
        break;
    case Shape::Lightning:
        normalized = {{0.35, 0}, {1, 0.1}, {0.6, 0.42}, {0.95, 0.48}, {0.12, 1}, {0.38, 0.58}, {0, 0.55}};
        break;
    case Shape::RightArrow:
    case Shape::LeftArrow:
    case Shape::UpArrow:
    case Shape::DownArrow:
        normalized = {{0, 0.25}, {0.6, 0.25}, {0.6, 0}, {1, 0.5}, {0.6, 1}, {0.6, 0.75}, {0, 0.75}};
        for (Point& point : normalized) {
            if (shape == Shape::LeftArrow) {
                point.x = 1.0 - point.x;
            } else if (shape == Shape::UpArrow || shape == Shape::DownArrow) {
                double x = point.x;
                point.x = point.y;
                point.y = shape == Shape::UpArrow ? 1.0 - x : x;
            }
        }
        break;
    case Shape::RoundedRectangle:
    case Shape::RoundedCallout:
        for (int corner = 0; corner < 4; ++corner) {
            double cx = (corner == 0 || corner == 3) ? 0.15 : 0.85;
            double cy = corner < 2 ? 0.15 : 0.85;
            for (int step = 0; step <= 12; ++step) {
                double angle = std::numbers::pi * (1.0 + corner * 0.5 + step / 24.0);
                normalized.push_back({cx + 0.15 * std::cos(angle), cy + 0.15 * std::sin(angle)});
            }
        }
        if (shape == Shape::RoundedCallout) {
            normalized.insert(normalized.begin() + 36, {{0.4, 0.99}, {0.2, 1.25}, {0.2, 0.98}});
        }
        break;
    default: {
        int count = 96;
        if (shape == Shape::Pentagon) {
            count = 5;
        }
        if (shape == Shape::Hexagon) {
            count = 6;
        }
        if (shape == Shape::Octagon) {
            count = 8;
        }
        if (shape == Shape::Star8) {
            count = 16;
        }
        if (shape == Shape::Burst) {
            count = 24;
        }
        if (shape == Shape::Gear) {
            count = 48;
        }
        if (shape == Shape::Star4) {
            count = 8;
        }
        if (shape == Shape::Star5) {
            count = 10;
        }
        if (shape == Shape::Star6) {
            count = 12;
        }
        for (int i = 0; i < count; ++i) {
            double angle = -std::numbers::pi / 2 + 2 * std::numbers::pi * i / count;
            double radius = 0.5;
            if ((shape == Shape::Star4 || shape == Shape::Star5 || shape == Shape::Star6 ||
                 shape == Shape::Star8 || shape == Shape::Burst) &&
                i % 2 == 1) {
                radius = 0.21;
            }
            if (shape == Shape::Gear && (i % 4 == 1 || i % 4 == 2)) {
                radius = 0.39;
            }
            Point point{0.5 + radius * std::cos(angle), 0.5 + radius * std::sin(angle)};
            if (shape == Shape::Heart) {
                double t = 2 * std::numbers::pi * i / count;
                point = {
                    0.5 + 0.5 * std::pow(std::sin(t), 3),
                    0.52 - (13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t)) /
                               32.0};
            }
            if (shape == Shape::CloudCallout) {
                radius = 0.44 + 0.06 * std::cos(12 * angle);
                point = {0.5 + radius * std::cos(angle), 0.5 + radius * std::sin(angle)};
            }
            normalized.push_back(point);
        }
        if (shape == Shape::OvalCallout || shape == Shape::CloudCallout) {
            normalized.insert(normalized.begin() + 60, {{0.3, 0.9}, {0.15, 1.25}, {0.18, 0.85}});
        }
        break;
    }
    }
    for (Point& point : normalized) {
        point.x = left + point.x * width;
        point.y = top + point.y * height;
    }
    return normalized;
}
void draw_shape(Image& image, Shape shape, Point start, Point end, const Ink& ink, bool outline, bool fill,
                Brush fill_brush, const Ink* fill_material) {
    std::vector<Point> points = shape_points(shape, start, end);
    polygon(image, points, ink, outline, fill,
            shape != Shape::Line && shape != Shape::Bezier && shape != Shape::Arc, fill_brush, fill_material);
}
Shape stamp_geometry_shape(StampShape shape) {
    const Shape shapes[] = {Shape::Circle,
                            Shape::RoundedRectangle,
                            Shape::Rectangle,
                            Shape::Rectangle,
                            Shape::Line,
                            Shape::Bezier,
                            Shape::Oval,
                            Shape::RoundedRectangle,
                            Shape::Polygon,
                            Shape::Triangle,
                            Shape::RightTriangle,
                            Shape::Diamond,
                            Shape::Pentagon,
                            Shape::Hexagon,
                            Shape::RightArrow,
                            Shape::LeftArrow,
                            Shape::UpArrow,
                            Shape::DownArrow,
                            Shape::Star4,
                            Shape::Star5,
                            Shape::Star6,
                            Shape::RoundedCallout,
                            Shape::OvalCallout,
                            Shape::CloudCallout,
                            Shape::Heart,
                            Shape::Lightning,
                            Shape::Octagon,
                            Shape::Trapezoid,
                            Shape::Parallelogram,
                            Shape::Chevron,
                            Shape::DoubleArrow,
                            Shape::Cross,
                            Shape::Gear,
                            Shape::Crescent,
                            Shape::Teardrop,
                            Shape::Leaf,
                            Shape::Star8,
                            Shape::Burst,
                            Shape::Arc};
    return shapes[static_cast<int>(shape)];
}
const char* stamp_shape_name(StampShape shape) {
    if (shape == StampShape::Pill) {
        return "Pill";
    }
    if (shape == StampShape::Square) {
        return "Square";
    }
    return shape_names[static_cast<int>(stamp_geometry_shape(shape))];
}
std::vector<Point> stamp_outline(StampShape shape, int width, int height) {
    if (shape == StampShape::Pill) {
        std::vector<Point> points;
        double rx = width * 0.5, ry = height * 0.5, radius = std::min(rx, ry);
        for (int i = 0; i < 96; ++i) {
            double angle = i * 2 * std::numbers::pi / 96;
            double cosine = std::cos(angle), sine = std::sin(angle);
            points.push_back({rx + std::copysign(rx - radius, cosine) + radius * cosine,
                              ry + std::copysign(ry - radius, sine) + radius * sine});
        }
        return points;
    }
    Shape geometry = stamp_geometry_shape(shape);
    std::vector<Point> points;
    if (geometry == Shape::Bezier || geometry == Shape::Arc) {
        // A fixed curved mask has no interactive control points. Keep the full
        // three-pixel stroke inside the capture rectangle, including its caps.
        for (int i = 0; i <= 96; ++i) {
            double t = i / 96.0, u = 1 - t;
            double x = t, y = 3 * u * u * t + t * t * t;
            if (geometry == Shape::Arc) {
                x = 0.5 - 0.5 * std::cos(t * std::numbers::pi);
                y = 1 - std::sin(t * std::numbers::pi);
            }
            double inset_x = std::min(1.5, width * 0.5);
            double inset_y = std::min(1.5, height * 0.5);
            points.push_back({inset_x + x * (width - 2 * inset_x), inset_y + y * (height - 2 * inset_y)});
        }
        return points;
    }
    if (geometry == Shape::Polygon) {
        return {{width * 0.08, height * 0.15},
                {width * 0.8, 0},
                {double(width), height * 0.72},
                {width * 0.42, double(height)},
                {0, height * 0.6}};
    }
    points = shape_points(geometry, {0, 0}, {static_cast<double>(width), static_cast<double>(height)});
    // Some drawing shapes deliberately extend beyond their drag box (callout
    // tails and circles). A stamp mask must fit its entire capture rectangle.
    double left = 0, top = 0, right = width, bottom = height;
    for (const Point& point : points) {
        left = std::min(left, point.x);
        top = std::min(top, point.y);
        right = std::max(right, point.x);
        bottom = std::max(bottom, point.y);
    }
    if (right > left && bottom > top) {
        for (Point& point : points) {
            point.x = (point.x - left) * width / (right - left);
            point.y = (point.y - top) * height / (bottom - top);
        }
    }
    return points;
}
Image make_stamp(const Image& image, Rect bounds, StampShape shape, bool transparent, Color key) {
    Image result = cropped(image, bounds);
    std::vector<Point> outline = stamp_outline(shape, bounds.w, bounds.h);
    Shape geometry = stamp_geometry_shape(shape);
    bool open = geometry == Shape::Line || geometry == Shape::Bezier || geometry == Shape::Arc;
    for (int y = 0; y < bounds.h; ++y) {
        for (int x = 0; x < bounds.w; ++x) {
            int covered = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    Point sample{x + (sx + 0.5) / 4, y + (sy + 0.5) / 4};
                    bool inside = !open && inside_polygon(outline, sample.x, sample.y);
                    if (open) {
                        for (std::size_t i = 1; i < outline.size(); ++i) {
                            inside = inside || segment_distance(sample, outline[i - 1], outline[i]) <= 1.5;
                        }
                    }
                    covered += inside ? 1 : 0;
                }
            }
            Color pixel = result.get(x, y);
            if (!covered || (transparent && equal(pixel, key))) {
                result.set(x, y, {0, 0, 0, 0});
            } else {
                if (!transparent && pixel.a < 255) {
                    Color opaque = key;
                    opaque.a = 255;
                    result.set(x, y, opaque);
                    result.blend(x, y, pixel);
                    pixel = result.get(x, y);
                }
                pixel.a = static_cast<std::uint8_t>((pixel.a * covered + 8) / 16);
                result.set(x, y, pixel);
            }
        }
    }
    return result;
}
Image rotate_quarter(const Image& image, int turns) {
    turns = (turns % 4 + 4) % 4;
    Image result;
    result.reset(turns % 2 ? image.height : image.width, turns % 2 ? image.width : image.height,
                 {0, 0, 0, 0});
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            int nx = x, ny = y;
            if (turns == 1) {
                nx = image.height - 1 - y;
                ny = x;
            }
            if (turns == 2) {
                nx = image.width - 1 - x;
                ny = image.height - 1 - y;
            }
            if (turns == 3) {
                nx = y;
                ny = image.width - 1 - x;
            }
            result.set(nx, ny, image.get(x, y));
        }
    }
    return result;
}
Image flipped(const Image& image, bool horizontal) {
    Image result = image;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            result.set(horizontal ? image.width - 1 - x : x, horizontal ? y : image.height - 1 - y,
                       image.get(x, y));
        }
    }
    return result;
}
} // namespace paint

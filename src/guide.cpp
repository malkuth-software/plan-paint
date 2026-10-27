#include "guide.hpp"
#include "document.hpp"
#include "paint_tools.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
namespace paint {
namespace {
double distance_squared(Lab a, Lab b) {
    return (a.l - b.l) * (a.l - b.l) + (a.a - b.a) * (a.a - b.a) + (a.b - b.b) * (a.b - b.b);
}
double edge_distance(Point point, Point start, Point end) {
    const double dx = end.x - start.x, dy = end.y - start.y, length = dx * dx + dy * dy;
    const double t =
        length > 0 ? std::clamp(((point.x - start.x) * dx + (point.y - start.y) * dy) / length, 0.0, 1.0) : 0;
    return std::hypot(point.x - start.x - t * dx, point.y - start.y - t * dy);
}
Lab median_lab(std::vector<Lab> samples) {
    std::vector<double> l, a, b;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        l.push_back(samples[index].l);
        a.push_back(samples[index].a);
        b.push_back(samples[index].b);
    }
    if (l.empty()) {
        return {};
    }
    std::sort(l.begin(), l.end());
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    const std::size_t middle = l.size() / 2;
    return {l[middle], a[middle], b[middle]};
}
} // namespace
std::vector<std::vector<Point>> mask_contours(const std::vector<std::uint8_t>& mask, int width, int height) {
    if (width < 1 || height < 1 || mask.size() != static_cast<std::size_t>(width) * height) {
        return {};
    }
    // Trace every boundary, including holes and disconnected selected islands.
    // Directed edges keep foreground on the right.
    std::unordered_multimap<int, int> edges;
    const int stride = width + 1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            if (!mask[index]) {
                continue;
            }
            const int tl = y * stride + x, tr = tl + 1, bl = tl + stride, br = bl + 1;
            if (y == 0 || !mask[index - width]) {
                edges.emplace(tl, tr);
            }
            if (x + 1 == width || !mask[index + 1]) {
                edges.emplace(tr, br);
            }
            if (y + 1 == height || !mask[index + width]) {
                edges.emplace(br, bl);
            }
            if (x == 0 || !mask[index - 1]) {
                edges.emplace(bl, tl);
            }
        }
    }
    std::vector<std::vector<Point>> contours;
    while (!edges.empty()) {
        const int first = (*edges.begin()).first;
        int current = first;
        std::vector<Point> loop;
        do {
            const std::unordered_multimap<int, int>::iterator found = edges.find(current);
            if (found == edges.end()) {
                break;
            }
            loop.push_back({static_cast<double>(current % stride), static_cast<double>(current / stride)});
            current = (*found).second;
            edges.erase(found);
        } while (current != first);
        std::vector<Point> simplified;
        for (std::size_t index = 0; index < loop.size(); ++index) {
            const Point a = loop[(index + loop.size() - 1) % loop.size()], b = loop[index],
                        c = loop[(index + 1) % loop.size()];
            if ((b.x - a.x) * (c.y - b.y) != (b.y - a.y) * (c.x - b.x)) {
                simplified.push_back(b);
            }
        }
        if (!simplified.empty()) {
            contours.push_back(std::move(simplified));
        }
    }
    return contours;
}
std::vector<Point> mask_outline(const std::vector<std::uint8_t>& mask, int width, int height) {
    const std::vector<std::vector<Point>> contours = mask_contours(mask, width, height);
    std::vector<Point> largest;
    double largest_area = 0;
    for (std::size_t contour = 0; contour < contours.size(); ++contour) {
        const std::vector<Point>& loop = contours[contour];
        double area = 0;
        for (std::size_t index = 0; index < loop.size(); ++index) {
            const Point a = loop[index], b = loop[(index + 1) % loop.size()];
            area += a.x * b.y - b.x * a.y;
        }
        if (std::abs(area) > largest_area) {
            largest_area = std::abs(area);
            largest = loop;
        }
    }
    return largest;
}
SelectionMask similar_colors(const Image& image, Point seed, double tolerance) {
    if (!std::isfinite(seed.x) || !std::isfinite(seed.y) || seed.x < 0 || seed.y < 0 ||
        seed.x >= image.width || seed.y >= image.height) {
        return {};
    }
    const int sx = static_cast<int>(std::floor(seed.x)), sy = static_cast<int>(std::floor(seed.y));
    if (!image.contains(sx, sy) || !std::isfinite(tolerance)) {
        return {};
    }
    const Color target = image.get(sx, sy);
    const Lab reference = to_oklab(target);
    tolerance = std::clamp(tolerance, 0.002, 0.25);
    SelectionMask result;
    result.bounds = {0, 0, image.width, image.height};
    result.coverage.resize(image.pixels.size());
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const Color color = image.pixels[i];
        const double alpha = (static_cast<int>(color.a) - target.a) / 255.0;
        const double distance = std::sqrt(
            (color.a || target.a ? distance_squared(to_oklab(color), reference) : 0) + 0.25 * alpha * alpha);
        result.coverage[i] = static_cast<std::uint8_t>(
            std::clamp((tolerance * 1.5 - distance) / (tolerance * 0.5), 0.0, 1.0) * 255);
    }
    trim_selection_mask(result);
    return result;
}
void trim_selection_mask(SelectionMask& mask) {
    if (mask.bounds.w < 1 || mask.bounds.h < 1 ||
        mask.coverage.size() != static_cast<std::size_t>(mask.bounds.w) * mask.bounds.h) {
        mask = {};
        return;
    }
    int left = mask.bounds.w, top = mask.bounds.h, right = 0, bottom = 0;
    for (int y = 0; y < mask.bounds.h; ++y) {
        for (int x = 0; x < mask.bounds.w; ++x) {
            if (mask.coverage[static_cast<std::size_t>(y) * mask.bounds.w + x]) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
        }
    }
    if (left >= right || top >= bottom) {
        mask = {};
        return;
    }
    const int width = right - left, height = bottom - top;
    std::vector<std::uint8_t> trimmed(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            trimmed[static_cast<std::size_t>(y) * width + x] =
                mask.coverage[static_cast<std::size_t>(y + top) * mask.bounds.w + left + x];
        }
    }
    mask.bounds = {mask.bounds.x + left, mask.bounds.y + top, width, height};
    mask.coverage = std::move(trimmed);
    mask.outline = mask_outline(mask.coverage, width, height);
}

static SelectionMask expand_lasso_void(const Image& image, const std::vector<Point>& polygon,
                                       double tolerance) {
    double left = image.width, top = image.height, right = 0, bottom = 0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        left = std::min(left, polygon[index].x);
        top = std::min(top, polygon[index].y);
        right = std::max(right, polygon[index].x);
        bottom = std::max(bottom, polygon[index].y);
    }
    const Point center{(left + right) * 0.5, (top + bottom) * 0.5};
    int seed = -1;
    double nearest = std::numeric_limits<double>::max();
    for (int y = std::max(0, static_cast<int>(std::floor(top)));
         y < std::min(image.height, static_cast<int>(std::ceil(bottom))); ++y) {
        for (int x = std::max(0, static_cast<int>(std::floor(left)));
             x < std::min(image.width, static_cast<int>(std::ceil(right))); ++x) {
            const double distance = std::hypot(x + 0.5 - center.x, y + 0.5 - center.y);
            if (distance < nearest && inside_polygon(polygon, x + 0.5, y + 0.5)) {
                nearest = distance;
                seed = y * image.width + x;
            }
        }
    }
    if (seed < 0) {
        return {};
    }
    SelectionMask result;
    result.bounds = {0, 0, image.width, image.height};
    result.coverage.assign(image.pixels.size(), 0);
    std::vector<std::uint8_t> visited(image.pixels.size(), 0);
    const Color background = image.pixels[seed];
    const Lab reference = to_oklab(background);
    tolerance = std::clamp(tolerance, 0.002, 0.25);
    std::queue<int> pending;
    pending.push(seed);
    visited[seed] = 1;
    while (!pending.empty()) {
        const int index = pending.front();
        pending.pop();
        const Color color = image.pixels[index];
        const double alpha_delta = (static_cast<int>(color.a) - background.a) / 255.0;
        const double delta =
            std::sqrt((color.a || background.a ? distance_squared(to_oklab(color), reference) : 0) +
                      0.25 * alpha_delta * alpha_delta);
        if (delta >= tolerance * 1.5) {
            continue;
        }
        const double coverage = std::clamp((tolerance * 1.5 - delta) / (tolerance * 0.5), 0.0, 1.0);
        result.coverage[index] =
            static_cast<std::uint8_t>(std::lround(coverage * coverage * (3 - 2 * coverage) * 255));
        const int x = index % image.width, y = index / image.width;
        const int neighbors[] = {x > 0 ? index - 1 : -1, x + 1 < image.width ? index + 1 : -1,
                                 y > 0 ? index - image.width : -1,
                                 y + 1 < image.height ? index + image.width : -1};
        for (int direction = 0; direction < 4; ++direction) {
            const int next = neighbors[direction];
            if (next >= 0 && !visited[next]) {
                visited[next] = 1;
                pending.push(next);
            }
        }
    }
    trim_selection_mask(result);
    return result;
}
SelectionMask tighten_lasso(const Image& image, const std::vector<Point>& polygon, bool inner_void,
                            double tolerance) {
    SelectionMask result;
    if (polygon.size() < 3 || image.width < 1 || image.height < 1) {
        return result;
    }
    if (inner_void) {
        return expand_lasso_void(image, polygon, tolerance);
    }
    double left = image.width, top = image.height, right = 0, bottom = 0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        left = std::min(left, polygon[index].x);
        top = std::min(top, polygon[index].y);
        right = std::max(right, polygon[index].x);
        bottom = std::max(bottom, polygon[index].y);
    }
    result.bounds.x = std::clamp(static_cast<int>(std::floor(left)), 0, image.width);
    result.bounds.y = std::clamp(static_cast<int>(std::floor(top)), 0, image.height);
    result.bounds.w = std::clamp(static_cast<int>(std::ceil(right)), 0, image.width) - result.bounds.x;
    result.bounds.h = std::clamp(static_cast<int>(std::ceil(bottom)), 0, image.height) - result.bounds.y;
    if (result.bounds.w <= 0 || result.bounds.h <= 0) {
        return {};
    }
    const Point center{(left + right) * 0.5, (top + bottom) * 0.5};
    const int width = result.bounds.w, height = result.bounds.h;
    const std::size_t size = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> inside(size, 0), connected(size, 0);
    std::vector<Lab> labs(size), boundary;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const Point point{result.bounds.x + x + 0.5, result.bounds.y + y + 0.5};
            if (!inside_polygon(polygon, point.x, point.y)) {
                continue;
            }
            inside[index] = 1;
            labs[index] = to_oklab(image.get(result.bounds.x + x, result.bounds.y + y));
            double distance = 2;
            for (std::size_t edge = 0; edge < polygon.size(); ++edge) {
                distance = std::min(
                    distance, edge_distance(point, polygon[edge], polygon[(edge + 1) % polygon.size()]));
            }
            if (distance <= 1.5) {
                boundary.push_back(labs[index]);
            }
        }
    }
    const Lab background = median_lab(boundary);
    tolerance = std::clamp(tolerance, 0.002, 0.25);
    const double threshold = tolerance * tolerance;
    // Select the relevant connected component nearest the lasso center.
    // The color threshold deliberately absorbs small JPEG background variations.
    int seed = -1;
    double nearest = std::numeric_limits<double>::max();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const double delta = distance_squared(labs[index], background);
            if (!inside[index] || delta <= threshold) {
                continue;
            }
            const double dx = result.bounds.x + x + 0.5 - center.x, dy = result.bounds.y + y + 0.5 - center.y;
            if (dx * dx + dy * dy < nearest) {
                seed = static_cast<int>(index);
                nearest = dx * dx + dy * dy;
            }
        }
    }
    if (seed < 0) {
        return {};
    }
    std::queue<int> pending;
    pending.push(seed);
    connected[seed] = 1;
    while (!pending.empty()) {
        const int index = pending.front();
        pending.pop();
        const int x = index % width, y = index / width;
        const int neighbors[] = {x > 0 ? index - 1 : -1, x + 1 < width ? index + 1 : -1,
                                 y > 0 ? index - width : -1, y + 1 < height ? index + width : -1};
        for (int direction = 0; direction < 4; ++direction) {
            const int next = neighbors[direction];
            if (next < 0 || connected[next] || !inside[next]) {
                continue;
            }
            const double delta = distance_squared(labs[next], background);
            if (delta <= threshold) {
                continue;
            }
            connected[next] = 1;
            pending.push(next);
        }
    }
    result.coverage.assign(size, 0);
    for (std::size_t index = 0; index < size; ++index) {
        if (!connected[index]) {
            continue;
        }
        const double delta = std::sqrt(distance_squared(labs[index], background));
        const double alpha = std::clamp((delta - tolerance) / std::max(0.008, tolerance), 0.0, 1.0);
        result.coverage[index] =
            static_cast<std::uint8_t>(std::lround(alpha * alpha * (3 - 2 * alpha) * 255));
    }
    trim_selection_mask(result);
    return result;
}
void Guide::seal_selection_body() {
    const int width = selection.bounds.w, height = selection.bounds.h;
    if (width < 1 || height < 1 ||
        selection.coverage.size() != static_cast<std::size_t>(width) * height) {
        return;
    }
    // Exterior-connected empty pixels stay paintable. Enclosed empty pixels
    // belong to the solid guide body, even when the selection was only ink.
    // Do this once at conversion, keeping per-pixel stencil lookups constant-time.
    std::vector<std::uint8_t> exterior(selection.coverage.size(), 0);
    std::queue<std::size_t> pending;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x != 0 && y != 0 && x != width - 1 && y != height - 1) continue;
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            if (!selection.coverage[index]) {
                exterior[index] = 1;
                pending.push(index);
            }
        }
    }
    while (!pending.empty()) {
        const std::size_t index = pending.front();
        pending.pop();
        const std::size_t absent = selection.coverage.size();
        const std::size_t x = index % width, y = index / width;
        const std::size_t neighbors[] = {
            x > 0 ? index - 1 : absent,
            x + 1 < static_cast<std::size_t>(width) ? index + 1 : absent,
            y > 0 ? index - width : absent,
            y + 1 < static_cast<std::size_t>(height) ? index + width : absent};
        for (const std::size_t next : neighbors) {
            if (next != absent && !exterior[next] && !selection.coverage[next]) {
                exterior[next] = 1;
                pending.push(next);
            }
        }
    }
    for (std::size_t index = 0; index < selection.coverage.size(); ++index) {
        if (!selection.coverage[index] && !exterior[index]) selection.coverage[index] = 255;
    }
}
void Guide::clear() {
    nodes.clear();
    segments.clear();
    curved_boundary.clear();
    closed = false;
    selection = {};
}
bool Guide::active() const {
    return nodes.size() >= 2 || !selection.coverage.empty();
}
double Guide::blocked(int x, int y) const {
    if (!selection.coverage.empty() && fill) {
        const int sx = x - selection.bounds.x, sy = y - selection.bounds.y;
        if (sx >= 0 && sy >= 0 && sx < selection.bounds.w && sy < selection.bounds.h) {
            return selection.coverage[static_cast<std::size_t>(sy) * selection.bounds.w + sx] / 255.0;
        }
        return 0;
    }
    if (nodes.size() < 2) {
        return 0;
    }
    const std::vector<Point>& outline = boundary();
    const Point point{x + 0.5, y + 0.5};
    if (fill && closed && inside_polygon(outline, point.x, point.y)) {
        return 1;
    }
    double distance = std::numeric_limits<double>::max();
    const std::size_t edges = closed ? outline.size() : outline.size() - 1;
    for (std::size_t index = 0; index < edges; ++index) {
        distance =
            std::min(distance, edge_distance(point, outline[index], outline[(index + 1) % outline.size()]));
    }
    return std::clamp(width * 0.5 + 0.5 - distance, 0.0, 1.0);
}
const std::vector<Point>& Guide::boundary() const {
    return segments.empty() ? nodes : curved_boundary;
}
void Guide::rebuild_boundary() {
    curved_boundary.clear();
    if (segments.empty() || nodes.empty()) {
        return;
    }
    curved_boundary.push_back(nodes.front());
    const std::size_t edges = closed ? nodes.size() : nodes.size() - 1;
    for (std::size_t edge = 0; edge < edges; ++edge) {
        bool curved = false;
        for (const GuideSegment& segment : segments) {
            if (segment.edge == edge) {
                const std::vector<Point> samples = segment.geometry.samples();
                curved_boundary.insert(curved_boundary.end(), samples.begin() + 1, samples.end());
                curved = true;
                break;
            }
        }
        if (!curved) {
            curved_boundary.push_back(nodes[(edge + 1) % nodes.size()]);
        }
    }
    if (closed && curved_boundary.size() > 1) {
        curved_boundary.pop_back();
    }
}
int Guide::swap_segment(std::size_t edge, CurveKind kind) {
    const std::size_t edges = nodes.size() < 2 ? 0 : closed ? nodes.size() : nodes.size() - 1;
    if (edge >= edges) {
        return -1;
    }
    int index = -1;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (segments[i].edge == edge) {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index >= 0 && segments[index].geometry.kind == kind) {
        return index;
    }
    if (index < 0) {
        index = static_cast<int>(segments.size());
        segments.push_back({edge, {}});
    }
    segments[index].geometry.kind = kind;
    segments[index].geometry.set_line(nodes[edge], nodes[(edge + 1) % nodes.size()]);
    selection = {};
    rebuild_boundary();
    return index;
}
void Guide::move_node(std::size_t index, Point point) {
    if (index >= nodes.size()) {
        return;
    }
    const Point previous = nodes[index];
    const Point delta{point.x - previous.x, point.y - previous.y};
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].x != previous.x || nodes[i].y != previous.y) {
            continue;
        }
        nodes[i] = point;
        for (GuideSegment& segment : segments) {
            if (segment.edge == i) {
                segment.geometry.start = point;
                segment.geometry.first_control.x += delta.x;
                segment.geometry.first_control.y += delta.y;
            }
            if ((segment.edge + 1) % nodes.size() == i) {
                segment.geometry.end = point;
                segment.geometry.second_control.x += delta.x;
                segment.geometry.second_control.y += delta.y;
            }
        }
    }
    selection = {};
    rebuild_boundary();
}
void Guide::move_handle(int segment, int handle, Point point) {
    if (segment < 0 || static_cast<std::size_t>(segment) >= segments.size()) {
        return;
    }
    segments[segment].geometry.move_handle(handle, point);
    selection = {};
    rebuild_boundary();
}
void Guide::translate(Point delta) {
    for (Point& point : nodes) {
        point.x += delta.x;
        point.y += delta.y;
    }
    for (GuideSegment& segment : segments) {
        for (Point* point : {&segment.geometry.start, &segment.geometry.end, &segment.geometry.first_control,
                             &segment.geometry.second_control}) {
            (*point).x += delta.x;
            (*point).y += delta.y;
        }
    }
    selection.bounds.x += static_cast<int>(std::lround(delta.x));
    selection.bounds.y += static_cast<int>(std::lround(delta.y));
    rebuild_boundary();
}
void constrain_paint(Image& image, const Image& base, const Guide& guide, bool preserve_alpha) {
    if (image.width != base.width || image.height != base.height || (!guide.active() && !preserve_alpha)) {
        return;
    }
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * image.width + x;
            Color color = interpolate_pixel(image.pixels[index], base.pixels[index], guide.blocked(x, y));
            if (preserve_alpha) {
                color.a = base.pixels[index].a;
                if (!color.a) {
                    color = base.pixels[index];
                }
            }
            image.pixels[index] = color;
        }
    }
}
void stencil_flood(Image& image, Point point, const Ink& ink, const Guide& guide, bool wrap,
                   const FloatingSelection* selection, const Gradient* gradient) {
    if (image.width < 1 || image.height < 1) {
        return;
    }
    int x = static_cast<int>(std::floor(point.x)), y = static_cast<int>(std::floor(point.y));
    if (wrap) {
        x = (x % image.width + image.width) % image.width;
        y = (y % image.height + image.height) % image.height;
    }
    const bool selected = selection && (*selection).active && (*selection).canvas_selection;
    if (!image.contains(x, y) || guide.blocked(x, y) >= 0.5 || (selected && !(*selection).contains(x, y))) {
        return;
    }
    const Color original = image.get(x, y);
    const MaterialSurface material(ink, ink.brush);
    std::vector<std::uint8_t> visited(image.pixels.size(), 0);
    std::queue<int> queue;
    queue.push(y * image.width + x);
    visited[y * image.width + x] = 1;
    int left = x, right = x, top = y, bottom = y;
    while (!queue.empty()) {
        const int index = queue.front();
        queue.pop();
        x = index % image.width;
        y = index / image.width;
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
        if (!gradient) {
            image.blend(x, y, material.sample(x, y, 32));
        }
        const int nx[] = {x - 1, x + 1, x, x}, ny[] = {y, y, y - 1, y + 1};
        for (int direction = 0; direction < 4; ++direction) {
            int px = nx[direction], py = ny[direction];
            if (wrap) {
                px = (px + image.width) % image.width;
                py = (py + image.height) % image.height;
            }
            if (!image.contains(px, py)) {
                continue;
            }
            const int next = py * image.width + px;
            if (visited[next] || guide.blocked(px, py) >= 0.5 ||
                (selected && !(*selection).contains(px, py)) || !equal(image.pixels[next], original)) {
                continue;
            }
            visited[next] = 1;
            queue.push(next);
        }
    }
    if (gradient) {
        const GradientSampler sampler(*gradient, {left, top, right - left + 1, bottom - top + 1});
        for (int py = top; py <= bottom; ++py) {
            for (int px = left; px <= right; ++px) {
                if (visited[static_cast<std::size_t>(py) * image.width + px]) {
                    image.blend(px, py, sampler.sample(px, py));
                }
            }
        }
    }
}
} // namespace paint

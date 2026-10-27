#include "document.hpp"
namespace paint {
std::vector<PathEdge> Document::path_edges() const {
    std::vector<PathEdge> result;
    for (std::size_t run = 0; run <= path.runs.size(); ++run) {
        if (run == path.runs.size() && !path.extending) {
            break;
        }
        const std::size_t first = run < path.runs.size() ? path.runs[run].start : path.start;
        const std::size_t count = run < path.runs.size() ? path.runs[run].count : path.nodes.size() - first;
        const bool closed = run < path.runs.size() ? !path.runs[run].continuous : !continuous_path;
        for (std::size_t index = 1; index < count; ++index) {
            result.push_back({first + index - 1, first + index});
        }
        if (closed && count > 2) {
            result.push_back({first + count - 1, first});
        }
    }
    return result;
}
int Document::swap_path_segment(PathEdge edge, CurveKind kind) {
    const std::vector<PathEdge> edges = path_edges();
    bool valid = false;
    for (std::size_t index = 0; index < edges.size(); ++index) {
        valid = valid || (edge.first == edges[index].first && edge.last == edges[index].last);
    }
    if (!valid) {
        return -1;
    }
    checkpoint();
    int selected = -1;
    for (std::size_t index = 0; index < path.segments.size(); ++index) {
        if (path.segments[index].first == edge.first && path.segments[index].last == edge.last) {
            selected = static_cast<int>(index);
            break;
        }
    }
    if (selected < 0) {
        path.segments.push_back({edge.first, edge.last, {}});
        selected = static_cast<int>(path.segments.size()) - 1;
    }
    CurveGeometry& geometry = path.segments[selected].geometry;
    geometry.kind = kind;
    geometry.set_line(path.nodes[edge.first], path.nodes[edge.last]);
    sync_path();
    return selected;
}
std::vector<Point> Document::path_contour(std::size_t start, std::size_t count, bool closed,
                                          double tolerance) const {
    std::vector<Point> result;
    if (count == 0 || start >= path.nodes.size() || count > path.nodes.size() - start) {
        return result;
    }
    result.push_back(path.nodes[start]);
    const std::size_t edges = count - 1 + (closed && count > 2 ? 1 : 0);
    for (std::size_t index = 0; index < edges; ++index) {
        const std::size_t first = start + index, last = start + (index + 1) % count;
        bool curved = false;
        for (std::size_t curve = 0; curve < path.segments.size(); ++curve) {
            const PathSegment& segment = path.segments[curve];
            if (segment.first == first && segment.last == last) {
                const std::vector<Point> samples = segment.geometry.samples(tolerance);
                if (samples.size() > 1) {
                    result.insert(result.end(), samples.begin() + 1, samples.end());
                }
                curved = true;
                break;
            }
        }
        if (!curved) {
            result.push_back(path.nodes[last]);
        }
    }
    return result;
}
} // namespace paint

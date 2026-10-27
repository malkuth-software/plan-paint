#include "spirograph.hpp"
#include "spiro_quad.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <unordered_set>
namespace paint {
namespace {
constexpr double tau = 2 * std::numbers::pi;
const QuadTrack& quad_track(int teeth) {
    static const QuadTrack ring96(96), ring105(105);
    return teeth == 105 ? ring105 : ring96;
}
// Joined circular arcs with positive curvature and a normalized 2*pi perimeter.
struct ArcPitch {
    const std::vector<double> normals, radii;
    const double half_width;
    std::vector<double> lengths;
    ArcPitch(std::vector<double> boundaries, std::vector<double> curvature, double width)
        : normals(std::move(boundaries)), radii(std::move(curvature)), half_width(width),
          lengths(radii.size() + 1, 0) {
        for (std::size_t i = 0; i < radii.size(); ++i) {
            lengths[i + 1] = lengths[i] + radii[i] * (normals[i + 1] - normals[i]);
        }
    }
    Point point(double normal) const {
        const double phase = normal - tau * std::floor(normal / tau);
        Point p{half_width, 0};
        for (std::size_t i = 0; i < radii.size(); ++i) {
            const double finish = std::min(phase, normals[i + 1]);
            if (finish > normals[i]) {
                p.x += radii[i] * (std::cos(finish) - std::cos(normals[i]));
                p.y += radii[i] * (std::sin(finish) - std::sin(normals[i]));
            }
        }
        return p;
    }
    double arc(double normal) const {
        const double turns = std::floor(normal / tau), phase = normal - turns * tau;
        for (std::size_t i = 0; i < radii.size(); ++i) {
            if (phase <= normals[i + 1]) {
                return turns * tau + lengths[i] + radii[i] * (phase - normals[i]);
            }
        }
        return (turns + 1) * tau;
    }
    double normal_at_arc(double length) const {
        const double turns = std::floor(length / tau), phase = length - turns * tau;
        for (std::size_t i = 0; i < radii.size(); ++i) {
            if (phase <= lengths[i + 1]) {
                return turns * tau + normals[i] + (phase - lengths[i]) / radii[i];
            }
        }
        return (turns + 1) * tau;
    }
    double curvature(double normal) const {
        const double phase = normal - tau * std::floor(normal / tau);
        for (std::size_t i = 0; i < radii.size(); ++i) {
            if (phase <= normals[i + 1]) {
                return radii[i];
            }
        }
        return radii.back();
    }
};
const ArcPitch& bar_pitch() {
    const double end_angle = .2;
    const double delta = (40 * std::numbers::pi / 2 - 60 * end_angle -
                         4 * (std::numbers::pi / 2 - end_angle)) / 76;
    const double alpha = std::numbers::pi / 2 - delta;
    static const ArcPitch pitch({0, end_angle, alpha, std::numbers::pi - alpha,
        std::numbers::pi - end_angle, std::numbers::pi + end_angle, std::numbers::pi + alpha,
        tau - alpha, tau - end_angle, tau}, {1.5, .1, 2, .1, 1.5, .1, 2, .1, 1.5},
        (60 * (1 - std::cos(end_angle)) + 4 * (std::cos(end_angle) - std::sin(delta)) +
         80 * std::sin(delta)) / 40);
    return pitch;
}
const ArcPitch& eye_pitch() {
    // Approximate lens: 80-unit sides and 4-unit rounded tips. The corner angle
    // sets the perimeter to exactly 60 tooth-radius units, matching 8/7 repeats.
    const double alpha = std::numbers::pi * 20 / 152;
    static const ArcPitch pitch({0, alpha, std::numbers::pi - alpha,
        std::numbers::pi + alpha, tau - alpha, tau},
        {4.0 / 60, 80.0 / 60, 4.0 / 60, 80.0 / 60, 4.0 / 60},
        (4 + 76 * std::cos(alpha)) / 60);
    return pitch;
}
const ArcPitch& triangle_pitch() {
    // Unequal rounded triangle, with 80-unit sides and 4-unit corners.
    // The perimeter constraint fixes c-a; closure fixes sin(a)+sin(c).
    const double b = 1.85, difference = std::numbers::pi * 50 / 76 - b;
    const double a = std::asin(std::sin(b) / (2 * std::cos(difference / 2))) - difference / 2;
    const double c = a + difference;
    static const ArcPitch pitch({0, a, b, std::numbers::pi - c,
        std::numbers::pi + c, tau - b, tau - a, tau},
        {4.0 / 54, 80.0 / 54, 4.0 / 54, 80.0 / 54, 4.0 / 54, 80.0 / 54, 4.0 / 54},
        (84 + 76 * (std::cos(a) - std::cos(b) - std::cos(c))) / 108);
    return pitch;
}
const ArcPitch& arc_pitch(SpiroProfileKind kind) {
    if (kind == SpiroProfileKind::Quad) {
        throw std::logic_error("Quad uses endpoint-pivot motion, not a convex arc map");
    }
    if (kind == SpiroProfileKind::Triangle) {
        return triangle_pitch();
    }
    return kind == SpiroProfileKind::Eye ? eye_pitch() : bar_pitch();
}
std::vector<Point> triangle_sockets() {
    return {{.70, 0}, {.50, -.18}, {.28, -.18}, {.30, 0}, {.28, .18}, {.50, .18},
            {-.48, -.30}, {-.70, -.30}, {-.48, 0}, {-.70, 0}, {-.48, .30}, {-.70, .30}};
}
std::vector<Point> eye_sockets() {
    return {{-.96, 0}, {-.78, 0}, {-.60, 0}, {-.42, 0}, {-.24, 0},
            {.06, .40}, {.10, .22}, {.25, .35}, {.30, .15}, {.45, .28},
            {.50, .08}, {.65, .20}, {.72, 0}};
}
std::vector<Point> bar_sockets() {
    std::vector<Point> result;
    for (int i = 0; i < 9; ++i) {
        result.push_back({-bar_pitch().half_width + (8 + 9.0 * i) / 40, 0});
    }
    return result;
}
std::array<double, 5> harmonics(const SpiroProfile& p) {
    return {p.oval, p.triangle, p.square, p.pentagon, p.hexagon};
}
Point rotated(Point p, double a) {
    return {p.x * std::cos(a) - p.y * std::sin(a), p.x * std::sin(a) + p.y * std::cos(a)};
}
std::vector<Point> sockets(int count) {
    std::vector<Point> result;
    for (int i = 0; i < count; ++i) {
        const double a = i * 2.399963229728653;
        const double r = .22 + .48 * i / std::max(1, count - 1);
        result.push_back({r * std::cos(a), r * std::sin(a)});
    }
    return result;
}
std::vector<Point> spiral_sockets(int teeth, int count) {
    // Approximate the kit's inward spiral, not dimensioned manufacturing coordinates.
    // Keep a constant radial step and roughly constant distance between adjacent holes.
    std::vector<Point> result;
    double angle = -std::numbers::pi / 2;
    for (int i = 0; i < count; ++i) {
        const double radius = teeth - 10.0 - 2 * i;
        result.push_back({radius * std::cos(angle) / teeth, radius * std::sin(angle) / teeth});
        if (i + 1 < count) {
            angle += 2 * std::asin(std::sqrt(117 / (4 * radius * (radius - 2))));
        }
    }
    return result;
}
} // namespace
bool SpiroProfile::circular() const {
    return kind == SpiroProfileKind::Harmonic && oval == 0 && triangle == 0 && square == 0 &&
           pentagon == 0 && hexagon == 0;
}
double SpiroProfile::support(double normal) const {
    if (kind != SpiroProfileKind::Harmonic) {
        const Point p = arc_pitch(kind).point(normal);
        return p.x * std::cos(normal) + p.y * std::sin(normal);
    }
    if (circular()) {
        return 1;
    }
    double value = 1;
    const std::array<double, 5> coefficients = harmonics(*this);
    for (int i = 0; i < 5; ++i) {
        value += coefficients[i] * std::cos((i + 2) * normal);
    }
    return value;
}
double SpiroProfile::derivative(double normal) const {
    if (kind != SpiroProfileKind::Harmonic) {
        const Point p = arc_pitch(kind).point(normal);
        return -p.x * std::sin(normal) + p.y * std::cos(normal);
    }
    if (circular()) {
        return 0;
    }
    double value = 0;
    const std::array<double, 5> coefficients = harmonics(*this);
    for (int i = 0; i < 5; ++i) {
        value -= coefficients[i] * (i + 2) * std::sin((i + 2) * normal);
    }
    return value;
}
double SpiroProfile::curvature_radius(double normal) const {
    if (kind != SpiroProfileKind::Harmonic) {
        return arc_pitch(kind).curvature(normal);
    }
    if (circular()) {
        return 1;
    }
    double value = 1;
    const std::array<double, 5> coefficients = harmonics(*this);
    for (int i = 0; i < 5; ++i) {
        const double n = i + 2;
        value += coefficients[i] * (1 - n * n) * std::cos(n * normal);
    }
    return value;
}
double SpiroProfile::arc(double normal) const {
    if (kind != SpiroProfileKind::Harmonic) {
        return arc_pitch(kind).arc(normal);
    }
    if (circular()) {
        return normal;
    }
    double value = normal;
    const std::array<double, 5> coefficients = harmonics(*this);
    for (int i = 0; i < 5; ++i) {
        const double n = i + 2;
        value += coefficients[i] * (1 - n * n) / n * std::sin(n * normal);
    }
    return value;
}
double SpiroProfile::normal_at_arc(double length) const {
    if (kind != SpiroProfileKind::Harmonic) {
        return arc_pitch(kind).normal_at_arc(length);
    }
    if (circular()) {
        return length;
    }
    // Monotone arc map; retain complete turns so the wheel phase cannot jump at a seam.
    const double turns = std::floor(length / tau), local = length - turns * tau;
    double lo = 0, hi = tau, x = local;
    for (int i = 0; i < 24; ++i) {
        const double residual = arc(x) - local;
        if (std::abs(residual) < 1e-13) {
            break;
        }
        if (residual < 0) {
            lo = x;
        } else {
            hi = x;
        }
        const double next = x - residual / curvature_radius(x);
        x = next > lo && next < hi ? next : (lo + hi) * .5;
    }
    return x + turns * tau;
}
Point SpiroProfile::point(double normal) const {
    if (kind == SpiroProfileKind::Quad) {
        return QuadTrack::boundary(normal);
    }
    if (kind != SpiroProfileKind::Harmonic) {
        return arc_pitch(kind).point(normal);
    }
    const double h = support(normal), d = derivative(normal);
    return {h * std::cos(normal) - d * std::sin(normal), h * std::sin(normal) + d * std::cos(normal)};
}
SpiroCurvature SpiroProfile::curvature_bounds() const {
    if (kind != SpiroProfileKind::Harmonic) {
        const ArcPitch& pitch = arc_pitch(kind);
        return {*std::min_element(pitch.radii.begin(), pitch.radii.end()),
                *std::max_element(pitch.radii.begin(), pitch.radii.end())};
    }
    double difference = 0;
    const std::array<double, 5> coefficients = harmonics(*this);
    for (int i = 0; i < 5; ++i) {
        difference += std::abs(coefficients[i]) * ((i + 2) * (i + 2) - 1);
    }
    return {1 - difference, 1 + difference};
}
const std::vector<SpiroGuide>& spiro_guides() {
    static const std::vector<SpiroGuide> parts{{"Ring 144/96", 96, {}, false, 144},
                                               {"Ring 120", 120},
                                               {"Ring 144", 144},
                                               {"Ring 180", 180},
                                               {"Ring 210", 210},
                                               {"Ring 240", 240},
                                               {"Oval 240", 240, {.18}},
                                               {"Long oval 300", 300, {.26}},
                                               {"Triangle 288", 288, {0, .07}},
                                               {"Square 288", 288, {0, 0, .036}},
                                               {"Pentagon 300", 300, {0, 0, 0, .022}},
                                               {"Hexagon 360", 360, {0, 0, 0, 0, .015}},
                                               {"Egg 288", 288, {.10, .035}},
                                               {"Shield 300", 300, {-.09, .035, .012}},
                                               {"Rack 96", 96, {}, true},
                                               {"Long rack 144", 144, {}, true},
                                               {"Ring 150/105", 105, {}, false, 150},
                                               {"Rack 150", 150, {}, false, 0, 25}};
    return parts;
}
const std::vector<SpiroInsert>& spiro_inserts() {
    static const std::vector<SpiroInsert> parts{
        {"Wheel 36", 36, {{.28, 0}, {.54, 0}, {.8, 0}, {-.4, .4}, {-.5, -.3}}},
        {"Wheel 40", 40, spiral_sockets(40, 13)},
        {"Wheel 48", 48, spiral_sockets(48, 17)},
        {"Wheel 12", 12, sockets(3)},
        {"Wheel 18", 18, sockets(4)},
        {"Wheel 24", 24, spiral_sockets(24, 5)},
        {"Wheel 30", 30, spiral_sockets(30, 8)},
        {"Prime 31", 31, sockets(6)},
        {"Prime 37", 37, sockets(7)},
        {"Wheel 45", 45, spiral_sockets(45, 16)},
        {"Wheel 56", 56, spiral_sockets(56, 21)},
        {"Wheel 60", 60, spiral_sockets(60, 23)},
        {"Wheel 64", 64, sockets(8)},
        {"Wheel 72", 72, spiral_sockets(72, 29)},
        {"Wheel 80", 80, spiral_sockets(80, 33)},
        {"Oval 36", 36, sockets(6), {.18}},
        {"Long oval 40", 40, sockets(6), {.25}},
        {"Triangle 42", 42, sockets(7), {0, .07}},
        {"Square 48", 48, sockets(7), {0, 0, .036}},
        {"Pentagon 48", 48, sockets(7), {0, 0, 0, .022}},
        {"Hexagon 48", 48, sockets(8), {0, 0, 0, 0, .015}},
        {"Egg 40", 40, sockets(6), {.10, .035}},
        {"Shield 42", 42, sockets(7), {-.09, .035, .012}},
        {"Wheel 32", 32, spiral_sockets(32, 9)},
        {"Wheel 42", 42, spiral_sockets(42, 14)},
        {"Wheel 52", 52, spiral_sockets(52, 19)},
        {"Wheel 63", 63, spiral_sockets(63, 25)},
        {"Wheel 75", 75, spiral_sockets(75, 31)},
        {"Wheel 84", 84, spiral_sockets(84, 35)},
        {"Bar 40", 40, bar_sockets(), {0, 0, 0, 0, 0, SpiroProfileKind::Bar}},
        {"Eye 60", 60, eye_sockets(), {0, 0, 0, 0, 0, SpiroProfileKind::Eye}},
        {"Triangle 54", 54, triangle_sockets(), {0, 0, 0, 0, 0, SpiroProfileKind::Triangle}},
        {"Quad 60", 60, {{.80, 0}, {.60, 0}, {.40, 0}, {.20, 0}, {0, -.25},
                         {0, .25}, {0, .50}, {0, .75}, {-.25, 0}, {-.50, 0}},
         {0, 0, 0, 0, 0, SpiroProfileKind::Quad}}};
    return parts;
}
void Spirograph::open(int width, int height) {
    *this = {};
    active = true;
    center = {width * .5, height * .5};
    scale = std::clamp(std::min(width, height) * .38 / 144.0, .12, 3.0);
}
void Spirograph::set_insert(int index) {
    if (index < 0 || index >= static_cast<int>(spiro_inserts().size())) {
        throw std::out_of_range("Spirograph insert");
    }
    if (!compatible(guide, index)) {
        return;
    }
    insert = index;
    selected_peg = -1;
    inserted = true;
    detached = false;
    angle = 0;
    rolling_offset = 0;
    pegs = {};
}
void Spirograph::remove_insert() {
    inserted = false;
    detached = false;
    selected_peg = -1;
    pegs = {};
}
void Spirograph::set_guide(int index) {
    if (index < 0 || index >= static_cast<int>(spiro_guides().size())) {
        throw std::out_of_range("Spirograph guide");
    }
    if (!compatible(index, insert)) {
        return;
    }
    const SpiroGuide& next = spiro_guides()[index];
    set_scale(guide_body_radius() / (next.outside_teeth ? next.outside_teeth : next.teeth));
    guide = index;
    detached = false;
    angle = 0;
    rolling_offset = 0;
}
double Spirograph::guide_radius() const {
    return guide_teeth() * scale;
}
int Spirograph::guide_teeth() const {
    const SpiroGuide& frame = spiro_guides().at(guide);
    return outside && frame.outside_teeth ? frame.outside_teeth : frame.teeth;
}
double Spirograph::guide_body_radius() const {
    const SpiroGuide& frame = spiro_guides().at(guide);
    return (frame.outside_teeth ? frame.outside_teeth : frame.teeth) * scale;
}
void Spirograph::set_scale(double value) {
    if (!std::isfinite(value) || value <= 0) {
        throw std::invalid_argument("Spirograph scale must be positive and finite.");
    }
    scale = std::clamp(value, .05, 64.0);
}
double Spirograph::wheel_radius() const {
    if (quad()) {
        return quad_track(guide_teeth()).radius() * scale;
    }
    return spiro_inserts().at(insert).teeth * scale;
}
bool Spirograph::quad() const {
    return spiro_inserts().at(insert).profile.kind == SpiroProfileKind::Quad;
}
bool Spirograph::rack() const {
    return spiro_guides().at(guide).rack;
}
bool Spirograph::capsule() const {
    return spiro_guides().at(guide).cap_radius > 0;
}
bool Spirograph::exterior() const {
    return outside || rack() || capsule();
}
bool Spirograph::compatible(int g, int w) const {
    if (spiro_inserts().at(w).profile.kind == SpiroProfileKind::Quad) {
        return !outside && (g == 0 || g == 16);
    }
    if (outside || spiro_guides().at(g).rack || spiro_guides().at(g).cap_radius > 0) {
        return true;
    }
    // A conservative curvature bound prevents an insert from cutting across its frame.
    const double lower = spiro_guides().at(g).profile.curvature_bounds().minimum;
    const double upper = spiro_inserts().at(w).profile.curvature_bounds().maximum;
    const double minimum = lower * spiro_guides().at(g).teeth, maximum = upper * spiro_inserts().at(w).teeth;
    return minimum > maximum + 1;
}
Point Spirograph::guide_point(double normal) const {
    if (capsule()) {
        const double radius = spiro_guides()[guide].cap_radius * scale;
        const double half = std::numbers::pi * (guide_radius() - radius) / 2;
        const double arc = (normal - tau * std::floor(normal / tau)) * guide_radius();
        const double quarter = std::numbers::pi * radius / 2;
        if (arc < quarter) {
            return {center.x + half + radius * std::cos(arc / radius),
                    center.y + radius * std::sin(arc / radius)};
        }
        if (arc < quarter + 2 * half) {
            return {center.x + half - (arc - quarter), center.y + radius};
        }
        if (arc < 3 * quarter + 2 * half) {
            const double angle = std::numbers::pi / 2 + (arc - quarter - 2 * half) / radius;
            return {center.x - half + radius * std::cos(angle), center.y + radius * std::sin(angle)};
        }
        if (arc < 3 * quarter + 4 * half) {
            return {center.x - half + (arc - 3 * quarter - 2 * half), center.y - radius};
        }
        const double angle = 3 * std::numbers::pi / 2 + (arc - 3 * quarter - 4 * half) / radius;
        return {center.x + half + radius * std::cos(angle), center.y + radius * std::sin(angle)};
    }
    const Point p = rack() ? Point{normal, 0} : spiro_guides().at(guide).profile.point(normal);
    return {center.x + guide_radius() * p.x, center.y + guide_radius() * p.y};
}
double Spirograph::guide_normal(double phase) const {
    if (rack()) {
        return std::numbers::pi / 2;
    }
    if (!capsule()) {
        return phase + (outside ? std::numbers::pi : 0);
    }
    const double radius = spiro_guides()[guide].cap_radius / guide_teeth();
    const double half = std::numbers::pi * (1 - radius) / 2;
    const double turns = std::floor(phase / tau), arc = phase - turns * tau;
    const double quarter = std::numbers::pi * radius / 2;
    double normal = 0;
    if (arc < quarter) {
        normal = arc / radius;
    } else if (arc < quarter + 2 * half) {
        normal = std::numbers::pi / 2;
    } else if (arc < 3 * quarter + 2 * half) {
        normal = std::numbers::pi / 2 + (arc - quarter - 2 * half) / radius;
    } else if (arc < 3 * quarter + 4 * half) {
        normal = 3 * std::numbers::pi / 2;
    } else {
        normal = 3 * std::numbers::pi / 2 + (arc - 3 * quarter - 4 * half) / radius;
    }
    return normal + turns * tau + std::numbers::pi;
}
double Spirograph::guide_arc(double phase) const {
    return rack() || capsule() ? phase : spiro_guides().at(guide).profile.arc(phase);
}
Point Spirograph::close_position() const {
    if (capsule()) {
        const double cap = spiro_guides()[guide].cap_radius * scale;
        return {center.x + std::numbers::pi * (guide_radius() - cap) / 2 + cap + 9 * scale,
                center.y - cap - 12 * scale};
    }
    Point p = guide_point(rack() ? -1.5 : -std::numbers::pi / 4);
    if (spiro_guides()[guide].outside_teeth) {
        p = {center.x + guide_body_radius() / std::sqrt(2.0),
             center.y - guide_body_radius() / std::sqrt(2.0)};
    }
    return {p.x + (rack() ? 0 : 9 * scale), p.y - 12 * scale};
}
Point Spirograph::resize_position() const {
    if (capsule()) {
        const double cap = spiro_guides()[guide].cap_radius * scale;
        return {center.x + std::numbers::pi * (guide_radius() - cap) / 2 + cap + 9 * scale,
                center.y + cap + 12 * scale};
    }
    Point point = guide_point(rack() ? 1.5 : std::numbers::pi / 4);
    if (spiro_guides()[guide].outside_teeth) {
        point = {center.x + guide_body_radius() / std::sqrt(2.0),
                 center.y + guide_body_radius() / std::sqrt(2.0)};
    }
    return {point.x + 9 * scale, point.y + 12 * scale};
}
double Spirograph::wheel_rotation(double phase) const {
    if (quad()) {
        return quad_track(guide_teeth()).pose(phase, rolling_offset).rotation;
    }
    const double length = guide_arc(phase);
    const double sign = exterior() ? -1 : 1;
    const double normal =
        spiro_inserts().at(insert).profile.normal_at_arc(sign * guide_radius() / wheel_radius() * length +
                                                       rolling_offset);
    return guide_normal(phase) - normal;
}
Point Spirograph::wheel_center(double phase) const {
    if (detached) {
        return detached_center;
    }
    if (quad()) {
        const Point p = quad_track(guide_teeth()).pose(phase, rolling_offset).center;
        return {center.x + scale * p.x, center.y + scale * p.y};
    }
    return wheel_center(phase, wheel_rotation(phase));
}
Point Spirograph::wheel_center(double phase, double rotation) const {
    if (quad()) {
        const QuadTrack& track = quad_track(guide_teeth());
        const Point p = track.pose(phase, track.offset_for_rotation(phase, rotation)).center;
        return {center.x + scale * p.x, center.y + scale * p.y};
    }
    const double normal =
        guide_normal(phase) - rotation;
    const Point p = rotated(spiro_inserts().at(insert).profile.point(normal), rotation);
    const Point contact = guide_point(phase);
    return {contact.x - wheel_radius() * p.x, contact.y - wheel_radius() * p.y};
}
void Spirograph::reposition(double phase) {
    if (!std::isfinite(phase) || !std::isfinite(angle) || std::abs(phase - angle) > 100 * std::numbers::pi) {
        throw std::invalid_argument("Invalid spirograph placement");
    }
    if (!active || !inserted) {
        return;
    }
    if (rack()) {
        phase = std::clamp(phase, -1.5, 1.5);
    }
    const double rotation = wheel_rotation(angle);
    if (quad()) {
        rolling_offset = quad_track(guide_teeth()).offset_for_rotation(phase, rotation);
        angle = phase;
        detached = false;
        return;
    }
    const double normal = guide_normal(phase) - rotation;
    const double length = guide_arc(phase);
    const double sign = exterior() ? -1 : 1;
    rolling_offset = spiro_inserts().at(insert).profile.arc(normal) - sign * guide_radius() / wheel_radius() * length;
    angle = phase;
    detached = false;
}
void Spirograph::lift_to(Point point, double tolerance) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(tolerance) || tolerance < 0) {
        throw std::invalid_argument("Invalid spirograph lift position");
    }
    if (!active || !inserted) {
        return;
    }
    const double phase = project(point, angle, true);
    const Point contact_center = wheel_center(phase, wheel_rotation(angle));
    if (std::hypot(point.x - contact_center.x, point.y - contact_center.y) <= tolerance) {
        reposition(phase);
    } else {
        detached = true;
        detached_center = point;
    }
}
double Spirograph::project(Point point, double near_phase, bool lifted) const {
    const bool circle = spiro_inserts()[insert].profile.circular();
    const double rotation = wheel_rotation(angle);
    if (rack() && (circle || lifted)) {
        const Point origin = wheel_center(0, rotation);
        return std::clamp((point.x - origin.x) / guide_radius(), -1.5, 1.5);
    }
    if (!rack() && !capsule() && circle && spiro_guides()[guide].profile.circular()) {
        return near_phase +
               std::remainder(std::atan2(point.y - center.y, point.x - center.x) - near_phase, tau);
    }
    // Track the nearest local center-path position. Search around the last contact
    // rather than jumping to a remote branch of a non-circular wheel's orbit.
    double best = near_phase, distance = 1e30;
    constexpr int samples = 48;
    for (int i = -samples; i <= samples; ++i) {
        const double a = rack() ? 1.5 * i / samples : near_phase + std::numbers::pi * i / samples;
        const Point c = lifted ? wheel_center(a, rotation) : wheel_center(a);
        const double d = std::hypot(c.x - point.x, c.y - point.y);
        if (d < distance) {
            distance = d;
            best = a;
        }
    }
    double lo = best - std::numbers::pi / samples, hi = best + std::numbers::pi / samples;
    if (rack()) {
        lo = std::max(-1.5, lo);
        hi = std::min(1.5, hi);
    }
    for (int i = 0; i < 40; ++i) {
        const double a = (2 * lo + hi) / 3, b = (lo + 2 * hi) / 3;
        const Point ca = lifted ? wheel_center(a, rotation) : wheel_center(a);
        const Point cb = lifted ? wheel_center(b, rotation) : wheel_center(b);
        if (std::hypot(ca.x - point.x, ca.y - point.y) < std::hypot(cb.x - point.x, cb.y - point.y)) {
            hi = b;
        } else {
            lo = a;
        }
    }
    return (lo + hi) * .5;
}
bool Spirograph::select_peg(int index) {
    selected_peg = index >= 0 && index < hole_count() && pegs[index].seated ? index : -1;
    return selected_peg >= 0;
}
bool Spirograph::assign_brush(Brush brush) {
    if (!select_peg(selected_peg)) {
        return false;
    }
    select_brush(pegs[selected_peg].effect, brush);
    refresh_peg(selected_peg);
    return true;
}
Ink Spirograph::peg_ink(int index) const {
    const SpiroPeg& peg = pegs.at(index);
    Ink ink = peg.effect;
    ink.primary = peg.ink;
    ink.size = peg.width;
    ink.alternate.reset();
    return ink;
}
void Spirograph::refresh_peg(int index) {
    if (index < 0 || index >= hole_count() || !pegs[index].seated) {
        return;
    }
    SpiroPeg& peg = pegs[index];
    peg.preview.reset(20, 16, {0, 0, 0, 0});
    Ink ink = peg_ink(index);
    if (!peg.loaded) {
        ink.primary = {90, 115, 145, 255};
    }
    ink.size = 5;
    DynamicBrushStroke brush;
    MaterialStroke round;
    for (int i = 0; i < 12; ++i) {
        const Point a{4.0 + i, 8 + 2 * std::sin(i * .4)}, b{5.0 + i, 8 + 2 * std::sin((i + 1) * .4)};
        if (ink.brush == Brush::Round) {
            round.segment(peg.preview, a, b, ink);
        } else {
            brush.segment(peg.preview, a, b, ink, peg.glitter);
        }
    }
}
Point Spirograph::hole(int index, double phase) const {
    const Point local = spiro_inserts().at(insert).holes.at(index);
    const Point wheel = wheel_center(phase);
    const double rotation = wheel_rotation(phase), c = std::cos(rotation), s = std::sin(rotation),
                 r = wheel_radius();
    return {wheel.x + r * (c * local.x - s * local.y), wheel.y + r * (s * local.x + c * local.y)};
}
int Spirograph::hole_count() const {
    return inserted ? static_cast<int>(spiro_inserts().at(insert).holes.size()) : 0;
}
int Spirograph::closing_turns() const {
    if (rack()) {
        return 0;
    }
    const int outer = guide_teeth(), inner = spiro_inserts().at(insert).teeth;
    return inner / std::gcd(outer, inner);
}
bool Spirograph::loaded() const {
    if (!active || !inserted) {
        return false;
    }
    for (int i = 0; i < hole_count(); ++i) {
        if (pegs[i].seated && pegs[i].loaded && pegs[i].ink.a) {
            return true;
        }
    }
    return false;
}
bool Spirograph::seat(int index, SpiroPeg peg) {
    if (!active || index < 0 || index >= hole_count() || pegs[index].seated) {
        return false;
    }
    peg.seated = true;
    peg.width = std::clamp(peg.width, 1, 256);
    pegs[index] = peg;
    refresh_peg(index);
    return true;
}
bool Spirograph::fill(int index, Color ink) {
    if (index < 0 || index >= hole_count() || !pegs[index].seated) {
        return false;
    }
    pegs[index].ink = ink;
    pegs[index].loaded = ink.a != 0;
    refresh_peg(index);
    return true;
}
std::vector<SpiroTrace> Spirograph::advance(double target) {
    if (detached) {
        return {};
    }
    if (!std::isfinite(target) || !std::isfinite(angle) || !std::isfinite(scale) || scale <= 0 ||
        std::abs(target - angle) > 100 * std::numbers::pi) {
        throw std::invalid_argument("Invalid spirograph motion");
    }
    std::vector<SpiroTrace> result;
    if (!active || !inserted) {
        return result;
    }
    if (rack()) {
        target = std::clamp(target, -1.5, 1.5);
    }
    const double delta = target - angle;
    // Bound pen travel to half an image pixel, including the insert's counter-rotation.
    const SpiroProfile& gp = spiro_guides()[guide].profile;
    const SpiroProfile& ip = spiro_inserts()[insert].profile;
    const double guide_curvature = gp.curvature_bounds().maximum;
    const double insert_curvature = quad() ? 1 : ip.curvature_bounds().minimum;
    const double speed = quad() ? 6 * guide_radius() : capsule()
                             ? 2.5 * (wheel_radius() * guide_teeth() / spiro_guides()[guide].cap_radius +
                                      guide_radius() / insert_curvature)
                         : gp.circular() && ip.circular() && !rack()
                             ? 2 * (guide_radius() + (outside ? wheel_radius() : -wheel_radius()))
                             : 2.5 * (wheel_radius() + guide_radius() * guide_curvature / insert_curvature);
    const double count = std::ceil(std::abs(delta) * speed / .5);
    if (!std::isfinite(count) || count > 2000000) {
        throw std::invalid_argument("Spirograph motion too large");
    }
    const int steps = std::max(1, static_cast<int>(count));
    if (loaded() && std::abs(delta) > 1e-12) {
        for (int i = 1; i <= steps; ++i) {
            const double a = angle + delta * (i - 1) / steps, b = angle + delta * i / steps;
            const Point ac = wheel_center(a), bc = wheel_center(b);
            const double ar = wheel_rotation(a), br = wheel_rotation(b), r = wheel_radius();
            for (int h = 0; h < hole_count(); ++h) {
                const SpiroPeg& peg = pegs[h];
                if (peg.seated && peg.loaded && peg.ink.a) {
                    const Point local = spiro_inserts()[insert].holes[h];
                    const Point pa = rotated(local, ar), pb = rotated(local, br);
                    result.push_back({{ac.x + r * pa.x, ac.y + r * pa.y},
                                      {bc.x + r * pb.x, bc.y + r * pb.y},
                                      peg.ink,
                                      peg.width,
                                      h});
                }
            }
        }
    }
    angle = target;
    return result;
}
void SpirographStroke::clear() {
    scratch_ = {};
    for (int i = 0; i < spiro_max_holes; ++i) {
        std::unordered_map<int, Color> released;
        layers_[i].swap(released);
        material_[i].clear();
        dynamic_[i].clear();
    }
}
Rect SpirographStroke::render(Image& image, const Image& base, const Spirograph& apparatus,
                              const std::vector<SpiroTrace>& traces) {
    if (traces.empty()) {
        return {};
    }
    if (base.width != image.width || base.height != image.height ||
        base.pixels.size() != image.pixels.size()) {
        throw std::invalid_argument("Spirograph stroke base does not match the image");
    }
    if (scratch_.width != image.width || scratch_.height != image.height) {
        clear();
        scratch_.reset(image.width, image.height, {0, 0, 0, 0});
    }
    std::unordered_set<int> affected;
    int left = image.width, top = image.height, right = 0, bottom = 0;
    for (const SpiroTrace& trace : traces) {
        const double margin = trace.width + 2;
        const int x0 = std::clamp(static_cast<int>(std::floor(std::min(trace.start.x, trace.end.x) - margin)),
                                  0, image.width);
        const int y0 = std::clamp(static_cast<int>(std::floor(std::min(trace.start.y, trace.end.y) - margin)),
                                  0, image.height);
        const int x1 = std::clamp(static_cast<int>(std::ceil(std::max(trace.start.x, trace.end.x) + margin)),
                                  0, image.width);
        const int y1 = std::clamp(static_cast<int>(std::ceil(std::max(trace.start.y, trace.end.y) + margin)),
                                  0, image.height);
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }
        std::unordered_map<int, Color>& layer = layers_.at(trace.peg);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const int index = y * image.width + x;
                const std::unordered_map<int, Color>::const_iterator found = layer.find(index);
                scratch_.pixels[index] = found == layer.end() ? Color{0, 0, 0, 0} : (*found).second;
            }
        }
        const Ink ink = apparatus.peg_ink(trace.peg);
        if (ink.brush == Brush::Round) {
            material_[trace.peg].segment(scratch_, trace.start, trace.end, ink);
        } else {
            dynamic_[trace.peg].segment(scratch_, trace.start, trace.end, ink,
                                        apparatus.pegs[trace.peg].glitter);
        }
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const int index = y * image.width + x;
                const Color color = scratch_.pixels[index];
                const std::unordered_map<int, Color>::const_iterator previous = layer.find(index);
                if (color.a && (previous == layer.end() || !equal((*previous).second, color))) {
                    layer[index] = color;
                    affected.insert(index);
                }
            }
        }
        left = std::min(left, x0);
        top = std::min(top, y0);
        right = std::max(right, x1);
        bottom = std::max(bottom, y1);
    }
    for (int index : affected) {
        const int x = index % image.width, y = index / image.width;
        image.pixels[index] = base.pixels[index];
        for (const std::unordered_map<int, Color>& layer : layers_) {
            const std::unordered_map<int, Color>::const_iterator found = layer.find(index);
            if (found != layer.end()) {
                image.blend(x, y, (*found).second);
            }
        }
    }
    return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
}
} // namespace paint

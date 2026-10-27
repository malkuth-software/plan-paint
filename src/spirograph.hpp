#pragma once
#include "paint_tools.hpp"
#include <array>
namespace paint {
constexpr int spiro_max_holes = 35;
constexpr int spiro_default_insert = 26; // Wheel 63 in the stable component catalog.
enum class SpiroProfileKind { Harmonic, Bar, Eye, Triangle, Quad };
struct SpiroCurvature {
    double minimum, maximum;
};
// Convex pitch curves use outward normal and exact arc length. Quad uses only
// point() as a boundary parameter; its nonconvex rolling model is QuadTrack.
struct SpiroProfile {
    double oval = 0, triangle = 0, square = 0, pentagon = 0, hexagon = 0;
    SpiroProfileKind kind = SpiroProfileKind::Harmonic;
    bool circular() const;
    double support(double normal) const;
    double derivative(double normal) const;
    double curvature_radius(double normal) const;
    double arc(double normal) const;
    double normal_at_arc(double arc) const;
    Point point(double normal) const;
    SpiroCurvature curvature_bounds() const;
};
struct SpiroInsert {
    const char* name;
    int teeth;
    std::vector<Point> holes;
    SpiroProfile profile{};
};
struct SpiroGuide {
    const char* name;
    int teeth;
    SpiroProfile profile{};
    bool rack = false;
    int outside_teeth = 0;
    // Rounded-end rack radius in tooth-radius units; zero uses the support profile.
    double cap_radius = 0;
};
const std::vector<SpiroGuide>& spiro_guides();
const std::vector<SpiroInsert>& spiro_inserts();
struct SpiroPeg {
    bool seated = false, loaded = false;
    Color ink{};
    int width = 1;
    Ink effect{};
    bool glitter = false;
    Image preview{};
};
struct SpiroTrace {
    Point start, end;
    Color ink;
    int width;
    int peg = 0;
};
struct Spirograph {
    bool active = false, inserted = true, detached = false;
    int guide = 0, insert = spiro_default_insert, selected_peg = -1;
    bool outside = false;
    double scale = 1.35, angle = 0, rolling_offset = 0;
    Point center{}, detached_center{};
    std::array<SpiroPeg, spiro_max_holes> pegs{};
    void open(int width, int height);
    void set_insert(int index);
    void remove_insert();
    void set_guide(int index);
    // Resize only the apparatus; phase, pen widths and loaded ink stay unchanged.
    void set_scale(double value);
    double guide_radius() const;
    int guide_teeth() const;
    double guide_body_radius() const;
    double wheel_radius() const;
    bool quad() const;
    Point guide_point(double normal) const;
    double guide_normal(double phase) const;
    double guide_arc(double phase) const;
    bool capsule() const;
    bool exterior() const;
    Point close_position() const;
    Point resize_position() const;
    double project(Point point, double near_phase, bool lifted = false) const;
    // Move along the guide without rolling or drawing; resume rolling from the new contact.
    void reposition(double phase);
    // Lift freely; seat only when the center is within tolerance of the track.
    void lift_to(Point point, double tolerance);
    bool rack() const;
    bool compatible(int guide_index, int insert_index) const;
    bool select_peg(int index);
    bool assign_brush(Brush brush);
    Ink peg_ink(int index) const;
    void refresh_peg(int index);
    double wheel_rotation(double phase) const;
    Point wheel_center(double phase) const;
    Point wheel_center(double phase, double rotation) const;
    Point hole(int index, double phase) const;
    int hole_count() const;
    int closing_turns() const;
    bool loaded() const;
    bool seat(int hole_index, SpiroPeg peg);
    bool fill(int hole_index, Color ink);
    std::vector<SpiroTrace> advance(double target);
};
// Sparse independent coats give crossings a stable peg order without a canvas per hole.
class SpirographStroke {
    Image scratch_;
    std::array<std::unordered_map<int, Color>, spiro_max_holes> layers_;
    std::array<MaterialStroke, spiro_max_holes> material_;
    std::array<DynamicBrushStroke, spiro_max_holes> dynamic_;

  public:
    void clear();
    Rect render(Image& image, const Image& base, const Spirograph& apparatus,
                const std::vector<SpiroTrace>& traces);
};
} // namespace paint

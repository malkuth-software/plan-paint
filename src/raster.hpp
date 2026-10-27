#pragma once
#include "image.hpp"
#include "geometry_style.hpp"
#include <memory>
#include <unordered_map>
namespace paint {
class EraserStroke {
    struct Pixel {
        Color original;
        double strength = 0;
    };
    std::unordered_map<int, Pixel> pixels_;

  public:
    void clear();
    void segment(Image& image, Point start, Point end, double diameter, bool soft);
};

enum class Pattern {
    Solid,
    Dither12,
    Dither25,
    Dither37,
    Dither50,
    Dither62,
    Dither75,
    Dither87,
    Horizontal,
    Vertical,
    Diagonal,
    Crosshatch,
    Checker,
    Bricks,
    Weave,
    Houndstooth,
    Dots,
    Waves,
    None,
    Buttons,
    CargoNet,
    Circuits,
    Cobblestones,
    StrawMat,
    Custom
};
inline constexpr int pattern_count = 25;
enum class Brush {
    Round,
    Calligraphy,
    CalligraphyLeft,
    Airbrush,
    Oil,
    Crayon,
    Marker,
    Pencil,
    Watercolor,
    Bristle,
    Pastel,
    Charcoal,
    Gel
};
inline constexpr int brush_count = 13;
enum class Shape {
    Line,
    Bezier,
    Oval,
    Rectangle,
    RoundedRectangle,
    Polygon,
    Triangle,
    RightTriangle,
    Diamond,
    Pentagon,
    Hexagon,
    RightArrow,
    LeftArrow,
    UpArrow,
    DownArrow,
    Star4,
    Star5,
    Star6,
    RoundedCallout,
    OvalCallout,
    CloudCallout,
    Heart,
    Lightning,
    Circle,
    Octagon,
    Trapezoid,
    Parallelogram,
    Chevron,
    DoubleArrow,
    Cross,
    Gear,
    Crescent,
    Teardrop,
    Leaf,
    Star8,
    Burst,
    Arc
};
inline constexpr int shape_count = 37;
enum class StampShape {
    Circle,
    Pill,
    Square,
    Rectangle,
    Line,
    Bezier,
    Oval,
    RoundedRectangle,
    Polygon,
    Triangle,
    RightTriangle,
    Diamond,
    Pentagon,
    Hexagon,
    RightArrow,
    LeftArrow,
    UpArrow,
    DownArrow,
    Star4,
    Star5,
    Star6,
    RoundedCallout,
    OvalCallout,
    CloudCallout,
    Heart,
    Lightning,
    Octagon,
    Trapezoid,
    Parallelogram,
    Chevron,
    DoubleArrow,
    Cross,
    Gear,
    Crescent,
    Teardrop,
    Leaf,
    Star8,
    Burst,
    Arc
};
inline constexpr int stamp_shape_count = 39;
const char* stamp_shape_name(StampShape shape);
Shape stamp_geometry_shape(StampShape shape);
std::vector<Point> stamp_outline(StampShape shape, int width, int height);
extern const char* pattern_names[pattern_count];
extern const char* brush_names[brush_count];
extern const char* shape_names[shape_count];
struct Ink {
    GeometryStyle geometry;
    // Immutable alternate material used only in this material's uncovered areas.
    std::shared_ptr<const Ink> alternate;
    Color primary{0, 0, 0, 255};
    Color secondary{255, 255, 255, 255};
    std::shared_ptr<Image> custom_pattern;
    std::uint64_t custom_pattern_revision = 0;
    Pattern pattern = Pattern::Solid;
    Brush brush = Brush::Round;
    int size = 3;
    bool transparent_pattern = false;
    bool smooth = true;
    bool supersample = false;
    std::uint32_t noise = 1;
    double grain_scale = 1.0;
    double paper_roughness = 0.65;
    double pigment_load = 0.65;
    double material_angle = -20.0;
};
// Temporary raster coordinates mapped back to the original paper for materials.
struct RasterSpace {
    int scale = 1;
    int x = 0, y = 0;
    int paper_x(int value) const;
    int paper_y(int value) const;
};
bool solid_material(const Ink& ink);
void select_brush(Ink& ink, Brush brush);
void select_pattern(Ink& ink, Pattern pattern);
Ink pencil_ink(Ink ink);
Color patterned(const Ink& ink, int x, int y);
void dab(Image& image, Point point, const Ink& ink, RasterSpace space = {});
void stroke(Image& image, Point start, Point end, const Ink& ink, RasterSpace space = {});
void pixel_line(Image& image, Point start, Point end, const Ink& ink);
void flood(Image& image, int x, int y, const Ink& ink);
bool inside_polygon(const std::vector<Point>& points, double x, double y);
void polygon(Image& image, const std::vector<Point>& points, const Ink& ink, bool outline, bool fill,
             bool closed = true, Brush fill_brush = Brush::Round, const Ink* fill_material = nullptr);
std::vector<Point> shape_points(Shape shape, Point start, Point end);
void draw_shape(Image& image, Shape shape, Point start, Point end, const Ink& ink, bool outline, bool fill,
                Brush fill_brush = Brush::Round, const Ink* fill_material = nullptr);
Image make_stamp(const Image& image, Rect bounds, StampShape shape, bool transparent, Color key);
Image rotate_quarter(const Image& image, int turns);
Image flipped(const Image& image, bool horizontal);
} // namespace paint

#pragma once
#include "material.hpp"
#include <unordered_map>
namespace paint {
enum class BrushFamily { Additive, Mix, Heal };
enum class MixEffect {
    Ripple,
    Glass,
    Counterflow,
    Braid,
    SupportLens,
    Rooms,
    Holonomy,
    Blur,
    Sharpen,
    Smudge
};
inline constexpr int mix_effect_count = 7;
const char* mix_effect_name(MixEffect effect);
enum class EraserMode { Hard, Soft, Blur, Sharpen, Smudge };
class StrokeStabilizer {
    Point filtered_, pointer_;

  public:
    void reset(Point point);
    Point advance(Point point, double lag);
};
// Causal 2D adaptation of the relational-current four-state filter.
// Each branch stores (position, constant current, local current, current derivative).
class StrokeStateTracker {
    struct Branch {
        double mean[4][2] = {};
        double covariance[4][4] = {};
        double transition[4][4] = {};
        double noise[4][4] = {};
        double evidence = 0;
    };
    Branch branches_[5];
    Point anchor_, filtered_;
    double last_time_ = 0, cell_time_ = 0, variance_ = 2.25, momentum_ = 0;

  public:
    void reset(Point point, double noise_pixels = 1.5, double momentum = 0);
    // Seconds since reset, strictly increasing; duplicate timestamps do not add evidence.
    Point advance(Point point, double seconds);
};
// Sparse spatial observations followed by a delayed, C2 cubic B-spline.
class SparseStrokeTracker {
    StrokeStateTracker tracker_;
    Point knots_[3], raw_, retained_, emitted_;
    double raw_time_ = 0, remaining_ = 6, dither_ = 0.2;
    std::uint32_t seed_ = 1;
    bool finished_ = false;
    void append(Point point, std::vector<Point>& output);
    double noise();

  public:
    void reset(Point point, double uncertainty = 1.5, double momentum = 60, double dither = 0.2);
    std::vector<Point> advance(Point point, double seconds);
    std::vector<Point> finish(Point point, double seconds);
};
Color interpolate_pixel(Color base, Color replacement, double amount);
Color sample_bilinear(const Image& image, double x, double y, bool wrap = false);
class DynamicBrushStroke {
    struct DryDeposit {
        Color original;
        double coverage = 0;
    };
    std::unordered_map<int, DryDeposit> dry_pixels_;
    double pending_ = 0;
    std::uint32_t dab_ = 0;
    bool started_ = false;
    void dab(Image& image, Point center, Point direction, const Ink& ink, bool glitter, bool wrap = false);

  public:
    void clear();
    void segment(Image& image, Point start, Point end, const Ink& ink, bool glitter, bool wrap = false);
};
class TransformStroke {
    Image base_;
    std::vector<double> coverage_;
    Point origin_;

  public:
    void begin(const Image& image, Point origin);
    void segment(Image& image, Point start, Point end, double diameter, MixEffect effect, double strength,
                 double scale, double phase, bool wrap = false);
};
class HealingBrush {
    Image source_, target_;
    Point source_point_, offset_;
    bool aligned_ = false;
    std::vector<double> coverage_;

  public:
    bool has_source() const;
    void clear();
    void capture(const Image& image, Point point);
    void begin(const Image& image, Point point);
    Point source_for(Point destination) const;
    void segment(Image& image, Point start, Point end, double diameter, double hardness, double correction,
                 bool wrap = false);
};
Image heal_stamp_material(const Image& basis, const Image& material);
} // namespace paint

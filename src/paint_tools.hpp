#pragma once
#include "material.hpp"
#include <unordered_map>
namespace paint {
enum class BrushFamily { Additive, Mix, Heal, Carpet, Dither };
enum class MixEffect {
    Ripple,
    Glass,
    Counterflow,
    Braid,
    SupportLens,
    Rooms,
    ContourRelief,
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
Color interpolate_pixel(Color base, Color replacement, double amount);
Color sample_bilinear(const Image& image, double x, double y, bool wrap = false);
class DynamicBrushStroke {
    MaterialStroke gel_;
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
class CarpetStroke {
    struct Deposit {
        Color original;
        double coverage = 0;
    };
    std::unordered_map<int, Deposit> deposits_;

  public:
    void clear();
    void segment(Image& image, Point a, Point b, double diameter, const Image& tile, double opacity,
                 bool wrap);
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
struct HealingOptions {
    bool clone = false;
    bool preserve_alpha = true;
    bool aligned = true;
};
class HealingBrush {
    Image source_, target_;
    Point source_point_, offset_;
    bool aligned_ = false;
    std::vector<double> coverage_;
    std::unordered_map<std::size_t, std::array<Lab, 1024>> source_lab_, target_lab_;
    HealingOptions options_;

  public:
    bool has_source() const;
    void clear();
    void capture(const Image& image, Point point);
    void begin(const Image& image, Point point, HealingOptions options = {});
    Point source_for(Point destination) const;
    Point source_origin() const {
        return source_point_;
    }
    void segment(Image& image, Point start, Point end, double diameter, double hardness, double correction,
                 bool wrap = false);
};
Image heal_stamp_material(const Image& basis, const Image& material);
} // namespace paint

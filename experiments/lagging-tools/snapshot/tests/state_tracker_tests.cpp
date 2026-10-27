#include "paint_tools.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
void dense_reference() {
    // Independently condition the dense cell-current covariance:
    // C_ij = 40000 * [1 + (1 + sqrt(3)*d/ell)*exp(-sqrt(3)*d/ell)].
    // Integrate cell overlaps, add anchor variance 2.25 and observation noise
    // 2.25 I, then combine the five Gaussian marginal likelihoods in 2D.
    const double times[] = {0.013, 0.031, 0.057, 0.091, 0.140, 0.197};
    const paint::Point observations[] = {{1.2, .8},   {4.9, -.7},  {8.1, 1.1},
                                         {15.6, 2.4}, {22.8, 5.3}, {28.2, 7.6}};
    const paint::Point expected[] = {
        {1.049664255965941, .699776170643961},   {4.530420531067570, -.389950462119757},
        {8.161500451142155, .722917948530432},   {15.094143000017363, 2.179640077500278},
        {22.961947929121475, 5.055244600943537}, {28.759106838541932, 7.709388972872042}};
    paint::StrokeStateTracker tracker;
    tracker.reset({0, 0});
    for (int i = 0; i < 6; ++i) {
        const paint::Point point = tracker.advance(observations[i], times[i]);
        require(std::hypot(point.x - expected[i].x, point.y - expected[i].y) < 1e-9,
                "state tracker differs from independent dense Gaussian conditioning");
    }
    const paint::Point repeated = tracker.advance({1000, 1000}, times[5]);
    require(std::hypot(repeated.x - expected[5].x, repeated.y - expected[5].y) < 1e-9,
            "duplicate timestamp counted as another observation");
    const paint::Point invalid = tracker.advance({std::numeric_limits<double>::quiet_NaN(), 0}, 0.2);
    require(invalid.x == repeated.x && invalid.y == repeated.y, "invalid observation poisoned state");
    const paint::Point pause = tracker.advance({50, 60}, 2);
    require(pause.x == 50 && pause.y == 60, "delivery gap extrapolates stale motion");
    tracker.reset({70, 80});
    const paint::Point dot = tracker.advance({70, 80}, .01);
    require(dot.x == 70 && dot.y == 80, "new stroke inherits old motion or moves a dot");
}
void momentum_response() {
    for (int hz : {60, 120, 240}) {
        for (double frequency : {4.0, 17.0}) {
            paint::StrokeStateTracker original, damped;
            original.reset({0, 0}, 1.5, 0);
            damped.reset({0, 0}, 1.5, 60);
            double old_error = 0, new_error = 0, lag = 0, corner = 0, stopped = 0;
            int samples = 0;
            for (int i = 1; i <= 5 * hz; ++i) {
                const double t = static_cast<double>(i) / hz;
                const paint::Point truth = t < 2   ? paint::Point{150 * t, 0}
                                           : t < 3 ? paint::Point{300, 150 * (t - 2)}
                                           : t < 4 ? paint::Point{300, 150}
                                                   : paint::Point{300, 150 - 150 * (t - 4)};
                const paint::Point input{truth.x,
                                         truth.y + 2 * std::sin(2 * std::numbers::pi * frequency * t)};
                const paint::Point before = original.advance(input, t);
                const paint::Point after = damped.advance(input, t);
                if (t > .5 && t < 1.9) {
                    old_error += before.y * before.y;
                    new_error += after.y * after.y;
                    lag += truth.x - after.x;
                    ++samples;
                }
                const double error = std::hypot(after.x - truth.x, after.y - truth.y);
                if (t >= 2) {
                    corner = std::max(corner, error);
                }
                if (t > 3.5 && t < 3.9) {
                    stopped = std::max(stopped, error);
                }
            }
            std::cout << "Momentum 60%, " << hz << " Hz input, " << frequency
                      << " Hz wobble: RMS original/damped = " << std::sqrt(old_error / samples) << '/'
                      << std::sqrt(new_error / samples) << "; line lag = " << lag / samples
                      << "; max corner/reversal error = " << corner << "; settled stop error = " << stopped
                      << '\n';
            require(new_error < old_error * .6, "momentum does not suppress small deviations");
            require(std::abs(lag / samples) < 1, "momentum brakes steady forward motion");
            require(corner < 30 && stopped < 3, "momentum does not recover after turns or stops");
        }
    }
}
void sparse_paths(const char* path) {
    std::ofstream csv;
    if (path) {
        csv.open(path);
        csv << "mode,x,y\n";
    }
    paint::SparseStrokeTracker single, divided;
    single.reset({0, 0});
    divided.reset({0, 0});
    std::vector<paint::Point> whole = single.advance({120, 0}, .4), split;
    for (int i = 1; i <= 120; ++i) {
        const std::vector<paint::Point> points = divided.advance({static_cast<double>(i), 0}, i / 300.0);
        split.insert(split.end(), points.begin(), points.end());
    }
    require(whole.size() == split.size(), "sparse sampling depends on straight event subdivision");
    for (std::size_t i = 0; i < whole.size(); ++i) {
        require(std::hypot(whole[i].x - split[i].x, whole[i].y - split[i].y) < 1e-8,
                "sparse reconstruction changes with event subdivision");
    }
    paint::SparseStrokeTracker short_stroke;
    short_stroke.reset({10, 20}, 1.5, 60, 0);
    require(short_stroke.advance({12, 20}, .02).empty(), "sub-spacing motion reached estimator");
    const std::vector<paint::Point> tail = short_stroke.finish({12, 20}, .03);
    require(!tail.empty() && tail.back().x > 10 && tail.back().x <= 12 && std::abs(tail.back().y - 20) < 1e-9,
            "short gesture vanished or endpoint overshot");
    require(short_stroke.finish({12, 20}, .04).empty(), "release deposited the tail twice");
    short_stroke.reset({10, 20});
    require(short_stroke.finish({10, 20}, .02).empty(), "stationary click acquired a noisy tail");
    for (int mode = 0; mode < 4; ++mode) {
        paint::SparseStrokeTracker sparse;
        paint::StrokeStateTracker dense;
        // Dense; sparse with original uncertainty; increased uncertainty;
        // increased uncertainty plus deterministic injected noise.
        const double uncertainty = mode == 1 ? 1.5 : 2.0;
        sparse.reset({0, 0}, uncertainty, 60, mode == 3 ? .2 : 0);
        dense.reset({0, 0}, 1.5, 60);
        std::vector<paint::Point> curve{{0, 0}};
        for (int i = 1; i <= 600; ++i) {
            const double t = i / 120.0;
            paint::Point input =
                t < 2   ? paint::Point{150 * t, 2 * std::sin(2 * std::numbers::pi * 4 * t)}
                : t < 3 ? paint::Point{300 + .6 * std::sin(2 * std::numbers::pi * 17 * t), 150 * (t - 2)}
                : t < 4 ? paint::Point{300 - 150 * (t - 3), 150}
                        : paint::Point{150, 150};
            if (mode == 0) {
                curve.push_back(dense.advance(input, t));
            } else {
                const std::vector<paint::Point> points = sparse.advance(input, t);
                curve.insert(curve.end(), points.begin(), points.end());
            }
        }
        if (mode != 0) {
            const std::vector<paint::Point> points = sparse.finish({150, 150}, 5.01);
            curve.insert(curve.end(), points.begin(), points.end());
        }
        double error = 0, max_angle = 0;
        int count = 0;
        for (std::size_t i = 0; i < curve.size(); ++i) {
            const paint::Point p = curve[i];
            require(std::isfinite(p.x) && std::isfinite(p.y), "sparse path became nonfinite");
            if (p.x > 75 && p.x < 270 && std::abs(p.y) < 20) {
                error += p.y * p.y;
                ++count;
            }
            if (csv) {
                csv << mode << ',' << p.x << ',' << p.y << '\n';
            }
            if (i >= 2) {
                const paint::Point a{curve[i - 1].x - curve[i - 2].x, curve[i - 1].y - curve[i - 2].y};
                const paint::Point b{p.x - curve[i - 1].x, p.y - curve[i - 1].y};
                if (std::hypot(a.x, a.y) > .05 && std::hypot(b.x, b.y) > .05) {
                    max_angle = std::max(max_angle,
                                         std::abs(std::atan2(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y)));
                }
            }
        }
        std::cout << "Reconstruction mode " << mode << ": line RMS " << std::sqrt(error / count)
                  << " px; largest sampled direction step " << max_angle * 180 / std::numbers::pi
                  << " degrees\n";
        if (mode > 0) {
            require(std::sqrt(error / count) < 1, "sparse reconstruction preserves too much slow wobble");
            require(max_angle < .3, "sparse reconstruction has a sharp sampled kink");
        }
    }
}

void trajectories(const char* csv_path) {
    std::ofstream csv;
    if (csv_path) {
        csv.open(csv_path);
        require(static_cast<bool>(csv), "cannot write trajectory evidence");
        csv << "hz,t,truth_x,truth_y,input_x,input_y,tracker_x,tracker_y,lag_x,lag_y\n";
    }
    for (int hz : {60, 120, 240}) {
        paint::StrokeStateTracker tracker;
        paint::StrokeStabilizer lag;
        tracker.reset({0, 0});
        lag.reset({0, 0});
        double raw_error = 0, error = 0, lag_error = 0, trailing = 0, lag_trailing = 0;
        double stop_error = 0, max_error = 0;
        int samples = 0;
        for (int i = 1; i <= 4 * hz; ++i) {
            const double t = static_cast<double>(i) / hz;
            // Line, right-angle turn, stationary hold, then reversal.
            const paint::Point truth = t < 1   ? paint::Point{200 * t, 0}
                                       : t < 2 ? paint::Point{200, 200 * (t - 1)}
                                       : t < 3 ? paint::Point{200, 200}
                                               : paint::Point{200, 200 - 200 * (t - 3)};
            const double wobble = 1.5 * std::sin(2 * std::numbers::pi * 17 * t);
            const paint::Point input{truth.x, truth.y + wobble};
            const paint::Point filtered = tracker.advance(input, t);
            const paint::Point delayed = lag.advance(input, 5);
            require(std::isfinite(filtered.x) && std::isfinite(filtered.y), "nonfinite stroke output");
            const double distance = std::hypot(filtered.x - truth.x, filtered.y - truth.y);
            max_error = std::max(max_error, distance);
            if (t > .25 && t < .9) {
                raw_error += wobble * wobble;
                error += filtered.y * filtered.y;
                lag_error += delayed.y * delayed.y;
                trailing += truth.x - filtered.x;
                lag_trailing += truth.x - delayed.x;
                ++samples;
            }
            if (t > 2.25 && t < 2.9) {
                stop_error = std::max(stop_error, distance);
            }
            if (csv) {
                csv << hz << ',' << t << ',' << truth.x << ',' << truth.y << ',' << input.x << ',' << input.y
                    << ',' << filtered.x << ',' << filtered.y << ',' << delayed.x << ',' << delayed.y << '\n';
            }
        }
        require(error < raw_error * .8, "tracker does not reduce transverse line jitter");
        require(std::abs(trailing / samples) < 1, "tracker retains substantial steady line lag");
        require(stop_error < 3 && max_error < 20, "stop or corner error is unbounded");
        std::cout << hz << " Hz: line jitter RMS raw/tracker/lag = " << std::sqrt(raw_error / samples) << '/'
                  << std::sqrt(error / samples) << '/' << std::sqrt(lag_error / samples)
                  << " px; trailing tracker/lag = " << trailing / samples << '/' << lag_trailing / samples
                  << " px; max path error = " << max_error << " px; settled stop error <= " << stop_error
                  << " px\n";
    }
    paint::StrokeStateTracker tracker;
    tracker.reset({0, 0});
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (int i = 1; i <= 100000; ++i) {
        const double t = static_cast<double>(i) / 240;
        const paint::Point point = tracker.advance({100 * std::sin(t), 100 * std::cos(t) - 100}, t);
        require(std::isfinite(point.x) && std::isfinite(point.y), "long stroke lost numerical stability");
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "100000 updates: " << elapsed * 10 << " us/update; tracker state "
              << sizeof(paint::StrokeStateTracker) << " bytes\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        dense_reference();
        sparse_paths(argc > 2 ? argv[2] : nullptr);
        momentum_response();
        trajectories(argc > 1 ? argv[1] : nullptr);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

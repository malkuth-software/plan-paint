/*
MIT License - Modified

Give glory to The Holy One, Blessed be He, from whom all good things come. To Him we attribute the ability
that has provided for all good to come and be demonstrated anywhere herein as the true owner of our labor, and
we declare ourselves unworthy servants who claim ownership for our proscribed time and season only. If in any
way we have erred, or our work is insufficient, we ask mercy and kindness, for our effort is good and earnest.

Copyright (c) 2026 Joshuah Rainstar

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

The above attribution and copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.


*/
// Binary64 RGBA lanes, open paper closure without seams or bubble.
// Restated from stx_conv/src/detail/surface_kernel.hpp, licensed above, commit ea2e6a3.
#pragma once
#include <stx_conv/surface.hpp>
namespace paint::lane_kernel {
using T = double __attribute__((ext_vector_type(4)));
using Mask = decltype(T{} > T{});
using stx_conv::surface::Jet;
struct alignas(32) RowCell {
    T anchor{};
    std::array<T, 5> currents{};
};
static inline T least(T a, T b) {
    return b < a ? b : a;
}
static inline T greatest(T a, T b) {
    return a < b ? b : a;
}
static inline T positive_part(T a) {
    return a > 0.0 ? a : T(0.0);
}
static inline T negative_part(T a) {
    return a < 0.0 ? T(0.0) - a : T(0.0);
}
static inline void order_pair(T& a, T& b) {
    const T l = least(a, b), h = greatest(a, b);
    a = l;
    b = h;
}
static inline T capacity_threshold(std::array<T, 5> capacity, T mass) {
    order_pair(capacity[0], capacity[1]);
    order_pair(capacity[3], capacity[4]);
    order_pair(capacity[2], capacity[4]);
    order_pair(capacity[2], capacity[3]);
    order_pair(capacity[1], capacity[4]);
    order_pair(capacity[0], capacity[3]);
    order_pair(capacity[0], capacity[2]);
    order_pair(capacity[1], capacity[3]);
    order_pair(capacity[1], capacity[2]);
    T result = mass / 5.0;
    T sum = capacity[0];
    result = greatest(result, (mass - sum) / 4.0);
    sum = sum + capacity[1];
    result = greatest(result, (mass - sum) / 3.0);
    sum = sum + capacity[2];
    result = greatest(result, (mass - sum) / 2.0);
    sum = sum + capacity[3];
    result = greatest(result, mass - sum);
    return result;
}
static inline void node_jet(std::span<const T, 8> window, std::size_t centre, Jet kind, T& m, T& q) noexcept {
    const T& before3 = window[centre >= 3U ? centre - 3U : 0U];
    const T& before2 = window[centre - 2U];
    const T& before1 = window[centre - 1U];
    const T& here = window[centre];
    const T& after1 = window[centre + 1U];
    const T& after2 = window[centre + 2U];
    const T& after3 = window[centre + 3U <= 7U ? centre + 3U : 7U];
    switch (kind) {
    case Jet::interior:
        m = (before2 - 8.0 * before1 + 8.0 * after1 - after2) / 12.0;
        q = (16.0 * after1 - after2 - 30.0 * here + 16.0 * before1 - before2) / 12.0;
        return;
    case Jet::paper_first:
        m = (4.0 * after1 - 3.0 * here - after2) / 2.0;
        q = 2.0 * here - 5.0 * after1 + 4.0 * after2 - after3;
        return;
    case Jet::paper_central:
    case Jet::central3:
        m = (after1 - before1) / 2.0;
        q = before1 - 2.0 * here + after1;
        return;
    case Jet::paper_last:
        m = (3.0 * here - 4.0 * before1 + before2) / 2.0;
        q = 2.0 * here - 5.0 * before1 + 4.0 * before2 - before3;
        return;
    case Jet::first4:
        m = (18.0 * after1 - 11.0 * here - 9.0 * after2 + 2.0 * after3) / 6.0;
        q = 2.0 * here - 5.0 * after1 + 4.0 * after2 - after3;
        return;
    case Jet::second4:
        m = (6.0 * after1 - 2.0 * before1 - 3.0 * here - after2) / 6.0;
        q = before1 - 2.0 * here + after1;
        return;
    case Jet::penultimate4:
        m = (2.0 * after1 + 3.0 * here - 6.0 * before1 + before2) / 6.0;
        q = before1 - 2.0 * here + after1;
        return;
    case Jet::last4:
        m = (11.0 * here - 18.0 * before1 + 9.0 * before2 - 2.0 * before3) / 6.0;
        q = 2.0 * here - 5.0 * before1 + 4.0 * before2 - before3;
        return;
    case Jet::first3:
        m = (4.0 * after1 - 3.0 * here - after2) / 2.0;
        q = here - 2.0 * after1 + after2;
        return;
    case Jet::last3:
        m = (3.0 * here - 4.0 * before1 + before2) / 2.0;
        q = here - 2.0 * before1 + before2;
        return;
    case Jet::first2:
        m = after1 - here;
        q = T(0.0);
        return;
    case Jet::last2:
        m = here - before1;
        q = T(0.0);
        return;
    default:
        // Unreachable for a shape made by cell_shape: the secant, as the
        // shortest segment has.
        m = after1 - here;
        q = T(0.0);
        return;
    }
}

static inline RowCell compile(std::span<const T, 8> w, const stx_conv::surface::CellShape& shape) {
    RowCell cell;
    cell.anchor = w[3];
    const T zero(0.0);
    T left_m(0.0);
    T left_q(0.0);
    T right_m(0.0);
    T right_q(0.0);
    node_jet(w, 3U, shape.left, left_m, left_q);
    node_jet(w, 4U, shape.right, right_m, right_q);
    const T delta = w[4] - w[3];
    // Raw currents: differences of the six quintic controls.
    std::array<T, 5> raw{};
    raw[0] = left_m / 5.0;
    raw[1] = left_m / 5.0 + left_q / 20.0;
    raw[3] = right_m / 5.0 - right_q / 20.0;
    raw[4] = right_m / 5.0;
    raw[2] = delta - raw[0] - raw[1] - raw[3] - raw[4];

    // The unsigned neighbour box, from the neighbouring cells' deltas and
    // terminal raw currents (away from a seam those equal raw[0] and raw[4]).
    const T previous_delta = shape.has_previous ? w[3] - w[2] : zero;
    const T next_delta = shape.has_next ? w[5] - w[4] : zero;
    const T previous_last = shape.has_previous ? raw[0] : zero;
    const T next_first = shape.has_next ? raw[4] : zero;
    std::array<T, 5> room_positive{};
    std::array<T, 5> room_negative{};
    T head_positive = least(positive_part(previous_delta),
                            positive_part(positive_part(raw[0]) - negative_part(previous_last)));
    T head_negative = least(negative_part(previous_delta),
                            positive_part(negative_part(raw[0]) - positive_part(previous_last)));
    for (std::size_t j = 0U; j < 5U; ++j) {
        head_positive = least(head_positive, positive_part(raw[j]));
        head_negative = least(head_negative, negative_part(raw[j]));
        room_positive[j] = head_positive;
        room_negative[j] = head_negative;
    }
    T tail_positive =
        least(positive_part(next_delta), positive_part(positive_part(raw[4]) - negative_part(next_first)));
    T tail_negative =
        least(negative_part(next_delta), positive_part(negative_part(raw[4]) - positive_part(next_first)));
    for (std::size_t step = 0U; step < 5U; ++step) {
        const std::size_t j = 4U - step;
        tail_positive = least(tail_positive, positive_part(raw[j]));
        tail_negative = least(tail_negative, negative_part(raw[j]));
        room_positive[j] = room_positive[j] + tail_positive;
        room_negative[j] = room_negative[j] + tail_negative;
    }
    T total_positive = zero;
    T total_negative = zero;
    for (std::size_t j = 0U; j < 5U; ++j) {
        total_positive = total_positive + room_positive[j];
        total_negative = total_negative + room_negative[j];
    }
    const T plus = positive_part(delta);
    const T minus = negative_part(delta);
    const T upper_base = plus + least(total_negative, 4.0 * plus);
    const T lower_base = minus + least(total_positive, 4.0 * minus);

    // Preclip, then restore the mass by one capped common shift.
    std::array<T, 5> clipped{};
    std::array<T, 5> down{};
    std::array<T, 5> up{};
    T sum = zero;
    for (std::size_t j = 0U; j < 5U; ++j) {
        const T high = room_positive[j] + upper_base;
        const T low = zero - (room_negative[j] + lower_base);
        clipped[j] = least(high, greatest(low, raw[j]));
        down[j] = clipped[j] - low;
        up[j] = high - clipped[j];
        sum = sum + clipped[j];
    }
    const T residual = sum - delta;
    const Mask lower = residual > 0.0;
    std::array<T, 5> capacity;
    for (int k = 0; k < 5; ++k) {
        capacity[k] = lower ? down[k] : up[k];
    }
    const T mass = lower ? residual : zero - residual;
    const T threshold = capacity_threshold(capacity, mass);
    for (std::size_t j = 0U; j < 5U; ++j) {
        const T shift = least(threshold, capacity[j]);
        cell.currents[j] = lower ? clipped[j] - shift : clipped[j] + shift;
    }
    return cell;
}
} // namespace paint::lane_kernel

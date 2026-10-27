#include "conv.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace paint {
namespace {
using u32 = unsigned int;
using i32 = int;
using i8 = signed char;
using u8 = unsigned char;
constexpr double EPSILON = 2.2204460492503130808472633361816e-16;
static double absolute(double value) {
    return std::abs(value);
}
static double maximum(double left, double right) {
    return std::max(left, right);
}
static i8 value_sign(double value) {
    return value < 0.0 ? -1 : value > 0.0 ? 1 : 0;
}
struct resample_plan {
    u32 source_length = 0, target_length = 0, segment_count = 0;
    std::vector<u32> offsets, cells, anchor_nodes;
    std::vector<double> anchor_weights, current_weights;
};
struct LineWorkspace {
    std::vector<double> first_jet, second_jet, raw_current, admitted_current, differences;
    std::vector<i8> coarse_signs, boundary_signs;
    std::vector<i32> boundary_locations;
    explicit LineWorkspace(u32 length)
        : first_jet(length * 4), second_jet(length * 4), raw_current(length * 20),
          admitted_current(length * 20), differences(length), coarse_signs(length), boundary_signs(length),
          boundary_locations(length) {}
    static i32 project_signed_current(double* raw, u32 raw_offset, u32 raw_stride, const i8 signs[5],
                                      double total, double* admitted) {
        double target[5];
        double candidate[5];
        double best[5];
        i8 order[5];
        u8 masks[6];
        double best_error = 1.7976931348623157e308;
        i32 found = 0;

        for (u32 index = 0; index < 5; index += 1) {
            target[index] = static_cast<double>(signs[index]) * raw[raw_offset + index * raw_stride];
            order[index] = static_cast<i8>(index);
        }
        for (u32 index = 1; index < 5; index += 1) {
            i8 entry = order[index];
            double entry_value = raw[raw_offset + static_cast<u32>(entry) * raw_stride];
            u32 position = index;
            while (position > 0 &&
                   raw[raw_offset + static_cast<u32>(order[position - 1]) * raw_stride] > entry_value) {
                order[position] = order[position - 1];
                position -= 1;
            }
            order[position] = entry;
        }

        u8 mask = 0;
        for (u32 index = 0; index < 5; index += 1) {
            if (signs[index] > 0) {
                mask |= static_cast<u8>(1u << index);
            }
        }
        u32 mask_count = 0;
        if (mask != 0) {
            masks[mask_count++] = mask;
        }
        for (u32 index = 0; index < 5; index += 1) {
            mask ^= static_cast<u8>(1u << static_cast<u32>(order[index]));
            if (mask != 0) {
                masks[mask_count++] = mask;
            }
        }

        for (u32 mask_index = 0; mask_index < mask_count; mask_index += 1) {
            mask = masks[mask_index];
            double numerator = -total;
            u32 active_count = 0;
            for (u32 index = 0; index < 5; index += 1) {
                if (mask & (1u << index)) {
                    numerator += static_cast<double>(signs[index]) * target[index];
                    active_count += 1;
                }
            }
            double lagrange = numerator / static_cast<double>(active_count);
            i32 valid = 1;
            double signed_mass = 0.0;
            double error = 0.0;
            for (u32 index = 0; index < 5; index += 1) {
                double value = 0.0;
                if (mask & (1u << index)) {
                    value = target[index] - lagrange * static_cast<double>(signs[index]);
                    if (value < -64.0 * EPSILON) {
                        valid = 0;
                        break;
                    }
                    if (value < 0.0) {
                        value = 0.0;
                    }
                }
                candidate[index] = value;
                signed_mass += static_cast<double>(signs[index]) * value;
                double residual = value - target[index];
                error += residual * residual;
            }
            if (!valid || absolute(signed_mass - total) > 256.0 * EPSILON * maximum(1.0, absolute(total))) {
                continue;
            }
            if (error < best_error) {
                for (u32 index = 0; index < 5; index += 1) {
                    best[index] = candidate[index];
                }
                best_error = error;
                found = 1;
            }
        }

        if (!found) {
            return 0;
        }
        for (u32 index = 0; index < 5; index += 1) {
            admitted[raw_offset + index * raw_stride] = static_cast<double>(signs[index]) * best[index];
        }
        return 1;
    }

    i32 build_line_profile(const double* source, u32 length, u32 source_offset, u32 source_node_stride) {
        const u32 channels = 4;
        const u32 intervals = length - 1;
        std::fill(admitted_current.begin(), admitted_current.end(), 0.0);

        for (u32 channel = 0; channel < channels; channel += 1) {
            u32 line_offset = source_offset + channel;
            u32 last_offset = line_offset + (length - 1) * source_node_stride;
            double source0 = source[line_offset];
            double source1 = source[line_offset + source_node_stride];
            double source2 = source[line_offset + 2 * source_node_stride];
            double source_last = source[last_offset];
            double source_penultimate = source[last_offset - source_node_stride];
            double source_antepenultimate = source[last_offset - 2 * source_node_stride];
            first_jet[channel] = (-3.0 * source0 + 4.0 * source1 - source2) / 2.0;
            first_jet[channels + channel] = (source2 - source0) / 2.0;
            first_jet[(length - 2) * channels + channel] = (source_last - source_antepenultimate) / 2.0;
            first_jet[(length - 1) * channels + channel] =
                (3.0 * source_last - 4.0 * source_penultimate + source_antepenultimate) / 2.0;

            second_jet[channel] = (2.0 * source0 - 5.0 * source1 + 4.0 * source2 -
                                   source[line_offset + 3 * source_node_stride]);
            second_jet[channels + channel] = source0 - 2.0 * source1 + source2;
            second_jet[(length - 2) * channels + channel] =
                (source_antepenultimate - 2.0 * source_penultimate + source_last);
            second_jet[(length - 1) * channels + channel] =
                (2.0 * source_last - 5.0 * source_penultimate + 4.0 * source_antepenultimate -
                 source[last_offset - 3 * source_node_stride]);

            for (u32 node = 2; node < length - 2; node += 1) {
                u32 node_offset = line_offset + node * source_node_stride;
                first_jet[node * channels + channel] = (source[node_offset - 2 * source_node_stride] -
                                                        8.0 * source[node_offset - source_node_stride] +
                                                        8.0 * source[node_offset + source_node_stride] -
                                                        source[node_offset + 2 * source_node_stride]) /
                                                       12.0;
                second_jet[node * channels + channel] =
                    (-source[node_offset + 2 * source_node_stride] +
                     16.0 * source[node_offset + source_node_stride] - 30.0 * source[node_offset] +
                     16.0 * source[node_offset - source_node_stride] -
                     source[node_offset - 2 * source_node_stride]) /
                    12.0;
            }
        }

        for (u32 interval = 0; interval < intervals; interval += 1) {
            for (u32 channel = 0; channel < channels; channel += 1) {
                double y0 = source[source_offset + interval * source_node_stride + channel];
                double y1 = source[source_offset + (interval + 1) * source_node_stride + channel];
                double f0 = first_jet[interval * channels + channel];
                double f1 = first_jet[(interval + 1) * channels + channel];
                double q0 = second_jet[interval * channels + channel];
                double q1 = second_jet[(interval + 1) * channels + channel];
                double control1 = y0 + f0 / 5.0;
                double control2 = y0 + 2.0 * f0 / 5.0 + q0 / 20.0;
                double control3 = y1 - 2.0 * f1 / 5.0 + q1 / 20.0;
                double control4 = y1 - f1 / 5.0;
                u32 offset = interval * 5 * channels + channel;
                raw_current[offset] = control1 - y0;
                raw_current[offset + channels] = control2 - control1;
                raw_current[offset + 2 * channels] = control3 - control2;
                raw_current[offset + 3 * channels] = control4 - control3;
                raw_current[offset + 4 * channels] = y1 - control4;
            }
        }

        for (u32 channel = 0; channel < channels; channel += 1) {
            for (u32 interval = 0; interval < intervals; interval += 1) {
                differences[interval] =
                    (source[source_offset + (interval + 1) * source_node_stride + channel] -
                     source[source_offset + interval * source_node_stride + channel]);
                coarse_signs[interval] = value_sign(differences[interval]);
            }

            i32 first_witness = -1;
            for (u32 interval = 0; interval < intervals; interval += 1) {
                if (coarse_signs[interval] != 0) {
                    first_witness = static_cast<i32>(interval);
                    break;
                }
            }
            if (first_witness < 0) {
                continue;
            }
            for (i32 interval = 0; interval < first_witness; interval += 1) {
                coarse_signs[interval] = coarse_signs[first_witness];
            }
            for (u32 interval = static_cast<u32>(first_witness) + 1; interval < intervals; interval += 1) {
                if (coarse_signs[interval] == 0) {
                    coarse_signs[interval] = coarse_signs[interval - 1];
                }
            }

            u32 boundary_count = 0;
            u32 previous_location = 0;
            for (u32 knot = 1; knot < intervals; knot += 1) {
                i8 left_sign = coarse_signs[knot - 1];
                i8 right_sign = coarse_signs[knot];
                if (left_sign == right_sign) {
                    continue;
                }
                u32 centre = 5 * knot;
                u32 candidate_start = previous_location + 1 > centre - 4 ? previous_location + 1 : centre - 4;
                u32 candidate_stop = 5 * intervals < centre + 5 ? 5 * intervals : centre + 5;
                u32 local_start = centre > 5 ? centre - 5 : 0;
                u32 local_stop = 5 * intervals < centre + 5 ? 5 * intervals : centre + 5;
                u32 best_boundary = centre;
                double best_cost = 1.7976931348623157e308;
                for (u32 candidate = candidate_start; candidate < candidate_stop; candidate += 1) {
                    double cost = 0.0;
                    for (u32 index = local_start; index < local_stop; index += 1) {
                        i8 expected = index < candidate ? left_sign : right_sign;
                        double value = raw_current[index * channels + channel];
                        if (static_cast<double>(expected) * value < 0.0) {
                            cost += value * value;
                        }
                    }
                    if (cost < best_cost) {
                        best_cost = cost;
                        best_boundary = candidate;
                    }
                }
                boundary_locations[boundary_count] = static_cast<i32>(best_boundary);
                boundary_signs[boundary_count] = right_sign;
                boundary_count += 1;
                previous_location = best_boundary;
            }

            u32 boundary_index = 0;
            i8 current_sign = coarse_signs[first_witness];
            for (u32 interval = 0; interval < intervals; interval += 1) {
                i8 signs[5];
                for (u32 current = 0; current < 5; current += 1) {
                    u32 location = interval * 5 + current;
                    while (boundary_index < boundary_count &&
                           location >= static_cast<u32>(boundary_locations[boundary_index])) {
                        current_sign = boundary_signs[boundary_index];
                        boundary_index += 1;
                    }
                    signs[current] = current_sign;
                }
                if (!project_signed_current(raw_current.data(), interval * 5 * channels + channel, channels,
                                            signs, differences[interval], admitted_current.data())) {
                    return 0;
                }
            }
        }
        return 1;
    }

    static void tail_weights(double u, double weights[5]) {
        double v = 1.0 - u;
        double u2 = u * u, u3 = u2 * u, u4 = u3 * u, u5 = u4 * u;
        double v2 = v * v, v3 = v2 * v, v4 = v3 * v, v5 = v4 * v;
        double basis[6] = {v5, 5.0 * v4 * u, 10.0 * v3 * u2, 10.0 * v2 * u3, 5.0 * v * u4, u5};
        double suffix = 0.0;
        for (i32 control = 5; control >= 1; control -= 1) {
            suffix += basis[control];
            weights[control - 1] = suffix;
        }
    }

    static void build_pixel_area_plan(resample_plan& plan, u32 source_length, u32 target_length) {
        plan.source_length = source_length;
        plan.target_length = target_length;
        plan.offsets.resize(target_length + 1);
        const double step = static_cast<double>(source_length) / target_length;
        constexpr double nodes[3] = {-0.7745966692414834, 0, 0.7745966692414834};
        constexpr double weights[3] = {5.0 / 9, 8.0 / 9, 5.0 / 9};
        for (u32 target = 0; target < target_length; ++target) {
            plan.offsets[target] = static_cast<u32>(plan.cells.size());
            const double left = target * step - 0.5, right = (target + 1) * step - 0.5;
            const int first = std::max(-1, static_cast<int>(std::floor(left)));
            const int last =
                std::min(static_cast<int>(source_length) - 1, static_cast<int>(std::floor(right)));
            for (int segment = first; segment <= last; ++segment) {
                const double low = std::max(left, static_cast<double>(segment));
                const double high = std::min(right, segment + 1.0);
                if (high <= low) {
                    continue;
                }
                const u32 cell =
                    static_cast<u32>(std::clamp(segment, 0, static_cast<int>(source_length) - 2));
                const bool edge = segment < 0 || segment == static_cast<int>(source_length) - 1;
                plan.cells.push_back(cell);
                plan.anchor_nodes.push_back(segment < 0 ? 0 : static_cast<u32>(segment));
                plan.anchor_weights.push_back((high - low) / step);
                double integrated[5] = {};
                if (!edge) {
                    const double middle = (low + high) * 0.5, half = (high - low) * 0.5;
                    for (int node = 0; node < 3; ++node) {
                        double tail[5];
                        tail_weights(middle + half * nodes[node] - cell, tail);
                        for (int i = 0; i < 5; ++i) {
                            integrated[i] += half * weights[node] / step * tail[i];
                        }
                    }
                }
                for (double weight : integrated) {
                    plan.current_weights.push_back(weight);
                }
            }
        }
        plan.segment_count = static_cast<u32>(plan.cells.size());
        plan.offsets[target_length] = plan.segment_count;
    }

    static i32 build_resample_plan(resample_plan& plan, u32 source_length, u32 target_length) {
        const double nodes[3] = {-0.7745966692414834, 0.0, 0.7745966692414834};
        const double quadrature_weights[3] = {5.0 / 9.0, 8.0 / 9.0, 5.0 / 9.0};
        u32 intervals = source_length - 1;
        u32 segment_count = 0;
        if (target_length >= source_length) {
            segment_count = target_length;
        } else {
            double step = target_length == 1
                              ? static_cast<double>(intervals)
                              : static_cast<double>(intervals) / static_cast<double>(target_length - 1);
            for (u32 target = 0; target < target_length; target += 1) {
                double left = target == 0 ? 0.0 : (static_cast<double>(target) - 0.5) * step;
                double right = target == target_length - 1 ? static_cast<double>(intervals)
                                                           : (static_cast<double>(target) + 0.5) * step;
                u32 first = static_cast<u32>(left);
                if (first >= intervals) {
                    first = intervals - 1;
                }
                u32 last = static_cast<u32>(right);
                if (static_cast<double>(last) < right) {
                    last += 1;
                }
                if (last > 0) {
                    last -= 1;
                }
                if (last >= intervals) {
                    last = intervals - 1;
                }
                segment_count += last - first + 1;
            }
        }
        plan.source_length = source_length;
        plan.target_length = target_length;
        plan.segment_count = segment_count;
        plan.offsets.resize(target_length + 1);
        plan.cells.resize(segment_count);
        plan.anchor_nodes.resize(segment_count);
        plan.anchor_weights.resize(segment_count);
        plan.current_weights.resize(segment_count * 5);

        u32 segment = 0;
        for (u32 target = 0; target < target_length; target += 1) {
            plan.offsets[target] = segment;
            if (target_length >= source_length) {
                double position = static_cast<double>(target) * static_cast<double>(intervals) /
                                  static_cast<double>(target_length - 1);
                u32 nearest = static_cast<u32>(position + 0.5);
                if (absolute(position - static_cast<double>(nearest)) <= 4.0e-13) {
                    plan.cells[segment] = nearest < intervals ? nearest : intervals - 1;
                    plan.anchor_nodes[segment] = nearest;
                    plan.anchor_weights[segment] = 1.0;
                    for (u32 current = 0; current < 5; current += 1) {
                        plan.current_weights[segment * 5 + current] = 0.0;
                    }
                } else {
                    u32 cell = static_cast<u32>(position);
                    if (cell >= intervals) {
                        cell = intervals - 1;
                    }
                    double weights[5];
                    tail_weights(position - static_cast<double>(cell), weights);
                    plan.cells[segment] = cell;
                    plan.anchor_nodes[segment] = cell;
                    plan.anchor_weights[segment] = 1.0;
                    for (u32 current = 0; current < 5; current += 1) {
                        plan.current_weights[segment * 5 + current] = weights[current];
                    }
                }
                segment += 1;
                continue;
            }

            double step = target_length == 1
                              ? static_cast<double>(intervals)
                              : static_cast<double>(intervals) / static_cast<double>(target_length - 1);
            double left = target == 0 ? 0.0 : (static_cast<double>(target) - 0.5) * step;
            double right = target == target_length - 1 ? static_cast<double>(intervals)
                                                       : (static_cast<double>(target) + 0.5) * step;
            double basin_width = right - left;
            u32 first = static_cast<u32>(left);
            if (first >= intervals) {
                first = intervals - 1;
            }
            u32 last = static_cast<u32>(right);
            if (static_cast<double>(last) < right) {
                last += 1;
            }
            if (last > 0) {
                last -= 1;
            }
            if (last >= intervals) {
                last = intervals - 1;
            }
            for (u32 cell = first; cell <= last; cell += 1) {
                double segment_left = left > static_cast<double>(cell) ? left : static_cast<double>(cell);
                double segment_right =
                    right < static_cast<double>(cell + 1) ? right : static_cast<double>(cell + 1);
                if (segment_right <= segment_left) {
                    continue;
                }
                double middle = 0.5 * (segment_left + segment_right);
                double half = 0.5 * (segment_right - segment_left);
                double integrated[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
                for (u32 quadrature = 0; quadrature < 3; quadrature += 1) {
                    double weights[5];
                    tail_weights(middle + half * nodes[quadrature] - static_cast<double>(cell), weights);
                    double factor = half * quadrature_weights[quadrature] / basin_width;
                    for (u32 current = 0; current < 5; current += 1) {
                        integrated[current] += factor * weights[current];
                    }
                }
                plan.cells[segment] = cell;
                plan.anchor_nodes[segment] = cell;
                plan.anchor_weights[segment] = (segment_right - segment_left) / basin_width;
                for (u32 current = 0; current < 5; current += 1) {
                    plan.current_weights[segment * 5 + current] = integrated[current];
                }
                segment += 1;
            }
        }
        plan.offsets[target_length] = segment;
        plan.segment_count = segment;
        return 1;
    }

    i32 resample_line(const double* source, u32 length, u32 source_offset, u32 source_stride,
                      const resample_plan& plan, double* destination, u32 destination_offset,
                      u32 destination_stride) {
        if (!build_line_profile(source, length, source_offset, source_stride)) {
            return 0;
        }
        for (u32 target = 0; target < plan.target_length; target += 1) {
            double value[4] = {0.0, 0.0, 0.0, 0.0};
            for (u32 segment = plan.offsets[target]; segment < plan.offsets[target + 1]; segment += 1) {
                u32 cell = plan.cells[segment];
                double sample[4] = {};
                for (u32 channel = 0; channel < 4; channel += 1) {
                    sample[channel] =
                        plan.anchor_weights[segment] *
                        source[source_offset + plan.anchor_nodes[segment] * source_stride + channel];
                }
                // Channels are independent lanes. Keep each channel's five-current
                // summation order while sharing coefficient loads and zero tests.
                for (u32 current = 0; current < 5; current += 1) {
                    double coefficient = plan.current_weights[segment * 5 + current];
                    if (coefficient != 0.0) {
                        for (u32 channel = 0; channel < 4; channel += 1) {
                            sample[channel] +=
                                coefficient * admitted_current[(cell * 5 + current) * 4 + channel];
                        }
                    }
                }
                for (u32 channel = 0; channel < 4; channel += 1) {
                    value[channel] += sample[channel];
                }
            }
            for (u32 channel = 0; channel < 4; channel += 1) {
                destination[destination_offset + target * destination_stride + channel] = value[channel];
            }
        }
        return 1;
    }
};
// Short lines have no five-point jet. This branch is selected once per line.
static void short_line(const std::vector<double>& source, int offset, int stride, int length,
                       std::vector<double>& destination, int out_offset, int out_stride, int target) {
    for (int i = 0; i < target; ++i) {
        double position =
            target > 1 ? static_cast<double>(i) * (length - 1) / (target - 1) : 0.5 * (length - 1);
        int left = static_cast<int>(position);
        int right = std::min(left + 1, length - 1);
        double fraction = position - left;
        for (int c = 0; c < 4; ++c) {
            double value = source[offset + left * stride + c] * (1.0 - fraction) +
                           source[offset + right * stride + c] * fraction;
            if (target < length) {
                double step = target == 1 ? length - 1.0 : static_cast<double>(length - 1) / (target - 1);
                double low = i == 0 ? 0.0 : (i - 0.5) * step;
                double high = i == target - 1 ? length - 1.0 : (i + 0.5) * step;
                double integral = 0.0;
                for (int cell = static_cast<int>(low); cell < length - 1 && cell < high; ++cell) {
                    double a = std::max(low, static_cast<double>(cell)) - cell;
                    double b = std::min(high, static_cast<double>(cell + 1)) - cell;
                    double base = source[offset + cell * stride + c];
                    double slope = source[offset + (cell + 1) * stride + c] - base;
                    integral += base * (b - a) + 0.5 * slope * (b * b - a * a);
                }
                if (high > low) {
                    value = integral / (high - low);
                }
            }
            destination[out_offset + i * out_stride + c] = value;
        }
    }
}
static void short_area_line(const std::vector<double>& source, int offset, int stride, int length,
                            std::vector<double>& destination, int out_offset, int out_stride, int target) {
    const double step = static_cast<double>(length) / target;
    for (int i = 0; i < target; ++i) {
        const double left = i * step - 0.5, right = (i + 1) * step - 0.5;
        for (int channel = 0; channel < 4; ++channel) {
            double integral = 0;
            if (left < 0) {
                integral += (std::min(right, 0.0) - left) * source[offset + channel];
            }
            if (right > length - 1) {
                integral +=
                    (right - std::max(left, length - 1.0)) * source[offset + (length - 1) * stride + channel];
            }
            for (int cell = std::max(0, static_cast<int>(std::floor(left)));
                 cell < length - 1 && cell < right; ++cell) {
                const double a = std::max(left, static_cast<double>(cell)) - cell;
                const double b = std::min(right, cell + 1.0) - cell;
                const double base = source[offset + cell * stride + channel];
                const double slope = source[offset + (cell + 1) * stride + channel] - base;
                integral += base * (b - a) + 0.5 * slope * (b * b - a * a);
            }
            destination[out_offset + i * out_stride + channel] = integral / step;
        }
    }
}
struct AxisJob {
    const std::vector<double>& input;
    std::vector<double>& output;
    const resample_plan& plan;
    int width, height, target;
    bool vertical, area;
    std::atomic<int> next{0};
    std::atomic<bool> failed{false};
};
static void axis_worker(AxisJob& job) {
    int length = job.vertical ? job.height : job.width;
    int lines = job.vertical ? job.width : job.height;
    LineWorkspace workspace(static_cast<u32>(length));
    for (;;) {
        int line = job.next.fetch_add(1, std::memory_order_relaxed);
        if (line >= lines) {
            break;
        }
        int source_offset = job.vertical ? line * 4 : line * job.width * 4;
        int source_stride = job.vertical ? job.width * 4 : 4;
        int destination_offset = job.vertical ? line * 4 : line * job.target * 4;
        int destination_stride = job.vertical ? job.width * 4 : 4;
        if (length < 5 && job.area) {
            short_area_line(job.input, source_offset, source_stride, length, job.output, destination_offset,
                            destination_stride, job.target);
        } else if (length < 5) {
            short_line(job.input, source_offset, source_stride, length, job.output, destination_offset,
                       destination_stride, job.target);
        } else {
            int ok = workspace.resample_line(job.input.data(), length, source_offset, source_stride, job.plan,
                                             job.output.data(), destination_offset, destination_stride);
            if (!ok) {
                job.failed.store(true, std::memory_order_relaxed);
            }
        }
    }
}
static void resize_axis(const std::vector<double>& input, int width, int height, int target, bool vertical,
                        std::vector<double>& output, bool area) {
    int length = vertical ? height : width;
    int lines = vertical ? width : height;
    output.resize(static_cast<std::size_t>(lines) * target * 4);
    resample_plan plan;
    if (length >= 5) {
        if (area) {
            LineWorkspace::build_pixel_area_plan(plan, length, target);
        } else {
            LineWorkspace::build_resample_plan(plan, length, target);
        }
    }
    AxisJob job{input, output, plan, width, height, target, vertical, area};
    unsigned count = std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
    if (static_cast<std::size_t>(width) * height < 65536) {
        count = 1;
    }
    if (count == 1) {
        axis_worker(job);
    } else {
        std::vector<std::jthread> workers;
        workers.reserve(count);
        for (unsigned index = 0; index < count; ++index) {
            workers.emplace_back(axis_worker, std::ref(job));
        }
        for (std::jthread& worker : workers) {
            worker.join();
        }
    }
    if (job.failed.load(std::memory_order_relaxed)) {
        throw std::runtime_error("CONV signed-current projection did not admit this image.");
    }
}
} // namespace
static void resize_image(const Image& source, int width, int height, Image& destination, bool area) {
    if (source.width < 1 || source.height < 1) {
        throw std::invalid_argument("Cannot resize an empty image.");
    }
    if (width == source.width && height == source.height) {
        destination = source;
        return;
    }
    Image result;
    result.reset(width, height, {0, 0, 0, 0});
    std::vector<double> input(source.pixels.size() * 4);
    for (std::size_t i = 0; i < source.pixels.size(); ++i) {
        Color color = source.pixels[i];
        double alpha = color.a / 255.0;
        input[i * 4] = color.r / 255.0 * alpha;
        input[i * 4 + 1] = color.g / 255.0 * alpha;
        input[i * 4 + 2] = color.b / 255.0 * alpha;
        input[i * 4 + 3] = alpha;
    }
    std::vector<double> middle, output;
    if (width == source.width) {
        resize_axis(input, source.width, source.height, height, true, output, area);
    } else if (height == source.height) {
        resize_axis(input, source.width, source.height, width, false, output, area);
    } else if (height < source.height) {
        resize_axis(input, source.width, source.height, height, true, middle, area);
        resize_axis(middle, source.width, height, width, false, output, area);
    } else {
        resize_axis(input, source.width, source.height, width, false, middle, area);
        resize_axis(middle, width, source.height, height, true, output, area);
    }
    for (std::size_t i = 0; i < result.pixels.size(); ++i) {
        double alpha = std::clamp(output[i * 4 + 3], 0.0, 1.0);
        double inverse = alpha > 1.0 / 65535.0 ? 1.0 / alpha : 0.0;
        Color color{static_cast<u8>(std::round(255.0 * std::clamp(output[i * 4] * inverse, 0.0, 1.0))),
                    static_cast<u8>(std::round(255.0 * std::clamp(output[i * 4 + 1] * inverse, 0.0, 1.0))),
                    static_cast<u8>(std::round(255.0 * std::clamp(output[i * 4 + 2] * inverse, 0.0, 1.0))),
                    static_cast<u8>(std::round(255.0 * alpha))};
        result.pixels[i] = color;
    }
    destination = std::move(result);
}
void conv_resize(const Image& source, int width, int height, Image& destination) {
    resize_image(source, width, height, destination, false);
}
void conv_resize_area(const Image& source, int width, int height, Image& destination) {
    resize_image(source, width, height, destination, true);
}
} // namespace paint

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019-2025, The OpenROAD Authors

#pragma once

#include <limits>
#include <vector>

#include "Util.h"

namespace cts {

// Improvements to the space-filling-curve sink clustering (Sec. II.B).
//
// The stock clusterer sorts sinks along a Hilbert curve and then sweeps the
// resulting order greedily, cutting a new cluster whenever the size, diameter
// or capacitance limit would be exceeded, and keeps the cheapest of `groupSize`
// rotations of that order.  Two things are left on the table:
//
//   * the order itself is fixed, even though the Hilbert curve occasionally
//     places nearby sinks far apart along the curve ("fault lines"), and
//   * the sweep is greedy, so a locally reasonable cut can force an expensive
//     one later.
//
// improveTour3Opt addresses the first and dpRpSegment the second.  Both operate
// on a linear order and preserve it as the unit of clustering, so the result is
// still a set of consecutive runs and the existing feasibility rules apply
// unchanged.

constexpr double kInfeasibleCost = std::numeric_limits<double>::infinity();

// Bentley-style tour improvement, limited to each vertex's r nearest
// neighbours so a pass costs O(n*r) rather than O(n^2).  Applies 2-Opt segment
// reversal and Or-Opt segment relocation (segments of 1 to 3 vertices) to the
// path, taking the first strictly improving move, and stops after maxPasses or
// a pass that finds nothing.  Cost is the sum of Manhattan steps along the
// path.  Returns the number of accepted moves.
int improveTour3Opt(const std::vector<Point<double>>& points,
                    std::vector<unsigned>& tour,
                    int neighbors = 25,
                    int maxPasses = 3);

// Optimal partition of `tour` into consecutive clusters, minimizing the sum of
// cluster costs (the restricted-partitioning dynamic program of Kahng &
// Robins).  sliceCost[i][len - 1] is the cost of the cluster tour[i .. i+len-1]
// for len in 1..maxLen, with kInfeasibleCost marking a cluster the caller will
// not allow; the caller therefore owns the feasibility rules and the DP cannot
// disagree with the greedy sweep about what is legal.
//
// `clusterPenalty` is charged once per cluster.  It is not cosmetic: the sum of
// cluster diameters falls monotonically as clusters get smaller, so with no
// penalty the optimum is a large number of tiny clusters, each of which would
// cost a real leaf buffer.  The penalty is what pins the cluster count, and
// because the count is monotone non-increasing in it, the caller can bisect on
// it to hit a target count.  O(n * maxLen).
//
// Returns one vector of sink indices per cluster, or an empty result if no
// feasible partition exists.
std::vector<std::vector<unsigned>> dpRpSegment(
    const std::vector<unsigned>& tour,
    const std::vector<std::vector<double>>& sliceCost,
    double clusterPenalty);

// Maximum Manhattan distance between any two of the points, which is also
// twice the radius of the smallest enclosing diamond.  Computed from the
// spans of u = x + y and v = x - y, since |dx| + |dy| = max(|du|, |dv|).
double pairwiseDiameter(const std::vector<Point<double>>& points);

// Centre of the smallest enclosing diamond (the L1 1-center): the location
// minimizing the maximum Manhattan distance to the points.  The radius it
// achieves is exactly pairwiseDiameter()/2.
Point<double> l1Center(const std::vector<Point<double>>& points);

}  // namespace cts

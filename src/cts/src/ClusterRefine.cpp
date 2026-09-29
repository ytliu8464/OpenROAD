// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019-2025, The OpenROAD Authors

#include "ClusterRefine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace cts {

namespace {

double l1(const Point<double>& a, const Point<double>& b)
{
  return std::abs(a.getX() - b.getX()) + std::abs(a.getY() - b.getY());
}

// Approximate nearest neighbours from a uniform grid sized so that a cell
// holds a handful of points.  Exactness is not required: these are only
// candidate moves, and every accepted move is checked against the true cost.
class NeighborGrid
{
 public:
  NeighborGrid(const std::vector<Point<double>>& points, const int perCell)
      : points_(points)
  {
    const size_t n = points.size();
    if (n == 0) {
      return;
    }
    double minX = points[0].getX(), maxX = minX;
    double minY = points[0].getY(), maxY = minY;
    for (const Point<double>& p : points) {
      minX = std::min(minX, p.getX());
      maxX = std::max(maxX, p.getX());
      minY = std::min(minY, p.getY());
      maxY = std::max(maxY, p.getY());
    }
    minX_ = minX;
    minY_ = minY;
    const int cells = std::max(
        1, static_cast<int>(std::sqrt(static_cast<double>(n) / perCell)));
    dim_ = cells;
    cellW_ = std::max((maxX - minX) / cells, 1e-9);
    cellH_ = std::max((maxY - minY) / cells, 1e-9);
    buckets_.resize(static_cast<size_t>(dim_) * dim_);
    for (size_t i = 0; i < n; ++i) {
      buckets_[cellOf(points[i])].push_back(i);
    }
  }

  // Fills `out` with up to `want` near neighbours of point `idx`, growing the
  // search ring until enough candidates are found.
  void near(const size_t idx, const int want, std::vector<unsigned>& out) const
  {
    out.clear();
    if (buckets_.empty()) {
      return;
    }
    const int cx = colOf(points_[idx]);
    const int cy = rowOf(points_[idx]);
    for (int ring = 0; ring < dim_; ++ring) {
      for (int dy = -ring; dy <= ring; ++dy) {
        for (int dx = -ring; dx <= ring; ++dx) {
          if (std::max(std::abs(dx), std::abs(dy)) != ring) {
            continue;  // only the new ring
          }
          const int x = cx + dx;
          const int y = cy + dy;
          if (x < 0 || y < 0 || x >= dim_ || y >= dim_) {
            continue;
          }
          for (const size_t j : buckets_[static_cast<size_t>(y) * dim_ + x]) {
            if (j != idx) {
              out.push_back(j);
            }
          }
        }
      }
      if (static_cast<int>(out.size()) >= want) {
        break;
      }
    }
    if (static_cast<int>(out.size()) > want) {
      std::partial_sort(out.begin(),
                        out.begin() + want,
                        out.end(),
                        [&](unsigned a, unsigned b) {
                          return l1(points_[idx], points_[a])
                                 < l1(points_[idx], points_[b]);
                        });
      out.resize(want);
    }
  }

 private:
  size_t cellOf(const Point<double>& p) const
  {
    return static_cast<size_t>(rowOf(p)) * dim_ + colOf(p);
  }
  int colOf(const Point<double>& p) const
  {
    return std::min(dim_ - 1,
                    std::max(0, static_cast<int>((p.getX() - minX_) / cellW_)));
  }
  int rowOf(const Point<double>& p) const
  {
    return std::min(dim_ - 1,
                    std::max(0, static_cast<int>((p.getY() - minY_) / cellH_)));
  }

  const std::vector<Point<double>>& points_;
  std::vector<std::vector<size_t>> buckets_;
  double minX_ = 0, minY_ = 0, cellW_ = 1, cellH_ = 1;
  int dim_ = 1;
};

// Length of the path edges touching position i, guarding both ends.
double edgeCost(const std::vector<Point<double>>& pts,
                const std::vector<unsigned>& tour,
                const int i)
{
  double cost = 0;
  if (i > 0) {
    cost += l1(pts[tour[i - 1]], pts[tour[i]]);
  }
  if (i + 1 < static_cast<int>(tour.size())) {
    cost += l1(pts[tour[i]], pts[tour[i + 1]]);
  }
  return cost;
}

}  // namespace

double pairwiseDiameter(const std::vector<Point<double>>& points)
{
  if (points.size() < 2) {
    return 0.0;
  }
  // |dx| + |dy| == max(|du|, |dv|) for u = x+y, v = x-y, so the largest
  // pairwise Manhattan distance is just the larger of the two spans.
  double uMin = std::numeric_limits<double>::max();
  double uMax = std::numeric_limits<double>::lowest();
  double vMin = uMin;
  double vMax = uMax;
  for (const Point<double>& p : points) {
    const double u = p.getX() + p.getY();
    const double v = p.getX() - p.getY();
    uMin = std::min(uMin, u);
    uMax = std::max(uMax, u);
    vMin = std::min(vMin, v);
    vMax = std::max(vMax, v);
  }
  return std::max(uMax - uMin, vMax - vMin);
}

Point<double> l1Center(const std::vector<Point<double>>& points)
{
  if (points.empty()) {
    return Point<double>(0, 0);
  }
  double uMin = std::numeric_limits<double>::max();
  double uMax = std::numeric_limits<double>::lowest();
  double vMin = uMin;
  double vMax = uMax;
  for (const Point<double>& p : points) {
    const double u = p.getX() + p.getY();
    const double v = p.getX() - p.getY();
    uMin = std::min(uMin, u);
    uMax = std::max(uMax, u);
    vMin = std::min(vMin, v);
    vMax = std::max(vMax, v);
  }
  const double u = 0.5 * (uMin + uMax);
  const double v = 0.5 * (vMin + vMax);
  return Point<double>(0.5 * (u + v), 0.5 * (u - v));
}

int improveTour3Opt(const std::vector<Point<double>>& points,
                    std::vector<unsigned>& tour,
                    const int neighbors,
                    const int maxPasses)
{
  const int n = tour.size();
  if (n < 4 || neighbors <= 0) {
    return 0;
  }
  const NeighborGrid grid(points, 4);

  // position[v] is where sink v currently sits in the tour.
  std::vector<int> position(points.size(), -1);
  for (int i = 0; i < n; ++i) {
    position[tour[i]] = i;
  }

  std::vector<unsigned> cand;
  int accepted = 0;
  for (int pass = 0; pass < maxPasses; ++pass) {
    int passMoves = 0;

    // 2-Opt: reverse tour[i+1 .. j] when that shortens the path.  On a path
    // (rather than a cycle) only the two edges at the segment ends change.
    for (int i = 0; i + 2 < n; ++i) {
      grid.near(tour[i], neighbors, cand);
      for (const unsigned c : cand) {
        const int j = position[c];
        if (j <= i + 1 || j >= n) {
          continue;
        }
        const double before
            = l1(points[tour[i]], points[tour[i + 1]])
              + (j + 1 < n ? l1(points[tour[j]], points[tour[j + 1]]) : 0.0);
        const double after
            = l1(points[tour[i]], points[tour[j]])
              + (j + 1 < n ? l1(points[tour[i + 1]], points[tour[j + 1]])
                           : 0.0);
        if (after < before - 1e-12) {
          std::reverse(tour.begin() + i + 1, tour.begin() + j + 1);
          for (int k = i + 1; k <= j; ++k) {
            position[tour[k]] = k;
          }
          ++passMoves;
          break;
        }
      }
    }

    // Or-Opt: lift a run of 1 to 3 sinks and reinsert it next to one of the
    // run head's near neighbours.
    for (int segLen = 1; segLen <= 3; ++segLen) {
      for (int i = 0; i + segLen <= n; ++i) {
        grid.near(tour[i], neighbors, cand);
        double removed = 0;
        if (i > 0) {
          removed += l1(points[tour[i - 1]], points[tour[i]]);
        }
        if (i + segLen < n) {
          removed += l1(points[tour[i + segLen - 1]], points[tour[i + segLen]]);
        }
        if (i > 0 && i + segLen < n) {
          removed -= l1(points[tour[i - 1]], points[tour[i + segLen]]);
        }
        bool moved = false;
        for (const unsigned c : cand) {
          const int j = position[c];
          if (j >= i - 1 && j <= i + segLen) {
            continue;  // inside or adjacent to the segment
          }
          double added = l1(points[c], points[tour[i]]);
          if (j + 1 < n) {
            added += l1(points[tour[i + segLen - 1]], points[tour[j + 1]]);
            added -= l1(points[c], points[tour[j + 1]]);
          }
          if (added < removed - 1e-12) {
            std::vector<unsigned> seg(tour.begin() + i,
                                      tour.begin() + i + segLen);
            tour.erase(tour.begin() + i, tour.begin() + i + segLen);
            const int at = (j > i) ? (j - segLen + 1) : (j + 1);
            tour.insert(tour.begin() + at, seg.begin(), seg.end());
            for (int k = std::min(i, at); k < n; ++k) {
              position[tour[k]] = k;
            }
            ++passMoves;
            moved = true;
            break;
          }
        }
        if (moved) {
          continue;
        }
      }
    }

    accepted += passMoves;
    if (passMoves == 0) {
      break;
    }
  }
  return accepted;
}

std::vector<std::vector<unsigned>> dpRpSegment(
    const std::vector<unsigned>& tour,
    const std::vector<std::vector<double>>& sliceCost,
    const double clusterPenalty)
{
  const int n = tour.size();
  std::vector<std::vector<unsigned>> clusters;
  if (n == 0) {
    return clusters;
  }

  // best[j] is the cheapest partition of the first j sinks; from[j] is where
  // the last cluster of that partition starts.
  std::vector<double> best(n + 1, kInfeasibleCost);
  std::vector<int> from(n + 1, -1);
  best[0] = 0.0;
  for (int j = 1; j <= n; ++j) {
    const int maxLen = std::min<int>(j, sliceCost[0].size());
    for (int len = 1; len <= maxLen; ++len) {
      const int i = j - len;
      if (best[i] == kInfeasibleCost) {
        continue;
      }
      const double w = sliceCost[i][len - 1];
      if (w == kInfeasibleCost) {
        // Longer runs starting at i are infeasible too, so stop extending.
        break;
      }
      const double cand = best[i] + w + clusterPenalty;
      if (cand < best[j]) {
        best[j] = cand;
        from[j] = i;
      }
    }
  }

  if (best[n] == kInfeasibleCost) {
    return clusters;  // caller falls back to the greedy sweep
  }
  for (int j = n; j > 0;) {
    const int i = from[j];
    clusters.emplace_back(tour.begin() + i, tour.begin() + j);
    j = i;
  }
  std::reverse(clusters.begin(), clusters.end());
  return clusters;
}

}  // namespace cts

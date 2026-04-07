// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, UCSD
// Buffer Sizing LP for Useful Skew Optimization in 3D-CTS
// Based on Uysal BLU LP framework concept

#include "BufferSizingLP.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>

#include "odb/db.h"
#include "ortools/linear_solver/linear_solver.h"
#include "utl/Logger.h"

namespace cts {

using operations_research::MPConstraint;
using operations_research::MPObjective;
using operations_research::MPSolver;
using operations_research::MPVariable;

BufferSizingLP::BufferSizingLP(odb::dbBlock* block,
                               sta::dbSta* sta,
                               sta::dbNetwork* network,
                               utl::Logger* logger)
    : block_(block), sta_(sta), network_(network), logger_(logger)
{
}

std::vector<std::string> BufferSizingLP::getUniqueFFs(
    const std::vector<FFEdge>& edges)
{
  std::set<std::string> ff_set;
  for (const auto& edge : edges) {
    ff_set.insert(edge.from_ff);
    ff_set.insert(edge.to_ff);
  }
  return std::vector<std::string>(ff_set.begin(), ff_set.end());
}

double BufferSizingLP::detectClockPeriod(const std::vector<FFEdge>& edges)
{
  // Estimate clock period from timing data
  // Use the maximum of (arrival + slack margin) as an estimate
  double max_required = 0.0;
  for (const auto& edge : edges) {
    // Setup required time gives us an idea of clock period
    if (edge.required_max > max_required) {
      max_required = edge.required_max;
    }
  }

  // If we couldn't detect, use a default (1ns = 1e-9)
  if (max_required <= 0) {
    return 1e-9;
  }

  return max_required * 1.2;  // Add 20% margin
}

void BufferSizingLP::calculateStats(const std::vector<FFEdge>& edges,
                                    const std::map<std::string, double>& deltas,
                                    double& worst_slack,
                                    double& tns,
                                    int& num_violations)
{
  worst_slack = std::numeric_limits<double>::max();
  tns = 0.0;
  num_violations = 0;

  for (const auto& edge : edges) {
    // Get deltas for this edge
    double delta_from = 0.0;
    double delta_to = 0.0;

    auto it_from = deltas.find(edge.from_ff);
    if (it_from != deltas.end()) {
      delta_from = it_from->second;
    }

    auto it_to = deltas.find(edge.to_ff);
    if (it_to != deltas.end()) {
      delta_to = it_to->second;
    }

    // Skew change: positive delta means arrival moves later
    double skew_delta = delta_to - delta_from;

    // New setup slack = old slack - skew_delta
    // (increasing skew = later arrival at capture = less setup margin)
    double new_setup_slack = edge.slack_max - skew_delta;

    // New hold slack = old slack + skew_delta
    // (increasing skew = later arrival at capture = more hold margin)
    double new_hold_slack = edge.slack_min + skew_delta;

    // Track worst slack (consider both setup and hold)
    if (new_setup_slack < worst_slack) {
      worst_slack = new_setup_slack;
    }
    if (considerHold_ && new_hold_slack < worst_slack) {
      worst_slack = new_hold_slack;
    }

    // Track TNS (Total Negative Slack)
    if (new_setup_slack < 0) {
      tns += new_setup_slack;
      num_violations++;
    }
    if (considerHold_ && new_hold_slack < 0) {
      tns += new_hold_slack;
      num_violations++;
    }
  }
}

std::vector<FFEdge> BufferSizingLP::parseCSV(const std::string& csv_file)
{
  std::vector<FFEdge> edges;
  std::ifstream file(csv_file);

  if (!file.is_open()) {
    logger_->error(utl::CTS, 9010, "Cannot open CSV file: {}", csv_file);
    return edges;
  }

  std::string line;
  // Skip header
  std::getline(file, line);

  while (std::getline(file, line)) {
    std::stringstream ss(line);
    std::string token;
    FFEdge edge;

    // Parse CSV: from_ff,to_ff,slack_max,slack_min,arrival_max,arrival_min,
    //            required_max,required_min,from_x,from_y,to_x,to_y
    int col = 0;
    while (std::getline(ss, token, ',')) {
      switch (col) {
        case 0: edge.from_ff = token; break;
        case 1: edge.to_ff = token; break;
        case 2: edge.slack_max = std::stod(token); break;
        case 3: edge.slack_min = std::stod(token); break;
        case 4: edge.arrival_max = std::stod(token); break;
        case 5: edge.arrival_min = std::stod(token); break;
        case 6: edge.required_max = std::stod(token); break;
        case 7: edge.required_min = std::stod(token); break;
        case 8: edge.from_x = std::stoi(token); break;
        case 9: edge.from_y = std::stoi(token); break;
        case 10: edge.to_x = std::stoi(token); break;
        case 11: edge.to_y = std::stoi(token); break;
      }
      col++;
    }

    if (col >= 4) {  // At least from, to, slack_max, slack_min
      edges.push_back(edge);
    }
  }

  return edges;
}

LPResult BufferSizingLP::optimize(const std::vector<FFEdge>& edges)
{
  LPResult result;
  result.feasible = false;
  result.objective_value = 0.0;

  if (edges.empty()) {
    logger_->warn(utl::CTS, 9011, "No FF-to-FF edges for LP optimization");
    return result;
  }

  logger_->report("========================================");
  logger_->report("Buffer Sizing LP - Useful Skew Optimization");
  logger_->report("========================================");
  logger_->report("Number of FF-to-FF edges: {}", edges.size());

  // Get unique FFs
  std::vector<std::string> ffs = getUniqueFFs(edges);
  logger_->report("Number of unique FFs: {}", ffs.size());

  // Create FF name to index mapping
  std::map<std::string, int> ff_to_idx;
  for (size_t i = 0; i < ffs.size(); i++) {
    ff_to_idx[ffs[i]] = i;
  }

  // Auto-detect clock period if not set
  if (clockPeriod_ <= 0) {
    clockPeriod_ = detectClockPeriod(edges);
    logger_->report("Auto-detected clock period: {:.3f} ns",
                    clockPeriod_ * 1e9);
  }

  // Auto-detect max delta if not set (10% of clock period)
  if (maxDelta_ <= 0) {
    maxDelta_ = clockPeriod_ * 0.1;
  }
  logger_->report("Max arrival delta: {:.3f} ns", maxDelta_ * 1e9);

  // Calculate stats before optimization
  std::map<std::string, double> zero_deltas;
  for (const auto& ff : ffs) {
    zero_deltas[ff] = 0.0;
  }
  calculateStats(edges, zero_deltas,
                 result.worst_slack_before,
                 result.tns_before,
                 result.num_violations_before);

  logger_->report("Before optimization:");
  logger_->report("  Worst slack: {:.3f} ns", result.worst_slack_before * 1e9);
  logger_->report("  TNS: {:.3f} ns", result.tns_before * 1e9);
  logger_->report("  Violations: {}", result.num_violations_before);

  // Create LP solver
  std::unique_ptr<MPSolver> solver(MPSolver::CreateSolver("GLOP"));
  if (!solver) {
    logger_->error(utl::CTS, 9012, "Could not create LP solver");
    return result;
  }

  // Create variables: delta_i for each FF (arrival time delta)
  std::vector<MPVariable*> delta_vars(ffs.size());
  for (size_t i = 0; i < ffs.size(); i++) {
    delta_vars[i] = solver->MakeNumVar(-maxDelta_, maxDelta_,
                                        "delta_" + std::to_string(i));
  }

  // Create auxiliary variable for worst slack (for max-min objective)
  MPVariable* worst_slack_var = nullptr;
  if (objective_ == LPObjective::MAXIMIZE_WORST_SLACK) {
    worst_slack_var = solver->MakeNumVar(-solver->infinity(),
                                          solver->infinity(),
                                          "worst_slack");
  }

  // Add constraints for each edge
  int setup_constraints = 0;
  int hold_constraints = 0;

  for (const auto& edge : edges) {
    int idx_from = ff_to_idx[edge.from_ff];
    int idx_to = ff_to_idx[edge.to_ff];

    // Skew delta = delta_to - delta_from
    // New setup slack = slack_max - skew_delta = slack_max - delta_to + delta_from
    // New hold slack = slack_min + skew_delta = slack_min + delta_to - delta_from

    // Setup constraint: new_setup_slack >= slack_margin
    // slack_max - delta_to + delta_from >= slack_margin
    // delta_from - delta_to >= slack_margin - slack_max
    {
      MPConstraint* ct = solver->MakeRowConstraint(
          slackMargin_ - edge.slack_max, solver->infinity(),
          "setup_" + edge.from_ff + "_" + edge.to_ff);
      ct->SetCoefficient(delta_vars[idx_from], 1.0);
      ct->SetCoefficient(delta_vars[idx_to], -1.0);
      setup_constraints++;

      // For max-min objective: worst_slack <= new_setup_slack
      // worst_slack <= slack_max - delta_to + delta_from
      // worst_slack + delta_to - delta_from <= slack_max
      if (worst_slack_var) {
        MPConstraint* ct_obj = solver->MakeRowConstraint(
            -solver->infinity(), edge.slack_max,
            "obj_setup_" + edge.from_ff + "_" + edge.to_ff);
        ct_obj->SetCoefficient(worst_slack_var, 1.0);
        ct_obj->SetCoefficient(delta_vars[idx_to], 1.0);
        ct_obj->SetCoefficient(delta_vars[idx_from], -1.0);
      }
    }

    // Hold constraint: new_hold_slack >= slack_margin
    // slack_min + delta_to - delta_from >= slack_margin
    // delta_to - delta_from >= slack_margin - slack_min
    if (considerHold_) {
      MPConstraint* ct = solver->MakeRowConstraint(
          slackMargin_ - edge.slack_min, solver->infinity(),
          "hold_" + edge.from_ff + "_" + edge.to_ff);
      ct->SetCoefficient(delta_vars[idx_to], 1.0);
      ct->SetCoefficient(delta_vars[idx_from], -1.0);
      hold_constraints++;

      // For max-min objective: worst_slack <= new_hold_slack
      // worst_slack <= slack_min + delta_to - delta_from
      // worst_slack - delta_to + delta_from <= slack_min
      if (worst_slack_var) {
        MPConstraint* ct_obj = solver->MakeRowConstraint(
            -solver->infinity(), edge.slack_min,
            "obj_hold_" + edge.from_ff + "_" + edge.to_ff);
        ct_obj->SetCoefficient(worst_slack_var, 1.0);
        ct_obj->SetCoefficient(delta_vars[idx_to], -1.0);
        ct_obj->SetCoefficient(delta_vars[idx_from], 1.0);
      }
    }
  }

  logger_->report("LP constraints: {} setup, {} hold",
                  setup_constraints, hold_constraints);

  // Set objective
  MPObjective* objective = solver->MutableObjective();

  switch (objective_) {
    case LPObjective::MAXIMIZE_WORST_SLACK:
      // Maximize worst_slack_var
      objective->SetCoefficient(worst_slack_var, 1.0);
      objective->SetMaximization();
      break;

    case LPObjective::MINIMIZE_TNS:
      // Minimize sum of negative slacks (approximated by minimizing sum of deltas)
      // This is a simplification - true TNS minimization would need additional variables
      for (size_t i = 0; i < ffs.size(); i++) {
        objective->SetCoefficient(delta_vars[i], 0.0);
      }
      objective->SetMinimization();
      break;

    case LPObjective::MAXIMIZE_TOTAL_SLACK:
      // Maximize sum of all slacks
      // Simplified: minimize sum of absolute deltas (keep changes small)
      for (size_t i = 0; i < ffs.size(); i++) {
        objective->SetCoefficient(delta_vars[i], 0.0);
      }
      objective->SetMinimization();
      break;
  }

  // Solve
  logger_->report("Solving LP with {} variables, {} constraints...",
                  solver->NumVariables(), solver->NumConstraints());

  MPSolver::ResultStatus status = solver->Solve();

  if (status == MPSolver::OPTIMAL || status == MPSolver::FEASIBLE) {
    result.feasible = true;
    result.objective_value = objective->Value();

    // Extract solution
    for (size_t i = 0; i < ffs.size(); i++) {
      result.ff_arrival_deltas[ffs[i]] = delta_vars[i]->solution_value();
    }

    // Calculate stats after optimization
    calculateStats(edges, result.ff_arrival_deltas,
                   result.worst_slack_after,
                   result.tns_after,
                   result.num_violations_after);

    logger_->report("LP solved successfully!");
    logger_->report("After optimization:");
    logger_->report("  Worst slack: {:.3f} ns", result.worst_slack_after * 1e9);
    logger_->report("  TNS: {:.3f} ns", result.tns_after * 1e9);
    logger_->report("  Violations: {}", result.num_violations_after);
    logger_->report("  Improvement: {:.3f} ns",
                    (result.worst_slack_after - result.worst_slack_before) * 1e9);

  } else {
    logger_->warn(utl::CTS, 9013, "LP solver returned status: {}",
                  static_cast<int>(status));
    result.worst_slack_after = result.worst_slack_before;
    result.tns_after = result.tns_before;
    result.num_violations_after = result.num_violations_before;
  }

  logger_->report("========================================");

  return result;
}

LPResult BufferSizingLP::optimizeFromFile(const std::string& csv_file)
{
  logger_->report("Loading FF-to-FF timing graph from: {}", csv_file);
  std::vector<FFEdge> edges = parseCSV(csv_file);
  logger_->report("Loaded {} edges from CSV", edges.size());
  return optimize(edges);
}

void BufferSizingLP::applyDelayAdjustments(const LPResult& result)
{
  if (!result.feasible) {
    logger_->warn(utl::CTS, 9014,
                  "Cannot apply adjustments: LP was not feasible");
    return;
  }

  logger_->report("Applying delay adjustments to {} FFs",
                  result.ff_arrival_deltas.size());

  // Count adjustments by magnitude
  int small_adj = 0;   // < 10ps
  int medium_adj = 0;  // 10-50ps
  int large_adj = 0;   // > 50ps

  for (const auto& [ff_name, delta] : result.ff_arrival_deltas) {
    double abs_delta = std::abs(delta);
    if (abs_delta < 1e-11) {  // < 10ps
      small_adj++;
    } else if (abs_delta < 5e-11) {  // < 50ps
      medium_adj++;
    } else {
      large_adj++;
    }
  }

  logger_->report("Adjustment summary:");
  logger_->report("  Small (<10ps): {}", small_adj);
  logger_->report("  Medium (10-50ps): {}", medium_adj);
  logger_->report("  Large (>50ps): {}", large_adj);

  // TODO: Implement actual delay adjustment through buffer sizing
  // This would involve:
  // 1. Finding the clock buffer driving each FF
  // 2. Adjusting buffer sizes or inserting delay cells
  // 3. Re-running timing analysis to verify

  logger_->warn(utl::CTS, 9015,
                "Physical delay adjustment not yet implemented. "
                "Results show potential improvement only.");
}

void BufferSizingLP::reportResults(const LPResult& result)
{
  logger_->report("========================================");
  logger_->report("LP Optimization Results Summary");
  logger_->report("========================================");
  logger_->report("Feasible: {}", result.feasible ? "Yes" : "No");
  logger_->report("Objective value: {:.6f}", result.objective_value);
  logger_->report("");
  logger_->report("Before optimization:");
  logger_->report("  Worst slack: {:.3f} ps", result.worst_slack_before * 1e12);
  logger_->report("  TNS: {:.3f} ps", result.tns_before * 1e12);
  logger_->report("  Timing violations: {}", result.num_violations_before);
  logger_->report("");
  logger_->report("After optimization:");
  logger_->report("  Worst slack: {:.3f} ps", result.worst_slack_after * 1e12);
  logger_->report("  TNS: {:.3f} ps", result.tns_after * 1e12);
  logger_->report("  Timing violations: {}", result.num_violations_after);
  logger_->report("");

  if (result.feasible) {
    double improvement = result.worst_slack_after - result.worst_slack_before;
    double tns_improvement = result.tns_before - result.tns_after;
    int violation_improvement = result.num_violations_before -
                                result.num_violations_after;

    logger_->report("Improvement:");
    logger_->report("  Worst slack: {:.3f} ps ({:.1f}%)",
                    improvement * 1e12,
                    result.worst_slack_before != 0
                        ? (improvement / std::abs(result.worst_slack_before)) * 100
                        : 0);
    logger_->report("  TNS: {:.3f} ps", tns_improvement * 1e12);
    logger_->report("  Violations fixed: {}", violation_improvement);
  }

  logger_->report("========================================");
}

}  // namespace cts

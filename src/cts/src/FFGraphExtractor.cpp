// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, UCSD
// FF-to-FF Sequential Timing Graph Extractor for 3D-CTS

#include "FFGraphExtractor.h"

#include <fstream>
#include <iostream>
#include <string>

#include "odb/db.h"
#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "sta/Graph.hh"
#include "sta/Network.hh"
#include "sta/PathExpanded.hh"
#include "sta/Sta.hh"
#include "utl/Logger.h"

namespace cts {

FFGraphExtractor::FFGraphExtractor(odb::dbBlock* block,
                                   sta::dbSta* sta,
                                   sta::dbNetwork* network,
                                   utl::Logger* logger)
    : block_(block), sta_(sta), network_(network), logger_(logger)
{
}

bool FFGraphExtractor::isSequential(odb::dbInst* inst)
{
  odb::dbMaster* master = inst->getMaster();
  return master->isSequential();
}

void FFGraphExtractor::getInstLocation(odb::dbInst* inst, int& x, int& y)
{
  inst->getLocation(x, y);
}

std::vector<odb::dbInst*> FFGraphExtractor::getSequentialInsts()
{
  std::vector<odb::dbInst*> seq_insts;

  for (odb::dbInst* inst : block_->getInsts()) {
    if (isSequential(inst)) {
      seq_insts.push_back(inst);
    }
  }

  return seq_insts;
}

void FFGraphExtractor::extractGraph(const std::string& output_file)
{
  logger_->report("========================================");
  logger_->report("FF-to-FF Timing Graph Extraction");
  logger_->report("Using ODB + STA APIs");
  logger_->report("========================================");

  // Clear previous edges
  edges_.clear();

  // Step 1: Get all sequential instances using ODB
  std::vector<odb::dbInst*> seq_insts = getSequentialInsts();
  logger_->report("Sequential instances (FFs): {}", seq_insts.size());

  // Build map from instance name to dbInst for location lookup
  // Include ALL instances, not just sequential ones
  std::map<std::string, odb::dbInst*> inst_map;
  for (odb::dbInst* inst : block_->getInsts()) {
    inst_map[inst->getName()] = inst;
  }

  // Step 2: Ensure timing is updated
  sta_->ensureGraph();
  sta_->ensureClkArrivals();
  sta_->updateTiming(false);

  sta::Graph* graph = sta_->graph();
  if (!graph) {
    logger_->error(utl::CTS, 9001, "STA graph not available");
    return;
  }

  logger_->report("Timing graph vertices: {}", graph->vertexCount());

  // Step 3: Open output CSV file
  std::ofstream out(output_file);
  if (!out.is_open()) {
    logger_->error(utl::CTS, 9002, "Cannot open output file: {}", output_file);
    return;
  }

  // Write CSV header (including location info)
  out << "from_ff,to_ff,slack_ns,arrival_ns,required_ns,from_x,from_y,to_x,to_y\n";

  int path_count = 0;
  int vertex_count = 0;

  // Step 4: Iterate through all vertices
  sta::VertexIterator vertex_iter(graph);
  while (vertex_iter.hasNext()) {
    sta::Vertex* vertex = vertex_iter.next();
    vertex_count++;

    if (vertex_count % 1000 == 0) {
      logger_->report("  Processed {} vertices, found {} paths...",
                      vertex_count, path_count);
    }

    sta::Pin* pin = vertex->pin();
    if (!pin) {
      continue;
    }

    // Check if this is a D pin (FF input)
    const char* pin_name = network_->pathName(pin);
    std::string pin_str(pin_name);

    if (pin_str.size() < 2 || pin_str.substr(pin_str.size() - 2) != "/D") {
      continue;
    }

    // Get worst slack path to this endpoint
    sta::Path* path = sta_->vertexWorstSlackPath(vertex, sta::MinMax::max());
    if (!path) {
      continue;
    }

    // Get timing values
    sta::Slack slack = path->slack(sta_);
    sta::Arrival arrival = path->arrival();
    sta::Required required = sta_->vertexRequired(vertex, sta::MinMax::max());

    // Get start point using path expansion
    sta::PathExpanded expanded(path, sta_);
    if (expanded.size() == 0) {
      continue;
    }

    const sta::Path* start_path = expanded.startPath();
    if (!start_path) {
      continue;
    }

    sta::Pin* start_pin = start_path->pin(sta_);
    if (!start_pin) {
      continue;
    }

    const char* start_pin_name = network_->pathName(start_pin);
    std::string start_pin_str(start_pin_name);

    // Check if start pin is Q or QN (FF output)
    if (start_pin_str.size() < 2) {
      continue;
    }

    std::string suffix = start_pin_str.substr(start_pin_str.size() - 2);
    if (suffix != "/Q" && suffix != "QN") {
      if (start_pin_str.size() >= 3) {
        suffix = start_pin_str.substr(start_pin_str.size() - 3);
        if (suffix != "/QN") {
          continue;
        }
      } else {
        continue;
      }
    }

    // Extract FF instance names
    std::string from_ff = start_pin_str;
    std::string to_ff = pin_str;

    size_t from_slash = from_ff.find_last_of('/');
    if (from_slash != std::string::npos) {
      from_ff = from_ff.substr(0, from_slash);
    }

    size_t to_slash = to_ff.find_last_of('/');
    if (to_slash != std::string::npos) {
      to_ff = to_ff.substr(0, to_slash);
    }

    // Get locations using ODB
    int from_x = 0, from_y = 0, to_x = 0, to_y = 0;

    auto from_it = inst_map.find(from_ff);
    if (from_it != inst_map.end()) {
      getInstLocation(from_it->second, from_x, from_y);
    }

    auto to_it = inst_map.find(to_ff);
    if (to_it != inst_map.end()) {
      getInstLocation(to_it->second, to_x, to_y);
    }

    // Store edge
    FFEdge edge;
    edge.from_ff = from_ff;
    edge.to_ff = to_ff;
    edge.slack = sta::delayAsFloat(slack);
    edge.arrival = sta::delayAsFloat(arrival);
    edge.required = sta::delayAsFloat(required);
    edge.from_x = from_x;
    edge.from_y = from_y;
    edge.to_x = to_x;
    edge.to_y = to_y;
    edges_.push_back(edge);

    // Write to CSV
    out << from_ff << "," << to_ff << ","
        << sta::delayAsFloat(slack) << ","
        << sta::delayAsFloat(arrival) << ","
        << sta::delayAsFloat(required) << ","
        << from_x << "," << from_y << ","
        << to_x << "," << to_y << "\n";

    path_count++;
  }

  out.close();

  logger_->report("========================================");
  logger_->report("COMPLETE");
  logger_->report("========================================");
  logger_->report("Vertices processed: {}", vertex_count);
  logger_->report("FF-to-FF paths found: {}", path_count);
  logger_->report("Total FFs in design: {}", seq_insts.size());
  logger_->report("Output: {}", output_file);
  logger_->report("========================================");
}

}  // namespace cts

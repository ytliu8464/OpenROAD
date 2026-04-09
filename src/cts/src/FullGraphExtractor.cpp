// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, UCSD
// Full Netlist Graph Extractor — dumps all cells and pin-to-pin
// connectivity from ODB for external sequential-graph weight computation.

#include "FullGraphExtractor.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "odb/db.h"
#include "utl/Logger.h"

namespace cts {

FullGraphExtractor::FullGraphExtractor(odb::dbBlock* block,
                                       utl::Logger* logger)
    : block_(block), logger_(logger)
{
}

void FullGraphExtractor::extractFullGraph(const std::string& output_base)
{
  logger_->report("==========================================");
  logger_->report("Full Netlist Graph Extraction (ODB)");
  logger_->report("==========================================");
  logger_->report("Output base: {}", output_base);

  std::string nodes_path = output_base + ".nodes.csv";
  std::string edges_path = output_base + ".edges.csv";

  writeNodes(nodes_path);
  writeEdges(edges_path);

  logger_->report("==========================================");
  logger_->report("Full graph extraction complete!");
  logger_->report("  nodes: {}", nodes_path);
  logger_->report("  edges: {}", edges_path);
  logger_->report("==========================================");
}

void FullGraphExtractor::writeNodes(const std::string& path)
{
  std::ofstream out(path);
  if (!out.is_open()) {
    logger_->error(utl::CTS, 9016, "Cannot open nodes file: {}", path);
    return;
  }

  out << "name,type,master,area_dbu2,is_sequential,is_macro,is_io,x,y\n";

  int inst_count = 0;
  int ff_count = 0;
  int macro_count = 0;
  int combo_count = 0;
  int io_count = 0;

  for (odb::dbInst* inst : block_->getInsts()) {
    odb::dbMaster* master = inst->getMaster();
    const std::string inst_name = inst->getName();
    const std::string master_name = master->getName();
    const int64_t area = master->getArea();
    const bool is_seq = master->isSequential();
    const bool is_macro = master->isBlock();

    std::string type;
    if (is_macro) {
      type = "MACRO";
      macro_count++;
    } else if (is_seq) {
      type = "FF";
      ff_count++;
    } else {
      type = "COMBO";
      combo_count++;
    }

    int x = 0, y = 0;
    inst->getLocation(x, y);

    out << inst_name << ',' << type << ',' << master_name << ','
        << area << ',' << (is_seq ? 1 : 0) << ',' << (is_macro ? 1 : 0)
        << ",0," << x << ',' << y << '\n';
    inst_count++;
  }

  for (odb::dbBTerm* bterm : block_->getBTerms()) {
    odb::dbSigType sig = bterm->getSigType();
    if (sig == odb::dbSigType::POWER || sig == odb::dbSigType::GROUND) {
      continue;
    }

    const std::string name = bterm->getName();
    odb::dbIoType io = bterm->getIoType();
    std::string type;
    std::string master;
    if (io == odb::dbIoType::INPUT) {
      type = "PI";
      master = "__PI__";
    } else if (io == odb::dbIoType::OUTPUT) {
      type = "PO";
      master = "__PO__";
    } else {
      type = "PIO";
      master = "__PIO__";
    }

    int x = 0, y = 0;
    odb::Rect bbox = bterm->getBBox();
    x = (bbox.xMin() + bbox.xMax()) / 2;
    y = (bbox.yMin() + bbox.yMax()) / 2;

    out << name << ',' << type << ',' << master
        << ",0,0,0,1," << x << ',' << y << '\n';
    io_count++;
  }

  out.close();

  logger_->report("Nodes written: {} total ({} FF, {} combo, {} macro, {} IO)",
                  inst_count + io_count, ff_count, combo_count,
                  macro_count, io_count);
}

void FullGraphExtractor::writeEdges(const std::string& path)
{
  std::ofstream out(path);
  if (!out.is_open()) {
    logger_->error(utl::CTS, 9017, "Cannot open edges file: {}", path);
    return;
  }

  out << "src_node,src_pin,dst_node,dst_pin,net\n";

  int64_t edge_count = 0;
  int net_count = 0;

  for (odb::dbNet* net : block_->getNets()) {
    odb::dbSigType sig = net->getSigType();
    if (sig == odb::dbSigType::POWER || sig == odb::dbSigType::GROUND) {
      continue;
    }

    const std::string net_name = net->getName();

    // Collect drivers and sinks from instance terminals
    struct PinRef {
      std::string node;
      std::string pin;
    };
    std::vector<PinRef> drivers;
    std::vector<PinRef> sinks;

    for (odb::dbITerm* iterm : net->getITerms()) {
      odb::dbMTerm* mterm = iterm->getMTerm();
      odb::dbIoType io = mterm->getIoType();
      if (io == odb::dbIoType::INOUT) {
        continue;
      }

      PinRef ref;
      ref.node = iterm->getInst()->getName();
      ref.pin = mterm->getName();

      if (io == odb::dbIoType::OUTPUT) {
        drivers.push_back(std::move(ref));
      } else {
        sinks.push_back(std::move(ref));
      }
    }

    // Block terminals: INPUT bterms are drivers, OUTPUT bterms are sinks
    for (odb::dbBTerm* bterm : net->getBTerms()) {
      odb::dbIoType io = bterm->getIoType();
      PinRef ref;
      ref.node = bterm->getName();
      ref.pin = "__port__";

      if (io == odb::dbIoType::INPUT) {
        drivers.push_back(std::move(ref));
      } else if (io == odb::dbIoType::OUTPUT) {
        sinks.push_back(std::move(ref));
      }
    }

    for (const auto& drv : drivers) {
      for (const auto& snk : sinks) {
        out << drv.node << ',' << drv.pin << ','
            << snk.node << ',' << snk.pin << ','
            << net_name << '\n';
        edge_count++;
      }
    }

    net_count++;
  }

  out.close();

  logger_->report("Edges written: {} (from {} nets)", edge_count, net_count);
}

}  // namespace cts

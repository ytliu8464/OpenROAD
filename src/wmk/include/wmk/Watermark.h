// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
//
// Routing watermark.
//
// Implements the routing watermarking scheme of
//   A. B. Kahng, S. Mantik, I. L. Markov, M. Potkonjak, P. Tucker,
//   H. Wang and G. Wolfe, "Robust IP Watermarking Methodologies for
//   Physical Design", in ISPD'98.
//
// The idea: hash a user-supplied message to deterministically select a
// subset of signal nets (the watermark nets) and impose a strong upper
// bound on the amount of wrong-way (non-preferred-direction) wiring used
// to route them.  Detection compares, for every net, the ratio
// WL_way / WL_tot; watermark nets are expected to rank at the low end.
//
// This header is the public API for the wmk module; it is SWIG-wrapped
// for Tcl.

#pragma once

#include <string>
#include <vector>

#include "odb/db.h"
#include "utl/Logger.h"

namespace wmk {

class Watermark
{
 public:
  Watermark(odb::dbDatabase* db, utl::Logger* logger);

  // Deterministically pick round(fraction * N_signal_nets) nets from the
  // block's signal nets (non-special, non-supply, non-clock) using a
  // PRNG seeded with MD5(message).  Tags each chosen net with a
  // dbBoolProperty named "watermark".  Returns the number of nets tagged.
  //
  // If a nets are already tagged from a prior call, they are cleared
  // first so repeated invocations are idempotent.
  int selectNets(const std::string& message, double fraction);

  // After detailed routing, classify every signal net as watermarked
  // (successfully) or not based on the ratio r = WL_way / WL_tot.
  // A net "passes" if its rank among all signal nets (sorted by
  // ascending r) is below the p-quantile.  Prints:
  //   X  = number of watermark nets
  //   s  = number that passed (below cutoff)
  //   x  = number that failed  (X - s)
  //   Pc = signature strength (binomial tail)
  // Returns the computed Pc.
  double reportWatermark(double p = 0.4);

  // Clear all "watermark" dbBoolProperty tags on nets in the current
  // block.  Useful for iterating on signature-selection parameters
  // without re-loading the design.
  int clearWatermark();

 private:
  odb::dbDatabase* db_ = nullptr;
  utl::Logger* logger_ = nullptr;
};

}  // namespace wmk

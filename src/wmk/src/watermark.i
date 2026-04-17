// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

%{
#include <string>
#include "ord/OpenRoad.hh"
#include "wmk/Watermark.h"
%}

%include "../../Exception.i"
%include <std_string.i>

%inline %{

int
set_routing_watermark_cmd(const char* message, double fraction)
{
  auto* w = ord::OpenRoad::openRoad()->getWatermark();
  return w->selectNets(std::string(message), fraction);
}

double
report_routing_watermark_cmd(double p)
{
  auto* w = ord::OpenRoad::openRoad()->getWatermark();
  return w->reportWatermark(p);
}

int
clear_routing_watermark_cmd()
{
  auto* w = ord::OpenRoad::openRoad()->getWatermark();
  return w->clearWatermark();
}

%}  // inline

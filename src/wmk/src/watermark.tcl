# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

sta::define_cmd_args "set_routing_watermark" {-message message \
                                              [-fraction fraction]}

proc set_routing_watermark { args } {
  sta::parse_key_args "set_routing_watermark" args \
    keys {-message -fraction} flags {}

  if { ![info exists keys(-message)] } {
    utl::error WMK 20 "The -message argument is required."
  }
  set message $keys(-message)

  set fraction 0.05
  if { [info exists keys(-fraction)] } {
    set fraction $keys(-fraction)
  }
  if { $fraction <= 0.0 || $fraction > 1.0 } {
    utl::error WMK 21 "The -fraction argument must be in (0, 1]."
  }

  wmk::set_routing_watermark_cmd $message $fraction
}

sta::define_cmd_args "report_routing_watermark" {[-p p]}

proc report_routing_watermark { args } {
  sta::parse_key_args "report_routing_watermark" args \
    keys {-p} flags {}

  set p 0.4
  if { [info exists keys(-p)] } {
    set p $keys(-p)
  }
  if { $p <= 0.0 || $p >= 1.0 } {
    utl::error WMK 22 "The -p argument must be in (0, 1)."
  }

  wmk::report_routing_watermark_cmd $p
}

proc clear_routing_watermark { args } {
  wmk::clear_routing_watermark_cmd
}

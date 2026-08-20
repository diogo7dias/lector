#pragma once

// The PLL byte the X3 (UC8253) panel is initialised with, i.e. its frame clock.
//
// Read from /perf/pll.txt on the card when that file exists, so a candidate can be tried
// by editing one file and power-cycling rather than by building and flashing a firmware
// per value. Anything unparseable, out of range, or absent gives the driver's stock 0x09.
//
// See src/platform/LectorUc8253X3Config.cpp for why the value is worth sweeping at all,
// and what has to stay clean before one is landed as the default.
unsigned char lectorX3PllByte();

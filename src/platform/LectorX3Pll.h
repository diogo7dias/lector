#pragma once

// The PLL byte the X3 (UC8253) panel is initialised with, i.e. its frame clock.
//
// Read from /perf/pll.txt on the card when that file exists, so a candidate can be tried
// by editing one file and power-cycling rather than by building and flashing a firmware
// per value. Anything unparseable, out of range, or absent gives the landed default, the
// fastest byte measured to still print clean on this panel.
//
// See src/platform/LectorUc8253X3Config.cpp for why the value is worth sweeping at all,
// and what has to stay clean before one is landed as the default.
unsigned char lectorX3PllByte();

// The vendor settle after a non-differential waveform, in milliseconds.
//
// Read from /perf/settle.txt on the card when that file exists, for the same reason the
// PLL byte is: it is a value nobody has ever verified against this panel, and sweeping it
// by rebuilding once per candidate is how a sweep does not get done. Anything unparseable
// or out of range gives the driver's own 200.
//
// It is paid on every HALF and every FULL that does not power the panel down, so it
// recurs on every clean the anti-ghost policy forces. See
// src/platform/LectorUc8253X3Config.cpp for what has to stay clean before a value lands.
unsigned short lectorX3SettleMs();

#pragma once

/* The real value is injected at compile time via xmake.lua's add_defines
 * (TMC_PC_VERSION="...") so the version is sourced from a single place
 * (top of xmake.lua). The string below is only a fallback for editor /
 * IDE indexers that don't see the build flags. */
#ifndef TMC_PC_VERSION
#define TMC_PC_VERSION "0.8.3"
#endif

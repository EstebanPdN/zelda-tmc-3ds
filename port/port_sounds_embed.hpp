#pragma once

#include <cstddef>

/* Compile-time embedded fallback for assets/sounds.json.
 *
 * The audio backend (port_m4a_backend.cpp) probes the on-disk
 * locations first (next to the binary, then a few cwd-relative dev
 * paths). When none of them hit — typical for a bare `tmc_pc +
 * baserom.gba` install — it falls back to this byte array so songs
 * still play without the user having to copy a separate file.
 *
 * The array is regenerated from assets/sounds.json on every tmc_pc
 * build by tools/generate_sounds_embed.py (invoked from xmake.lua's
 * before_build hook). When assets/sounds.json is absent at build
 * time, kSize is 0 and the audio backend treats it as "no embedded
 * fallback". */
namespace PortSoundsEmbed {

extern const unsigned char kData[];
extern const std::size_t kSize;

}  // namespace PortSoundsEmbed

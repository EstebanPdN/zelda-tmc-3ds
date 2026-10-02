/* Embedded copy of assets/sounds.json baked into the asset_extractor binary
 * at build time via xmake's utils.bin2c rule. The rule writes a raw
 *   0xNN, 0xNN, ...
 * byte sequence into <autogen>/rules/utils/bin2c/sounds.json.h which we
 * include inside the array initializer below.
 *
 * Purpose: guarantee the file shows up next to the binary even when a
 * release tarball forgets to ship it (the v0.1.6 packaging bug behind
 * issue #50). assets_extractor_main.cpp writes this blob to
 *   <exe_dir>/sounds.json
 * after the asset extraction finishes — runtime port_m4a_backend.cpp's
 * search path picks it up at exactly that location.
 *
 * Sized via array decay (not strlen) so binary content stays intact even
 * if it ever stops being valid UTF-8 / contains an embedded NUL. */

#include <cstddef>

/* `extern` is required because `const` at namespace scope defaults to
 * internal linkage in C++ — without it the symbols never escape this
 * translation unit and the linker can't find them. The `extern "C"`
 * suppresses C++ name mangling so the declaration in
 * assets_extractor_main.cpp matches. */
extern "C" {

extern const unsigned char kEmbeddedSoundsJson[];
const unsigned char kEmbeddedSoundsJson[] = {
#include "sounds.json.h"
};

extern const std::size_t kEmbeddedSoundsJsonSize;
const std::size_t kEmbeddedSoundsJsonSize = sizeof(kEmbeddedSoundsJson);

} /* extern "C" */

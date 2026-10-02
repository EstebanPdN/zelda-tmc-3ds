#include "spriteframe.h"
#include "reader.h"
#include "simple_format.h"
#include <util/file.h>

void SpriteFrameAsset::convertToHumanReadable(const std::vector<char>& baserom) {
    Reader reader(baserom, start, size);
    auto file = util::open_file(assetPath.string(), "w");
    while (reader.cursor < size) {
        u8 num_gfx_tiles = reader.read_u8();
        u8 unk = reader.read_u8();
        u16 first_gfx_tile_index = reader.read_u16();

        auto line = assetfmt::Format("\tsprite_frame first_tile_index={:#x}", first_gfx_tile_index);
        line += opt_param(", num_tiles={}", 0, num_gfx_tiles);
        line += opt_param(", unknown={:#x}", 0, unk);
        std::fputs(line.c_str(), file.get());
        std::fputc('\n', file.get());
    }
}

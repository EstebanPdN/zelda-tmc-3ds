/* tloz-tmc-ua: host test for port/port_ua_panel.h — prints the font byte codes the
 * panel hook produces, for ua/check.sh and for rendering with the real font. */
#include "gba/types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u8* gRomData; u32 gRomSize;
#include "port_ua_panel.h"

int main(int argc, char** argv) {
    static u8 rom[0x1000000];
    int big = argc > 1 && strcmp(argv[1], "big") == 0;
    char line[512];
    memset(rom + 0xFFFFF0, 0xFF, 16);
    if (!(argc > 2 && strcmp(argv[2], "off") == 0)) {
        memcpy(rom + 0xFFFFF0, "TMC-UA\0\1", 8);
    }
    gRomData = rom; gRomSize = sizeof rom;
    while (fgets(line, sizeof line, stdin)) {
        char buf[256];
        const char* out;
        line[strcspn(line, "\r\n")] = 0;
        out = Port_UA_PanelText(line, buf, sizeof buf, big);
        printf("%s\t", line);
        for (const unsigned char* p = (const unsigned char*)out; *p; p++) printf("%02x", *p);
        printf("\n");
    }
    return 0;
}

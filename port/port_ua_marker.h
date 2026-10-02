/**
 * @file port_ua_marker.h
 * @brief ROM marker of the tloz-tmc-ua Ukrainian build.
 *
 * Written by tloz-tmc-ua/build-port-rom.py at offset 0xFFFFF0:
 * 8 bytes "TMC-UA" '\0' <format version>. The area is 0xFF padding in the
 * retail ROM and is never read by the game. Shared by port_ua.h (loaded ROM)
 * and port_ua_splash.h (ROM file on the SD card, before it is loaded).
 */
#ifndef PORT_UA_MARKER_H
#define PORT_UA_MARKER_H

#define PORT_UA_MARKER_OFFSET 0xFFFFF0u
#define PORT_UA_MARKER "TMC-UA"
#define PORT_UA_MARKER_LEN 6u

#endif /* PORT_UA_MARKER_H */

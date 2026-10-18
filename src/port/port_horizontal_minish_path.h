#ifndef PORT_HORIZONTAL_MINISH_PATH_H
#define PORT_HORIZONTAL_MINISH_PATH_H

/* Each leaf plane is 128 tiles wide. Keep both the native 32-tile DMA window
 * and the wider visible span within that plane, including transition cameras
 * briefly before the room origin. Never let an unsigned offset freeze a page
 * while its fine scroll continues to wrap. */
static inline int Port_HorizontalMinishPathScroll(int scroll, int roomWidth, int shift, int viewWidth) {
    if (scroll < 0)
        scroll = 0;
    int offset = scroll + (scroll >> shift) + (1024 - roomWidth) / 2;
    int span = viewWidth > 256 ? viewWidth : 256;
    if (span > 1024)
        span = 1024;
    if (offset < 0)
        offset = 0;
    if (offset > 1024 - span)
        offset = 1024 - span;
    return offset;
}
#endif

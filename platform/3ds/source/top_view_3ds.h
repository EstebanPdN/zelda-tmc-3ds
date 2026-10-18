#ifndef TOP_VIEW_3DS_H
#define TOP_VIEW_3DS_H

/* Logical source geometry for the native 160-line renderer. */
typedef struct TopView3DSSource {
    int renderWidth, renderHeight;
    int sourceX, sourceY, sourceWidth, sourceHeight;
} TopView3DSSource;

enum {
    TOP_VIEW_3DS_ASPECT_WIDE = 0,
    TOP_VIEW_3DS_ASPECT_ORIGINAL,
    TOP_VIEW_3DS_ASPECT_STRETCH,
};

enum {
    TOP_VIEW_3DS_DISPLAY_BLUR = 0,
    TOP_VIEW_3DS_DISPLAY_BILINEAR,
    TOP_VIEW_3DS_DISPLAY_PIXEL_PERFECT,
};

typedef struct TopView3DSPlan {
    TopView3DSSource source;
    int drawX;
    int drawY;
    int drawWidth;
    int drawHeight;
    int linearFilter;
    int useSharpBilinear;
    int sharpBilinearScale;
} TopView3DSPlan;

/* Pure presentation policy shared by CPU uploads and the GPU texture path.
 * Aspect and filter are independent. Pixel Perfect centers the native source
 * without changing camera geometry; Blur/Bilinear retain their existing scaling. */
void TopView3DS_BuildPlan(int aspect, int displayStyle, int renderWidth, TopView3DSPlan* plan);

#endif /* TOP_VIEW_3DS_H */

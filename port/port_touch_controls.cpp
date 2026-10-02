#include "port_touch_controls.h"
#include "port_runtime_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef __ANDROID__

/* Engine-side helper (port_linked_stubs.c): 1 while the R button has a
 * specific context action, driving the R face-button glow. */
extern "C" int Port_TouchControls_RActionAvailable(void);

namespace {

constexpr float kPi = 3.14159265f;

struct TouchPoint {
    int64_t id;
    float x;
    float y;
};

enum class TouchShape {
    Circle,
    Stadium,
};

struct TouchZone {
    PortInput input;
    TouchShape shape;
    float cx;
    float cy;
    float radius;
    SDL_FRect bounds;
    const char* label;
};

struct JoyGeom {
    float cx;
    float cy;
    float outerR;
    float knobR;
};

struct DpadGeom {
    float cx;
    float cy;
    float half;
    float arm;
};

bool IsDpadScheme() {
    return Port_Config_TouchScheme() == PORT_TOUCH_SCHEME_DPAD;
}

bool sVisible = true;
std::vector<TouchPoint> sTouches;
std::array<bool, PORT_INPUT_COUNT> sHeld{};
int sLastWindowW = 0;
int sLastWindowH = 0;

bool sJoyActive = false;
int64_t sJoyFinger = 0;
float sJoyKnobDx = 0.f;
float sJoyKnobDy = 0.f;
/* Floating-stick anchor: set on grab, cleared on release. While set, the
 * stick's live center is (sJoyFloatX, sJoyFloatY) instead of the home
 * position from BuildJoyGeom. */
bool sJoyFloating = false;
float sJoyFloatX = 0.f;
float sJoyFloatY = 0.f;

bool sSettingsRequested = false;

float Clamp(float value, float lo, float hi) {
    return std::max(lo, std::min(value, hi));
}

float MinDim(int w, int h) {
    return static_cast<float>(std::max(1, std::min(w, h)));
}

SDL_FRect MakeCentered(float cx, float cy, float w, float h) {
    return SDL_FRect{ cx - w * 0.5f, cy - h * 0.5f, w, h };
}

bool HitCircle(float cx, float cy, float r, float x, float y) {
    const float dx = x - cx;
    const float dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

bool HitStadium(const SDL_FRect& r, float x, float y) {
    const float h = r.h;
    const float ry = h * 0.5f;
    const float cxL = r.x + ry;
    const float cxR = r.x + r.w - ry;
    const float cy = r.y + ry;
    if (HitCircle(cxL, cy, ry, x, y)) {
        return true;
    }
    if (HitCircle(cxR, cy, ry, x, y)) {
        return true;
    }
    return x >= r.x + ry && x <= r.x + r.w - ry && y >= r.y && y <= r.y + r.h;
}

bool ZoneContains(const TouchZone& z, float x, float y) {
    if (z.shape == TouchShape::Circle) {
        return HitCircle(z.cx, z.cy, z.radius, x, y);
    }
    return HitStadium(z.bounds, x, y);
}

float LayoutUnit(int w, int h) {
    return Clamp(MinDim(w, h) * 0.095f * Port_Config_TouchScale(), 40.0f, 170.0f);
}

JoyGeom BuildJoyGeom(int w, int h) {
    const float fw = static_cast<float>(std::max(1, w));
    const float fh = static_cast<float>(std::max(1, h));
    const float unit = LayoutUnit(w, h);
    JoyGeom g;
    g.cx = fw * 0.19f;
    g.cy = fh * 0.76f;
    g.outerR = Clamp(unit * 1.05f, 70.0f, 138.0f);
    g.knobR = Clamp(unit * 0.33f, 24.0f, 46.0f);
    return g;
}

struct SettingsBtnGeom {
    float cx;
    float cy;
    float r;
};

SettingsBtnGeom BuildSettingsBtnGeom(int w, int h) {
    const float fh = static_cast<float>(std::max(1, h));
    const float unit = LayoutUnit(w, h);
    SettingsBtnGeom g;
    g.r = Clamp(unit * 0.50f, 32.f, 60.f);
    g.cx = g.r + unit * 0.35f;
    g.cy = fh * 0.50f;
    return g;
}

DpadGeom BuildDpadGeom(int w, int h) {
    const float fw = static_cast<float>(std::max(1, w));
    const float fh = static_cast<float>(std::max(1, h));
    const float unit = LayoutUnit(w, h);
    DpadGeom g;
    g.cx = fw * 0.19f;
    g.cy = fh * 0.76f;
    g.half = Clamp(unit * 1.20f, 80.0f, 160.0f);
    g.arm = g.half * 0.62f;
    return g;
}

float JoyMaxTravel(const JoyGeom& g) {
    return std::max(10.0f, g.outerR - g.knobR - 8.0f);
}

/* Face cluster: B / A / R arranged on the thumb arc. R is TMC's context
 * action (talk / read / lift / open / roll) — as heavily used as A/B — so
 * it lives with them instead of a far-corner pill. The engine-driven glow
 * (Port_TouchControls_RActionAvailable) tells the player when R will do
 * something specific. */
std::array<TouchZone, 6> BuildButtonZones(int w, int h) {
    const float fw = static_cast<float>(std::max(1, w));
    const float fh = static_cast<float>(std::max(1, h));
    const float unit = LayoutUnit(w, h);
    const float gap = unit * 0.98f;
    const float faceR = unit * 0.53f;
    const float faceX = fw * 0.83f;
    const float faceY = fh * 0.72f;
    const float smallH = unit * 0.70f;
    const float smallW = unit * 1.42f;
    const float selW = unit * 1.95f;
    const float staW = unit * 1.55f;

    return { {
        { PORT_INPUT_B, TouchShape::Circle, faceX - gap * 0.62f, faceY + gap * 0.42f, faceR, {}, "B" },
        { PORT_INPUT_A, TouchShape::Circle, faceX + gap * 0.45f, faceY - gap * 0.10f, faceR, {}, "A" },
        { PORT_INPUT_R, TouchShape::Circle, faceX - gap * 0.42f, faceY - gap * 0.62f, faceR, {}, "R" },
        { PORT_INPUT_SELECT, TouchShape::Stadium, 0, 0, 0, MakeCentered(fw * 0.40f, fh * 0.90f, selW, smallH),
          "Select" },
        { PORT_INPUT_START, TouchShape::Stadium, 0, 0, 0, MakeCentered(fw * 0.58f, fh * 0.90f, staW, smallH), "Start" },
        { PORT_INPUT_L, TouchShape::Stadium, 0, 0, 0, MakeCentered(fw * 0.16f, fh * 0.10f, smallW, smallH), "L" },
    } };
}

bool HitAnyButtonZone(float x, float y, int w, int h) {
    for (const TouchZone& z : BuildButtonZones(w, h)) {
        if (ZoneContains(z, x, y)) {
            return true;
        }
    }
    return false;
}

void FillCircle(SDL_Renderer* ren, float cx, float cy, float rad, Uint8 rr, Uint8 gg, Uint8 bb, Uint8 aa) {
    if (rad <= 0.0f) {
        return;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, rr, gg, bb, aa);
    const int y0 = static_cast<int>(std::floor(cy - rad));
    const int y1 = static_cast<int>(std::ceil(cy + rad));
    for (int yi = y0; yi <= y1; ++yi) {
        const float dy = static_cast<float>(yi) + 0.5f - cy;
        const float inner = rad * rad - dy * dy;
        if (inner < 0.0f) {
            continue;
        }
        const float half = std::sqrt(inner);
        const SDL_FRect line = { cx - half, static_cast<float>(yi), half * 2.0f, 1.0f };
        SDL_RenderFillRect(ren, &line);
    }
}

void StrokeCircle(SDL_Renderer* ren, float cx, float cy, float rad, Uint8 rr, Uint8 gg, Uint8 bb, Uint8 aa) {
    if (rad <= 0.0f) {
        return;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, rr, gg, bb, aa);
    constexpr int kSeg = 40;
    float px = cx + rad;
    float py = cy;
    for (int i = 1; i <= kSeg; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(kSeg)) * (2.0f * kPi);
        const float nx = cx + std::cos(t) * rad;
        const float ny = cy + std::sin(t) * rad;
        SDL_RenderLine(ren, px, py, nx, ny);
        px = nx;
        py = ny;
    }
}

void FillStadium(SDL_Renderer* ren, const SDL_FRect& r, Uint8 rr, Uint8 gg, Uint8 bb, Uint8 aa) {
    const float h = r.h;
    if (h <= 0.f || r.w <= 0.f) {
        return;
    }
    const float ry = h * 0.5f;
    const float cxL = r.x + ry;
    const float cxR = r.x + r.w - ry;
    const float cy = r.y + ry;
    const float ry2 = ry * ry;

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, rr, gg, bb, aa);

    const int y0 = static_cast<int>(std::floor(r.y));
    const int y1 = static_cast<int>(std::ceil(r.y + h));
    for (int yi = y0; yi < y1; ++yi) {
        const float y = static_cast<float>(yi) + 0.5f;
        const float dy = y - cy;
        if (dy * dy > ry2 + 1e-3f) {
            continue;
        }
        const float inner = ry2 - dy * dy;
        if (inner <= 0.f) {
            continue;
        }
        const float halfChord = std::sqrt(inner);
        const float x0 = cxL - halfChord;
        const float x1 = cxR + halfChord;
        if (x1 <= x0) {
            continue;
        }
        const SDL_FRect line = { x0, static_cast<float>(yi), x1 - x0, 1.0f };
        SDL_RenderFillRect(ren, &line);
    }
}

void StrokeStadium(SDL_Renderer* ren, const SDL_FRect& r, Uint8 rr, Uint8 gg, Uint8 bb, Uint8 aa) {
    const float h = r.h;
    const float ry = h * 0.5f;
    const float cxL = r.x + ry;
    const float cxR = r.x + r.w - ry;
    const float cy = r.y + ry;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, rr, gg, bb, aa);

    auto arc = [&](float cx, float cyC, float rad, float a0, float a1, int steps) {
        float px = cx + std::cos(a0) * rad;
        float py = cyC + std::sin(a0) * rad;
        for (int i = 1; i <= steps; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(steps);
            const float a = a0 + (a1 - a0) * u;
            const float nx = cx + std::cos(a) * rad;
            const float ny = cyC + std::sin(a) * rad;
            SDL_RenderLine(ren, px, py, nx, ny);
            px = nx;
            py = ny;
        }
    };

    arc(cxL, cy, ry, kPi * 0.5f, kPi * 1.5f, 16);
    arc(cxR, cy, ry, -kPi * 0.5f, kPi * 0.5f, 16);
    SDL_RenderLine(ren, cxL, r.y, cxR, r.y);
    SDL_RenderLine(ren, cxL, r.y + h, cxR, r.y + h);
}

void TryTriggerSettings(float x, float y) {
    if (!sVisible || sLastWindowW <= 0 || sLastWindowH <= 0) {
        return;
    }
    const SettingsBtnGeom g = BuildSettingsBtnGeom(sLastWindowW, sLastWindowH);
    if (HitCircle(g.cx, g.cy, g.r, x, y)) {
        sSettingsRequested = true;
    }
}

/* Floating joystick: any touch in the left ~45% of the screen (below the
 * L pill, above the Select pill) grabs the stick and RE-CENTERS it at the
 * touch point — no more hunting for a fixed circle mid-fight. The visual
 * circle follows. Falls back to the home position on release. */
void TryAssignJoystick(int64_t fingerId, float x, float y) {
    if (!sVisible || sJoyActive || sLastWindowW <= 0 || sLastWindowH <= 0) {
        return;
    }
    if (IsDpadScheme()) {
        return;
    }
    const float fw = static_cast<float>(sLastWindowW);
    const float fh = static_cast<float>(sLastWindowH);
    if (x > fw * 0.45f || y < fh * 0.22f || y > fh * 0.86f) {
        return;
    }
    if (HitAnyButtonZone(x, y, sLastWindowW, sLastWindowH)) {
        return;
    }
    /* Keep the settings gear tappable: its hit circle wins. */
    {
        const SettingsBtnGeom sg = BuildSettingsBtnGeom(sLastWindowW, sLastWindowH);
        if (HitCircle(sg.cx, sg.cy, sg.r * 1.2f, x, y)) {
            return;
        }
    }
    sJoyActive = true;
    sJoyFinger = fingerId;
    sJoyFloatX = x;
    sJoyFloatY = y;
    sJoyFloating = true;
}

void TestDpadFinger(const DpadGeom& g, float x, float y) {
    const float dx = x - g.cx;
    const float dy = y - g.cy;
    if (std::abs(dx) > g.half || std::abs(dy) > g.half) {
        return;
    }
    const float dead = g.half * 0.18f;
    if (std::abs(dx) < dead && std::abs(dy) < dead) {
        return;
    }
    const float adx = std::abs(dx);
    const float ady = std::abs(dy);
    if (adx >= dead && adx > ady * 0.42f) {
        if (dx < 0.f)
            sHeld[PORT_INPUT_LEFT] = true;
        else
            sHeld[PORT_INPUT_RIGHT] = true;
    }
    if (ady >= dead && ady > adx * 0.42f) {
        if (dy < 0.f)
            sHeld[PORT_INPUT_UP] = true;
        else
            sHeld[PORT_INPUT_DOWN] = true;
    }
}

void ClearJoystick(void) {
    sJoyActive = false;
    sJoyKnobDx = 0.f;
    sJoyKnobDy = 0.f;
    sJoyFloating = false;
}

/* Live stick center: the grab point while floating, else the home spot. */
JoyGeom LiveJoyGeom(int w, int h) {
    JoyGeom g = BuildJoyGeom(w, h);
    if (sJoyFloating) {
        g.cx = sJoyFloatX;
        g.cy = sJoyFloatY;
    }
    return g;
}

void UpdateJoystickInput(const JoyGeom& g) {
    if (!sJoyActive) {
        return;
    }

    const TouchPoint* finger = nullptr;
    for (const TouchPoint& t : sTouches) {
        if (t.id == sJoyFinger) {
            finger = &t;
            break;
        }
    }
    if (finger == nullptr) {
        ClearJoystick();
        return;
    }

    const float dx = finger->x - g.cx;
    const float dy = finger->y - g.cy;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float maxT = JoyMaxTravel(g);
    const float dead = std::max(12.0f, maxT * 0.16f);

    if (dist <= dead) {
        sJoyKnobDx = 0.f;
        sJoyKnobDy = 0.f;
        return;
    }

    const float scale = std::min(dist, maxT) / dist;
    sJoyKnobDx = dx * scale;
    sJoyKnobDy = dy * scale;

    const float nx = dx / dist;
    const float ny = dy / dist;
    constexpr float kTh = 0.32f;
    if (ny < -kTh) {
        sHeld[PORT_INPUT_UP] = true;
    }
    if (ny > kTh) {
        sHeld[PORT_INPUT_DOWN] = true;
    }
    if (nx < -kTh) {
        sHeld[PORT_INPUT_LEFT] = true;
    }
    if (nx > kTh) {
        sHeld[PORT_INPUT_RIGHT] = true;
    }
}

void UpdateHeldState() {
    /* Slide-off hysteresis: remember what was held last frame; a zone a
     * finger is already holding keeps a 1.35x-radius grace hitbox so a
     * small thumb slide doesn't drop the press mid-action. */
    std::array<bool, PORT_INPUT_COUNT> prevHeld = sHeld;
    sHeld.fill(false);
    if (!sVisible || sLastWindowW <= 0 || sLastWindowH <= 0) {
        return;
    }

    const bool dpad = IsDpadScheme();
    if (!dpad) {
        const JoyGeom joyG = LiveJoyGeom(sLastWindowW, sLastWindowH);
        UpdateJoystickInput(joyG);
    }

    const auto buttons = BuildButtonZones(sLastWindowW, sLastWindowH);
    const DpadGeom dpadG = BuildDpadGeom(sLastWindowW, sLastWindowH);
    for (const TouchPoint& touch : sTouches) {
        if (!dpad && sJoyActive && touch.id == sJoyFinger) {
            continue;
        }
        if (dpad) {
            TestDpadFinger(dpadG, touch.x, touch.y);
        }
        for (const TouchZone& z : buttons) {
            bool hit;
            if (z.shape == TouchShape::Circle) {
                const float grace = prevHeld[z.input] ? 1.35f : 1.0f;
                hit = HitCircle(z.cx, z.cy, z.radius * grace, touch.x, touch.y);
            } else {
                hit = HitStadium(z.bounds, touch.x, touch.y);
            }
            if (hit) {
                sHeld[z.input] = true;
            }
        }
    }
}

void UpsertTouch(int64_t id, float x, float y) {
    sVisible = true;
    for (TouchPoint& touch : sTouches) {
        if (touch.id == id) {
            touch.x = x;
            touch.y = y;
            UpdateHeldState();
            return;
        }
    }
    sTouches.push_back({ id, x, y });
    UpdateHeldState();
}

void RemoveTouch(int64_t id) {
    if (sJoyActive && id == sJoyFinger) {
        ClearJoystick();
    }
    sTouches.erase(
        std::remove_if(sTouches.begin(), sTouches.end(), [id](const TouchPoint& touch) { return touch.id == id; }),
        sTouches.end());
    UpdateHeldState();
}

/* Apply the user's opacity multiplier to a base alpha. */
Uint8 A(int base) {
    const float v = static_cast<float>(base) * Port_Config_TouchOpacity();
    return static_cast<Uint8>(Clamp(v, 0.f, 255.f));
}

void DrawSettingsButton(SDL_Renderer* ren, int w, int h) {
    const SettingsBtnGeom g = BuildSettingsBtnGeom(w, h);

    FillCircle(ren, g.cx, g.cy, g.r, 88, 92, 98, A(95));
    StrokeCircle(ren, g.cx, g.cy, g.r - 0.5f, 118, 122, 130, A(115));

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 232, 236, 242, A(225));
    const float lineW = g.r * 1.05f;
    const float lineH = std::max(2.5f, g.r * 0.13f);
    const float spacing = g.r * 0.33f;
    for (int i = -1; i <= 1; ++i) {
        const SDL_FRect r = { g.cx - lineW * 0.5f, g.cy + static_cast<float>(i) * spacing - lineH * 0.5f, lineW,
                              lineH };
        SDL_RenderFillRect(ren, &r);
    }
}

void DrawJoystick(SDL_Renderer* ren, int w, int h) {
    const JoyGeom g = LiveJoyGeom(w, h);

    /* Floating grab shows a slightly hotter ring so the re-centered stick
     * is obvious; the idle home circle stays subtle. */
    FillCircle(ren, g.cx, g.cy, g.outerR, 88, 92, 98, A(sJoyFloating ? 92 : 78));
    StrokeCircle(ren, g.cx, g.cy, g.outerR - 0.5f, 118, 122, 130, A(sJoyFloating ? 115 : 95));

    const float kx = g.cx + sJoyKnobDx;
    const float ky = g.cy + sJoyKnobDy;
    const bool deflect = (sJoyKnobDx != 0.f || sJoyKnobDy != 0.f);
    FillCircle(ren, kx, ky, g.knobR, 96, 100, 108, A(deflect ? 125 : 92));
    StrokeCircle(ren, kx, ky, g.knobR - 0.5f, 120, 124, 132, A(deflect ? 105 : 88));
}

void DrawDpadArrow(SDL_Renderer* ren, float cx, float cy, float size, int dir, bool held) {
    const Uint8 a = held ? 240 : 200;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 232, 236, 242, a);
    const float h = size;
    const int steps = std::max(6, static_cast<int>(h));
    for (int i = 0; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float halfChord = (h * 0.5f) * (1.0f - t);
        if (halfChord <= 0.5f)
            continue;
        SDL_FRect strip;
        if (dir == 0) {
            strip = { cx - halfChord, cy + h * 0.5f - t * h, halfChord * 2.0f, 1.0f };
        } else if (dir == 2) {
            strip = { cx - halfChord, cy - h * 0.5f + t * h, halfChord * 2.0f, 1.0f };
        } else if (dir == 3) {
            strip = { cx + h * 0.5f - t * h, cy - halfChord, 1.0f, halfChord * 2.0f };
        } else {
            strip = { cx - h * 0.5f + t * h, cy - halfChord, 1.0f, halfChord * 2.0f };
        }
        SDL_RenderFillRect(ren, &strip);
    }
}

void DrawDpad(SDL_Renderer* ren, int w, int h) {
    const DpadGeom g = BuildDpadGeom(w, h);
    const float arm = g.arm;
    const float half = g.half;

    auto drawArm = [&](SDL_FRect r, bool held) {
        const Uint8 fillA = held ? 125 : 80;
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 88, 92, 98, fillA);
        SDL_RenderFillRect(ren, &r);
        const Uint8 strokeA = held ? 110 : 80;
        SDL_SetRenderDrawColor(ren, 118, 122, 130, strokeA);
        SDL_RenderRect(ren, &r);
    };

    const float thick = arm;
    SDL_FRect horiz = { g.cx - half, g.cy - thick * 0.5f, half * 2.f, thick };
    drawArm(horiz, sHeld[PORT_INPUT_LEFT] || sHeld[PORT_INPUT_RIGHT]);
    SDL_FRect vert = { g.cx - thick * 0.5f, g.cy - half, thick, half * 2.f };
    drawArm(vert, sHeld[PORT_INPUT_UP] || sHeld[PORT_INPUT_DOWN]);

    const Uint8 hot = 150;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (sHeld[PORT_INPUT_UP]) {
        SDL_SetRenderDrawColor(ren, 96, 220, 160, hot);
        SDL_FRect r{ g.cx - thick * 0.5f, g.cy - half, thick, half - thick * 0.5f };
        SDL_RenderFillRect(ren, &r);
    }
    if (sHeld[PORT_INPUT_DOWN]) {
        SDL_SetRenderDrawColor(ren, 96, 220, 160, hot);
        SDL_FRect r{ g.cx - thick * 0.5f, g.cy + thick * 0.5f, thick, half - thick * 0.5f };
        SDL_RenderFillRect(ren, &r);
    }
    if (sHeld[PORT_INPUT_LEFT]) {
        SDL_SetRenderDrawColor(ren, 96, 220, 160, hot);
        SDL_FRect r{ g.cx - half, g.cy - thick * 0.5f, half - thick * 0.5f, thick };
        SDL_RenderFillRect(ren, &r);
    }
    if (sHeld[PORT_INPUT_RIGHT]) {
        SDL_SetRenderDrawColor(ren, 96, 220, 160, hot);
        SDL_FRect r{ g.cx + thick * 0.5f, g.cy - thick * 0.5f, half - thick * 0.5f, thick };
        SDL_RenderFillRect(ren, &r);
    }

    const float aw = thick * 0.46f;
    DrawDpadArrow(ren, g.cx, g.cy - half + thick * 0.5f, aw, 0, sHeld[PORT_INPUT_UP]);
    DrawDpadArrow(ren, g.cx + half - thick * 0.5f, g.cy, aw, 1, sHeld[PORT_INPUT_RIGHT]);
    DrawDpadArrow(ren, g.cx, g.cy + half - thick * 0.5f, aw, 2, sHeld[PORT_INPUT_DOWN]);
    DrawDpadArrow(ren, g.cx - half + thick * 0.5f, g.cy, aw, 3, sHeld[PORT_INPUT_LEFT]);
}

void DrawFaceCircle(SDL_Renderer* ren, const TouchZone& z) {
    const bool held = sHeld[z.input];
    const float r = z.radius;
    const float cx = z.cx;
    const float cy = z.cy;

    /* Context glow: when the engine says R currently has a specific
     * action (speak/read/lift/open/...), tint the R button green — the
     * touch equivalent of the GBA's R-action HUD icon, which the overlay
     * itself covers. (Accessor declared at file scope below the includes;
     * defined in port_linked_stubs.c.) */
    const bool glow = (z.input == PORT_INPUT_R) && Port_TouchControls_RActionAvailable();

    const Uint8 fillA = A(held ? 125 : (glow ? 110 : 88));
    if (glow) {
        FillCircle(ren, cx, cy, r, 66, 150, 104, fillA);
        StrokeCircle(ren, cx, cy, r - 0.5f, 96, 220, 160, A(held ? 160 : 130));
    } else {
        FillCircle(ren, cx, cy, r, 88, 92, 98, fillA);
        StrokeCircle(ren, cx, cy, r - 0.5f, 118, 122, 130, A(held ? 105 : 82));
    }

    constexpr float kTw = 8.0f;
    constexpr float kTh = 8.0f;
    const float textW = static_cast<float>(std::strlen(z.label)) * kTw;
    SDL_SetRenderDrawColor(ren, 232, 236, 242, A(held ? 240 : 205));
    SDL_RenderDebugText(ren, cx - textW * 0.5f, cy - kTh * 0.5f, z.label);
}

void DrawStadiumControl(SDL_Renderer* ren, const TouchZone& z) {
    const bool held = sHeld[z.input];
    const SDL_FRect& b = z.bounds;

    FillStadium(ren, b, 88, 92, 98, A(held ? 120 : 85));
    StrokeStadium(ren, b, 118, 122, 130, A(held ? 102 : 78));

    constexpr float kTw = 8.0f;
    constexpr float kTh = 8.0f;
    const float textW = static_cast<float>(std::strlen(z.label)) * kTw;
    SDL_SetRenderDrawColor(ren, 232, 236, 242, A(held ? 240 : 205));
    SDL_RenderDebugText(ren, b.x + (b.w - textW) * 0.5f, b.y + (b.h - kTh) * 0.5f, z.label);
}

void DrawButtonZone(SDL_Renderer* ren, const TouchZone& z) {
    if (z.shape == TouchShape::Stadium) {
        DrawStadiumControl(ren, z);
        return;
    }
    DrawFaceCircle(ren, z);
}

} // namespace

extern "C" void Port_TouchControls_NotifyRenderSize(int width, int height) {
    if (width > 0 && height > 0) {
        sLastWindowW = width;
        sLastWindowH = height;
    }
}

/* Latch every currently-held button onto the engine's per-frame edge cache.
 * Called right after a pointer DOWN resolves held-state so a sub-frame tap
 * (DOWN+UP in one poll batch — synthesized `adb input`, and fast real taps
 * that land and lift between two 60 Hz polls) still registers for one game
 * frame, mirroring the keyboard KEY_DOWN edge path. */
static void StampHeldEdges() {
    for (int i = 0; i < PORT_INPUT_COUNT; ++i) {
        if (sHeld[i]) {
            Port_Config_StampInputEdge(static_cast<PortInput>(i));
        }
    }
}

extern "C" void Port_TouchControls_HandleEvent(const SDL_Event* event) {
    if (event == nullptr) {
        return;
    }
    if (!Port_Config_GetTouchControls()) {
        /* Switched off: the whole overlay goes, settings button included, and
         * it claims nothing — a tap on the game is just a tap on the game.
         * The way back is the panel's own settings tab, which turns this
         * switch on again and brings the button with it, so there is no
         * state you can strand yourself in. Anything held when the switch
         * flipped is released here; it would otherwise stay latched with
         * nothing left to release it. */
        if (std::any_of(sHeld.begin(), sHeld.end(), [](bool h) { return h; })) {
            sTouches.clear();
            sHeld.fill(false);
            ClearJoystick();
        }
        return;
    }

    if (event->type == SDL_EVENT_FINGER_DOWN) {
        const int64_t fid = static_cast<int64_t>(event->tfinger.fingerID);
        const float x = event->tfinger.x * static_cast<float>(sLastWindowW);
        const float y = event->tfinger.y * static_cast<float>(sLastWindowH);
        TryTriggerSettings(x, y);
        UpsertTouch(fid, x, y);
        TryAssignJoystick(fid, x, y);
        UpdateHeldState();
        StampHeldEdges();
    } else if (event->type == SDL_EVENT_FINGER_MOTION) {
        UpsertTouch(static_cast<int64_t>(event->tfinger.fingerID), event->tfinger.x * static_cast<float>(sLastWindowW),
                    event->tfinger.y * static_cast<float>(sLastWindowH));
    } else if (event->type == SDL_EVENT_FINGER_UP || event->type == SDL_EVENT_FINGER_CANCELED) {
        RemoveTouch(static_cast<int64_t>(event->tfinger.fingerID));
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        const float x = event->button.x;
        const float y = event->button.y;
        TryTriggerSettings(x, y);
        UpsertTouch(-1, x, y);
        TryAssignJoystick(-1, x, y);
        UpdateHeldState();
        StampHeldEdges();
    } else if (event->type == SDL_EVENT_MOUSE_MOTION && (event->motion.state & SDL_BUTTON_LMASK) != 0) {
        UpsertTouch(-1, event->motion.x, event->motion.y);
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
        RemoveTouch(-1);
    }
}

extern "C" void Port_TouchControls_NotifyGamepadUsed(void) {
    sVisible = false;
    sTouches.clear();
    sHeld.fill(false);
    ClearJoystick();
}

extern "C" void Port_TouchControls_SetGamepadAvailable(bool available) {
    if (available) {
        Port_TouchControls_NotifyGamepadUsed();
    } else {
        sVisible = true;
    }
}

extern "C" bool Port_TouchControls_InputPressed(PortInput input) {
    if (!Port_Config_GetTouchControls()) {
        return false;
    }
    return input >= 0 && input < PORT_INPUT_COUNT && sHeld[input];
}

extern "C" void Port_TouchControls_Render(SDL_Renderer* renderer, int windowWidth, int windowHeight) {
    Port_TouchControls_NotifyRenderSize(windowWidth, windowHeight);
    if (!Port_Config_GetTouchControls()) {
        return; /* nothing drawn, and no UpdateHeldState: nothing may be held */
    }
    UpdateHeldState();
    if (!renderer || !sVisible) {
        return;
    }

    if (IsDpadScheme()) {
        DrawDpad(renderer, windowWidth, windowHeight);
    } else {
        DrawJoystick(renderer, windowWidth, windowHeight);
    }
    for (const TouchZone& z : BuildButtonZones(windowWidth, windowHeight)) {
        DrawButtonZone(renderer, z);
    }
    DrawSettingsButton(renderer, windowWidth, windowHeight);
}

extern "C" bool Port_TouchControls_ConsumeSettingsRequest(void) {
    if (!sSettingsRequested) {
        return false;
    }
    sSettingsRequested = false;
    sTouches.clear();
    sHeld.fill(false);
    ClearJoystick();
    return true;
}

#else

extern "C" void Port_TouchControls_NotifyRenderSize(int, int) {
}
extern "C" void Port_TouchControls_HandleEvent(const SDL_Event*) {
}
extern "C" void Port_TouchControls_NotifyGamepadUsed(void) {
}
extern "C" void Port_TouchControls_SetGamepadAvailable(bool) {
}
extern "C" bool Port_TouchControls_InputPressed(PortInput) {
    return false;
}
extern "C" void Port_TouchControls_Render(SDL_Renderer*, int, int) {
}
extern "C" bool Port_TouchControls_ConsumeSettingsRequest(void) {
    return false;
}

#endif

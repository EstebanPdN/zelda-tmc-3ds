/* Included by the existing compositor: reuse Minish Cap's runtime-decoded
 * stone plate, red header chip, pale buttons and native message fonts. */
#include "updater.h"
#include <ctype.h>

static bool sUpdateConfirm, sUpdateNotes;
static unsigned sUpdatePage, sUpdatePages = 1;

static void UpdateUI_Reset(void) {
    UI_LOCK();
    sUpdateConfirm = sUpdateNotes = false;
    sUpdatePage = 0;
    UI_UNLOCK();
}

bool Port_SecondScreen_3DS_UpdateOpen(void) {
    UI_LOCK();
    bool open = sUi.tab == SS_TAB_SETTINGS && sUi.settingsPage == SS_SETTINGS_UPDATE;
    UI_UNLOCK();
    return open;
}

static void UpdateSmallText(const SSurf* s, const char* text, int x, int y, int style) {
    char label[128];
    snprintf(label,sizeof(label),"%s",text);
    for (char* c=label;*c;++c) *c=(char)toupper((unsigned char)*c);
    text=label;
    if (!Port_SecondScreenTheme_DrawBigText(s->px, s->w, s->h, s->stride, x, y, 1, style, text))
        DrawTextStr(s, text, x, y + 4, 1, Port_SecondScreenTheme_Color(SSC_MENU_INK), 0);
}

static int UpdateTextWidth(const char* text) {
    char label[128];
    snprintf(label,sizeof(label),"%s",text);
    for (char* c=label;*c;++c) *c=(char)toupper((unsigned char)*c);
    text=label;
    int w = Port_SecondScreenTheme_BigTextWidth(text, 1);
    return w ? w : TextWidthPx(text, 1);
}

static void UpdateCenteredText(const SSurf* s, const char* text, int cx, int y) {
    UpdateSmallText(s, text, cx - UpdateTextWidth(text)/2, y, SS_TEXT_NAVY);
}

static void UpdateButton(const SSurf* s, TargetList* tl, float x0, float y0,
                          float x1, float y1, const char* text, int action, bool pressed,
                          float u, int ts) {
    DrawMenuButton(s, x0, y0, x1, y1, "", pressed, 0, u, ts);
    UpdateSmallText(s, text, (int)((x0+x1-UpdateTextWidth(text))/2),
                    (int)((y0+y1)/2-8), SS_TEXT_NAVY);
    if (action) AddTarget(tl, x0, y0, x1, y1, action, 0);
}

static void PaintUpdatePanel(const SSurf* s, TargetList* tl, float x0, float y0,
                              float x1, float y1, float u, int ts) {
    UpdateStatus st; Updater_GetStatus(&st);
    bool confirm, notes;
    UI_LOCK(); confirm = sUpdateConfirm; notes = sUpdateNotes; UI_UNLOCK();
    float row = 26, gap = 4;
    int cx = (int)((x0+x1)/2);
    char text[96];
    snprintf(text, sizeof(text), "CHANNEL: %s", st.prerelease ? "PRE-RELEASE" : "STABLE");
    UpdateButton(s, tl, x0,y0,x1,y0+row,text,SS_ACT_UPDATE_CHANNEL,false,u,ts);
    UpdateButton(s, tl, x0,y0+row+gap,x1,y0+2*row+gap,
                 st.version[0] ? st.version : "NO RELEASE SELECTED",SS_ACT_UPDATE_RELEASE,notes,u,ts);
    const char* message = confirm ? "DOWNLOAD AND INSTALL" : st.message;
    if (!message[0]) message = "CHECK FOR UPDATE";
    // Long network errors are split at words inside the actual panel width.
    char first[80], second[80] = ""; snprintf(first,sizeof(first),"%s",message);
    if (UpdateTextWidth(first) > x1-x0) {
        char* split = strrchr(first,' ');
        while (split && UpdateTextWidth(first) > x1-x0) {
            *split = 0;
            snprintf(second,sizeof(second),"%s",message+strlen(first)+1);
            if (UpdateTextWidth(first) <= x1-x0) break;
            split = strrchr(first,' ');
        }
    }
    UpdateCenteredText(s,first,cx,(int)(y0+60));
    if (second[0]) UpdateCenteredText(s,second,cx,(int)(y0+75));
    else UpdateCenteredText(s,confirm ? "UNSAVED PROGRESS IS LOST" :
                              notes ? "CHANGELOG ON TOP SCREEN" : "TAP RELEASE FOR CHANGELOG",cx,(int)(y0+78));
    if (notes && !confirm && !Updater_Busy()) {
        UpdateButton(s,tl,x0,y0+99,x0+68,y0+120,"PREV",SS_ACT_UPDATE_PREV,false,u,ts);
        UpdateButton(s,tl,x1-68,y0+99,x1,y0+120,"NEXT",SS_ACT_UPDATE_NEXT,false,u,ts);
    }
    if (st.state == UPDATE_DOWNLOADING || st.state == UPDATE_VERIFYING || st.state == UPDATE_INSTALLING) {
        FillRect(s,(int)x0,(int)(y0+103),(int)x1,(int)(y0+111),Port_SecondScreenTheme_Color(SSC_MENU_STONE_DARK));
        FillRect(s,(int)x0,(int)(y0+103),(int)(x0+(x1-x0)*st.progress/100),(int)(y0+111),
                 Port_SecondScreenTheme_Color(SSC_RUPEE_GREEN));
    }
    const char* action = confirm ? "INSTALL UPDATE" : st.state == UPDATE_INSTALLING ? "PLEASE WAIT" :
        Updater_Busy() ? "CANCEL" : st.state == UPDATE_AVAILABLE ? "DOWNLOAD UPDATE" :
        st.state == UPDATE_DONE ? "UPDATE INSTALLED" : "CHECK FOR UPDATE";
    UpdateButton(s,tl,x0,y1-26,x1,y1,action,SS_ACT_UPDATE_ACTION,confirm,u,ts);
}

static bool HandleUpdateTap(int action, int arg) {
    if (!Port_SecondScreen_3DS_UpdateOpen()) return false;
    UpdateStatus st; Updater_GetStatus(&st);
    if (st.state == UPDATE_INSTALLING || st.state == UPDATE_DONE) return true;
    bool busy = Updater_Busy();
    if (action == SS_ACT_SETTINGS_BACK) {
        UI_LOCK(); bool confirm = sUpdateConfirm; sUpdateConfirm = false; UI_UNLOCK();
        if (confirm) return true;
        Updater_Cancel();
        if (busy) return true; // Wait for the worker before resuming the game.
        UpdateUI_Reset();
        return false;
    }
    if (action == SS_ACT_TAB) {
        if (busy) return true;
        UpdateUI_Reset(); return false;
    }
    if (action == SS_ACT_UPDATE_ACTION) {
        if (busy) { Updater_Cancel(); return true; }
        UI_LOCK(); bool confirm = sUpdateConfirm; sUpdateConfirm = false; UI_UNLOCK();
        if (confirm && st.state == UPDATE_AVAILABLE) Updater_Download();
        else if (st.state == UPDATE_AVAILABLE) {
            UI_LOCK(); sUpdateConfirm = true; UI_UNLOCK();
        } else Updater_Check();
        return true;
    }
    if (busy) return true;
    switch (action) {
        case SS_ACT_UPDATE_CHANNEL:
            UpdateUI_Reset(); Updater_SetChannel(!st.prerelease); break;
        case SS_ACT_UPDATE_RELEASE:
            if (st.version[0]) { UI_LOCK(); sUpdateNotes = true; sUpdatePage = 0; UI_UNLOCK(); }
            break;
        case SS_ACT_UPDATE_PREV:
            UI_LOCK(); if (sUpdatePage) --sUpdatePage; UI_UNLOCK(); break;
        case SS_ACT_UPDATE_NEXT:
            UI_LOCK(); if (sUpdatePage+1 < sUpdatePages) ++sUpdatePage; UI_UNLOCK(); break;
        default: return true;
    }
    return true;
}

/* Main-thread top painter. Theme caches have been initialized before either
 * screen worker starts; the top view reads only their immutable art. */
bool Port_SecondScreen_3DS_PaintUpdateTop(uint32_t* pixels, int stride) {
    if (!Port_SecondScreen_3DS_UpdateOpen()) return false;
    UpdateStatus st; Updater_GetStatus(&st);
    bool notes; unsigned page;
    UI_LOCK(); notes = sUpdateNotes; page = sUpdatePage; UI_UNLOCK();
    static unsigned paintedRevision = UINT32_MAX, paintedPage = UINT32_MAX;
    static bool paintedNotes;
    if (paintedRevision == st.revision && paintedPage == page && paintedNotes == notes) return false;
    paintedRevision=st.revision; paintedPage=page; paintedNotes=notes;
    static char lines[512][43];
    static unsigned count, revision = UINT32_MAX;
    static bool lastNotes;
    if (revision != st.revision || notes != lastNotes) {
        static char body[12289], formatted[512][43];
        if (notes && st.version[0]) Updater_GetNotes(body,sizeof(body));
        else snprintf(body,sizeof(body),"Installed: v%s\n\n%s\n\nSelect a release below to read its changelog.\nChoose Stable or Pre-release to check that channel.",TMC_PORT_VERSION,st.message);
        unsigned n = Update_FormatNotes(body,formatted,512);
        count = 0;
        for (unsigned i=0; i<n && count<512; i++) {
            const char* text = formatted[i];
            do {
                size_t take = strlen(text);
                char part[43]; snprintf(part,sizeof(part),"%s",text);
                while (take && UpdateTextWidth(part)>354) part[--take]=0;
                if (take<strlen(text)) {
                    char* space = strrchr(part,' ');
                    if (space && space!=part) { take = space-part; part[take]=0; }
                }
                if (!take && *text) { take=1; part[0]=*text; part[1]=0; }
                snprintf(lines[count++],43,"%s",part);
                text += take; while (*text==' ') ++text;
            } while (*text && count<512);
        }
        revision=st.revision; lastNotes=notes;
    }
    unsigned pages = (count+8)/9; if (!pages) pages=1;
    if (page>=pages) page=pages-1;
    UI_LOCK(); sUpdatePages=pages; sUpdatePage=page; UI_UNLOCK();
    SSurf s={pixels,400,240,stride};
    Port_SecondScreenTheme_DrawBackdrop(pixels,400,240,stride,0,0,400,240,2);
    Port_SecondScreenTheme_DrawPlate(pixels,400,240,stride,4,4,392,232,2);
    char title[80]; snprintf(title,sizeof(title),notes ? "CHANGELOG %s" : "UPDATE",st.version);
    for(char* c=title;*c;++c) *c=(char)toupper((unsigned char)*c);
    DrawPanelHeaderChip(&s,200,14,title,1,1.0f/3);
    Port_SecondScreenTheme_DrawWell(pixels,400,240,stride,17,47,366,165,1);
    for (unsigned i=page*9;i<count && i<(page+1)*9;i++)
        UpdateSmallText(&s,lines[i],23,49+(i-page*9)*18,SS_TEXT_INK);
    char footer[64]; snprintf(footer,sizeof(footer),"PAGE %u OF %u",page+1,pages);
    const int footerW=UpdateTextWidth(footer)+24;
    const int footerY=211, footerH=24;
    DrawMenuButton(&s,200-footerW/2,footerY,200+(footerW+1)/2,footerY+footerH,"",0,0,1.f/3,1);
    // The Settings banner font has visible ink on rows 2..14. Center that
    // thirteen-pixel band, rather than the full sixteen-pixel glyph cell.
    UpdateSmallText(&s,footer,200-UpdateTextWidth(footer)/2,
                    footerY+(footerH-13)/2-2,SS_TEXT_NAVY);
    return true;
}

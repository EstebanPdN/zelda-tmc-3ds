/* JNI bridge for the second-screen panel (Phase 3a). Android-only — this
 * translation unit is not added to the build on other platforms (see
 * xmake.lua). Implicit JNI naming (Java_<package>_<Class>_<method>) is used
 * throughout, matching the rest of this codebase's JNI surface; there is no
 * JNI_OnLoad here to register against — SDL's own static-linked glue owns
 * that, and implicit-named natives resolve without registration. */

#include <android/native_window_jni.h>
#include <jni.h>

#include "port_second_screen.h"

extern "C" JNIEXPORT void JNICALL Java_dev_picori_tmc_SecondScreenView_nativeSurfaceCreated(
    JNIEnv* env, jobject /*thiz*/, jobject surface, jint width, jint height) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    /* Port_SecondScreen_OnSurfaceReady takes ownership of this one
     * reference (releases it internally when replaced/lost) — don't
     * release it here too. */
    Port_SecondScreen_OnSurfaceReady(window, width, height);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_picori_tmc_SecondScreenView_nativeSurfaceDestroyed(JNIEnv* /*env*/, jobject /*thiz*/) {
    Port_SecondScreen_OnSurfaceLost();
}

extern "C" JNIEXPORT void JNICALL Java_dev_picori_tmc_SecondScreenView_nativeTap(
    JNIEnv* /*env*/, jobject /*thiz*/, jint x, jint y, jboolean longPress) {
    Port_SecondScreen_OnTap(x, y, longPress ? 1 : 0);
}

/* Which display the game activity itself came up on — the shell knows, the
 * panel's settings row needs it to tell "swap screens" apart from "swap
 * screens, once you restart". Reported once, from the manager's ctor. */
extern "C" JNIEXPORT void JNICALL Java_dev_picori_tmc_SecondScreenManager_nativeSetGameOnSecondaryDisplay(
    JNIEnv* /*env*/, jobject /*thiz*/, jboolean onSecondary) {
    Port_SecondScreen_SetGameOnSecondaryDisplay(onSecondary ? 1 : 0);
}

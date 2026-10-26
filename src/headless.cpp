// Headless OpenGL for offline renders (clips, screenshots): an EGL context on a GPU device with a pbuffer the size
// of the render, no window and no X server. The hardware GPU (NVIDIA first) is preferred over software ones.
#include "headless.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef ZEN_HAVE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace headless {

namespace {
EGLDisplay dpy_ = EGL_NO_DISPLAY;
EGLContext ctx_ = EGL_NO_CONTEXT;
EGLSurface surf_ = EGL_NO_SURFACE;
bool nvidia_ = false;
}

bool init(int width, int height) {
    auto queryDevices = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    auto queryDeviceString = (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
    auto platformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!queryDevices || !platformDisplay) return false;
    EGLDeviceEXT devs[16];
    EGLint n = 0;
    if (!queryDevices(16, devs, &n) || n <= 0) return false;
    // ZEN_EGL_DEVICE=i picks a device; else the first hardware one, NVIDIA before others
    const char* force = std::getenv("ZEN_EGL_DEVICE");
    int best = -1, bestScore = -1;
    for (int i = 0; i < n; i++) {
        const char* ext = queryDeviceString ? queryDeviceString(devs[i], EGL_EXTENSIONS) : nullptr;
        const bool software = ext && std::strstr(ext, "EGL_MESA_device_software");
        const bool nvidia = ext && std::strstr(ext, "EGL_NV_device_cuda");
        const int score = force ? (std::atoi(force) == i ? 3 : 0) : (software ? 0 : nvidia ? 2 : 1);
        if (score > bestScore) { bestScore = score; best = i; }
    }
    // the chosen device first, the others as fallbacks
    for (int pass = 0; pass < n + 1; pass++) {
        const int i = pass == 0 ? best : pass - 1;
        if (pass > 0 && i == best) continue;
        EGLDisplay d = platformDisplay(EGL_PLATFORM_DEVICE_EXT, devs[i], nullptr);
        EGLint maj = 0, min = 0;
        if (d == EGL_NO_DISPLAY || !eglInitialize(d, &maj, &min)) continue;
        const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                  EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                  EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
        EGLConfig cfg;
        EGLint nc = 0;
        if (!eglChooseConfig(d, cfgAttr, &cfg, 1, &nc) || nc < 1 || !eglBindAPI(EGL_OPENGL_API)) { eglTerminate(d); continue; }
        const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                  EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                  EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE, EGL_TRUE, EGL_NONE};
        EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctxAttr);
        const EGLint pbAttr[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
        EGLSurface s = c != EGL_NO_CONTEXT ? eglCreatePbufferSurface(d, cfg, pbAttr) : EGL_NO_SURFACE;
        if (s == EGL_NO_SURFACE || !eglMakeCurrent(d, s, s, c)) {
            if (c != EGL_NO_CONTEXT) eglDestroyContext(d, c);
            eglTerminate(d);
            continue;
        }
        dpy_ = d; ctx_ = c; surf_ = s;
        const char* vendor = eglQueryString(d, EGL_VENDOR);
        nvidia_ = vendor && std::strstr(vendor, "NVIDIA");
        std::printf("[gl] headless EGL on device %d (%s)\n", i, eglQueryString(d, EGL_VENDOR));
        return true;
    }
    return false;
}

void* proc(const char* name) { return (void*)eglGetProcAddress(name); }

bool nvidia() { return nvidia_; }

void shutdown() {
    if (dpy_ == EGL_NO_DISPLAY) return;
    eglMakeCurrent(dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surf_ != EGL_NO_SURFACE) eglDestroySurface(dpy_, surf_);
    if (ctx_ != EGL_NO_CONTEXT) eglDestroyContext(dpy_, ctx_);
    eglTerminate(dpy_);
    dpy_ = EGL_NO_DISPLAY;
}

} // namespace headless

#else

namespace headless {
bool init(int, int) { return false; }
void* proc(const char*) { return nullptr; }
void shutdown() {}
bool nvidia() { return false; }
} // namespace headless

#endif

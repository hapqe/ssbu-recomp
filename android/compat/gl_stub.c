/* OpenGL 1.1 and WGL entry points for the Android port. nvn_gl.c holds the NVN front end that the Vulkan back end shares,
 * and its OpenGL renderer; that renderer cannot start here (wglCreateContext fails), so none of these is reached in a
 * run. They exist so that nvn_gl.c links unchanged. */
#include "windows.h"
#include <GL/gl.h>
#include <android/log.h>
static void gl_unreachable(const char *fn) { __android_log_print(ANDROID_LOG_ERROR, "OpenSmash", "OpenGL call %s on Android (the OpenGL renderer is not available)", fn); }
#define V0(name, args) void name args { gl_unreachable(#name); }
V0(glEnable, (GLenum c)) V0(glDisable, (GLenum c)) V0(glBindTexture, (GLenum t, GLuint x))
V0(glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p)) V0(glGenTextures, (GLsizei n, GLuint *t))
V0(glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) V0(glClear, (GLbitfield m)) V0(glStencilFunc, (GLenum f, GLint r, GLuint m))
V0(glScissor, (GLint x, GLint y, GLsizei w, GLsizei h)) V0(glPixelStorei, (GLenum p, GLint v)) V0(glDepthFunc, (GLenum f))
V0(glAlphaFunc, (GLenum f, GLclampf r)) V0(glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))
V0(glTexSubImage2D, (GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void *p))
V0(glGetIntegerv, (GLenum p, GLint *v)) V0(glGetFloatv, (GLenum p, GLfloat *v)) V0(glClearDepth, (GLdouble d)) V0(glClearStencil, (GLint s))
V0(glCullFace, (GLenum m)) V0(glFrontFace, (GLenum m)) V0(glDeleteTextures, (GLsizei n, const GLuint *t)) V0(glDepthMask, (GLboolean f))
V0(glDrawArrays, (GLenum m, GLint f, GLsizei n)) V0(glFinish, (void)) V0(glFlush, (void)) V0(glGetBooleanv, (GLenum p, GLboolean *v))
V0(glGetTexImage, (GLenum t, GLint l, GLenum f, GLenum ty, void *p)) V0(glGetTexLevelParameteriv, (GLenum t, GLint l, GLenum p, GLint *v))
V0(glLineWidth, (GLfloat w)) V0(glLogicOp, (GLenum op)) V0(glPointSize, (GLfloat s)) V0(glPolygonMode, (GLenum f, GLenum m))
V0(glTexParameteri, (GLenum t, GLenum p, GLint v)) V0(glTexParameteriv, (GLenum t, GLenum p, const GLint *v))
V0(glTexSubImage1D, (GLenum t, GLint l, GLint x, GLsizei w, GLenum f, GLenum ty, const void *p))
V0(glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) V0(glStencilOp, (GLenum a, GLenum b, GLenum c)) V0(glStencilMask, (GLuint m))
V0(glPolygonOffset, (GLfloat f, GLfloat u)) V0(glBlendFunc, (GLenum s, GLenum d)) V0(glDepthRange, (GLdouble n, GLdouble f))
V0(glReadBuffer, (GLenum m)) V0(glDrawBuffer, (GLenum m))
V0(glTexImage2D, (GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *p))
V0(glCopyTexSubImage2D, (GLenum t, GLint l, GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h)) V0(glHint, (GLenum t, GLenum m))
GLboolean glIsEnabled(GLenum c) { gl_unreachable("glIsEnabled"); (void)c; return 0; }
GLenum glGetError(void) { return 0; }
const GLubyte *glGetString(GLenum n) { (void)n; return (const GLubyte *)""; }
void *wglGetProcAddress(const char *name) { (void)name; return NULL; }
int ChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR *p) { (void)dc; (void)p; return 0; }
BOOL SetPixelFormat(HDC dc, int f, const PIXELFORMATDESCRIPTOR *p) { (void)dc; (void)f; (void)p; return FALSE; }
BOOL SwapBuffers(HDC dc) { (void)dc; return FALSE; }
HDC GetDC(HWND w) { (void)w; return NULL; }
int ReleaseDC(HWND w, HDC dc) { (void)w; (void)dc; return 0; }
HGLRC wglCreateContext(HDC dc) { (void)dc; return NULL; }
BOOL wglMakeCurrent(HDC dc, HGLRC rc) { (void)dc; (void)rc; return FALSE; }
BOOL wglDeleteContext(HGLRC rc) { (void)rc; return FALSE; }
HWND CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID p) {
    (void)ex; (void)cls; (void)name; (void)style; (void)x; (void)y; (void)w; (void)h; (void)parent; (void)menu; (void)inst; (void)p;
    return NULL;
}
BOOL DestroyWindow(HWND w) { (void)w; return TRUE; }
int GetWindowTextA(HWND w, LPSTR out, int n) { (void)w; if (n) out[0] = 0; return 0; }

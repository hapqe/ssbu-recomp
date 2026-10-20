/* GL/gl.h for the Android port: the types and constants the shared NVN front end (nvn_front.h, nvn_c4.c) names.
 * Android has no desktop OpenGL: the Vulkan back end is the only renderer, and the GL entry points declared here are
 * stubs (gl_stub.c) that report and stop if they are ever reached. */
#ifndef ANDROID_COMPAT_GL_H
#define ANDROID_COMPAT_GL_H
#include <stdint.h>
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat, GLclampf;
typedef double GLdouble;
typedef void GLvoid;
typedef intptr_t GLintptr;
typedef intptr_t GLsizeiptr;
typedef char GLchar;
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_ALWAYS 0x0207
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_ALPHA_TEST 0x0BC0
#define GL_ALPHA_TEST_FUNC 0x0BC1
#define GL_ALPHA_TEST_REF 0x0BC2
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_SCISSOR_TEST 0x0C11
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ROW_LENGTH 0x0D02
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_RGBA8 0x8058
#define GL_SRGB8_ALPHA8 0x8C43
#define GL_RGBA16F 0x881A
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_VIEWPORT 0x0BA2
#define GL_SCISSOR_BOX 0x0C10
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_LOWER_LEFT 0x8CA1
#define GL_UPPER_LEFT 0x8CA2
#define GL_NEGATIVE_ONE_TO_ONE 0x935E
#define GL_ZERO_TO_ONE 0x935F
#define GL_CLIP_ORIGIN 0x935C
#define GL_CLIP_DEPTH_MODE 0x935D
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007
#define GL_QUAD_STRIP 0x0008
#define GL_POLYGON 0x0009
#define GL_NONE 0
#define GL_ZERO 0
#define GL_ONE 1
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_FUNC 0x0B74
#define GL_BLEND 0x0BE2
#define GL_COLOR_LOGIC_OP 0x0BF2
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_TEXTURE_1D 0x0DE0
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_BYTE 0x1400
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_CLEAR 0x1500
#define GL_INVERT 0x150A
#define GL_COLOR 0x1800
#define GL_DEPTH 0x1801
#define GL_STENCIL 0x1802
#define GL_STENCIL_INDEX 0x1901
#define GL_DEPTH_COMPONENT 0x1902
#define GL_RED 0x1903
#define GL_GREEN 0x1904
#define GL_BLUE 0x1905
#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_POINT 0x1B00
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_INCR 0x1E02
#define GL_DECR 0x1E03
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_CLAMP 0x2900
#define GL_REPEAT 0x2901
#define GL_POLYGON_OFFSET_POINT 0x2A01
#define GL_POLYGON_OFFSET_LINE 0x2A02
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_NO_ERROR 0
#ifdef __cplusplus
extern "C" {
#endif
GLboolean glIsEnabled(GLenum cap);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glBindTexture(GLenum t, GLuint tex);
void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p);
void glGenTextures(GLsizei n, GLuint *t);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glClear(GLbitfield m);
void glStencilFunc(GLenum f, GLint r, GLuint m);
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
void glPixelStorei(GLenum p, GLint v);
void glDepthFunc(GLenum f);
void glAlphaFunc(GLenum f, GLclampf r);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void glTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void *p);
void glGetIntegerv(GLenum p, GLint *v);
void glGetFloatv(GLenum p, GLfloat *v);
GLenum glGetError(void);
void glClearDepth(GLdouble d);
void glClearStencil(GLint s);
void glCullFace(GLenum m);
void glFrontFace(GLenum m);
void glDeleteTextures(GLsizei n, const GLuint *t);
void glDepthMask(GLboolean f);
void glDrawArrays(GLenum m, GLint first, GLsizei n);
void glFinish(void);
void glFlush(void);
void glGetBooleanv(GLenum p, GLboolean *v);
const GLubyte *glGetString(GLenum name);
void glGetTexImage(GLenum t, GLint l, GLenum f, GLenum ty, void *p);
void glGetTexLevelParameteriv(GLenum t, GLint l, GLenum p, GLint *v);
void glLineWidth(GLfloat w);
void glLogicOp(GLenum op);
void glPointSize(GLfloat s);
void glPolygonMode(GLenum f, GLenum m);
void glTexParameteri(GLenum t, GLenum p, GLint v);
void glTexParameteriv(GLenum t, GLenum p, const GLint *v);
void glTexSubImage1D(GLenum t, GLint l, GLint x, GLsizei w, GLenum f, GLenum ty, const void *p);
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void glStencilOp(GLenum a, GLenum b, GLenum c);
void glStencilMask(GLuint m);
void glPolygonOffset(GLfloat f, GLfloat u);
void glBlendFunc(GLenum s, GLenum d);
void glDepthRange(GLdouble n, GLdouble f);
void glReadBuffer(GLenum m);
void glDrawBuffer(GLenum m);
void glTexImage2D(GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *p);
void glCopyTexSubImage2D(GLenum t, GLint l, GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h);
void glHint(GLenum t, GLenum m);
void *wglGetProcAddress(const char *name);
#ifdef __cplusplus
}
#endif
#endif

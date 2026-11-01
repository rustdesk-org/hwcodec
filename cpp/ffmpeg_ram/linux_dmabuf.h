#ifndef HWCODEC_LINUX_DMABUF_H
#define HWCODEC_LINUX_DMABUF_H

#include "linux_dmabuf_ffi.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <va/va_drmcommon.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory>

// Only the opted-in, unprivileged encoder opens graphics libraries. In particular,
// linking this feature must not load vendor EGL/GLES drivers in the root service.
#define HW_GPU_EGL(X) \
  X(eglGetPlatformDisplay) X(eglInitialize) X(eglBindAPI) X(eglChooseConfig) \
  X(eglCreateContext) X(eglMakeCurrent) X(eglDestroyContext) X(eglTerminate) \
  X(eglGetProcAddress)
#define HW_GPU_GL(X) \
  X(glCreateShader) X(glShaderSource) X(glCompileShader) X(glGetShaderiv) \
  X(glDeleteShader) X(glCreateProgram) X(glAttachShader) X(glLinkProgram) \
  X(glGetProgramiv) X(glDeleteProgram) X(glGenVertexArrays) X(glBindVertexArray) \
  X(glDeleteVertexArrays) X(glGenFramebuffers) X(glDeleteFramebuffers) \
  X(glDisable) X(glGenTextures) X(glActiveTexture) X(glBindTexture) \
  X(glTexParameteri) X(glUseProgram) X(glUniform1i) X(glUniform2f) \
  X(glGetUniformLocation) X(glBindFramebuffer) X(glFramebufferTexture2D) \
  X(glCheckFramebufferStatus) X(glViewport) X(glDrawArrays) X(glFinish) \
  X(glGetError) X(glDeleteTextures)
#define HW_GPU_GBM(X) X(gbm_create_device) X(gbm_device_destroy)

class DmaBufConverter {
#define DECLARE(name) decltype(&::name) name = nullptr;
  HW_GPU_EGL(DECLARE)
  HW_GPU_GL(DECLARE)
  HW_GPU_GBM(DECLARE)
#undef DECLARE
  PFNEGLCREATEIMAGEKHRPROC create_image_ = nullptr;
  PFNEGLDESTROYIMAGEKHRPROC destroy_image_ = nullptr;
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_ = nullptr;
  void *egl_ = nullptr, *gles_ = nullptr, *gbm_lib_ = nullptr;
  int fd_ = -1;
  gbm_device *gbm_ = nullptr;
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext context_ = EGL_NO_CONTEXT;
  GLuint program_ = 0, vao_ = 0, fbo_ = 0;
  std::string node_;

  GLuint shader(GLenum type, const char *text) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
  }

  bool init(const char *node) {
    if (!node || !*node) return false;
    node_ = node;
    egl_ = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    gles_ = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    gbm_lib_ = dlopen("libgbm.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!egl_ || !gles_ || !gbm_lib_) return false;
#define LOAD_EGL(name) name = reinterpret_cast<decltype(name)>(dlsym(egl_, #name)); if (!name) return false;
#define LOAD_GL(name) name = reinterpret_cast<decltype(name)>(dlsym(gles_, #name)); if (!name) return false;
#define LOAD_GBM(name) name = reinterpret_cast<decltype(name)>(dlsym(gbm_lib_, #name)); if (!name) return false;
    HW_GPU_EGL(LOAD_EGL)
    HW_GPU_GL(LOAD_GL)
    HW_GPU_GBM(LOAD_GBM)
#undef LOAD_EGL
#undef LOAD_GL
#undef LOAD_GBM
    create_image_ = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    destroy_image_ = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    image_target_ = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!create_image_ || !destroy_image_ || !image_target_) return false;
    fd_ = open(node, O_RDWR | O_CLOEXEC);
    if (fd_ < 0) return false;
    gbm_ = gbm_create_device(fd_);
    if (!gbm_) return false;
    display_ = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm_, nullptr);
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
      display_ = EGL_NO_DISPLAY;
      return false;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) return false;
    EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    EGLConfig config;
    EGLint count = 0;
    if (!eglChooseConfig(display_, attrs, &config, 1, &count) || !count) return false;
    EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attrs);
    if (context_ == EGL_NO_CONTEXT || !eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_)) return false;
    const char *vs = R"(#version 300 es
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
    // Match libyuv's limited-range BT.601 coefficients, including RGB quantization
    // before subsampling. Mesa 25's VPP RGB conversion hardcodes BT.709 instead.
    const char *fs = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision highp float;
uniform samplerExternalOES scanout;
uniform vec2 size;
uniform int chroma;
out vec4 output_pixel;
vec3 rgb(vec2 p) { return floor(texture(scanout, p / size).rgb * 255.0 + 0.5) / 255.0; }
void main() {
  if (chroma == 0) {
    float y = dot(rgb(gl_FragCoord.xy), vec3(66.0, 129.0, 25.0) / 256.0) + 16.0 / 255.0;
    output_pixel = vec4(y, 0.0, 0.0, 1.0);
  } else {
    vec2 p = gl_FragCoord.xy * 2.0;
    vec3 c = (rgb(p + vec2(-0.5, -0.5)) + rgb(p + vec2(0.5, -0.5)) +
              rgb(p + vec2(-0.5, 0.5)) + rgb(p + vec2(0.5, 0.5))) * 0.25;
    float u = dot(c, vec3(-38.0, -74.0, 112.0) / 256.0) + 128.0 / 255.0;
    float v = dot(c, vec3(112.0, -94.0, -18.0) / 256.0) + 128.0 / 255.0;
    output_pixel = vec4(u, v, 0.0, 1.0);
  }
})";
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return false; }
    program_ = glCreateProgram();
    glAttachShader(program_, v); glAttachShader(program_, f); glLinkProgram(program_);
    glDeleteShader(v); glDeleteShader(f);
    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) return false;
    glGenVertexArrays(1, &vao_); glBindVertexArray(vao_);
    glGenFramebuffers(1, &fbo_); glDisable(GL_DITHER);
    bool ok = glGetError() == GL_NO_ERROR;
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return ok;
  }

  EGLImageKHR import_image(uint32_t w, uint32_t h, uint32_t format, uint32_t planes,
                           const int *fds, const uint32_t *offsets,
                           const uint32_t *pitches, const uint64_t *modifiers) {
    if (planes < 1 || planes > 4) return EGL_NO_IMAGE_KHR;
    EGLint a[64]; int n = 0;
    a[n++] = EGL_WIDTH; a[n++] = w; a[n++] = EGL_HEIGHT; a[n++] = h;
    a[n++] = EGL_LINUX_DRM_FOURCC_EXT; a[n++] = format;
    const EGLint fd_keys[] = {EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT,
                             EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE3_FD_EXT};
    const EGLint off_keys[] = {EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
                              EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT};
    const EGLint pitch_keys[] = {EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT,
                                EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT};
    for (uint32_t p = 0; p < planes; ++p) {
      a[n++] = fd_keys[p]; a[n++] = fds[p];
      a[n++] = off_keys[p]; a[n++] = offsets[p];
      a[n++] = pitch_keys[p]; a[n++] = pitches[p];
      if (modifiers[p] != ((1ULL << 56) - 1)) {
        a[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT + 2 * p; a[n++] = uint32_t(modifiers[p]);
        a[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT + 2 * p; a[n++] = uint32_t(modifiers[p] >> 32);
      }
    }
    a[n++] = EGL_NONE;
    return create_image_(display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, a);
  }

public:
  static std::unique_ptr<DmaBufConverter> create(const char *node) {
    std::unique_ptr<DmaBufConverter> c(new DmaBufConverter());
    if (!c->init(node)) return nullptr;
    return c;
  }
  DmaBufConverter() = default;
  DmaBufConverter(const DmaBufConverter &) = delete;
  DmaBufConverter &operator=(const DmaBufConverter &) = delete;
  ~DmaBufConverter() {
    if (context_ != EGL_NO_CONTEXT) {
      if (eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_)) {
        if (program_) glDeleteProgram(program_);
        if (fbo_) glDeleteFramebuffers(1, &fbo_);
        if (vao_) glDeleteVertexArrays(1, &vao_);
      }
      eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      eglDestroyContext(display_, context_);
    }
    if (display_ != EGL_NO_DISPLAY) eglTerminate(display_);
    if (gbm_) gbm_device_destroy(gbm_);
    if (fd_ >= 0) close(fd_);
    if (gbm_lib_) dlclose(gbm_lib_);
    if (gles_) dlclose(gles_);
    if (egl_) dlclose(egl_);
  }

  int convert(const HwDmaBufDescriptor &d, const char *node,
              VADisplay va, VASurfaceID target) {
    if (!node || node_ != node || d.fd < 0 || !d.width || !d.height ||
        d.width > 16384 || d.height > 16384 || (d.width & 1) || (d.height & 1) ||
        d.num_planes < 1 || d.num_planes > 4) return AVERROR(EINVAL);
    switch (d.format) {
    case 0x34325258: case 0x34325241: case 0x34324258: case 0x34324241: // XR24/AR24/XB24/AB24
    case 0x30335258: case 0x30335241: case 0x30334258: case 0x30334241: // XR30/AR30/XB30/AB30
      break;
    default: return AVERROR(ENOSYS);
    }
    // The caller's source may be tiled/compressed; bounds cap the descriptor,
    // rather than pretending each auxiliary plane has the full image height.
    for (uint32_t p = 0; p < d.num_planes; ++p)
      if (!d.pitches[p] || uint64_t(d.offsets[p]) + uint64_t(d.pitches[p]) * d.height > 256ULL * 1024 * 1024)
        return AVERROR(EINVAL);
    if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_)) return AVERROR(EIO);
    int fds[4] = {d.fd, d.fd, d.fd, d.fd};
    uint64_t mods[4] = {d.modifier, d.modifier, d.modifier, d.modifier};
    EGLImageKHR source = import_image(d.width, d.height, d.format, d.num_planes, fds, d.offsets, d.pitches, mods);
    if (source == EGL_NO_IMAGE_KHR) {
      eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      return AVERROR(ENOSYS);
    }
    GLuint textures[3] = {};
    EGLImageKHR dest[2] = {EGL_NO_IMAGE_KHR, EGL_NO_IMAGE_KHR};
    VADRMPRIMESurfaceDescriptor output = {};
    int result = AVERROR(EIO);
    glGenTextures(3, textures);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_EXTERNAL_OES, textures[0]);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    image_target_(GL_TEXTURE_EXTERNAL_OES, source);
    VAStatus vas = vaExportSurfaceHandle(va, target, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
        VA_EXPORT_SURFACE_WRITE_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS, &output);
    // Failed exports do not grant ownership of the output descriptor's fd fields.
    bool exported = vas == VA_STATUS_SUCCESS;
    do {
      if (!exported || output.num_layers != 2 || output.num_objects > 4 ||
          output.layers[0].drm_format != 0x20203852 || // DRM_FORMAT_R8
          output.layers[1].drm_format != 0x38385247) break; // DRM_FORMAT_GR88
      glUseProgram(program_); glBindVertexArray(vao_);
      glUniform1i(glGetUniformLocation(program_, "scanout"), 0);
      glUniform2f(glGetUniformLocation(program_, "size"), d.width, d.height);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
      bool complete = true;
      for (uint32_t p = 0; p < 2; ++p) {
        uint32_t w = p ? d.width / 2 : d.width, h = p ? d.height / 2 : d.height;
        uint32_t np = output.layers[p].num_planes;
        if (np < 1 || np > 4) { complete = false; break; }
        for (uint32_t j = 0; j < np; ++j) {
          uint32_t obj = output.layers[p].object_index[j];
          if (obj >= output.num_objects) { complete = false; break; }
          fds[j] = output.objects[obj].fd; mods[j] = output.objects[obj].drm_format_modifier;
        }
        if (!complete) break;
        dest[p] = import_image(w, h, output.layers[p].drm_format, np, fds,
                              output.layers[p].offset, output.layers[p].pitch, mods);
        if (dest[p] == EGL_NO_IMAGE_KHR) { complete = false; break; }
        glBindTexture(GL_TEXTURE_2D, textures[p + 1]); image_target_(GL_TEXTURE_2D, dest[p]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[p + 1], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { complete = false; break; }
        glViewport(0, 0, w, h); glUniform1i(glGetUniformLocation(program_, "chroma"), p);
        glDrawArrays(GL_TRIANGLES, 0, 3);
      }
      // Complete both GPU reads and writes before releasing the imported fd/images
      // or handing this surface to the VAAPI encoder on its own command stream.
      glFinish();
      if (complete && glGetError() == GL_NO_ERROR) result = 0;
    } while (false);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteTextures(3, textures);
    destroy_image_(display_, source);
    for (auto image : dest) if (image != EGL_NO_IMAGE_KHR) destroy_image_(display_, image);
    if (exported) for (uint32_t o = 0; o < output.num_objects && o < 4; ++o) close(output.objects[o].fd);
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return result;
  }
};
#undef HW_GPU_EGL
#undef HW_GPU_GL
#undef HW_GPU_GBM
#endif

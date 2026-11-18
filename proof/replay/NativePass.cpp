// Replay a captured fullscreen pass on GLES, not a playback of its output
// image. Input: shader pair, named uniforms and full-resolution input textures
// ONLY.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs = std::filesystem;
void check(bool ok, const std::string &msg) {
  if (!ok)
    throw std::runtime_error(msg);
}
void glCheck(const std::string &stage) {
  auto e = glGetError();
  check(e == GL_NO_ERROR, stage + ": GLES error " + std::to_string(e));
}
std::string text(fs::path p) {
  std::ifstream f(p);
  check(f.good(), p.string());
  return {std::istreambuf_iterator<char>(f), {}};
}
GLuint compile(GLenum stage, std::string src) {
  auto s = glCreateShader(stage);
  const char *ptr = src.c_str();
  glShaderSource(s, 1, &ptr, nullptr);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  char msg[4096]{};
  glGetShaderInfoLog(s, sizeof(msg), nullptr, msg);
  check(ok, msg);
  return s;
}
int main(int argc, char **argv) try {
  check(argc == 3 || argc == 4,
        "usage: native-pass snapshot-dir output.rgba32f [iterations]");
  fs::path dir = argv[1];
  const int runs = argc == 4 ? std::stoi(argv[3]) : 3;
  check(runs > 0, "iterations");
  auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  check(eglInitialize(display, nullptr, nullptr),
        "EGL initialize; try EGL_PLATFORM=surfaceless");
  check(eglBindAPI(EGL_OPENGL_ES_API), "GLES");
  const EGLint configAttrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
  EGLConfig config;
  EGLint count;
  check(eglChooseConfig(display, configAttrs, &config, 1, &count) && count,
        "EGL config");
  const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE},
               surfaceAttrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttrs);
  auto surface = eglCreatePbufferSurface(display, config, surfaceAttrs);
  check(eglMakeCurrent(display, surface, surface, context), "EGL context");
  std::cout << glGetString(GL_RENDERER) << '\n';
  auto p = glCreateProgram();
  for (auto [stage, file] : {std::pair{GL_VERTEX_SHADER, "pass.vert"},
                             std::pair{GL_FRAGMENT_SHADER, "pass.frag"}}) {
    auto sh = compile(stage, text(dir / file));
    glAttachShader(p, sh);
    glDeleteShader(sh);
  }
  glLinkProgram(p);
  GLint ok;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  char log[4096]{};
  glGetProgramInfoLog(p, sizeof(log), nullptr, log);
  check(ok, log);
  glUseProgram(p);
  // No per-frame allocation/upload. Capture decoding belongs to validation
  // startup.
  constexpr int W = 480, H = 270;
  GLuint out, fb;
  glGenTextures(1, &out);
  glBindTexture(GL_TEXTURE_2D, out);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, W, H);
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         out, 0);
  check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
        "RGBA16F render target");
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  std::istringstream lines(text(dir / "bindings"));
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream r(line);
    char kind;
    std::string name;
    r >> kind >> name;
    auto loc = glGetUniformLocation(p, name.c_str());
    if (kind == 'T') {
      int unit, w, h, d;
      GLenum target, internal, fmt, filter;
      r >> unit >> target >> internal >> w >> h >> d >> fmt >> filter;
      check(r && w > 0 && h > 0 && d > 0, "invalid texture metadata " + name);
      auto file = dir / (name + ".f32");
      auto size = fs::file_size(file);
      check(size == size_t(w) * h * d * (fmt == GL_RED ? 4 : 16),
            "texture size " + name);
      std::vector<float> pixels(size / 4);
      check(bool(std::ifstream(file, std::ios::binary)
                     .read((char *)pixels.data(), size)),
            "texture read " + name);
      GLuint t;
      glGenTextures(1, &t);
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(target, t);
      if (target == GL_TEXTURE_3D)
        glTexImage3D(target, 0, internal, w, h, d, 0, fmt, GL_FLOAT,
                     pixels.data());
      else
        glTexImage2D(target, 0, internal, w, h, 0, fmt, GL_FLOAT,
                     pixels.data());
      for (auto param : {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER})
        glTexParameteri(target, param, filter);
      for (auto param :
           {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
        glTexParameteri(target, param, GL_CLAMP_TO_EDGE);
      glUniform1i(loc, unit);
    } else {
      GLenum type;
      r >> type;
      float f[16]{};
      int n = 0;
      while (n < 16 && r >> f[n])
        ++n;
      switch (type) {
      case GL_INT:
      case GL_BOOL:
        glUniform1i(loc, int(f[0]));
        break;
      case GL_FLOAT:
        glUniform1f(loc, f[0]);
        break;
      case GL_FLOAT_VEC3:
        glUniform3fv(loc, 1, f);
        break;
      case GL_FLOAT_VEC2:
        glUniform2fv(loc, 1, f);
        break;
      case GL_FLOAT_MAT4:
        glUniformMatrix4fv(loc, 1, GL_FALSE, f);
        break;
      default:
        throw std::runtime_error("unsupported uniform " + name);
      }
    }
    glCheck(name);
  }
  glViewport(0, 0, W, H);
  glDisable(GL_DITHER);
  GLuint vao;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  std::vector<double> ms;
  for (int i = -1; i < runs; i++) {
    auto start = std::chrono::steady_clock::now();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    auto end = std::chrono::steady_clock::now();
    if (i >= 0)
      ms.push_back(
          std::chrono::duration<double, std::milli>(end - start).count());
  }
  glCheck("draw");
  std::vector<float> raw(W * H * 4);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, raw.data());
  glCheck("readback");
  for (auto v : raw)
    check(std::isfinite(v), "nonfinite result");
  check(bool(std::ofstream(argv[2], std::ios::binary)
                 .write((char *)raw.data(), raw.size() * 4)),
        "output");
  std::cout << "warm-pass-ms";
  for (auto v : ms)
    std::cout << ' ' << v;
  std::cout << '\n';
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglDestroySurface(display, surface);
  eglTerminate(display);
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}

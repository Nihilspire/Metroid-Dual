/* gl3d.c - OpenGL ES 2 drawing for the Linux build: a textured-quad blitter for both screens and the 3D view
 * (a C port of the Android app's Renderer3D.java / tools/viewer3d/metroid3d.html). Included by metroiddual.c.
 * GL functions are loaded through SDL_GL_GetProcAddress, so the program links against nothing but SDL2. */
#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS 1
#include <SDL_opengles2.h>

/* ------------------------------------------------------------------ loader */
#define GLFNS(X) \
  X(GLuint, CreateProgram, (void)) X(GLuint, CreateShader, (GLenum)) X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
  X(void, CompileShader, (GLuint)) X(void, GetShaderiv, (GLuint, GLenum, GLint *)) X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
  X(void, AttachShader, (GLuint, GLuint)) X(void, LinkProgram, (GLuint)) X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
  X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) X(GLint, GetAttribLocation, (GLuint, const GLchar *)) \
  X(GLint, GetUniformLocation, (GLuint, const GLchar *)) X(void, GenTextures, (GLsizei, GLuint *)) X(void, BindTexture, (GLenum, GLuint)) \
  X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
  X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
  X(void, TexParameteri, (GLenum, GLenum, GLint)) X(void, PixelStorei, (GLenum, GLint)) X(void, GenBuffers, (GLsizei, GLuint *)) \
  X(void, BindBuffer, (GLenum, GLuint)) X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) X(void, UseProgram, (GLuint)) \
  X(void, EnableVertexAttribArray, (GLuint)) X(void, DisableVertexAttribArray, (GLuint)) \
  X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
  X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) X(void, Uniform1i, (GLint, GLint)) X(void, Uniform1f, (GLint, GLfloat)) \
  X(void, Uniform2f, (GLint, GLfloat, GLfloat)) X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat)) X(void, Uniform3fv, (GLint, GLsizei, const GLfloat *)) \
  X(void, ActiveTexture, (GLenum)) X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) X(void, Clear, (GLbitfield)) \
  X(void, Enable, (GLenum)) X(void, Disable, (GLenum)) X(void, DrawArrays, (GLenum, GLint, GLsizei)) X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
  X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) X(const GLubyte *, GetString, (GLenum)) \
  X(void, BlendFunc, (GLenum, GLenum)) X(void, DepthMask, (GLboolean)) X(GLenum, GetError, (void))
#define GLDECL(r, n, a) r (APIENTRY *n) a;
static struct { GLFNS(GLDECL) } gl;
static int gl_load(void) {
#define GLLOAD(r, n, a) if (!(*(void **)&gl.n = SDL_GL_GetProcAddress("gl" #n))) { logm("GL: missing gl%s", #n); return 0; }
  GLFNS(GLLOAD)
  return 1;
}

/* ------------------------------------------------------------------ matrices (column-major, as android.opengl.Matrix) */
static void m_mul(float *r, const float *a, const float *b) {
  float t[16];
  for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float s = 0; for (int k = 0; k < 4; k++) s += a[i + 4 * k] * b[k + 4 * j]; t[i + 4 * j] = s; }
  memcpy(r, t, sizeof t);
}
static void m_ident(float *m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1; }
static void m_persp(float *m, float fovy, float asp, float n, float f) {
  float c = 1.0f / tanf(fovy * (float)M_PI / 360.0f), rr = 1.0f / (n - f);
  memset(m, 0, 64); m[0] = c / asp; m[5] = c; m[10] = (f + n) * rr; m[11] = -1; m[14] = 2 * f * n * rr;
}
static void m_lookat(float *m, float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz) {
  float fx = cx - ex, fy = cy - ey, fz = cz - ez, rl = 1.0f / sqrtf(fx * fx + fy * fy + fz * fz);
  fx *= rl; fy *= rl; fz *= rl;
  float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux, rs = 1.0f / sqrtf(sx * sx + sy * sy + sz * sz);
  sx *= rs; sy *= rs; sz *= rs;
  float vx = sy * fz - sz * fy, vy = sz * fx - sx * fz, vz = sx * fy - sy * fx;
  m_ident(m);
  m[0] = sx; m[1] = vx; m[2] = -fx; m[4] = sy; m[5] = vy; m[6] = -fy; m[8] = sz; m[9] = vz; m[10] = -fz;
  for (int i = 0; i < 4; i++) m[12 + i] += m[i] * -ex + m[4 + i] * -ey + m[8 + i] * -ez;
}
static void m_ortho(float *m, float l, float r, float b, float t, float n, float f) {
  memset(m, 0, 64);
  m[0] = 2 / (r - l); m[5] = 2 / (t - b); m[10] = -2 / (f - n);
  m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = -(f + n) / (f - n); m[15] = 1;
}

/* ------------------------------------------------------------------ shaders */
static GLuint gl_shader(GLenum type, const char *src) {
  GLuint s = gl.CreateShader(type); gl.ShaderSource(s, 1, &src, NULL); gl.CompileShader(s);
  GLint ok = 0; gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char b[2048]; gl.GetShaderInfoLog(s, sizeof b, NULL, b); logm("GL shader: %s", b); return 0; }
  return s;
}
static GLuint gl_program(const char *vs, const char *fs) {
  GLuint v = gl_shader(GL_VERTEX_SHADER, vs), f = gl_shader(GL_FRAGMENT_SHADER, fs);
  if (!v || !f) return 0;
  GLuint p = gl.CreateProgram(); gl.AttachShader(p, v); gl.AttachShader(p, f); gl.LinkProgram(p);
  GLint ok = 0; gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char b[2048]; gl.GetProgramInfoLog(p, sizeof b, NULL, b); logm("GL link: %s", b); return 0; }
  return p;
}
#define FS_PREC "#ifdef GL_ES\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n#endif\n"
static void gl_tex_params(GLint wrap) {
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}

/* ------------------------------------------------------------------ blitter: textured quads in window pixels */
static GLuint blit_prog, blit_vbo; static GLint b_pos, b_uv, b_tex;
static int blit_init(void) {
  blit_prog = gl_program(
    "attribute vec2 aPos; attribute vec2 aUv; varying vec2 vUv; void main(){ vUv = aUv; gl_Position = vec4(aPos, 0.0, 1.0); }",
    FS_PREC "varying vec2 vUv; uniform sampler2D uTex; void main(){ gl_FragColor = texture2D(uTex, vUv); }");
  if (!blit_prog) return 0;
  b_pos = gl.GetAttribLocation(blit_prog, "aPos"); b_uv = gl.GetAttribLocation(blit_prog, "aUv"); b_tex = gl.GetUniformLocation(blit_prog, "uTex");
  gl.GenBuffers(1, &blit_vbo);
  return 1;
}
/* Draws texture tex (tw x th used of its full size) into rect x,y,w,h of a W x H viewport. */
static void blit(GLuint tex, float x, float y, float w, float h, int W, int H, int blend) {
  float x0 = x / W * 2 - 1, x1 = (x + w) / W * 2 - 1, y0 = 1 - y / H * 2, y1 = 1 - (y + h) / H * 2;
  float v[] = { x0, y0, 0, 0,  x1, y0, 1, 0,  x1, y1, 1, 1,  x0, y0, 0, 0,  x1, y1, 1, 1,  x0, y1, 0, 1 };
  gl.UseProgram(blit_prog);
  gl.BindBuffer(GL_ARRAY_BUFFER, blit_vbo); gl.BufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
  gl.EnableVertexAttribArray(b_pos); gl.VertexAttribPointer(b_pos, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
  gl.EnableVertexAttribArray(b_uv); gl.VertexAttribPointer(b_uv, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
  gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, tex); gl.Uniform1i(b_tex, 0);
  gl.Disable(GL_DEPTH_TEST); gl.Disable(GL_CULL_FACE);
  if (blend) { gl.Enable(GL_BLEND); gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); } else gl.Disable(GL_BLEND);
  gl.DrawArrays(GL_TRIANGLES, 0, 6);
  gl.Disable(GL_BLEND);
  gl.DisableVertexAttribArray(b_pos); gl.DisableVertexAttribArray(b_uv);
}
typedef struct { GLuint id; int w, h; } GTex;
static void gtex_rgba(GTex *t, int w, int h, const void *px) {
  if (!t->id) gl.GenTextures(1, &t->id);
  gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, t->id); gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (t->w != w || t->h != h) { gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); gl_tex_params(GL_CLAMP_TO_EDGE); t->w = w; t->h = h; }
  else gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
}
static void gtex_565(GTex *t, int w, int h, const void *px) {
  if (!t->id) gl.GenTextures(1, &t->id);
  gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, t->id); gl.PixelStorei(GL_UNPACK_ALIGNMENT, 2);
  if (t->w != w || t->h != h) { gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, px); gl_tex_params(GL_CLAMP_TO_EDGE); t->w = w; t->h = h; }
  else gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, px);
}

/* ------------------------------------------------------------------ 3D view */
#define SNAP 0x3820
#define SNT 0x800
#define SPAL 0x1800
#define SCHR 0x1820
static const uint32_t NESPAL[64] = {
  0x747474,0x24188C,0x0000A8,0x44009C,0x8C0074,0xA80010,0xA40000,0x7C0800,0x402C00,0x004400,0x005000,0x003C14,0x183C5C,0,0,0,
  0xBCBCBC,0x0070EC,0x2038EC,0x8000F0,0xBC00BC,0xE40058,0xD82800,0xC84C0C,0x887000,0x009400,0x00A800,0x009038,0x008088,0,0,0,
  0xFCFCFC,0x3CBCFC,0x5C94FC,0xCC88FC,0xF478FC,0xFC74B4,0xFC7460,0xFC9838,0xF0BC3C,0x80D010,0x4CDC48,0x58F898,0x00E8D8,0x787878,0,0,
  0xFCFCFC,0xA8E4FC,0xC4D4FC,0xD4C8FC,0xFCC4FC,0xFCC4D8,0xFCBCB0,0xFCD8A8,0xFCE4A0,0xE0FCA0,0xA8F0BC,0xB0FCCC,0x9CFCF0,0xC4C4C4,0,0 };
static uint32_t rgbi(int i) { return NESPAL[i & 63]; }
static int r_fdiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
static int fmodi(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

typedef struct { const char *cam, *back; int depth, cave, light; } R3DOpts;
static uint8_t r_snap[SNAP]; static int r_have;
static GLuint r_prog, r_vbo, r_atlas, r_cave, r_mask;
static GLint uAlpha, transStart, transCount;
static GLint aPos, aUv, aShade, aKind, uMvp, uTex, uCave, uBg, uEdge, uLight, uLightOn, uMode, uTint, uFog, uMask;
static uint8_t r_atlas_px[512 * 256 * 4], r_mask_px[256 * 256], r_lastChr[0x2000], r_lastPal[0x20];
static int r_atlas_valid;
static float *verts; static int nv, vcap;
static float pitch_ = 16, yaw_ = 0, zoom_ = 1, camDist = 400, lightX = 128, lightY = -120;
static int lightMiss = 999; static float lightAmt;   /* light holds Samus's last spot and fades (see Renderer3D.java) */
static int hasLight, caveStart, caveCount, hudStart, spriteStart;
static int ox, oy, bgBase, spBase, ntT, ntP;
/* Samus's centre on screen from RAM (object slot $0300: action, $030C nametable, $030D Y, $030E X); 0 outside normal
 * play. Colour matching alone also caught frozen enemies and beam shots. See Renderer3D.samusOnScreen. */
static int samus_on_screen(const uint8_t *f, int *sx, int *sy) {
  if (f[0x1D] != 0 || (f[0x1E] != 3 && f[0x1E] != 8) || f[0x300] == 0) return 0;   /* 3 in play, 8 entrance */
  int h = f[0x30C] & 1, X = f[0x30E], Y = f[0x30D];
  /* side-scrolling rooms can have the vertical nametable bit set while not scrolling vertically: try y mod 240 too */
  for (int k = 0; k < 4; k++) {
    int x = (((X + ((k & 1) == 0 ? 256 * h : 0) - ox) % 512) + 512) % 512;
    int y = (((Y + ((k & 1) == 1 ? 240 * h : 0) - oy) % 480) + 480) % 480;
    if (k >= 2) y %= 240;
    if (x < 256 && y < 240) { *sx = x; *sy = y; return 1; }
  }
  return 0;
}
static int slab[64], emptyTile[256], passable[256];
static float u0_, v0_, u1_, v1_;

static const char *R_VS =
  "attribute vec3 aPos; attribute vec2 aUv; attribute float aShade; attribute float aKind;\n"
  "uniform mat4 uMvp; varying vec2 vUv; varying float vShade; varying float vKind; varying vec3 vWorld;\n"
  "void main(){ vUv=aUv; vShade=aShade; vKind=aKind; vWorld=aPos; gl_Position=uMvp*vec4(aPos,1.0); }";
static const char *R_FS = FS_PREC
  "varying vec2 vUv; varying float vShade; varying float vKind; varying vec3 vWorld;\n"
  "uniform sampler2D uTex; uniform sampler2D uCave; uniform vec3 uBg; uniform int uEdge; uniform vec2 uLight;\n"
  "uniform float uLightOn; uniform int uMode; uniform vec3 uTint; uniform vec3 uFog;\n"
  "uniform sampler2D uMask; uniform float uAlpha;\n"
  "void main(){\n"
  "  if (uMode == 1) {\n"
  "    float k = 0.2;\n"
  "    k = mix(k, mix(0.4, 0.08, smoothstep(20.0, 150.0, distance(vWorld.xy, uLight))), uLightOn);\n"
  "    gl_FragColor = vec4(texture2D(uCave, vUv).rgb * k, 1.0); return;\n"
  "  }\n"
  "  vec4 c = texture2D(uTex, vUv);\n"
  "  if (uMode == 2) { if (c.a < 0.5) discard; gl_FragColor = vec4(c.rgb, 1.0); return; }\n"
  "  if (c.a < 0.5) { if (vKind < 0.5) c = vec4(uBg * 0.55, 1.0); else discard; }\n"
  "  if (vKind > 4.5 && vKind < 5.5 && texture2D(uMask, vec2((vWorld.x + 0.5) / 256.0, (-vWorld.y + 0.5) / 256.0)).r > 0.5) discard;\n"
  "  float shade = vShade;\n"
  "  shade *= mix(1.0, mix(1.15, 0.3, smoothstep(24.0, 160.0, distance(vWorld.xy, uLight))), uLightOn);\n"
  "  vec3 col = c.rgb * shade; float a = 1.0;\n"
  "  if (uEdge != 2) {\n"
  "    float dx = max(max(-vWorld.x, vWorld.x - 256.0), 0.0), dy = max(max(vWorld.y + 8.0, -232.0 - vWorld.y), 0.0);\n"
  "    float o = max(dx, dy);\n"
  "    if (uEdge == 1 && o > 0.0) discard;\n"
  "    if (uEdge == 0) { a = 1.0 - smoothstep(0.0, 24.0, o); if (a < 0.02) discard; }\n"
  "  }\n"
  "  gl_FragColor = vec4(mix(uFog, col, a), uAlpha);\n"
  "}";

/* Backdrop: a dimmed wall of the area's own blocks - the 2x2-tile blocks ("macros") the area's room structures are
 * built from, read from the player's ROM, weighted by how often the rooms use them and laid out in 32 px clumps by a
 * fixed hash. Drawn with the live pattern table and palette. Scrolls at half speed following only the smooth scroll
 * movement (the scroll registers jump by half a screen when the game swaps nametables at a room change). Same as
 * Renderer3D.java. */
static uint8_t bd_px[256 * 256 * 4];
static int bd_pool[1024 * 6], bd_n, bd_area = -1, bd_dirty = 1, bd_ok, bd_lastOx = -99999, bd_lastOy;
static const uint8_t *bd_rom; static float accX, accY;
static int romoff(int bank, int addr) { return 16 + bank * 0x4000 + ((addr - 0x8000) & 0x3FFF); }
static void macro_pool(const uint8_t *r, size_t len, int area) {
  static const int BANK[5] = { 1, 2, 4, 3, 5 };
  bd_n = 0;
  if (!r || len < 0x20010 || area < 0 || area > 4) return;
#define RW(o) ((o) + 1 < (int)len ? r[o] | r[(o) + 1] << 8 : 0)
#define RB(o) ((o) < (int)len ? r[o] : 0xFF)
  int b = BANK[area], hdr = romoff(b, 0x9598), rooms = RW(hdr + 2), structs = RW(hdr + 4), macros = RW(hdr + 6), nRooms = (structs - rooms) >> 1;
  static int count[1024]; memset(count, 0, sizeof count);
  for (int n = 0; n < nRooms && n < 128; n++) {
    int p = romoff(b, RW(romoff(b, rooms) + n * 2)) + 1;
    for (int g = 0; g < 200; g++) {
      int c = RB(p);
      if (c == 0xFF || c == 0xFD) break;
      if (c == 0xFE) { p++; continue; }
      int si = RB(p + 1), pal = RB(p + 2) & 3, sp = romoff(b, RW(romoff(b, structs) + si * 2));
      for (int row = 0; row < 32; row++) {
        int ln = RB(sp); if (ln == 0xFF) break;
        int cnt = (ln & 15) ? ln & 15 : 16;
        for (int k = 0; k < cnt; k++) count[RB(sp + 1 + k) * 4 + pal]++;
        sp += 1 + cnt;
      }
      p += 3;
    }
  }
  for (int key = 0; key < 1024; key++) {
    if (!count[key]) continue;
    int mp = romoff(b, macros) + (key >> 2) * 4, ok = 1;
    for (int i = 0; i < 4 && ok; i++) ok = RB(mp + i) < 0xA0;
    if (!ok) continue;
    for (int i = 0; i < 4; i++) bd_pool[bd_n * 6 + i] = RB(mp + i);
    bd_pool[bd_n * 6 + 4] = key & 3; bd_pool[bd_n * 6 + 5] = count[key]; bd_n++;
  }
#undef RW
#undef RB
}
/* The pattern tables an area loads (MRoom.chr): the backdrop is only drawn while its blocks' live tiles still match
 * these. Anywhere else (title, password screen, ending credits) the same tile numbers hold other graphics. */
static const int GFX[][4] = { {6,0x8000,0x0000,0x09A0},{4,0x8D60,0x0000,0x0520},{1,0x8D60,0x1000,0x0400},{6,0x9DA0,0x1000,0x0150},
    {5,0x8D60,0x1200,0x0450},{6,0x9EF0,0x1800,0x0800},{1,0x9160,0x0C00,0x0400},{6,0xA6F0,0x1000,0x0260},{6,0xA950,0x1700,0x0070},
    {2,0x8D60,0x0C00,0x0400},{6,0xA9C0,0x1000,0x02E0},{6,0xACA0,0x1200,0x0600},{6,0xB2A0,0x1900,0x0090},{5,0x91B0,0x1D00,0x0300},
    {2,0x9160,0x0C00,0x0400},{6,0xB330,0x1700,0x00C0},{4,0x9360,0x1E00,0x0200},{3,0x8D60,0x0C00,0x0400},{6,0xB3F0,0x1700,0x00C0},
    {3,0x9160,0x0C00,0x0400},{6,0x89A0,0x0C00,0x0100},{6,0x8BE0,0x1400,0x0500},{6,0x9980,0x1FC0,0x0040},{6,0xB4C0,0x1000,0x0400},
    {6,0xB4C0,0x0A00,0x00A0},{6,0x9980,0x0FC0,0x0040},{6,0xB4C0,0x1D00,0x02A0},{6,0x90E0,0x0000,0x07B0},{6,0x9890,0x1F40,0x0010} };
static const int AREA_GFX[5][12] = { {0,3,4,5,6,0x19,0x16}, {0,4,5,7,8,9,0x19,0x16}, {0,4,5,0x0A,0x0F,0x10,0x11,0x19,0x16},
    {0,5,0x0A,0x0B,0x0C,0x0D,0x0E,0x1A,0x1C,0x19,0x16}, {0,4,5,0x0A,0x12,0x13,0x19,0x16} };
static const int AREA_GFX_N[5] = { 7, 8, 9, 11, 8 };
static const int START_GFX[7] = {0x15,0,0x14,0x17,0x18,0x19,0x16};
static uint8_t bd_chr[0x2000];
static void area_chr(const uint8_t *r, size_t len, int area) {
  memset(bd_chr, 0, sizeof bd_chr);
  for (int k = 0; k < 7 + AREA_GFX_N[area]; k++) {
    const int *g = GFX[k < 7 ? START_GFX[k] : AREA_GFX[area][k - 7]];
    int s = romoff(g[0], g[1]);
    for (int i = 0; i < g[3] && s + i < (int)len; i++) bd_chr[(g[2] + i) & 0x1FFF] = r[s + i];
  }
}
static int bd_hash(int x, int y, int s) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 1274126177u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (int)((h ^ (h >> 16)) & 0x7FFFFFFF);
}
static void build_backdrop(const uint8_t *f) {
  int area = f[0x74] & 15;
  if (area != bd_area || rom != bd_rom) {
    macro_pool(rom, rom_len, area); if (bd_n) area_chr(rom, rom_len, area); bd_area = area; bd_rom = rom; bd_dirty = 1;
  }
  if (!bd_dirty) return;
  bd_dirty = 0;
  static int w[1024]; int total = 0;
  for (int e = 0; e < bd_n; e++) {
    int ok = bgBase == 0x1000;
    for (int i = 0; i < 4 && ok; i++) { int t = bd_pool[e * 6 + i]; ok = !emptyTile[t] && !memcmp(f + SCHR + 0x1000 + t * 16, bd_chr + 0x1000 + t * 16, 16); }
    w[e] = ok ? bd_pool[e * 6 + 5] : 0;
  }
  /* only the area's 3 most-placed blocks (its main rock), so the backdrop reads as one wall, not a jumble */
  for (int keep = 0, cut = 0x7FFFFFFF; keep < 3; keep++) {
    int best = 0; for (int e = 0; e < bd_n; e++) if (w[e] < cut && w[e] > best) best = w[e];
    if (!best) break; cut = best;
    if (keep == 2) for (int e = 0; e < bd_n; e++) if (w[e] < best) w[e] = 0;
  }
  for (int e = 0; e < bd_n; e++) total += w[e];
  bd_ok = total > 0;
  if (!bd_ok) return;
  for (int by = 0; by < 8; by++) for (int bx = 0; bx < 8; bx++) {
    int h = bd_hash(bx, by, area + 1) % total, e = 0;
    while (h >= w[e]) { h -= w[e]; e++; }
    int pal = bd_pool[e * 6 + 4];
    for (int q = 0; q < 16; q++) {
      int tx = bx * 32 + (q & 3) * 8, ty = by * 32 + (q >> 2) * 8, t = bd_pool[e * 6 + ((q >> 2) & 1) * 2 + (q & 1)];
      for (int y = 0; y < 8; y++) {
        int lo = f[SCHR + bgBase + t * 16 + y], hi = f[SCHR + bgBase + t * 16 + y + 8];
        for (int x = 0; x < 8; x++) {
          int c = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1), k = ((ty + y) * 256 + tx + x) * 4;
          uint32_t col = c ? rgbi(f[SPAL + pal * 4 + c]) : 0;
          bd_px[k] = col >> 16; bd_px[k + 1] = col >> 8; bd_px[k + 2] = col; bd_px[k + 3] = 255;
        }
      }
    }
  }
  gl.ActiveTexture(GL_TEXTURE1); gl.BindTexture(GL_TEXTURE_2D, r_cave); gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, bd_px);
}
static void follow_scroll(void) {
  if (bd_lastOx != -99999) {
    int dx = fmodi(ox - bd_lastOx + 256, 512) - 256, dy = fmodi(oy - bd_lastOy + 240, 480) - 240;
    if (abs(dx) <= 16) accX = fmodf(accX + dx + 512, 512);
    if (abs(dy) <= 16) accY = fmodf(accY + dy + 512, 512);
  }
  bd_lastOx = ox; bd_lastOy = oy;
}

static int r3d_init(void) {
  r_prog = gl_program(R_VS, R_FS);
  if (!r_prog) return 0;
#define AL(v) v = gl.GetAttribLocation(r_prog, #v)
#define UL(v) v = gl.GetUniformLocation(r_prog, #v)
  AL(aPos); AL(aUv); AL(aShade); AL(aKind);
  UL(uMvp); UL(uTex); UL(uCave); UL(uBg); UL(uEdge); UL(uLight); UL(uLightOn); UL(uMode); UL(uTint); UL(uFog); UL(uMask); UL(uAlpha);
  GLuint t[3]; gl.GenTextures(3, t); r_atlas = t[0]; r_cave = t[1]; r_mask = t[2];
  gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.BindTexture(GL_TEXTURE_2D, r_mask);
  gl.TexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 256, 256, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, r_mask_px); gl_tex_params(GL_CLAMP_TO_EDGE);
  gl.BindTexture(GL_TEXTURE_2D, r_cave);
  gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); gl_tex_params(GL_REPEAT);
  bd_area = -1;
  gl.BindTexture(GL_TEXTURE_2D, r_atlas);
  gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); gl_tex_params(GL_CLAMP_TO_EDGE);
  gl.GenBuffers(1, &r_vbo);
  r_atlas_valid = 0;
  return 1;
}

/* Snapshot of the frame: RAM $000-$7FF | nametables $2000-$2FFF | palette | pattern tables (emu_mx held). */
static void r3d_capture(void) {
  for (int i = 0; i < 0x800; i++) r_snap[i] = (uint8_t)nes_peek(i);
  for (int i = 0; i < 0x1000; i++) r_snap[SNT + i] = (uint8_t)nes_ppu_peek(0x2000 + i);
  for (int i = 0; i < 0x20; i++) r_snap[SPAL + i] = (uint8_t)nes_ppu_peek(0x3F00 + i);
  for (int i = 0; i < 0x2000; i++) r_snap[SCHR + i] = (uint8_t)nes_ppu_peek(i);
  r_have = 1;
}

static void view(const uint8_t *f) {
  int ctrl = f[0xFF];
  ox = f[0xFD] + (ctrl & 1) * 256; oy = f[0xFC] + ((ctrl >> 1) & 1) * 240;
  bgBase = (ctrl & 0x10) ? 0x1000 : 0; spBase = (ctrl & 0x08) ? 0x1000 : 0;
}
static void ntTile(const uint8_t *f, int wx, int wy) {
  wx = ((wx % 512) + 512) % 512; wy = ((wy % 480) + 480) % 480;
  int tbl = (wx >= 256 ? 1 : 0) + (wy >= 240 ? 2 : 0), x = (wx % 256) >> 3, y = (wy % 240) >> 3;
  ntT = f[SNT + tbl * 0x400 + y * 32 + x];
  int a = f[SNT + tbl * 0x400 + 0x3C0 + (y >> 2) * 8 + (x >> 2)];
  ntP = (a >> (((y & 2) << 1) | (x & 2))) & 3;
}
static int isStatueTile(int t) {
  static const int S[] = { 0xFA, 0xFB, 0xC5, 0xC6, 0xC7, 0xD5, 0xD6, 0xD7, 0xE5, 0xE6, 0xE7, 0xC8, 0xC9, 0xEB, 0xD8, 0xD9, 0xEA, 0xE8, 0xE9 };
  for (size_t i = 0; i < sizeof S / sizeof S[0]; i++) if (S[i] == t) return 1;
  return 0;
}
static int isDoor(int t) { return t == 0x0F || t == 0x1F || t == 0x2F || t == 0x6A || t == 0x6B || t == 0x6C; }
static int isHud(int y, int t, int x) {
  if ((y != 0x18 && y != 0x22 && y != 0x2C) || x >= 0x50) return 0;
  return t == 0x5E || t == 0x5F || t == 0x76 || t == 0x7F || t == 0x3A || t == 0x6E || t == 0x6F || t == 0xFF || t == 0x58 || t == 0x59 || (t >= 0xA0 && t <= 0xA9);
}
static void findSlabs(const uint8_t *f, int area) {
  for (int s = 0; s < 64; s++) { int o = 0x200 + s * 4, y = f[o], t = f[o + 1]; slab[s] = y < 0xEF && (isDoor(t) || (area == 3 && isStatueTile(t))); }
  for (int s = 0; s < 64; s++) {
    int o = 0x200 + s * 4, y = f[o], t = f[o + 1], x = f[o + 3];
    if (y >= 0xEF || t != 0x28) continue;
    int run = 0;
    for (int k = 0; k < 64 && run < 3; k++) {
      int ok = 0x200 + k * 4;
      if (f[ok] == y && f[ok + 3] == x + 8 * (run + 1) && (f[ok + 1] == 0x38 || f[ok + 1] == 0x28)) { run++; k = -1; }
    }
    if (run == 3) for (int k = 0; k < 64; k++) {
      int ok = 0x200 + k * 4, kx = f[ok + 3];
      if (f[ok] == y && kx >= x && kx <= x + 24 && (f[ok + 1] == 0x38 || f[ok + 1] == 0x28)) slab[k] = 1;
    }
  }
}
static void buildAtlas(const uint8_t *f) {
  if (r_atlas_valid && !memcmp(f + SPAL, r_lastPal, 0x20) && !memcmp(f + SCHR, r_lastChr, 0x2000)) return;
  memcpy(r_lastPal, f + SPAL, 0x20); memcpy(r_lastChr, f + SCHR, 0x2000); r_atlas_valid = 1;
  /* passable tiles that look exactly like a solid one: hidden passages and pipes, drawn see-through */
  for (int t = 0; t < 256; t++) {
    passable[t] = 0;
    if (t < 0xA2 || t == 0xFF || t == 0xF0 || t == 0xF1) continue;
    int blank = 1; for (int i = 0; i < 16 && blank; i++) blank = f[SCHR + bgBase + t * 16 + i] == 0;
    if (blank) continue;
    for (int u = 0; u < 0xA0 && !passable[t]; u++) passable[t] = !memcmp(f + SCHR + bgBase + t * 16, f + SCHR + bgBase + u * 16, 16);
  }
  for (int set = 0; set < 8; set++) {
    int spr = set >= 4, pal = set & 3, base = spr ? spBase : bgBase;
    for (int t = 0; t < 256; t++) {
      int idx = set * 256 + t, ax = (idx % 64) * 8, ay = (idx / 64) * 8;
      for (int y = 0; y < 8; y++) {
        int lo = f[SCHR + base + t * 16 + y], hi = f[SCHR + base + t * 16 + y + 8];
        for (int x = 0; x < 8; x++) {
          int c = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1), k = ((ay + y) * 512 + ax + x) * 4;
          uint32_t col = rgbi(f[SPAL + (spr ? 16 : 0) + pal * 4 + c]);
          r_atlas_px[k] = col >> 16; r_atlas_px[k + 1] = col >> 8; r_atlas_px[k + 2] = col; r_atlas_px[k + 3] = c ? 255 : 0;
        }
      }
    }
  }
  gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, r_atlas); gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 256, GL_RGBA, GL_UNSIGNED_BYTE, r_atlas_px);
  bd_dirty = 1;
}
static void uvRect(int set, int t) {
  int idx = set * 256 + t; float e = 0.02f;
  u0_ = ((idx % 64) * 8 + e) / 512.0f; v0_ = ((idx / 64) * 8 + e) / 256.0f;
  u1_ = ((idx % 64) * 8 + 8 - e) / 512.0f; v1_ = ((idx / 64) * 8 + 8 - e) / 256.0f;
}
static void push(float x, float y, float z, float u, float v, float shade, float kind) {
  if ((nv + 1) * 7 > vcap) { vcap = vcap ? vcap * 2 : 1 << 17; verts = realloc(verts, vcap * sizeof(float)); }
  float *o = verts + nv * 7; o[0] = x; o[1] = y; o[2] = z; o[3] = u; o[4] = v; o[5] = shade; o[6] = kind; nv++;
}
static void quad(float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, float dx, float dy, float dz,
                 float ua, float va, float ub, float vb, float uc, float vc, float ud, float vd, float shade, float kind) {
  push(ax, ay, az, ua, va, shade, kind); push(bx, by, bz, ub, vb, shade, kind); push(cx, cy, cz, uc, vc, shade, kind);
  push(ax, ay, az, ua, va, shade, kind); push(cx, cy, cz, uc, vc, shade, kind); push(dx, dy, dz, ud, vd, shade, kind);
}

/* Item balls: the 2x2 BG metatile $88 $89 / $8A $8B (same tiles in every area's CHR) - a power-up held by a
 * Chozo statue or sitting on a pedestal. Drawn as a camera-facing picture instead of a cube.
 * Lava: tiles $F0 (surface) / $F1 (body), recognised by their pattern (Brinstar, Norfair, Kraid and Ridley all load the
 * same lava graphics there) - drawn as a liquid volume through the block depth instead of on the back wall, so the
 * cave backdrop never shows through it. */
static const uint8_t LAVA_PAT[32] = { 0x45,0xd7,0xff,0xff,0xfd,0xff,0xbf,0xfb,0,0,0,0,0x02,0,0x40,0x04,
                                      0xff,0xbb,0xff,0xff,0xef,0xff,0x7f,0xfd,0,0x44,0,0,0x10,0,0x80,0x02 };
static int lavaOk;
static int isLava(int t) { return lavaOk && (t == 0xF0 || t == 0xF1); }
static void sphere(float cx, float cy, int set, const int *t4, float x0, float y0) {
  /* A ball looks the same from every side, so it's a flat picture turned to face the camera (billboard), centred in
     the block depth. Depth-tested, so the statue's hand in front still overlaps its lower edge. */
  float p = pitch_ * (float)M_PI / 180, y = yaw_ * (float)M_PI / 180;
  float rx = cosf(y), rz = -sinf(y), ux = -sinf(p) * sinf(y), uy = cosf(p), uz = -sinf(p) * cosf(y);
  for (int i = 0; i < 4; i++) {
    float a0 = x0 + (i & 1) * 8 - cx, a1 = a0 + 8, b0 = y0 - (i >> 1) * 8 - cy, b1 = b0 - 8;   /* offsets from the centre */
#define BP(a, b) cx + rx * (a) + ux * (b), cy + uy * (b), rz * (a) + uz * (b)
    uvRect(set, t4[i]);
    quad(BP(a0, b1), BP(a1, b1), BP(a1, b0), BP(a0, b0), u0_, v1_, u1_, v1_, u1_, v0_, u0_, v0_, 1, 2);
#undef BP
  }
}
static void buildScene(const uint8_t *f, const R3DOpts *op, int W, int H) {
  nv = 0;
  int D = op->depth; float zf = D / 2.0f, zb = -D / 2.0f;
  float backZ = !strcmp(op->back, "far") ? -D * 2.5f : zb - 0.01f;
  int tx0 = r_fdiv(ox, 8), ty0 = r_fdiv(oy + 8, 8), cols = r_fdiv(ox + 255, 8) - tx0 + 1, rows = r_fdiv(oy + 231, 8) - ty0 + 1;
  if (cols > 40) cols = 40; if (rows > 40) rows = 40;
  memset(r_mask_px, 0, 256 * 240);
  for (int ty = r_fdiv(oy, 8); ty <= r_fdiv(oy + 239, 8); ty++) for (int tx = r_fdiv(ox, 8); tx <= r_fdiv(ox + 255, 8); tx++) {
    ntTile(f, tx * 8, ty * 8); int t = ntT;
    if (t == 0xFF || t == 0xA0 || t == 0xA1) continue;
    for (int py = 0; py < 8; py++) {
      int sy = ty * 8 + py - oy; if (sy < 0 || sy >= 240) continue;
      int bits = f[SCHR + bgBase + t * 16 + py] | f[SCHR + bgBase + t * 16 + py + 8]; if (!bits) continue;
      for (int px = 0; px < 8; px++) { int sx = tx * 8 + px - ox; if (sx >= 0 && sx < 256 && ((bits >> (7 - px)) & 1)) r_mask_px[sy * 256 + sx] = 255; }
    }
  }
  static unsigned char shapedTile[256];   /* solid tiles with >= 24/64 see-through pixels: cut-out layer stack (see Renderer3D) */
  for (int t = 0; t < 256; t++) {
    int e = 1; for (int i = 0; i < 16 && e; i++) e = f[SCHR + bgBase + t * 16 + i] == 0; emptyTile[t] = e;
    int holes = 0; for (int y = 0; y < 8; y++) holes += 8 - __builtin_popcount(f[SCHR + bgBase + t * 16 + y] | f[SCHR + bgBase + t * 16 + y + 8]);
    shapedTile[t] = t < 0xA0 && !e && holes >= 24;
  }
  static unsigned char solid[42][42], ball[42][42], lava[42][42], pass[42][42], full[42][42];
  static int trans[42 * 42 * 2]; int nTrans = 0;
  lavaOk = !memcmp(f + SCHR + bgBase + 0xF0 * 16, LAVA_PAT, 32);
  for (int r = -1; r <= rows; r++) for (int c = -1; c <= cols; c++) {
    ntTile(f, (tx0 + c) * 8, (ty0 + r) * 8);
    solid[r + 1][c + 1] = ntT != 0xFF && ntT < 0xA0 && !emptyTile[ntT];
    full[r + 1][c + 1] = solid[r + 1][c + 1] && !shapedTile[ntT];
    lava[r + 1][c + 1] = isLava(ntT);
    pass[r + 1][c + 1] = passable[ntT];
    ball[r + 1][c + 1] = 0;
  }
  for (int r = -1; r < rows; r++) for (int c = -1; c < cols; c++) {        /* item balls: $88 $89 / $8A $8B */
    ntTile(f, (tx0 + c) * 8, (ty0 + r) * 8); if (ntT != 0x88) continue;
    int p = ntP;
    ntTile(f, (tx0 + c + 1) * 8, (ty0 + r) * 8); if (ntT != 0x89) continue;
    ntTile(f, (tx0 + c) * 8, (ty0 + r + 1) * 8); if (ntT != 0x8A) continue;
    ntTile(f, (tx0 + c + 1) * 8, (ty0 + r + 1) * 8); if (ntT != 0x8B) continue;
    ball[r + 1][c + 1] = ball[r + 1][c + 2] = ball[r + 2][c + 1] = ball[r + 2][c + 2] = 1;
    solid[r + 1][c + 1] = solid[r + 1][c + 2] = solid[r + 2][c + 1] = solid[r + 2][c + 2] = 0;
    static const int t4[4] = { 0x88, 0x89, 0x8A, 0x8B };
    float x0 = (tx0 + c) * 8 - ox, y0 = -((ty0 + r) * 8 - oy);
    sphere(x0 + 8, y0 - 7.5f, p, t4, x0, y0);
  }
  int backOff = !strcmp(op->back, "off");
  for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) {
    int wx = (tx0 + c) * 8, wy = (ty0 + r) * 8;
    ntTile(f, wx, wy); int t = ntT, p = ntP;
    if (t == 0xFF || ball[r + 1][c + 1]) continue;
    float x0 = wx - ox, x1 = x0 + 8, y0 = -(wy - oy), y1 = y0 - 8;
    uvRect(p, t); float u0 = u0_, v0 = v0_, u1 = u1_, v1 = v1_;
    if (lava[r + 1][c + 1]) {
      /* liquid volume: front face (wave pixels cut out), flat surface 2 px down the wave tile, open sides */
      quad(x0, y1, zf, x1, y1, zf, x1, y0, zf, x0, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 1, 2);
      if (!lava[r][c + 1]) {
        float ys = y0 - 2; uvRect(p, 0xF1);
        quad(x0, ys, zf, x1, ys, zf, x1, ys, zb, x0, ys, zb, u0_, v1_, u1_, v1_, u1_, v0_, u0_, v0_, 0.92f, 0);
      }
      float ys = lava[r][c + 1] ? y0 : y0 - 2;
      if (!lava[r + 1][c] && !solid[r + 1][c]) quad(x0, y1, zb, x0, y1, zf, x0, ys, zf, x0, ys, zb, u0, v1, u1, v1, u1, v0, u0, v0, 0.7f, 0);
      if (!lava[r + 1][c + 2] && !solid[r + 1][c + 2]) quad(x1, y1, zf, x1, y1, zb, x1, ys, zb, x1, ys, zf, u0, v1, u1, v1, u1, v0, u0, v0, 0.7f, 0);
      continue;
    }
    if ((t == 0xA0 || t == 0xA1) && !emptyTile[t]) {
      float zt = zb + D * 0.4f;
      quad(x0, y1, zt, x1, y1, zt, x1, y0, zt, x0, y0, zt, u0, v1, u1, v1, u1, v0, u0, v0, 0.9f, 0);
      if (!solid[r][c + 1]) quad(x0, y0, zt, x1, y0, zt, x1, y0, zb, x0, y0, zb, u0, v0, u1, v0, u1, v1, u0, v1, 0.78f, 0);
      if (!solid[r + 2][c + 1]) quad(x0, y1, zb, x1, y1, zb, x1, y1, zt, x0, y1, zt, u0, v1, u1, v1, u1, v0, u0, v0, 0.45f, 0);
      if (!solid[r + 1][c]) quad(x0, y1, zb, x0, y1, zt, x0, y0, zt, x0, y0, zb, u0, v1, u1, v1, u1, v0, u0, v0, 0.6f, 0);
      if (!solid[r + 1][c + 2]) quad(x1, y1, zt, x1, y1, zb, x1, y0, zb, x1, y0, zt, u0, v1, u1, v1, u1, v0, u0, v0, 0.6f, 0);
      continue;
    }
    if (pass[r + 1][c + 1]) {   /* walk-through tile that looks solid (passages, pipes, bushes): on the back wall + faint block */
      if (!backOff) quad(x0, y1, backZ, x1, y1, backZ, x1, y0, backZ, x0, y0, backZ, u0, v1, u1, v1, u1, v0, u0, v0, 0.62f, 1);
      trans[nTrans * 2] = r * 64 + c; trans[nTrans * 2 + 1] = p << 8 | t; nTrans++; continue;
    }
    if (!solid[r + 1][c + 1]) {
      if (!backOff && !emptyTile[t]) quad(x0, y1, backZ, x1, y1, backZ, x1, y0, backZ, x0, y0, backZ, u0, v1, u1, v1, u1, v0, u0, v0, 0.62f, 1);
      continue;
    }
    if (shapedTile[t]) {
      float step = fmaxf(1.0f, D / 8.0f);
      for (float zz = zb + step / 2; zz < zf; zz += step) quad(x0, y1, zz, x1, y1, zz, x1, y0, zz, x0, y0, zz, u0, v1, u1, v1, u1, v0, u0, v0, 0.72f, 2);
      quad(x0, y1, zf, x1, y1, zf, x1, y0, zf, x0, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 1, 2);
      continue;
    }
    quad(x0, y1, zf, x1, y1, zf, x1, y0, zf, x0, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 1, 0);
    if (!full[r][c + 1]) quad(x0, y0, zf, x1, y0, zf, x1, y0, zb, x0, y0, zb, u0, v0, u1, v0, u1, v1, u0, v1, 0.86f, 0);
    if (!full[r + 2][c + 1]) quad(x0, y1, zb, x1, y1, zb, x1, y1, zf, x0, y1, zf, u0, v1, u1, v1, u1, v0, u0, v0, 0.45f, 0);
    if (!full[r + 1][c]) quad(x0, y1, zb, x0, y1, zf, x0, y0, zf, x0, y0, zb, u0, v1, u1, v1, u1, v0, u0, v0, 0.66f, 0);
    if (!full[r + 1][c + 2]) quad(x1, y1, zf, x1, y1, zb, x1, y0, zb, x1, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 0.66f, 0);
  }
  findSlabs(f, f[0x74] & 15);
  for (int s = 0; s < 64; s++) {
    int o = 0x200 + s * 4, y = f[o] + 1, t = f[o + 1], at = f[o + 2], x = f[o + 3];
    if (y >= 240 || !slab[s] || isHud(y, t, x)) continue;
    uvRect(4 + (at & 3), t);
    float ua = (at & 0x40) ? u1_ : u0_, ub = (at & 0x40) ? u0_ : u1_, va = (at & 0x80) ? v1_ : v0_, vb = (at & 0x80) ? v0_ : v1_;
    float X0 = x, X1 = x + 8, Y0 = -y, Y1 = -y - 8;
    for (float zz = zb + 0.5f; zz < zf; zz += 1) quad(X0, Y1, zz, X1, Y1, zz, X1, Y0, zz, X0, Y0, zz, ua, vb, ub, vb, ub, va, ua, va, 0.72f, 2);
    quad(X0, Y1, zf, X1, Y1, zf, X1, Y0, zf, X0, Y0, zf, ua, vb, ub, vb, ub, va, ua, va, 1, 2);
  }
  caveStart = nv; caveCount = 0;
  if (op->cave && bd_ok) {     /* backdrop plane, half-speed parallax, one texel = one game pixel at its depth */
    float z = -D * 3 - 24, X0 = -1400, X1 = 1656, Yt = 900, Yb = -1300, k = 0.5f;
    float sc = (camDist - z) / camDist;
    float ua = (X0 - 128) / (256 * sc) + accX * k / 256.0f, ub = (X1 - 128) / (256 * sc) + accX * k / 256.0f;
    float va = (-Yt - 120) / (256 * sc) + accY * k / 256.0f, vb = (-Yb - 120) / (256 * sc) + accY * k / 256.0f;
    quad(X0, Yb, z, X1, Yb, z, X1, Yt, z, X0, Yt, z, ua, vb, ub, vb, ub, va, ua, va, 1, 3);
    caveCount = nv - caveStart;
  }
  transStart = nv;            /* see-through blocks, drawn blended after the scene */
  for (int i = 0; i < nTrans; i++) {
    int r = trans[i * 2] / 64, c = trans[i * 2] % 64, p = trans[i * 2 + 1] >> 8, t = trans[i * 2 + 1] & 255;
    float x0 = (tx0 + c) * 8 - ox, x1 = x0 + 8, y0 = -((ty0 + r) * 8 - oy), y1 = y0 - 8;
    uvRect(p, t); float u0 = u0_, v0 = v0_, u1 = u1_, v1 = v1_;
    quad(x0, y1, zf, x1, y1, zf, x1, y0, zf, x0, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 1.1f, 6);
    if (!solid[r][c + 1] && !pass[r][c + 1]) quad(x0, y0, zf, x1, y0, zf, x1, y0, zb, x0, y0, zb, u0, v0, u1, v0, u1, v1, u0, v1, 0.95f, 6);
    if (!solid[r + 2][c + 1] && !pass[r + 2][c + 1]) quad(x0, y1, zb, x1, y1, zb, x1, y1, zf, x0, y1, zf, u0, v1, u1, v1, u1, v0, u0, v0, 0.6f, 6);
    if (!solid[r + 1][c] && !pass[r + 1][c]) quad(x0, y1, zb, x0, y1, zf, x0, y0, zf, x0, y0, zb, u0, v1, u1, v1, u1, v0, u0, v0, 0.75f, 6);
    if (!solid[r + 1][c + 2] && !pass[r + 1][c + 2]) quad(x1, y1, zf, x1, y1, zb, x1, y0, zb, x1, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 0.75f, 6);
  }
  transCount = nv - transStart;
  spriteStart = nv; int found = 0;
  {  /* light: centre of Samus = the palette-0 sprites around the lowest-numbered one */
    int ax = -1, ay = 0, bx0 = 999, by0 = 999, bx1 = -999, by1 = -999;
    for (int s = 0; s < 64; s++) {
      int o = 0x200 + s * 4, y = f[o] + 1, t = f[o + 1], at = f[o + 2], x = f[o + 3];
      if (y >= 240 || (at & 3) || isHud(y, t, x) || slab[s]) continue;
      if (ax < 0) { ax = x; ay = y; }
      if (abs(x - ax) > 24 || abs(y - ay) > 40) continue;
      if (x < bx0) bx0 = x; if (y < by0) by0 = y; if (x + 8 > bx1) bx1 = x + 8; if (y + 8 > by1) by1 = y + 8;
    }
    int sx, sy;
    float tx = lightX, ty = lightY;
    if (samus_on_screen(f, &sx, &sy)) { found = 1; tx = sx; ty = -sy; }      /* from the game's own RAM */
    else if (ax >= 0) { found = 1; tx = (bx0 + bx1) / 2.0f; ty = -(by0 + by1) / 2.0f; }
    float k = lightAmt < 0.05f ? 1.0f : 0.35f;   /* ease: a stray detection can't strobe the room */
    lightX += (tx - lightX) * k; lightY += (ty - lightY) * k;
    lightMiss = found ? 0 : (lightMiss < 999 ? lightMiss + 1 : 999);
    hasLight = lightMiss < 45;
    lightAmt += ((hasLight ? 1.0f : 0.0f) - lightAmt) * 0.08f;
  }
  for (int s = 63; s >= 0; s--) {
    int o = 0x200 + s * 4, y = f[o] + 1, t = f[o + 1], at = f[o + 2], x = f[o + 3];
    if (y >= 240 || isHud(y, t, x) || slab[s]) continue;
    uvRect(4 + (at & 3), t);
    float ua = (at & 0x40) ? u1_ : u0_, ub = (at & 0x40) ? u0_ : u1_, va = (at & 0x80) ? v1_ : v0_, vb = (at & 0x80) ? v0_ : v1_;
    float X0 = x, X1 = x + 8, Y0 = -y, Y1 = -y - 8;
    quad(X0, Y1, 0, X1, Y1, 0, X1, Y0, 0, X0, Y0, 0, ua, vb, ub, vb, ub, va, ua, va, 1, (at & 0x20) ? 5 : 2);
  }
  hudStart = nv;
  float asp = 256.0f * 8 / 7 / 224.0f, rw = W, rh = W / asp;
  if (rh > H) { rh = H; rw = H * asp; }
  float rx = (W - rw) / 2, ry = (H - rh) / 2, sx = rw / 256.0f, sy = rh / 224.0f;
  for (int s = 0; s < 64; s++) {
    int o = 0x200 + s * 4, y = f[o] + 1, t = f[o + 1], at = f[o + 2], x = f[o + 3];
    if (y >= 240 || !isHud(y, t, x) || t == 0xFF) continue;
    uvRect(4 + (at & 3), t);
    float ua = (at & 0x40) ? u1_ : u0_, ub = (at & 0x40) ? u0_ : u1_, va = (at & 0x80) ? v1_ : v0_, vb = (at & 0x80) ? v0_ : v1_;
    float X0 = rx + x * sx, X1 = rx + (x + 8) * sx, Y0 = ry + (y - 8) * sy, Y1 = ry + y * sy;
    quad(X0, Y1, 0, X1, Y1, 0, X1, Y0, 0, X0, Y0, 0, ua, vb, ub, vb, ub, va, ua, va, 1, 4);
  }
}

static void cam_preset(const char *n, float *p) {
  if (!strcmp(n, "diorama")) { p[0] = 30; p[1] = -24; p[2] = 0.92f; }
  else if (!strcmp(n, "side")) { p[0] = 0; p[1] = 0; p[2] = 1; }
  else if (!strcmp(n, "low")) { p[0] = -10; p[1] = 18; p[2] = 1.05f; }
  else if (!strcmp(n, "far")) { p[0] = 22; p[1] = 0; p[2] = 0.62f; }
  else { p[0] = 16; p[1] = 0; p[2] = 1; }
}
/* Draws the 3D view into the current W x H viewport. */
static void r3d_draw(const R3DOpts *op, int W, int H) {
  if (!r_have) return;
  const uint8_t *f = r_snap;
  view(f); buildAtlas(f); follow_scroll(); buildScene(f, op, W, H);
  if (op->cave) build_backdrop(f);    /* after buildScene (uses emptyTile); the plane shows from the next frame */
  float pr[3]; cam_preset(op->cam, pr);
  pitch_ += (pr[0] - pitch_) * 0.12f; yaw_ += (pr[1] - yaw_) * 0.12f; zoom_ += (pr[2] - zoom_) * 0.12f;
  uint32_t bgc = rgbi(f[SPAL]);
  gl.ClearColor(0, 0, 0, 1); gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  gl.Enable(GL_DEPTH_TEST); gl.Enable(GL_CULL_FACE); gl.Disable(GL_BLEND);
  float fov = 32, asp = W / (float)H, halfH = fmaxf(112.0f, 128.0f * 8 / 7 / asp) / zoom_;
  float dist = halfH / tanf(fov / 2 * (float)M_PI / 180) + op->depth;
  camDist = dist;
  float p = pitch_ * (float)M_PI / 180, y = yaw_ * (float)M_PI / 180, cx = 128, cy = -120;
  float ex = cx + dist * sinf(y) * cosf(p), ey = cy + dist * sinf(p), ez = dist * cosf(y) * cosf(p);
  float proj[16], viewM[16], stretch[16], tmp[16], mvp[16];
  m_persp(proj, fov, asp, 10, 5000); m_lookat(viewM, ex, ey, ez, cx, cy, 0, 0, 1, 0);
  m_ident(stretch); stretch[0] = 8.0f / 7; stretch[12] = 128 - 128 * 8.0f / 7;
  m_mul(tmp, viewM, stretch); m_mul(mvp, proj, tmp);

  gl.UseProgram(r_prog);
  gl.BindBuffer(GL_ARRAY_BUFFER, r_vbo); gl.BufferData(GL_ARRAY_BUFFER, nv * 7 * 4, verts, GL_STREAM_DRAW);
  int st = 28;
  gl.EnableVertexAttribArray(aPos); gl.VertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, st, (void *)0);
  gl.EnableVertexAttribArray(aUv); gl.VertexAttribPointer(aUv, 2, GL_FLOAT, GL_FALSE, st, (void *)12);
  if (aShade >= 0) { gl.EnableVertexAttribArray(aShade); gl.VertexAttribPointer(aShade, 1, GL_FLOAT, GL_FALSE, st, (void *)20); }
  if (aKind >= 0) { gl.EnableVertexAttribArray(aKind); gl.VertexAttribPointer(aKind, 1, GL_FLOAT, GL_FALSE, st, (void *)24); }
  gl.UniformMatrix4fv(uMvp, 1, GL_FALSE, mvp);
  gl.Uniform3f(uBg, ((bgc >> 16) & 255) / 255.0f, ((bgc >> 8) & 255) / 255.0f, (bgc & 255) / 255.0f);
  gl.Uniform1i(uEdge, 1);
  gl.Uniform1f(uLightOn, op->light ? lightAmt : 0); gl.Uniform2f(uLight, lightX, lightY);
  gl.Uniform3f(uTint, 0.5f, 0.5f, 0.5f); gl.Uniform3f(uFog, 0, 0, 0);
  gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, r_atlas); gl.Uniform1i(uTex, 0);
  gl.ActiveTexture(GL_TEXTURE1); gl.BindTexture(GL_TEXTURE_2D, r_cave); gl.Uniform1i(uCave, 1);
  gl.ActiveTexture(GL_TEXTURE2); gl.BindTexture(GL_TEXTURE_2D, r_mask); gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 240, GL_LUMINANCE, GL_UNSIGNED_BYTE, r_mask_px); gl.Uniform1i(uMask, 2);
  gl.Uniform1i(uMode, 0); gl.Uniform1f(uAlpha, 1);
  gl.DrawArrays(GL_TRIANGLES, 0, caveStart);
  if (caveCount > 0) { gl.Uniform1i(uMode, 1); gl.DrawArrays(GL_TRIANGLES, caveStart, caveCount); }
  if (transCount > 0) {
    gl.Uniform1i(uMode, 0); gl.Uniform1f(uAlpha, 0.42f);
    gl.Enable(GL_BLEND); gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); gl.DepthMask(GL_FALSE);
    gl.DrawArrays(GL_TRIANGLES, transStart, transCount);
    gl.DepthMask(GL_TRUE); gl.Disable(GL_BLEND); gl.Uniform1f(uAlpha, 1);
  }
  if (hudStart > spriteStart) { gl.Uniform1i(uMode, 0); gl.Disable(GL_DEPTH_TEST); gl.DrawArrays(GL_TRIANGLES, spriteStart, hudStart - spriteStart); gl.Enable(GL_DEPTH_TEST); }
  if (nv > hudStart) {
    float o[16]; m_ortho(o, 0, W, H, 0, -1, 1);
    gl.Disable(GL_DEPTH_TEST); gl.Disable(GL_CULL_FACE);
    gl.UniformMatrix4fv(uMvp, 1, GL_FALSE, o); gl.Uniform1i(uMode, 2);
    gl.DrawArrays(GL_TRIANGLES, hudStart, nv - hudStart);
  }
  gl.Disable(GL_DEPTH_TEST); gl.Disable(GL_CULL_FACE);
  gl.DisableVertexAttribArray(aPos); gl.DisableVertexAttribArray(aUv);
  if (aShade >= 0) gl.DisableVertexAttribArray(aShade);
  if (aKind >= 0) gl.DisableVertexAttribArray(aKind);
  gl.ActiveTexture(GL_TEXTURE0);
}

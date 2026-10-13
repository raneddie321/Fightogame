// FIGHTO - 2D batch renderer with a baked Inter font atlas.
#include "draw2d.h"
#include "../gfx/renderer.h"
#include "../platform/platform.h"

struct Glyph {
    i16 x, y, w, h, xoff, yoff;
    float advance;
};
struct FontData {
    float size, ascent, descent, lineHeight;
    Glyph glyphs[96];
};
struct UIVert {
    vec2 pos;
    vec2 uv;
    u8 col[4];
};

static struct {
    Shader shader;
    u32 atlas;
    int atlasW, atlasH;
    FontData fonts[FONT_COUNT];
    u32 vao, vbo, ibo;
    Array<UIVert> verts;
    Array<u32> inds;
    int w, h;
    vec2 white;
    bool ok;
} U;

bool ui_init() {
    memset(&U, 0, sizeof(U));
    if (!shader_load(&U.shader, "ui.glsl", "")) return false;
    Array<u8> data = {};
    if (!plat_read_asset("fonts/fonts.bin", &data) || data.count < 20 || memcmp(data.data, "FFNT", 4) != 0) {
        log_error("Font atlas missing or invalid");
        data.free_mem();
        return false;
    }
    const u8* p = data.data + 4;
    u32 hdr[4];
    memcpy(hdr, p, 16);
    p += 16;
    U.atlasW = (int)hdr[1];
    U.atlasH = (int)hdr[2];
    int fontCount = (int)hdr[3];
    for (int f = 0; f < fontCount && f < FONT_COUNT; f++) {
        p += 16; // name
        float m[4];
        memcpy(m, p, 16);
        p += 16;
        FontData& fd = U.fonts[f];
        fd.size = m[0]; fd.ascent = m[1]; fd.descent = m[2]; fd.lineHeight = m[3];
        u32 gc;
        memcpy(&gc, p, 4);
        p += 4;
        for (u32 g = 0; g < gc; g++) {
            u32 cp;
            i16 v[6];
            float adv;
            memcpy(&cp, p, 4);
            memcpy(v, p + 4, 12);
            memcpy(&adv, p + 16, 4);
            p += 20;
            if (cp >= 32 && cp < 128) {
                Glyph& gl = fd.glyphs[cp - 32];
                gl.x = v[0]; gl.y = v[1]; gl.w = v[2]; gl.h = v[3]; gl.xoff = v[4]; gl.yoff = v[5];
                gl.advance = adv;
            }
        }
    }
    glGenTextures(1, &U.atlas);
    glBindTexture(GL_TEXTURE_2D, U.atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, U.atlasW, U.atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, p);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    data.free_mem();
    U.white = vec2(4.0f / U.atlasW, 4.0f / U.atlasH);

    glGenVertexArrays(1, &U.vao);
    glGenBuffers(1, &U.vbo);
    glGenBuffers(1, &U.ibo);
    glBindVertexArray(U.vao);
    glBindBuffer(GL_ARRAY_BUFFER, U.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, U.ibo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)offsetof(UIVert, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)offsetof(UIVert, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UIVert), (void*)offsetof(UIVert, col));
    glBindVertexArray(0);
    U.ok = true;
    return true;
}

void ui_shutdown() {
    U.verts.free_mem();
    U.inds.free_mem();
}

float ui_scale() { return U.h / 1080.0f; }
int ui_width() { return U.w; }
int ui_height() { return U.h; }

static void flush() {
    if (!U.ok || U.inds.count == 0) return;
    shader_use(&U.shader);
    set_vec2(&U.shader, "uScreen", vec2((float)U.w, (float)U.h));
    set_float(&U.shader, "uIsFont", 1.0f);
    set_int(&U.shader, "uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, U.atlas);
    glBindVertexArray(U.vao);
    glBindBuffer(GL_ARRAY_BUFFER, U.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(UIVert) * (size_t)U.verts.count), U.verts.data, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, U.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(u32) * (size_t)U.inds.count), U.inds.data, GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, U.inds.count, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    U.verts.clear();
    U.inds.clear();
}

void ui_begin(int w, int h) {
    U.w = w;
    U.h = h;
    U.verts.clear();
    U.inds.clear();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void ui_end() {
    flush();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glUseProgram(0);
}

void ui_clip(float x, float y, float w, float h) {
    flush();
    if (w <= 0 || h <= 0) { glDisable(GL_SCISSOR_TEST); return; }
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)x, (int)(U.h - y - h), (int)w, (int)h);
}

static void put(vec2 p, vec2 uv, vec4 c) {
    UIVert v;
    v.pos = p;
    v.uv = uv;
    v.col[0] = (u8)(saturate(c.x) * 255.0f);
    v.col[1] = (u8)(saturate(c.y) * 255.0f);
    v.col[2] = (u8)(saturate(c.z) * 255.0f);
    v.col[3] = (u8)(saturate(c.w) * 255.0f);
    U.verts.push(v);
}

static void quad_uv(vec2 a, vec2 b, vec2 c, vec2 d, vec2 ua, vec2 ub, vec2 uc, vec2 ud, vec4 ca, vec4 cb, vec4 cc, vec4 cd) {
    u32 base = (u32)U.verts.count;
    put(a, ua, ca); put(b, ub, cb); put(c, uc, cc); put(d, ud, cd);
    U.inds.push(base); U.inds.push(base + 1); U.inds.push(base + 2);
    U.inds.push(base); U.inds.push(base + 2); U.inds.push(base + 3);
}

void ui_rect(float x, float y, float w, float h, vec4 c) {
    quad_uv(vec2(x, y), vec2(x + w, y), vec2(x + w, y + h), vec2(x, y + h), U.white, U.white, U.white, U.white, c, c, c, c);
}

void ui_rect_grad(float x, float y, float w, float h, vec4 top, vec4 bottom) {
    quad_uv(vec2(x, y), vec2(x + w, y), vec2(x + w, y + h), vec2(x, y + h), U.white, U.white, U.white, U.white, top, top, bottom, bottom);
}

void ui_rect_outline(float x, float y, float w, float h, float t, vec4 c) {
    ui_rect(x, y, w, t, c);
    ui_rect(x, y + h - t, w, t, c);
    ui_rect(x, y + t, t, h - 2 * t, c);
    ui_rect(x + w - t, y + t, t, h - 2 * t, c);
}

void ui_quad(vec2 a, vec2 b, vec2 c, vec2 d, vec4 col) {
    quad_uv(a, b, c, d, U.white, U.white, U.white, U.white, col, col, col, col);
}

void ui_triangle(vec2 a, vec2 b, vec2 c, vec4 col) {
    u32 base = (u32)U.verts.count;
    put(a, U.white, col); put(b, U.white, col); put(c, U.white, col);
    U.inds.push(base); U.inds.push(base + 1); U.inds.push(base + 2);
}

void ui_line(vec2 a, vec2 b, float t, vec4 c) {
    vec2 d = normalize(b - a);
    vec2 n(-d.y * t * 0.5f, d.x * t * 0.5f);
    ui_quad(a + n, b + n, b - n, a - n, c);
}

void ui_circle(vec2 c, float r, vec4 col, int seg) {
    u32 center = (u32)U.verts.count;
    put(c, U.white, col);
    for (int i = 0; i <= seg; i++) {
        float a = TAU_F * i / seg;
        put(vec2(c.x + cosf(a) * r, c.y + sinf(a) * r), U.white, col);
    }
    for (int i = 0; i < seg; i++) { U.inds.push(center); U.inds.push(center + 1 + (u32)i); U.inds.push(center + 2 + (u32)i); }
}

void ui_ring(vec2 c, float r0, float r1, vec4 col, int seg, float a0, float a1) {
    for (int i = 0; i < seg; i++) {
        float t0 = a0 + (a1 - a0) * i / seg, t1 = a0 + (a1 - a0) * (i + 1) / seg;
        vec2 d0(cosf(t0), sinf(t0)), d1(cosf(t1), sinf(t1));
        ui_quad(c + d0 * r0, c + d1 * r0, c + d1 * r1, c + d0 * r1, col);
    }
}

float ui_text_width(int font, float size, const char* s) {
    const FontData& f = U.fonts[font];
    float sc = size / f.lineHeight;
    float w = 0, best = 0;
    for (; *s; s++) {
        if (*s == '\n') { best = fmaxf(best, w); w = 0; continue; }
        int c = (u8)*s;
        if (c < 32 || c > 126) c = '?';
        w += f.glyphs[c - 32].advance * sc;
    }
    return fmaxf(best, w);
}

float ui_text(int font, float x, float y, float size, const char* s, vec4 color, int align) {
    if (!U.ok) return 0;
    const FontData& f = U.fonts[font];
    float sc = size / f.lineHeight;
    float w = ui_text_width(font, size, s);
    float cx = x;
    if (align == ALIGN_CENTER) cx -= w * 0.5f;
    else if (align == ALIGN_RIGHT) cx -= w;
    float startX = cx;
    float iw = 1.0f / U.atlasW, ih = 1.0f / U.atlasH;
    for (; *s; s++) {
        if (*s == '\n') { cx = startX; y += size; continue; }
        int c = (u8)*s;
        if (c < 32 || c > 126) c = '?';
        const Glyph& g = f.glyphs[c - 32];
        if (g.w > 0 && g.h > 0) {
            float gx = cx + g.xoff * sc, gy = y + g.yoff * sc;
            float gw = g.w * sc, gh = g.h * sc;
            vec2 u0(g.x * iw, g.y * ih), u1((g.x + g.w) * iw, (g.y + g.h) * ih);
            quad_uv(vec2(gx, gy), vec2(gx + gw, gy), vec2(gx + gw, gy + gh), vec2(gx, gy + gh),
                    u0, vec2(u1.x, u0.y), u1, vec2(u0.x, u1.y), color, color, color, color);
        }
        cx += g.advance * sc;
    }
    return w;
}

float ui_text_shadow(int font, float x, float y, float size, const char* s, vec4 color, int align) {
    float o = fmaxf(1.0f, size * 0.06f);
    ui_text(font, x + o, y + o, size, s, vec4(0, 0, 0, color.w * 0.75f), align);
    return ui_text(font, x, y, size, s, color, align);
}

float ui_text_wrapped(int font, float x, float y, float size, float maxW, const char* s, vec4 color, int align) {
    char line[512];
    int lineLen = 0;
    float startY = y;
    const char* p = s;
    while (*p) {
        // Read next word
        const char* wstart = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        int wlen = (int)(p - wstart);
        char trial[512];
        int tl = 0;
        if (lineLen > 0) { memcpy(trial, line, (size_t)lineLen); tl = lineLen; trial[tl++] = ' '; }
        if (tl + wlen >= (int)sizeof(trial) - 1) wlen = (int)sizeof(trial) - 2 - tl;
        memcpy(trial + tl, wstart, (size_t)wlen);
        tl += wlen;
        trial[tl] = 0;
        if (lineLen > 0 && ui_text_width(font, size, trial) > maxW) {
            line[lineLen] = 0;
            ui_text_shadow(font, x, y, size, line, color, align);
            y += size * 1.15f;
            memcpy(line, wstart, (size_t)wlen);
            lineLen = wlen;
        } else {
            memcpy(line, trial, (size_t)tl);
            lineLen = tl;
        }
        if (*p == '\n') {
            line[lineLen] = 0;
            ui_text_shadow(font, x, y, size, line, color, align);
            y += size * 1.15f;
            lineLen = 0;
            p++;
        } else if (*p == ' ') {
            p++;
        }
    }
    if (lineLen > 0) {
        line[lineLen] = 0;
        ui_text_shadow(font, x, y, size, line, color, align);
        y += size * 1.15f;
    }
    return y - startY;
}

// FIGHTO - immediate mode 2D drawing (HUD, menus, text, minimap).
#pragma once
#include "../core/vmath.h"

enum FontId { FONT_UI = 0, FONT_BOLD = 1, FONT_TITLE = 2, FONT_COUNT = 3 };
enum TextAlign { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

bool ui_init();
void ui_shutdown();
void ui_begin(int screenW, int screenH);
void ui_end();
float ui_scale();     // screen height / 1080
int ui_width();
int ui_height();

void ui_rect(float x, float y, float w, float h, vec4 color);
void ui_rect_grad(float x, float y, float w, float h, vec4 top, vec4 bottom);
void ui_rect_outline(float x, float y, float w, float h, float t, vec4 color);
void ui_quad(vec2 a, vec2 b, vec2 c, vec2 d, vec4 color);
void ui_line(vec2 a, vec2 b, float thickness, vec4 color);
void ui_circle(vec2 c, float r, vec4 color, int segments = 24);
void ui_ring(vec2 c, float r0, float r1, vec4 color, int segments = 32, float a0 = 0.0f, float a1 = TAU_F);
void ui_triangle(vec2 a, vec2 b, vec2 c, vec4 color);

// size = pixel height of the line. Returns the text width.
float ui_text(int font, float x, float y, float size, const char* s, vec4 color, int align = ALIGN_LEFT);
float ui_text_shadow(int font, float x, float y, float size, const char* s, vec4 color, int align = ALIGN_LEFT);
float ui_text_width(int font, float size, const char* s);
// Word-wrapped text; returns the total height used.
float ui_text_wrapped(int font, float x, float y, float size, float maxWidth, const char* s, vec4 color, int align = ALIGN_LEFT);

// Scissor (clip) rectangle in pixels; pass w<=0 to disable.
void ui_clip(float x, float y, float w, float h);

inline vec4 rgba(float r, float g, float b, float a = 1.0f) { return vec4(r, g, b, a); }
inline vec4 with_alpha(vec4 c, float a) { return vec4(c.x, c.y, c.z, c.w * a); }

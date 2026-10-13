// FIGHTO - dialogue database, conversation playback and subtitles.
#include "game.h"
#include "script.h"
#include "../ui/draw2d.h"
#include "../audio/audio.h"

struct DLine {
    char speaker[16];
    char text[240];
};
struct Conversation {
    char id[40];
    int first, count;
};

static Array<DLine> g_lines;
static Array<Conversation> g_convs;

struct Playing {
    DLine queue[24];
    int count, index;
    float timer;
    float duration;
};
static Playing P;

struct Speaker { const char* id; const char* name; vec3 color; };
static const Speaker g_speakers[] = {
    {"damian", "Damian", vec3(1.0f, 0.85f, 0.55f)},
    {"marcus", "Marcus Reyes", vec3(0.55f, 0.85f, 1.0f)},
    {"eli", "Eli Cross", vec3(0.75f, 1.0f, 0.75f)},
    {"tommy", "Tommy Kowalski", vec3(0.9f, 0.9f, 0.9f)},
    {"varga", "Nikolai Varga", vec3(1.0f, 0.45f, 0.4f)},
    {"kincaid", "Cmdr. Ava Kincaid", vec3(0.95f, 0.75f, 0.25f)},
    {"thorne", "Director Elias Thorne", vec3(0.75f, 0.8f, 0.95f)},
    {"okafor", "Dr. Imani Okafor", vec3(0.6f, 1.0f, 0.85f)},
    {"radio", "Radio", vec3(0.7f, 0.7f, 0.75f)},
    {"cop", "Officer", vec3(0.55f, 0.65f, 1.0f)},
    {"announcer", "Pit Announcer", vec3(1.0f, 0.6f, 0.2f)},
    {"brick", "\"Brick\" Halloran", vec3(1.0f, 0.5f, 0.45f)},
    {"operative", "Operative", vec3(0.9f, 0.4f, 0.35f)},
    {"overseer", "The Overseer", vec3(0.45f, 0.85f, 1.0f)},
    {"tech", "A.E.G.I.S. Tech", vec3(0.95f, 0.75f, 0.25f)},
    {"soldier", "A.E.G.I.S. Soldier", vec3(0.95f, 0.75f, 0.25f)},
    {"civilian", "Civilian", vec3(0.85f, 0.85f, 0.85f)},
    {"thug", "Kings Thug", vec3(1.0f, 0.4f, 0.35f)},
    {"news", "WBRK News", vec3(0.8f, 0.8f, 0.9f)},
    {"shard", "???", vec3(0.4f, 1.0f, 0.9f)},
    {"sal", "Sal the Promoter", vec3(1.0f, 0.75f, 0.35f)},
    {"ace", "Ace", vec3(0.95f, 0.8f, 0.5f)},
    {"maya", "Maya Ortiz", vec3(0.9f, 0.8f, 1.0f)},
};

const char* speaker_display(const char* id, vec3* color) {
    for (int i = 0; i < ARRAY_COUNT(g_speakers); i++) {
        if (str_eq(g_speakers[i].id, id)) {
            if (color) *color = g_speakers[i].color;
            return g_speakers[i].name;
        }
    }
    if (color) *color = vec3(0.9f, 0.9f, 0.9f);
    return id;
}

static bool load_file(const char* file) {
    char path[128];
    fmt_str(path, sizeof(path), "dialogue/%s", file);
    Array<u8> data = {};
    if (!plat_read_asset(path, &data)) { log_error("Missing dialogue file %s", path); return false; }
    const char* p = (const char*)data.data;
    char line[512];
    Conversation* cur = 0;
    while (*p) {
        int n = 0;
        while (*p && *p != '\n') { if (n < 511) line[n++] = *p; p++; }
        if (*p) p++;
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) n--;
        line[n] = 0;
        if (n == 0 || line[0] == '#') continue;
        if (line[0] == '@') {
            Conversation c;
            memset(&c, 0, sizeof(c));
            str_copy(c.id, sizeof(c.id), line + 1);
            c.first = g_lines.count;
            g_convs.push(c);
            cur = &g_convs.last();
            continue;
        }
        char* colon = strchr(line, ':');
        if (!colon || !cur) continue;
        *colon = 0;
        DLine dl;
        memset(&dl, 0, sizeof(dl));
        str_copy(dl.speaker, sizeof(dl.speaker), line);
        const char* t = colon + 1;
        while (*t == ' ') t++;
        str_copy(dl.text, sizeof(dl.text), t);
        g_lines.push(dl);
        cur->count++;
    }
    data.free_mem();
    return true;
}

bool dialogue_load_all() {
    g_lines.clear();
    g_convs.clear();
    Array<u8> idx = {};
    if (!plat_read_asset("dialogue/index.txt", &idx)) return false;
    const char* cur = (const char*)idx.data;
    ScriptLine l;
    int ln = 0;
    while (script_next_line(&cur, &l, &ln)) load_file(l.argv[0]);
    idx.free_mem();
    log_info("Loaded %d conversations (%d lines)", g_convs.count, g_lines.count);
    return true;
}

static float line_duration(const char* text) { return 1.4f + (float)strlen(text) * 0.052f; }

static void begin_line() {
    if (P.index >= P.count) return;
    P.timer = 0;
    P.duration = line_duration(P.queue[P.index].text);
    audio_play(SND_DIALOGUE_BLIP, 0.35f, str_eq(P.queue[P.index].speaker, "damian") ? 0.9f : 1.1f);
}

void dialogue_start(const char* id) {
    for (int i = 0; i < g_convs.count; i++) {
        if (!str_eq(g_convs[i].id, id)) continue;
        P.count = 0;
        P.index = 0;
        for (int k = 0; k < g_convs[i].count && P.count < 24; k++) P.queue[P.count++] = g_lines[g_convs[i].first + k];
        begin_line();
        return;
    }
    log_error("Dialogue '%s' not found", id);
}

void dialogue_say(const char* speaker, const char* text, float duration) {
    if (P.index >= P.count) { P.count = 0; P.index = 0; }
    if (P.count >= 24) return;
    DLine& d = P.queue[P.count++];
    str_copy(d.speaker, sizeof(d.speaker), speaker);
    str_copy(d.text, sizeof(d.text), text);
    if (P.count - 1 == P.index) begin_line();
    FIGHTO_UNUSED(duration);
}

bool dialogue_active() { return P.index < P.count; }
void dialogue_clear() { P.count = 0; P.index = 0; }

void dialogue_skip_line() {
    if (!dialogue_active()) return;
    P.index++;
    begin_line();
}

void dialogue_update(float dt) {
    if (!dialogue_active()) return;
    P.timer += dt;
    if (P.timer >= P.duration) {
        P.index++;
        begin_line();
    }
}

void dialogue_render() {
    if (!dialogue_active() || !g.settings.subtitles) return;
    const DLine& l = P.queue[P.index];
    vec3 col;
    const char* name = speaker_display(l.speaker, &col);
    float s = ui_scale();
    float w = (float)ui_width();
    float h = (float)ui_height();
    float boxW = fminf(w * 0.7f, 1300 * s);
    float size = 30 * s;
    float y = h - (g.letterbox > 0.5f ? 0.12f * h + 120 * s : 200 * s);
    float a = saturate(P.timer * 6.0f) * saturate((P.duration - P.timer) * 4.0f + 0.3f);
    ui_rect(w * 0.5f - boxW * 0.5f - 20 * s, y - 14 * s, boxW + 40 * s, 110 * s, vec4(0, 0, 0, 0.55f * a));
    ui_text_shadow(FONT_BOLD, w * 0.5f, y - 6 * s, 26 * s, name, vec4(col.x, col.y, col.z, a), ALIGN_CENTER);
    ui_text_wrapped(FONT_UI, w * 0.5f, y + 26 * s, size, boxW, l.text, vec4(1, 1, 1, a), ALIGN_CENTER);
}

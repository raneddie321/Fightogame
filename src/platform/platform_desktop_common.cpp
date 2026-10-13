// FIGHTO - helpers shared by the desktop platform backends (Windows / Linux).
// Uses the C standard I/O library for asset and save file access.
#include "platform.h"
#include "platform_desktop.h"
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#define fighto_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define fighto_mkdir(p) mkdir(p, 0755)
#endif

static char g_assetRoot[512] = "assets";
static char g_userDir[512] = "userdata";

const char* key_name(int k) {
    static char buf[2][8];
    static int which = 0;
    if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) {
        which ^= 1;
        buf[which][0] = (char)k;
        buf[which][1] = 0;
        return buf[which];
    }
    switch (k) {
    case KEY_SPACE: return "Space";
    case KEY_ESCAPE: return "Esc";
    case KEY_ENTER: return "Enter";
    case KEY_TAB: return "Tab";
    case KEY_BACKSPACE: return "Backspace";
    case KEY_SHIFT: return "Shift";
    case KEY_CTRL: return "Ctrl";
    case KEY_ALT: return "Alt";
    case KEY_UP: return "Up";
    case KEY_DOWN: return "Down";
    case KEY_LEFT: return "Left";
    case KEY_RIGHT: return "Right";
    case KEY_F1: return "F1"; case KEY_F2: return "F2"; case KEY_F3: return "F3"; case KEY_F4: return "F4";
    case KEY_F5: return "F5"; case KEY_F6: return "F6"; case KEY_F7: return "F7"; case KEY_F8: return "F8";
    case KEY_F9: return "F9"; case KEY_F10: return "F10"; case KEY_F11: return "F11"; case KEY_F12: return "F12";
    case KEY_MOUSE_LEFT: return "LMB";
    case KEY_MOUSE_RIGHT: return "RMB";
    case KEY_MOUSE_MIDDLE: return "MMB";
    case KEY_MOUSE_X1: return "Mouse4";
    case KEY_MOUSE_X2: return "Mouse5";
    default: return "?";
    }
}

static bool file_exists(const char* p) {
    FILE* f = fopen(p, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

void desktop_init_paths(const char* exePath) {
    // Locate the assets folder: next to the executable first, then working directory.
    char dir[512];
    str_copy(dir, sizeof(dir), exePath ? exePath : "");
    int len = (int)strlen(dir);
    while (len > 0 && dir[len - 1] != '/' && dir[len - 1] != '\\') len--;
    dir[len] = 0;
    char probe[600];
    const char* candidates[] = {"assets", "../assets", "../../assets", "../share/fighto/assets"};
    bool found = false;
    for (int i = 0; i < ARRAY_COUNT(candidates) && !found; i++) {
        fmt_str(probe, sizeof(probe), "%s%s/config/game.cfg", dir, candidates[i]);
        if (file_exists(probe)) {
            fmt_str(g_assetRoot, sizeof(g_assetRoot), "%s%s", dir, candidates[i]);
            found = true;
        }
    }
    for (int i = 0; i < ARRAY_COUNT(candidates) && !found; i++) {
        fmt_str(probe, sizeof(probe), "%s/config/game.cfg", candidates[i]);
        if (file_exists(probe)) {
            str_copy(g_assetRoot, sizeof(g_assetRoot), candidates[i]);
            found = true;
        }
    }
    if (!found) log_error("Could not locate the FIGHTO assets folder (looked next to %s)", exePath ? exePath : "?");

    // User data directory
    const char* env = getenv("FIGHTO_USERDIR");
    if (env && env[0]) {
        str_copy(g_userDir, sizeof(g_userDir), env);
    } else {
#if defined(_WIN32)
        const char* appdata = getenv("APPDATA");
        if (appdata && appdata[0]) fmt_str(g_userDir, sizeof(g_userDir), "%s\\FIGHTO", appdata);
        else fmt_str(g_userDir, sizeof(g_userDir), "%suserdata", dir);
#else
        const char* home = getenv("HOME");
        if (home && home[0]) fmt_str(g_userDir, sizeof(g_userDir), "%s/.local/share/fighto", home);
        else str_copy(g_userDir, sizeof(g_userDir), "userdata");
#endif
    }
    // Create the directory chain
    char tmp[512];
    str_copy(tmp, sizeof(tmp), g_userDir);
    for (int i = 1; tmp[i]; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char c = tmp[i];
            tmp[i] = 0;
            fighto_mkdir(tmp);
            tmp[i] = c;
        }
    }
    fighto_mkdir(tmp);
    log_info("Assets: %s", g_assetRoot);
    log_info("User data: %s", g_userDir);
}

static bool read_whole_file(const char* path, Array<u8>* out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return false; }
    out->resize((int)size + 1);
    size_t got = size > 0 ? fread(out->data, 1, (size_t)size, f) : 0;
    fclose(f);
    if ((long)got != size) { out->clear(); return false; }
    out->data[size] = 0; // convenient terminator for text files
    out->count = (int)size;
    return true;
}

bool plat_read_asset(const char* path, Array<u8>* out) {
    char full[700];
    fmt_str(full, sizeof(full), "%s/%s", g_assetRoot, path);
    return read_whole_file(full, out);
}

static void user_path(char* out, int cap, const char* name) {
#if defined(_WIN32)
    fmt_str(out, cap, "%s\\%s", g_userDir, name);
#else
    fmt_str(out, cap, "%s/%s", g_userDir, name);
#endif
}

bool plat_write_user_file(const char* name, const void* data, size_t size) {
    char path[700], tmp[720];
    user_path(path, sizeof(path), name);
    fmt_str(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (!f) { log_error("Cannot open %s for writing", tmp); return false; }
    size_t w = size ? fwrite(data, 1, size, f) : 0;
    bool ok = (w == size) && fflush(f) == 0;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); log_error("Write failed for %s", tmp); return false; }
#if defined(_WIN32)
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(tmp);
        log_error("Rename failed for %s", path);
        return false;
    }
#else
    if (rename(tmp, path) != 0) { remove(tmp); log_error("Rename failed for %s", path); return false; }
#endif
    // Verify by reading back
    Array<u8> check = {};
    bool same = read_whole_file(path, &check) && check.count == (int)size && (size == 0 || memcmp(check.data, data, size) == 0);
    check.free_mem();
    if (!same) log_error("Verification failed for %s", path);
    return same;
}

bool plat_read_user_file(const char* name, Array<u8>* out) {
    char path[700];
    user_path(path, sizeof(path), name);
    return read_whole_file(path, out);
}

bool plat_user_file_exists(const char* name) {
    char path[700];
    user_path(path, sizeof(path), name);
    return file_exists(path);
}

bool plat_copy_user_file(const char* from, const char* to) {
    Array<u8> data = {};
    if (!plat_read_user_file(from, &data)) return false;
    bool ok = plat_write_user_file(to, data.data, (size_t)data.count);
    data.free_mem();
    return ok;
}

bool plat_is_mobile() { return false; }

void plat_log(const char* msg, bool error) {
    FILE* out = error ? stderr : stdout;
    fprintf(out, "%s%s\n", error ? "[FIGHTO ERROR] " : "[FIGHTO] ", msg);
    fflush(out);
    static FILE* logFile = 0;
    static bool tried = false;
    if (!tried) {
        tried = true;
        char path[700];
        user_path(path, sizeof(path), "fighto.log");
        logFile = fopen(path, "w");
    }
    if (logFile) { fprintf(logFile, "%s%s\n", error ? "ERROR: " : "", msg); fflush(logFile); }
}

void desktop_parse_args(int argc, char** argv, GameLaunchOptions* opt, bool* windowed) {
    memset(opt, 0, sizeof(*opt));
    opt->width = 1280;
    opt->height = 720;
    *windowed = false;
    str_copy(opt->autotestOut, sizeof(opt->autotestOut), "autotest_out");
    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "--autotest")) {
            opt->autotest = true;
            opt->unitScreenshot = true;
            *windowed = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') str_copy(opt->autotestOut, sizeof(opt->autotestOut), argv[++i]);
        } else if (str_eq(argv[i], "--windowed")) {
            *windowed = true;
        } else if (str_eq(argv[i], "--width") && i + 1 < argc) {
            opt->width = str_to_int(argv[++i]);
        } else if (str_eq(argv[i], "--height") && i + 1 < argc) {
            opt->height = str_to_int(argv[++i]);
        }
    }
}

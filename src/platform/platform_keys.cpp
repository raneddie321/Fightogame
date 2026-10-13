// FIGHTO - display names for keys (shared by every platform backend).
#include "platform.h"

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

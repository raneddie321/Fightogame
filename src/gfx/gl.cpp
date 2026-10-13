// FIGHTO - OpenGL loader implementation.
#include "gl.h"
#include "../platform/platform.h"

namespace fgl {
#define FIGHTO_GL_DEFINE(ret, name, args) PFN_##name name = 0;
FIGHTO_GL_FUNCS(FIGHTO_GL_DEFINE)
#undef FIGHTO_GL_DEFINE
}

static bool g_isES = false;

bool gl_load() {
    bool ok = true;
#define FIGHTO_GL_LOAD(ret, name, args) \
    fgl::name = (fgl::PFN_##name)plat_gl_proc(#name); \
    if (!fgl::name) { log_error("Missing OpenGL function %s", #name); ok = false; }
    FIGHTO_GL_FUNCS(FIGHTO_GL_LOAD)
#undef FIGHTO_GL_LOAD
    if (ok) {
        const char* ver = (const char*)glGetString(GL_VERSION);
        g_isES = ver && strstr(ver, "OpenGL ES") != 0;
        log_info("OpenGL: %s | %s | %s", ver ? ver : "?", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VENDOR));
    }
    return ok;
}

bool gl_is_es() { return g_isES; }

bool gl_has_extension(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) {
        const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e && str_eq(e, name)) return true;
    }
    return false;
}

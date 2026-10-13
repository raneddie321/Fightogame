// FIGHTO 2D user interface shader (text, panels, icons, minimap).
#ifdef VERTEX
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uScreen;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    vec2 p = aPos / uScreen * 2.0 - 1.0;
    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT
in vec2 vUV;
in vec4 vColor;
uniform highp sampler2D uTex;
uniform float uIsFont;
layout(location = 0) out vec4 oColor;
void main() {
    vec4 t = texture(uTex, vUV);
    vec4 c;
    if (uIsFont > 0.5) c = vec4(vColor.rgb, vColor.a * t.r);
    else c = vColor * t;
    oColor = vec4(c.rgb * c.a, c.a);
}
#endif

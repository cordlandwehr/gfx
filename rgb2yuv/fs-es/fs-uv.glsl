#version 300 es

precision mediump float;
uniform sampler2D texMap;
out vec4 fragColor;

void main() {
    ivec2 tex_size = textureSize(texMap, 0);
    float dx = 1.0 / float(tex_size.x);
    vec2 tex_coord = gl_FragCoord.xy / vec2(tex_size);
    vec2 tex_pos = vec2(tex_coord.x * 4.0 - dx, (tex_coord.y - 1.0) * 2.0);

    vec3 conv_u = vec3(0.299, 0.587, 0.114);
    vec3 conv_v = vec3(0.615, -0.51499, -0.10001);
    vec4 color;

    vec3 rgb = texture(texMap, tex_pos).rgb;
    color.r = dot(conv_u, rgb);
    color.g = dot(conv_v, rgb);

    tex_pos.x += dx * 2.0;
    rgb = texture(texMap, tex_pos).rgb;
    color.b = dot(conv_u, rgb);
    color.a = dot(conv_v, rgb);

    fragColor = color;
}

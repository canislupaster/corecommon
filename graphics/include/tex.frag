R"(//#version 400 core
precision mediump float;

uniform sampler2D tex;

in vec2 fragtexpos;

layout(location=0) out vec4 outColor;

void main() {
    outColor = texture(tex, fragtexpos);
}
)"
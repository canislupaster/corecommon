R"(#version 400 core

uniform vec4 color;

layout(location=0) out vec4 outColor;

void main() {
    outColor += vec4(1,1,1,1)*color.a;
})"
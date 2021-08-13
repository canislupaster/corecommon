R"(#version 400 core

layout(location=0) in vec3 position;
layout(location=2) in vec2 texpos;

out vec2 fragtexpos;

void main() {
    gl_Position = vec4(position, 1.0);
    fragtexpos = texpos;
}
)"
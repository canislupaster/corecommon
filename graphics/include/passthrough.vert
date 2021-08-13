R"(#version 400 core

layout(location=0) in vec3 position;

out vec2 fragtexpos;

void main() {
    gl_Position = vec4(position, 1.0);
    fragtexpos = vec2(position.x/2 + 0.5, position.y/2 + 0.5);
}
)"
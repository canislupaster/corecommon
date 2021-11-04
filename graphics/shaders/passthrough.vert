#version 400 core

layout(location=0) in vec3 position;

uniform mat3 tex_transform;

out vec2 fragtexpos;

void main() {
    gl_Position = vec4(position, 1.0);
    fragtexpos = vec2(tex_transform*vec3(position.x, -position.y, 1))/2 + 0.5;
}

R"(//#version 400 core

layout(location=0) in vec3 position;

uniform mat2 tex_transform;

out vec2 fragtexpos;

void main() {
    gl_Position = vec4(position, 1.0);
    fragtexpos = tex_transform*vec2(position.x, position.y)/2.0 + 0.5;
}
)"
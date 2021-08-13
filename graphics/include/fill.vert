R"(#version 400 core

uniform object {
    mat4 space;
    mat4 cam;
};

uniform mat4 transform;

layout(location=0) in vec3 position;

void main() {
    gl_Position = space*cam*transform*vec4(position, 1.0);
}
)"
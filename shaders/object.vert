#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(push_constant) uniform Scene {
    mat4 mvp;
    mat4 model;
} scene;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 world_position;

void main()
{
    vec4 world = scene.model * vec4(in_position, 1.0);
    gl_Position = scene.mvp * vec4(in_position, 1.0);
    normal = mat3(scene.model) * in_normal;
    world_position = world.xyz;
}

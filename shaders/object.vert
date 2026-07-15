#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(push_constant) uniform Scene {
    mat4 mvp;
    mat3 model_rotation;
} scene;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 world_position;

void main()
{
    vec3 world = scene.model_rotation * in_position;
    gl_Position = scene.mvp * vec4(in_position, 1.0);
    normal = scene.model_rotation * in_normal;
    world_position = world;
}

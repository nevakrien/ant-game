#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(push_constant) uniform Scene {
    mat4 view_projection;
    layout(offset = 64) vec4 rotation;
    layout(offset = 80) vec4 position;
} scene;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 world_position;

void main()
{
    float rotation_length = length(scene.rotation);
    vec4 q = rotation_length > 0.0
        ? scene.rotation / rotation_length
        : vec4(0.0, 0.0, 0.0, 1.0);
    vec3 world = in_position + 2.0 * cross(q.xyz, cross(q.xyz, in_position) + q.w * in_position);
    world += scene.position.xyz;
    gl_Position = scene.view_projection * vec4(world, 1.0);
    normal = in_normal + 2.0 * cross(q.xyz, cross(q.xyz, in_normal) + q.w * in_normal);
    world_position = world;
}

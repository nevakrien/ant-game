#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

struct Transform {
    vec4 rotation;
    vec4 position;
};

layout(std430, set = 0, binding = 0) readonly buffer Transforms {
    Transform transforms[];
};

layout(std430, set = 0, binding = 3) readonly buffer Instances {
    uint transform_ids[];
};

layout(push_constant) uniform Scene {
    mat4 view_projection;
} scene;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 world_position;

void main()
{
    Transform transform = transforms[transform_ids[gl_InstanceIndex]];
    float rotation_length = length(transform.rotation);
    vec4 q = rotation_length > 0.0
        ? transform.rotation / rotation_length
        : vec4(0.0, 0.0, 0.0, 1.0);
    vec3 world = in_position + 2.0 * cross(q.xyz, cross(q.xyz, in_position) + q.w * in_position);
    world += transform.position.xyz;
    gl_Position = scene.view_projection * vec4(world, 1.0);
    normal = in_normal + 2.0 * cross(q.xyz, cross(q.xyz, in_normal) + q.w * in_normal);
    world_position = world;
}

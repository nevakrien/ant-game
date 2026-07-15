#version 450

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 world_position;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Lighting {
    layout(offset = 64) vec4 light_direction;
    layout(offset = 80) vec4 base_color;
    layout(offset = 96) vec4 rim_color;
} lighting;

void main()
{
    vec3 n = normalize(normal);
    vec3 light_direction = normalize(lighting.light_direction.xyz);
    float diffuse = max(dot(n, light_direction), 0.0);
    float rim = pow(1.0 - abs(dot(n, normalize(vec3(0.0, -6.0, 2.0) - world_position))), 3.0);
    vec3 color = lighting.base_color.rgb * (0.28 + 0.72 * diffuse)
        + lighting.rim_color.rgb * rim;
    out_color = vec4(color, 1.0);
}

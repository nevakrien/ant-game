#version 450

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 world_position;
layout(location = 0) out vec4 out_color;

void main()
{
    vec3 n = normalize(normal);
    vec3 light_direction = normalize(vec3(-0.5, -0.7, 1.0));
    float diffuse = max(dot(n, light_direction), 0.0);
    float rim = pow(1.0 - abs(dot(n, normalize(vec3(0.0, -6.0, 2.0) - world_position))), 3.0);
    vec3 copper = vec3(0.72, 0.25, 0.10);
    vec3 color = copper * (0.18 + 0.82 * diffuse) + vec3(0.35, 0.12, 0.05) * rim;
    out_color = vec4(color, 1.0);
}

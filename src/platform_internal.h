#ifndef ANT_GAME_PLATFORM_INTERNAL_H
#define ANT_GAME_PLATFORM_INTERNAL_H

#include "render.h"

#include <SDL.h>
#include <vulkan/vulkan.h>

#include <stddef.h>
#include <stdint.h>

typedef struct PlatformMesh {
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkBuffer index_buffer;
    VkDeviceMemory index_memory;
    uint32_t index_count;
} PlatformMesh;

typedef struct PlatformModel {
    Model model;
    uint32_t first_instance;
    uint32_t instance_count;
} PlatformModel;

typedef struct GpuTransform {
    float rotation[4];
    float position[4];
} GpuTransform;

typedef struct GpuTriangle {
    float vertices[3][4];
    uint32_t neighbors[4];
} GpuTriangle;

typedef struct GpuAnt {
    uint32_t data[4];
    float position_speed[4];
    float tangent[4];
} GpuAnt;

typedef struct TransformRecord {
    Transform transform;
    int dirty;
    int ant_owned;
    int ant_surface;
} TransformRecord;

typedef struct PlatformDrawable {
    ModelHandle model_handle;
    TransformHandle transform_handle;
} PlatformDrawable;

typedef struct AntSwarm {
    VkBuffer triangle_buffer;
    VkDeviceMemory triangle_memory;
    VkBuffer ant_buffer;
    VkDeviceMemory ant_memory;
    VkDescriptorSet descriptor_set;
    uint32_t triangle_count;
    uint32_t ant_count;
    TransformHandle surface_transform;
} AntSwarm;

typedef struct PushConstants {
    float view_projection[16];
    float light_direction[4];
    float base_color[4];
    float rim_color[4];
} PushConstants;

typedef struct AntUpdate {
    float delta_seconds;
    float surface_offset;
    uint32_t ant_count;
    uint32_t triangle_count;
    uint32_t surface_transform;
    uint32_t padding[3];
} AntUpdate;

enum {
    MAX_TRANSFORMS = 65536,
    MAX_ANT_SWARMS = 1024,
    ANT_WORKGROUP_SIZE = 64
};

struct Platform {
    SDL_Window *window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    uint32_t graphics_family;
    uint32_t present_family;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D extent;
    uint32_t image_count;
    VkImage *images;
    VkImageView *image_views;
    VkFramebuffer *framebuffers;
    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;
    PlatformMesh *meshes;
    size_t mesh_count;
    size_t mesh_capacity;
    PlatformModel *models;
    size_t model_count;
    size_t model_capacity;
    TransformRecord *transforms;
    size_t transform_count;
    size_t transform_capacity;
    PlatformDrawable *drawables;
    size_t drawable_count;
    size_t drawable_capacity;
    VkBuffer transform_buffer;
    VkDeviceMemory transform_memory;
    VkBuffer instance_buffer;
    VkDeviceMemory instance_memory;
    int instances_dirty;
    VkDescriptorSetLayout transform_descriptor_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet transform_descriptor_set;
    VkPipelineLayout compute_pipeline_layout;
    VkPipeline compute_pipeline;
    AntSwarm *swarms;
    size_t swarm_count;
    size_t swarm_capacity;
    float swarm_delta_seconds;
    int ants_need_dispatch;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence frame_fence;
    int framebuffer_resized;
};

int render_build_ant_buffers(Platform *platform, TransformHandle surface_transform,
                             const ObjectNavMesh *navmesh, const Ant *ants,
                             size_t ant_count, uint32_t triangle_count,
                             GpuTriangle **triangles, GpuAnt **gpu_ants);

#endif

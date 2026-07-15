#include "platform.h"
#include "platform_internal.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OBJECT_SHADER_DIR
#define OBJECT_SHADER_DIR "shaders"
#endif

enum {
    VIEW_PROJECTION_PUSH_OFFSET = offsetof(PushConstants, view_projection),
    VIEW_PROJECTION_PUSH_SIZE = sizeof(((PushConstants *)0)->view_projection),
    LIGHT_PUSH_OFFSET = offsetof(PushConstants, light_direction),
    LIGHT_PUSH_SIZE = sizeof(((PushConstants *)0)->light_direction),
    MODEL_PUSH_OFFSET = offsetof(PushConstants, base_color),
    MODEL_PUSH_SIZE = sizeof(((PushConstants *)0)->base_color) +
                      sizeof(((PushConstants *)0)->rim_color)
};

_Static_assert(sizeof(PushConstants) == 112, "push constants must fit Vulkan's guaranteed minimum");
_Static_assert(LIGHT_PUSH_OFFSET == 64, "light direction must match the shader offset");
_Static_assert(MODEL_PUSH_OFFSET == 80, "model colors must match the shader offset");
_Static_assert(sizeof(GpuTransform) == 32, "transform must match std430");
_Static_assert(sizeof(GpuTriangle) == 64, "triangle must match std430");
_Static_assert(sizeof(GpuAnt) == 48, "ant must match std430");

static int vk_error(VkResult result, const char *operation)
{
    if (result == VK_SUCCESS) return 0;
    fprintf(stderr, "%s failed (VkResult %d)\n", operation, result);
    return -1;
}

static int create_instance(Platform *app)
{
    unsigned extension_count = 0;
    const char **extensions = NULL;
    if (!SDL_Vulkan_GetInstanceExtensions(app->window, &extension_count, NULL)) {
        fprintf(stderr, "SDL Vulkan extensions: %s\n", SDL_GetError());
        return -1;
    }
    extensions = malloc(sizeof(*extensions) * extension_count);
    if (!extensions || !SDL_Vulkan_GetInstanceExtensions(app->window, &extension_count, extensions)) {
        free(extensions);
        fprintf(stderr, "Could not query Vulkan instance extensions\n");
        return -1;
    }
    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Vulkan Utah Teapot",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "ant-game",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0
    };
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions
    };
    VkResult result = vkCreateInstance(&create_info, NULL, &app->instance);
    free(extensions);
    return vk_error(result, "vkCreateInstance");
}

static int device_has_swapchain(VkPhysicalDevice device)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    VkExtensionProperties *properties = malloc(sizeof(*properties) * count);
    if (!properties) return 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, properties);
    int found = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (strcmp(properties[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) found = 1;
    free(properties);
    return found;
}

static int find_queue_families(Platform *app, VkPhysicalDevice device)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    VkQueueFamilyProperties *properties = malloc(sizeof(*properties) * count);
    if (!properties) return -1;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties);
    uint32_t graphics = UINT32_MAX, present = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, app->surface, &supported);
        if ((properties[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT) && graphics == UINT32_MAX) graphics = i;
        if (supported && present == UINT32_MAX) present = i;
    }
    free(properties);
    if (graphics == UINT32_MAX || present == UINT32_MAX) return -1;
    app->graphics_family = graphics;
    app->present_family = present;
    return 0;
}

static int create_device(Platform *app)
{
    uint32_t count = 0;
    if (vk_error(vkEnumeratePhysicalDevices(app->instance, &count, NULL), "enumerate GPUs") || !count)
        return -1;
    VkPhysicalDevice *devices = malloc(sizeof(*devices) * count);
    if (!devices) return -1;
    vkEnumeratePhysicalDevices(app->instance, &count, devices);
    for (uint32_t i = 0; i < count; ++i) {
        if (device_has_swapchain(devices[i]) && find_queue_families(app, devices[i]) == 0) {
            app->physical_device = devices[i];
            break;
        }
    }
    free(devices);
    if (app->physical_device == VK_NULL_HANDLE) {
        fprintf(stderr, "No Vulkan GPU with graphics, compute, and presentation support was found\n");
        return -1;
    }

    float priority = 1.0f;
    uint32_t families[2] = {app->graphics_family, app->present_family};
    uint32_t queue_count = app->graphics_family == app->present_family ? 1 : 2;
    VkDeviceQueueCreateInfo queues[2] = {0};
    for (uint32_t i = 0; i < queue_count; ++i) {
        queues[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queues[i].queueFamilyIndex = families[i];
        queues[i].queueCount = 1;
        queues[i].pQueuePriorities = &priority;
    }
    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queue_count,
        .pQueueCreateInfos = queues,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = extensions
    };
    if (vk_error(vkCreateDevice(app->physical_device, &create_info, NULL, &app->device), "vkCreateDevice")) return -1;
    vkGetDeviceQueue(app->device, app->graphics_family, 0, &app->graphics_queue);
    vkGetDeviceQueue(app->device, app->present_family, 0, &app->present_queue);
    return 0;
}

static uint32_t find_memory_type(Platform *app, uint32_t bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(app->physical_device, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    return UINT32_MAX;
}

static int create_buffer(Platform *app, VkDeviceSize size, VkBufferUsageFlags usage,
                         const void *data, VkBuffer *buffer, VkDeviceMemory *memory)
{
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (vk_error(vkCreateBuffer(app->device, &buffer_info, NULL, buffer), "vkCreateBuffer")) return -1;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(app->device, *buffer, &requirements);
    uint32_t memory_type = find_memory_type(app, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) return -1;
    VkMemoryAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type
    };
    if (vk_error(vkAllocateMemory(app->device, &allocate_info, NULL, memory), "vkAllocateMemory")) return -1;
    if (vk_error(vkBindBufferMemory(app->device, *buffer, *memory, 0), "vkBindBufferMemory")) return -1;
    void *mapped = NULL;
    if (vk_error(vkMapMemory(app->device, *memory, 0, size, 0, &mapped), "vkMapMemory")) return -1;
    if (data) memcpy(mapped, data, (size_t)size);
    else memset(mapped, 0, (size_t)size);
    vkUnmapMemory(app->device, *memory);
    return 0;
}

static void destroy_mesh(Platform *app, PlatformMesh *mesh)
{
    vkDestroyBuffer(app->device, mesh->index_buffer, NULL);
    vkFreeMemory(app->device, mesh->index_memory, NULL);
    vkDestroyBuffer(app->device, mesh->vertex_buffer, NULL);
    vkFreeMemory(app->device, mesh->vertex_memory, NULL);
    memset(mesh, 0, sizeof(*mesh));
}

static int create_mesh(Platform *app, const ObjectMesh *source, PlatformMesh *mesh)
{
    memset(mesh, 0, sizeof(*mesh));
    if (!source || !source->vertices || !source->indices ||
        !source->vertex_count || !source->index_count) return -1;
    int result = create_buffer(app, (VkDeviceSize)source->vertex_count * sizeof(*source->vertices),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, source->vertices, &mesh->vertex_buffer, &mesh->vertex_memory);
    if (!result)
        result = create_buffer(app, (VkDeviceSize)source->index_count * sizeof(*source->indices),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, source->indices, &mesh->index_buffer, &mesh->index_memory);
    if (result) destroy_mesh(app, mesh);
    else mesh->index_count = source->index_count;
    return result;
}

int render_add_mesh(Platform *app, const ObjectMesh *source, MeshHandle *mesh_handle)
{
    if (!app || !mesh_handle || app->mesh_count >= UINT32_MAX) return -1;
    PlatformMesh mesh;
    if (create_mesh(app, source, &mesh)) return -1;
    if (app->mesh_count == app->mesh_capacity) {
        size_t capacity = app->mesh_capacity ? app->mesh_capacity * 2 : 4;
        PlatformMesh *meshes = realloc(app->meshes, capacity * sizeof(*meshes));
        if (!meshes) {
            destroy_mesh(app, &mesh);
            return -1;
        }
        app->meshes = meshes;
        app->mesh_capacity = capacity;
    }
    *mesh_handle = (MeshHandle)app->mesh_count;
    app->meshes[app->mesh_count++] = mesh;
    return 0;
}

int render_update_mesh(Platform *app, MeshHandle mesh_handle, const ObjectMesh *source)
{
    if (!app || mesh_handle >= app->mesh_count) return -1;
    PlatformMesh mesh;
    if (create_mesh(app, source, &mesh)) return -1;
    vkDeviceWaitIdle(app->device);
    destroy_mesh(app, &app->meshes[mesh_handle]);
    app->meshes[mesh_handle] = mesh;
    return 0;
}

int render_add_antable(Platform *app, TransformHandle surface_transform,
                         const ObjectNavMesh *navmesh, const Ant *ants,
                         size_t ant_count)
{
    if (!app || surface_transform >= app->transform_count ||
        app->transforms[surface_transform].ant_owned || !navmesh || !ants ||
        !ant_count ||
        !navmesh->mesh.vertices || !navmesh->mesh.indices || !navmesh->neighbors ||
        navmesh->mesh.index_count % 3 || ant_count > UINT32_MAX ||
        app->swarm_count >= MAX_ANT_SWARMS) return -1;
    uint32_t triangle_count = navmesh->mesh.index_count / 3;
    if (!triangle_count) return -1;
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(app->physical_device, &properties);
    if ((VkDeviceSize)triangle_count * sizeof(GpuTriangle) > properties.limits.maxStorageBufferRange ||
        (VkDeviceSize)ant_count * sizeof(GpuAnt) > properties.limits.maxStorageBufferRange ||
        (ant_count + ANT_WORKGROUP_SIZE - 1) / ANT_WORKGROUP_SIZE >
            properties.limits.maxComputeWorkGroupCount[0]) return -1;
    if (app->swarm_count == app->swarm_capacity) {
        size_t capacity = app->swarm_capacity ? app->swarm_capacity * 2 : 4;
        AntSwarm *swarms = realloc(app->swarms, capacity * sizeof(*swarms));
        if (!swarms) return -1;
        app->swarms = swarms;
        app->swarm_capacity = capacity;
    }

    GpuTriangle *triangles = NULL;
    GpuAnt *gpu_ants = NULL;
    if (render_build_ant_buffers(app, surface_transform, navmesh, ants, ant_count,
                                 triangle_count, &triangles, &gpu_ants)) return -1;

    AntSwarm swarm = {0};
    int result = create_buffer(app, (VkDeviceSize)triangle_count * sizeof(*triangles),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, triangles,
        &swarm.triangle_buffer, &swarm.triangle_memory);
    if (!result)
        result = create_buffer(app, (VkDeviceSize)ant_count * sizeof(*gpu_ants),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, gpu_ants,
            &swarm.ant_buffer, &swarm.ant_memory);
    free(triangles);
    free(gpu_ants);
    if (result) {
        vkDestroyBuffer(app->device, swarm.ant_buffer, NULL);
        vkFreeMemory(app->device, swarm.ant_memory, NULL);
        vkDestroyBuffer(app->device, swarm.triangle_buffer, NULL);
        vkFreeMemory(app->device, swarm.triangle_memory, NULL);
        return -1;
    }

    VkDescriptorSetAllocateInfo set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = app->descriptor_pool, .descriptorSetCount = 1,
        .pSetLayouts = &app->transform_descriptor_layout
    };
    if (vk_error(vkAllocateDescriptorSets(app->device, &set_info, &swarm.descriptor_set),
        "ant swarm descriptor set")) {
        vkDestroyBuffer(app->device, swarm.ant_buffer, NULL);
        vkFreeMemory(app->device, swarm.ant_memory, NULL);
        vkDestroyBuffer(app->device, swarm.triangle_buffer, NULL);
        vkFreeMemory(app->device, swarm.triangle_memory, NULL);
        return -1;
    }
    VkDescriptorBufferInfo buffer_infos[3] = {
        {.buffer = app->transform_buffer, .range = VK_WHOLE_SIZE},
        {.buffer = swarm.triangle_buffer, .range = VK_WHOLE_SIZE},
        {.buffer = swarm.ant_buffer, .range = VK_WHOLE_SIZE}
    };
    VkWriteDescriptorSet writes[3] = {0};
    for (uint32_t binding = 0; binding < 3; ++binding) {
        writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[binding].dstSet = swarm.descriptor_set;
        writes[binding].dstBinding = binding;
        writes[binding].descriptorCount = 1;
        writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[binding].pBufferInfo = &buffer_infos[binding];
    }
    vkUpdateDescriptorSets(app->device, 3, writes, 0, NULL);
    swarm.triangle_count = triangle_count;
    swarm.ant_count = (uint32_t)ant_count;
    swarm.surface_transform = surface_transform;

    app->swarms[app->swarm_count++] = swarm;
    app->transforms[surface_transform].ant_surface = 1;
    for (size_t i = 0; i < ant_count; ++i)
        app->transforms[ants[i].transform_handle].ant_owned = 1;
    app->ants_need_dispatch = 1;
    return 0;
}

static VkSurfaceFormatKHR choose_surface_format(const VkSurfaceFormatKHR *formats, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return formats[i];
    return formats[0];
}

static VkPresentModeKHR choose_present_mode(const VkPresentModeKHR *modes, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) return modes[i];
    return VK_PRESENT_MODE_FIFO_KHR;
}

static int create_swapchain(Platform *app)
{
    VkSurfaceCapabilitiesKHR capabilities;
    uint32_t format_count = 0, mode_count = 0;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physical_device, app->surface, &capabilities);
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical_device, app->surface, &format_count, NULL);
    vkGetPhysicalDeviceSurfacePresentModesKHR(app->physical_device, app->surface, &mode_count, NULL);
    if (!format_count || !mode_count) return -1;
    VkSurfaceFormatKHR *formats = malloc(sizeof(*formats) * format_count);
    VkPresentModeKHR *modes = malloc(sizeof(*modes) * mode_count);
    if (!formats || !modes) { free(formats); free(modes); return -1; }
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical_device, app->surface, &format_count, formats);
    vkGetPhysicalDeviceSurfacePresentModesKHR(app->physical_device, app->surface, &mode_count, modes);
    VkSurfaceFormatKHR format = choose_surface_format(formats, format_count);
    VkPresentModeKHR present_mode = choose_present_mode(modes, mode_count);
    free(formats); free(modes);

    if (capabilities.currentExtent.width != UINT32_MAX) app->extent = capabilities.currentExtent;
    else {
        int width, height;
        SDL_Vulkan_GetDrawableSize(app->window, &width, &height);
        app->extent.width = (uint32_t)width;
        app->extent.height = (uint32_t)height;
        if (app->extent.width < capabilities.minImageExtent.width) app->extent.width = capabilities.minImageExtent.width;
        if (app->extent.width > capabilities.maxImageExtent.width) app->extent.width = capabilities.maxImageExtent.width;
        if (app->extent.height < capabilities.minImageExtent.height) app->extent.height = capabilities.minImageExtent.height;
        if (app->extent.height > capabilities.maxImageExtent.height) app->extent.height = capabilities.maxImageExtent.height;
    }
    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && image_count > capabilities.maxImageCount) image_count = capabilities.maxImageCount;
    uint32_t families[] = {app->graphics_family, app->present_family};
    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = app->surface,
        .minImageCount = image_count,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = app->extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = app->graphics_family == app->present_family ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = app->graphics_family == app->present_family ? 0 : 2,
        .pQueueFamilyIndices = families,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE
    };
    if (vk_error(vkCreateSwapchainKHR(app->device, &create_info, NULL, &app->swapchain), "vkCreateSwapchainKHR")) return -1;
    app->swapchain_format = format.format;
    vkGetSwapchainImagesKHR(app->device, app->swapchain, &app->image_count, NULL);
    app->images = calloc(app->image_count, sizeof(*app->images));
    app->image_views = calloc(app->image_count, sizeof(*app->image_views));
    app->framebuffers = calloc(app->image_count, sizeof(*app->framebuffers));
    if (!app->images || !app->image_views || !app->framebuffers) return -1;
    vkGetSwapchainImagesKHR(app->device, app->swapchain, &app->image_count, app->images);
    for (uint32_t i = 0; i < app->image_count; ++i) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = app->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = app->swapchain_format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
        };
        if (vk_error(vkCreateImageView(app->device, &view_info, NULL, &app->image_views[i]), "color image view")) return -1;
    }
    return 0;
}

static int create_depth_resources(Platform *app)
{
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = {app->extent.width, app->extent.height, 1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    if (vk_error(vkCreateImage(app->device, &image_info, NULL, &app->depth_image), "depth image")) return -1;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(app->device, app->depth_image, &requirements);
    uint32_t type = find_memory_type(app, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) return -1;
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = type};
    if (vk_error(vkAllocateMemory(app->device, &allocation, NULL, &app->depth_memory), "depth memory")) return -1;
    if (vk_error(vkBindImageMemory(app->device, app->depth_image, app->depth_memory, 0), "bind depth memory")) return -1;
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = app->depth_image, .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1}
    };
    return vk_error(vkCreateImageView(app->device, &view_info, NULL, &app->depth_view), "depth view");
}

static int create_render_pass(Platform *app)
{
    VkAttachmentDescription attachments[2] = {
        {.format = app->swapchain_format, .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
        {.format = VK_FORMAT_D32_SFLOAT, .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
    };
    VkAttachmentReference color = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &color, .pDepthStencilAttachment = &depth};
    VkSubpassDependency dependency = {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2, .pAttachments = attachments, .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = 1, .pDependencies = &dependency};
    return vk_error(vkCreateRenderPass(app->device, &info, NULL, &app->render_pass), "vkCreateRenderPass");
}

static int read_shader(const char *name, uint32_t **code, size_t *size)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.spv", OBJECT_SHADER_DIR, name);
    FILE *file = fopen(path, "rb");
    if (!file) { fprintf(stderr, "Could not open shader %s\n", path); return -1; }
    if (fseek(file, 0, SEEK_END) || (*size = (size_t)ftell(file)) == 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file); return -1;
    }
    *code = malloc(*size);
    if (!*code || fread(*code, 1, *size, file) != *size) { free(*code); *code = NULL; fclose(file); return -1; }
    fclose(file);
    return 0;
}

static int create_transform_and_compute_resources(Platform *app)
{
    VkDescriptorSetLayoutBinding bindings[4] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT}
    };
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 4, .pBindings = bindings
    };
    if (vk_error(vkCreateDescriptorSetLayout(app->device, &layout_info, NULL,
        &app->transform_descriptor_layout), "transform descriptor layout")) return -1;

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 4 + MAX_ANT_SWARMS * 4
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1 + MAX_ANT_SWARMS,
        .poolSizeCount = 1, .pPoolSizes = &pool_size
    };
    if (vk_error(vkCreateDescriptorPool(app->device, &pool_info, NULL,
        &app->descriptor_pool), "transform descriptor pool")) return -1;

    VkDeviceSize transform_buffer_size =
        (VkDeviceSize)MAX_TRANSFORMS * sizeof(GpuTransform);
    if (create_buffer(app, transform_buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        NULL, &app->transform_buffer, &app->transform_memory)) return -1;
    VkDeviceSize instance_buffer_size = (VkDeviceSize)MAX_TRANSFORMS * sizeof(uint32_t);
    if (create_buffer(app, instance_buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        NULL, &app->instance_buffer, &app->instance_memory)) return -1;
    VkDescriptorSetAllocateInfo set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = app->descriptor_pool, .descriptorSetCount = 1,
        .pSetLayouts = &app->transform_descriptor_layout
    };
    if (vk_error(vkAllocateDescriptorSets(app->device, &set_info,
        &app->transform_descriptor_set), "transform descriptor set")) return -1;
    VkDescriptorBufferInfo buffer_infos[2] = {
        {.buffer = app->transform_buffer, .offset = 0, .range = transform_buffer_size},
        {.buffer = app->instance_buffer, .offset = 0, .range = instance_buffer_size}
    };
    VkWriteDescriptorSet writes[2] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = app->transform_descriptor_set, .dstBinding = 0,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &buffer_infos[0]},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = app->transform_descriptor_set, .dstBinding = 3,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &buffer_infos[1]}
    };
    vkUpdateDescriptorSets(app->device, 2, writes, 0, NULL);

    VkPushConstantRange compute_push = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 32
    };
    VkPipelineLayoutCreateInfo compute_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &app->transform_descriptor_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &compute_push
    };
    if (vk_error(vkCreatePipelineLayout(app->device, &compute_layout_info, NULL,
        &app->compute_pipeline_layout), "ant compute pipeline layout")) return -1;

    uint32_t *code = NULL;
    size_t size = 0;
    if (read_shader("ant.comp", &code, &size)) return -1;
    VkShaderModuleCreateInfo module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size, .pCode = code
    };
    VkShaderModule module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(app->device, &module_info, NULL, &module);
    free(code);
    if (vk_error(result, "ant compute shader module")) return -1;
    VkPipelineShaderStageCreateInfo stage = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main"
    };
    VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stage, .layout = app->compute_pipeline_layout
    };
    result = vkCreateComputePipelines(app->device, VK_NULL_HANDLE, 1,
        &pipeline_info, NULL, &app->compute_pipeline);
    vkDestroyShaderModule(app->device, module, NULL);
    return vk_error(result, "ant compute pipeline");
}

static int create_pipeline(Platform *app)
{
    uint32_t *vertex_code = NULL, *fragment_code = NULL;
    size_t vertex_size = 0, fragment_size = 0;
    if (read_shader("object.vert", &vertex_code, &vertex_size) || read_shader("object.frag", &fragment_code, &fragment_size)) {
        free(vertex_code); free(fragment_code); return -1;
    }
    VkShaderModuleCreateInfo module_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkShaderModule vertex_module = VK_NULL_HANDLE, fragment_module = VK_NULL_HANDLE;
    module_info.codeSize = vertex_size; module_info.pCode = vertex_code;
    VkResult result = vkCreateShaderModule(app->device, &module_info, NULL, &vertex_module);
    module_info.codeSize = fragment_size; module_info.pCode = fragment_code;
    if (result == VK_SUCCESS) result = vkCreateShaderModule(app->device, &module_info, NULL, &fragment_module);
    free(vertex_code); free(fragment_code);
    if (vk_error(result, "shader module")) return -1;

    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vertex_module, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragment_module, .pName = "main"}
    };
    VkVertexInputBindingDescription binding = {.binding = 0, .stride = sizeof(ObjectVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(ObjectVertex, position)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(ObjectVertex, normal)}
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo multisample = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo depth = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_attachment};
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic_states};

    VkPushConstantRange push_ranges[2] = {
        {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
         .offset = VIEW_PROJECTION_PUSH_OFFSET,
         .size = VIEW_PROJECTION_PUSH_SIZE},
        {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
         .offset = LIGHT_PUSH_OFFSET, .size = LIGHT_PUSH_SIZE + MODEL_PUSH_SIZE}
    };
    VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &app->transform_descriptor_layout,
        .pushConstantRangeCount = 2, .pPushConstantRanges = push_ranges};
    result = vkCreatePipelineLayout(app->device, &layout_info, NULL, &app->pipeline_layout);
    if (result == VK_SUCCESS) {
        VkGraphicsPipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &assembly, .pViewportState = &viewport, .pRasterizationState = &raster,
            .pMultisampleState = &multisample, .pDepthStencilState = &depth, .pColorBlendState = &blend,
            .pDynamicState = &dynamic, .layout = app->pipeline_layout, .renderPass = app->render_pass, .subpass = 0};
        result = vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &app->pipeline);
    }
    vkDestroyShaderModule(app->device, vertex_module, NULL);
    vkDestroyShaderModule(app->device, fragment_module, NULL);
    return vk_error(result, "graphics pipeline");
}

static int create_framebuffers(Platform *app)
{
    for (uint32_t i = 0; i < app->image_count; ++i) {
        VkImageView attachments[] = {app->image_views[i], app->depth_view};
        VkFramebufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = app->render_pass, .attachmentCount = 2, .pAttachments = attachments,
            .width = app->extent.width, .height = app->extent.height, .layers = 1};
        if (vk_error(vkCreateFramebuffer(app->device, &info, NULL, &app->framebuffers[i]), "framebuffer")) return -1;
    }
    return 0;
}

static void destroy_swapchain(Platform *app)
{
    if (!app->device) return;
    for (uint32_t i = 0; i < app->image_count; ++i) {
        if (app->framebuffers) vkDestroyFramebuffer(app->device, app->framebuffers[i], NULL);
        if (app->image_views) vkDestroyImageView(app->device, app->image_views[i], NULL);
    }
    free(app->framebuffers); free(app->image_views); free(app->images);
    app->framebuffers = NULL; app->image_views = NULL; app->images = NULL; app->image_count = 0;
    vkDestroyPipeline(app->device, app->pipeline, NULL); app->pipeline = VK_NULL_HANDLE;
    vkDestroyPipelineLayout(app->device, app->pipeline_layout, NULL); app->pipeline_layout = VK_NULL_HANDLE;
    vkDestroyRenderPass(app->device, app->render_pass, NULL); app->render_pass = VK_NULL_HANDLE;
    vkDestroyImageView(app->device, app->depth_view, NULL); app->depth_view = VK_NULL_HANDLE;
    vkDestroyImage(app->device, app->depth_image, NULL); app->depth_image = VK_NULL_HANDLE;
    vkFreeMemory(app->device, app->depth_memory, NULL); app->depth_memory = VK_NULL_HANDLE;
    vkDestroySwapchainKHR(app->device, app->swapchain, NULL); app->swapchain = VK_NULL_HANDLE;
}

static int build_swapchain(Platform *app)
{
    if (create_swapchain(app) || create_depth_resources(app) || create_render_pass(app) ||
        create_pipeline(app) || create_framebuffers(app)) return -1;
    app->framebuffer_resized = 0;
    return 0;
}

static int recreate_swapchain(Platform *app)
{
    int width, height;
    SDL_Vulkan_GetDrawableSize(app->window, &width, &height);
    if (width == 0 || height == 0) return 0;
    vkDeviceWaitIdle(app->device);
    destroy_swapchain(app);
    return build_swapchain(app);
}

static int create_commands_and_sync(Platform *app)
{
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = app->graphics_family};
    if (vk_error(vkCreateCommandPool(app->device, &pool_info, NULL, &app->command_pool), "command pool")) return -1;
    VkCommandBufferAllocateInfo command_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = app->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    if (vk_error(vkAllocateCommandBuffers(app->device, &command_info, &app->command_buffer), "command buffer")) return -1;
    VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    if (vk_error(vkCreateSemaphore(app->device, &semaphore_info, NULL, &app->image_available), "semaphore") ||
        vk_error(vkCreateSemaphore(app->device, &semaphore_info, NULL, &app->render_finished), "semaphore") ||
        vk_error(vkCreateFence(app->device, &fence_info, NULL, &app->frame_fence), "fence")) return -1;
    return 0;
}

static int upload_dirty_transforms(Platform *app)
{
    if (!app->transform_count) return 0;
    int has_dirty = 0;
    for (size_t i = 0; i < app->transform_count; ++i) has_dirty |= app->transforms[i].dirty;
    if (!has_dirty) return 0;

    GpuTransform *gpu_transforms = NULL;
    VkDeviceSize size = (VkDeviceSize)app->transform_count * sizeof(*gpu_transforms);
    if (vk_error(vkMapMemory(app->device, app->transform_memory, 0, size, 0,
        (void **)&gpu_transforms), "map transforms")) return -1;
    for (size_t i = 0; i < app->transform_count; ++i) {
        if (!app->transforms[i].dirty) continue;
        const Transform *transform = &app->transforms[i].transform;
        memcpy(gpu_transforms[i].rotation, transform->rotation, sizeof(gpu_transforms[i].rotation));
        memcpy(gpu_transforms[i].position, transform->position, sizeof(transform->position));
        gpu_transforms[i].position[3] = 1.0f;
        app->transforms[i].dirty = 0;
    }
    vkUnmapMemory(app->device, app->transform_memory);
    return 0;
}

static int upload_model_instances(Platform *app)
{
    if (!app->instances_dirty) return 0;
    uint32_t *instances = NULL;
    VkDeviceSize size = (VkDeviceSize)app->drawable_count * sizeof(*instances);
    if (size && vk_error(vkMapMemory(app->device, app->instance_memory, 0, size, 0,
        (void **)&instances), "map model instances")) return -1;

    uint32_t next_instance = 0;
    for (size_t model_index = 0; model_index < app->model_count; ++model_index) {
        PlatformModel *model = &app->models[model_index];
        model->first_instance = next_instance;
        for (size_t drawable_index = 0; drawable_index < app->drawable_count; ++drawable_index) {
            const PlatformDrawable *drawable = &app->drawables[drawable_index];
            if (drawable->model_handle == model_index)
                instances[next_instance++] = drawable->transform_handle;
        }
        model->instance_count = next_instance - model->first_instance;
    }
    if (size) vkUnmapMemory(app->device, app->instance_memory);
    app->instances_dirty = 0;
    return 0;
}

static int record_commands(Platform *app, uint32_t image_index, const Scene *scene)
{
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vk_error(vkBeginCommandBuffer(app->command_buffer, &begin), "begin command buffer")) return -1;
    if ((app->swarm_delta_seconds > 0.0f || app->ants_need_dispatch) && app->swarm_count) {
        vkCmdBindPipeline(app->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          app->compute_pipeline);
        for (size_t i = 0; i < app->swarm_count; ++i) {
            const AntSwarm *swarm = &app->swarms[i];
            AntUpdate update = {
                .delta_seconds = app->swarm_delta_seconds,
                .surface_offset = 0.0f,
                .ant_count = swarm->ant_count,
                .triangle_count = swarm->triangle_count,
                .surface_transform = swarm->surface_transform
            };
            vkCmdBindDescriptorSets(app->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                app->compute_pipeline_layout, 0, 1, &swarm->descriptor_set, 0, NULL);
            vkCmdPushConstants(app->command_buffer, app->compute_pipeline_layout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(update), &update);
            vkCmdDispatch(app->command_buffer,
                (swarm->ant_count + ANT_WORKGROUP_SIZE - 1) / ANT_WORKGROUP_SIZE, 1, 1);
        }
        VkMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
        };
        vkCmdPipelineBarrier(app->command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0, 1, &barrier, 0, NULL, 0, NULL);
    }
    VkClearValue clears[2] = {
        {.color = {{0.025f, 0.035f, 0.055f, 1.0f}}},
        {.depthStencil = {1.0f, 0}}
    };
    VkRenderPassBeginInfo render = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = app->render_pass, .framebuffer = app->framebuffers[image_index],
        .renderArea = {.extent = app->extent}, .clearValueCount = 2, .pClearValues = clears};
    vkCmdBeginRenderPass(app->command_buffer, &render, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(app->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipeline);
    vkCmdBindDescriptorSets(app->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        app->pipeline_layout, 0, 1, &app->transform_descriptor_set, 0, NULL);
    VkViewport viewport = {0, 0, (float)app->extent.width, (float)app->extent.height, 0, 1};
    VkRect2D scissor = {.extent = app->extent};
    vkCmdSetViewport(app->command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(app->command_buffer, 0, 1, &scissor);
    vkCmdPushConstants(app->command_buffer, app->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
        VIEW_PROJECTION_PUSH_OFFSET, VIEW_PROJECTION_PUSH_SIZE, scene->view_projection);
    vkCmdPushConstants(app->command_buffer, app->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
        LIGHT_PUSH_OFFSET, LIGHT_PUSH_SIZE, scene->light_direction);
    for (size_t i = 0; i < app->model_count; ++i) {
        const PlatformModel *model = &app->models[i];
        if (!model->instance_count) continue;
        const PlatformMesh *mesh = &app->meshes[model->model.mesh_handle];
        float colors[8] = {
            model->model.base_color[0], model->model.base_color[1], model->model.base_color[2], 1.0f,
            model->model.rim_color[0], model->model.rim_color[1], model->model.rim_color[2], 1.0f
        };
        vkCmdPushConstants(app->command_buffer, app->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
            MODEL_PUSH_OFFSET, MODEL_PUSH_SIZE, colors);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(app->command_buffer, 0, 1, &mesh->vertex_buffer, &offset);
        vkCmdBindIndexBuffer(app->command_buffer, mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(app->command_buffer, mesh->index_count, model->instance_count,
                         0, 0, model->first_instance);
    }
    vkCmdEndRenderPass(app->command_buffer);
    return vk_error(vkEndCommandBuffer(app->command_buffer), "end command buffer");
}

int render_draw(Platform *app, const Scene *scene)
{
    if (!app || !scene) return -1;
    if (app->framebuffer_resized && recreate_swapchain(app)) return -1;
    int width, height;
    SDL_Vulkan_GetDrawableSize(app->window, &width, &height);
    if (width == 0 || height == 0) { SDL_Delay(20); return 0; }
    vkWaitForFences(app->device, 1, &app->frame_fence, VK_TRUE, UINT64_MAX);
    if (upload_dirty_transforms(app) || upload_model_instances(app)) return -1;
    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(app->device, app->swapchain, UINT64_MAX,
        app->image_available, VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return recreate_swapchain(app);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return vk_error(result, "acquire swapchain image");
    vkResetFences(app->device, 1, &app->frame_fence);
    vkResetCommandBuffer(app->command_buffer, 0);
    if (record_commands(app, image_index, scene)) return -1;
    app->swarm_delta_seconds = 0.0f;
    app->ants_need_dispatch = 0;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &app->image_available, .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1, .pCommandBuffers = &app->command_buffer,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &app->render_finished};
    if (vk_error(vkQueueSubmit(app->graphics_queue, 1, &submit, app->frame_fence), "vkQueueSubmit")) return -1;
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &app->render_finished,
        .swapchainCount = 1, .pSwapchains = &app->swapchain, .pImageIndices = &image_index};
    result = vkQueuePresentKHR(app->present_queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || app->framebuffer_resized)
        return recreate_swapchain(app);
    return vk_error(result, "vkQueuePresentKHR");
}

static void cleanup(Platform *app)
{
    if (app->device) vkDeviceWaitIdle(app->device);
    destroy_swapchain(app);
    if (app->device) {
        vkDestroyPipeline(app->device, app->compute_pipeline, NULL);
        vkDestroyPipelineLayout(app->device, app->compute_pipeline_layout, NULL);
        for (size_t i = 0; i < app->swarm_count; ++i) {
            vkDestroyBuffer(app->device, app->swarms[i].ant_buffer, NULL);
            vkFreeMemory(app->device, app->swarms[i].ant_memory, NULL);
            vkDestroyBuffer(app->device, app->swarms[i].triangle_buffer, NULL);
            vkFreeMemory(app->device, app->swarms[i].triangle_memory, NULL);
        }
        free(app->swarms);
        vkDestroyDescriptorPool(app->device, app->descriptor_pool, NULL);
        vkDestroyDescriptorSetLayout(app->device, app->transform_descriptor_layout, NULL);
        vkDestroyBuffer(app->device, app->instance_buffer, NULL);
        vkFreeMemory(app->device, app->instance_memory, NULL);
        vkDestroyBuffer(app->device, app->transform_buffer, NULL);
        vkFreeMemory(app->device, app->transform_memory, NULL);
        vkDestroyFence(app->device, app->frame_fence, NULL);
        vkDestroySemaphore(app->device, app->render_finished, NULL);
        vkDestroySemaphore(app->device, app->image_available, NULL);
        vkDestroyCommandPool(app->device, app->command_pool, NULL);
        for (size_t i = 0; i < app->mesh_count; ++i) destroy_mesh(app, &app->meshes[i]);
        free(app->meshes);
        free(app->models);
        free(app->drawables);
        free(app->transforms);
        vkDestroyDevice(app->device, NULL);
    }
    if (app->surface) vkDestroySurfaceKHR(app->instance, app->surface, NULL);
    if (app->instance) vkDestroyInstance(app->instance, NULL);
    if (app->window) SDL_DestroyWindow(app->window);
    SDL_Quit();
}

Platform *platform_create(const char *title, int width, int height)
{
    Platform *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        free(app);
        return NULL;
    }
    app->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!app->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        platform_destroy(app);
        return NULL;
    }
    if (create_instance(app) || !SDL_Vulkan_CreateSurface(app->window, app->instance, &app->surface) ||
        create_device(app) || create_commands_and_sync(app) ||
        create_transform_and_compute_resources(app) || build_swapchain(app)) {
        if (!app->surface) fprintf(stderr, "SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
        platform_destroy(app);
        return NULL;
    }
    return app;
}

void platform_destroy(Platform *app)
{
    if (!app) return;
    cleanup(app);
    free(app);
}

void platform_poll_input(Platform *app, Input *input)
{
    memset(input, 0, sizeof(*input));
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
            input->quit_requested = 1;
        if (event.type == SDL_WINDOWEVENT &&
            (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
             event.window.event == SDL_WINDOWEVENT_RESIZED))
            app->framebuffer_resized = 1;
    }

    int mouse_x, mouse_y, window_width, window_height;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    SDL_GetWindowSize(app->window, &window_width, &window_height);
    input->pointer_x = window_width > 1 ? 2.0f * mouse_x / (window_width - 1) - 1.0f : 0.0f;
    input->pointer_y = window_height > 1 ? 1.0f - 2.0f * mouse_y / (window_height - 1) : 0.0f;
    if (input->pointer_x < -1.0f) input->pointer_x = -1.0f;
    if (input->pointer_x > 1.0f) input->pointer_x = 1.0f;
    if (input->pointer_y < -1.0f) input->pointer_y = -1.0f;
    if (input->pointer_y > 1.0f) input->pointer_y = 1.0f;
}

float platform_aspect_ratio(const Platform *app)
{
    return app->extent.height ? (float)app->extent.width / (float)app->extent.height : 1.0f;
}

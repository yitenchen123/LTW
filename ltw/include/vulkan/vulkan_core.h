#ifndef VULKAN_CORE_H_
#define VULKAN_CORE_H_

/*
 * Minimal Vulkan header stub for LTW non-Android builds.
 *
 * Only nir_print.c includes "vulkan/vulkan_core.h", and it uses nothing but
 * the VkDescriptorType enum (plus a handful of its values). On Android the
 * full Vulkan headers come from the NDK sysroot; this stub provides just
 * enough for nir_print.c to compile on iOS/macOS/Linux.
 */
#include <stdint.h>

typedef enum VkDescriptorType {
    VK_DESCRIPTOR_TYPE_SAMPLER = 0,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = 1,
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE = 2,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE = 3,
    VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER = 4,
    VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER = 5,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER = 6,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER = 7,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC = 8,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC = 9,
    VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT = 10,
    VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK = 1000380000,
    VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR = 1000150000,
    VK_DESCRIPTOR_TYPE_MAX_ENUM = 0x7FFFFFFF
} VkDescriptorType;

#endif /* VULKAN_CORE_H_ */

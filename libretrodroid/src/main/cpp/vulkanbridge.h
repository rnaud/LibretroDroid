#pragma once
#define VK_NO_PROTOTYPES
#include "libretro/libretro-common/include/libretro_vulkan.h"
#include <mutex>
#include <vector>

// Experimental synchronous Vulkan readback into the existing GLES presenter.
// One completed frame at a time deliberately prioritizes correctness over overlap.
class VulkanBridge {
public:
    static VulkanBridge& get() { static VulkanBridge bridge; return bridge; }
    bool requested = false;
    bool negotiate(const retro_hw_render_context_negotiation_interface_vulkan* info);
    void initialize();
    void destroy();
    const retro_hw_render_interface_vulkan* interface() const { return api.device ? &api : nullptr; }
    const void* readFrame(unsigned width, unsigned height, bool duplicate);
private:
    retro_hw_render_context_negotiation_interface_vulkan negotiation{};
    retro_hw_render_interface_vulkan api{};
    retro_vulkan_image image{};
    bool hasImage = false;
    bool negotiatedDevice = false;
    uint32_t sourceFamily = VK_QUEUE_FAMILY_IGNORED;
    std::mutex queueMutex;
    std::vector<VkSemaphore> waits;
    std::vector<VkCommandBuffer> commands;
    VkSemaphore signal = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    size_t capacity = 0;
    std::vector<uint32_t> pixels;
    void* loader = nullptr;
    void allocate(size_t bytes);
    void freeBuffer();
#define VK_FN(name) PFN_##name name = nullptr;
    VK_FN(vkGetInstanceProcAddr) VK_FN(vkGetDeviceProcAddr)
    VK_FN(vkCreateInstance) VK_FN(vkDestroyInstance) VK_FN(vkEnumeratePhysicalDevices)
    VK_FN(vkGetPhysicalDeviceQueueFamilyProperties) VK_FN(vkGetPhysicalDeviceMemoryProperties)
    VK_FN(vkCreateDevice) VK_FN(vkDestroyDevice) VK_FN(vkGetDeviceQueue)
    VK_FN(vkDeviceWaitIdle) VK_FN(vkCreateCommandPool) VK_FN(vkDestroyCommandPool)
    VK_FN(vkAllocateCommandBuffers) VK_FN(vkResetCommandBuffer) VK_FN(vkBeginCommandBuffer) VK_FN(vkEndCommandBuffer)
    VK_FN(vkCreateFence) VK_FN(vkDestroyFence) VK_FN(vkResetFences) VK_FN(vkWaitForFences)
    VK_FN(vkCreateBuffer) VK_FN(vkDestroyBuffer) VK_FN(vkGetBufferMemoryRequirements)
    VK_FN(vkAllocateMemory) VK_FN(vkFreeMemory) VK_FN(vkBindBufferMemory) VK_FN(vkMapMemory) VK_FN(vkUnmapMemory)
    VK_FN(vkCmdPipelineBarrier) VK_FN(vkCmdCopyImageToBuffer) VK_FN(vkQueueSubmit)
#undef VK_FN
};

#if defined(__aarch64__)
#include <adrenotools/driver.h>
#endif
#include "vulkanbridge.h"
#include "log.h"
#include <dlfcn.h>
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        LOGE("Vulkan %s failed: %d", operation, result);
        throw std::runtime_error(operation);
    }
}
}

bool VulkanBridge::negotiate(const retro_hw_render_context_negotiation_interface_vulkan* info) {
    if (!info || info->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN ||
        info->interface_version < 1 || info->interface_version > 2) return false;
    negotiation = {};
    negotiation.interface_type = info->interface_type;
    negotiation.interface_version = info->interface_version;
    negotiation.get_application_info = info->get_application_info;
    negotiation.create_device = info->create_device;
    negotiation.destroy_device = info->destroy_device;
    if (info->interface_version >= 2) {
        negotiation.create_instance = info->create_instance;
        negotiation.create_device2 = info->create_device2;
    }
    return true;
}

void VulkanBridge::initialize() {
    if (!requested || api.device) return;
    if (!driverLibrary.empty()) {
#if defined(__aarch64__)
        const auto separator = driverLibrary.find_last_of('/');
        if (separator == std::string::npos || hookDirectory.empty())
            throw std::runtime_error("Invalid custom Vulkan driver path");
        const auto directory = driverLibrary.substr(0, separator + 1);
        const auto filename = driverLibrary.substr(separator + 1);
        loader = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_CUSTOM,
            nullptr, hookDirectory.c_str(), directory.c_str(), filename.c_str(), nullptr, nullptr);
        if (!loader) throw std::runtime_error("Could not load the selected Vulkan driver");
        LOGI("Custom Vulkan driver active");
#else
        throw std::runtime_error("Custom Vulkan drivers require ARM64");
#endif
    } else {
        loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!loader) throw std::runtime_error("Vulkan loader unavailable");
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr"));
    if (!vkGetInstanceProcAddr) throw std::runtime_error("Vulkan loader entry point unavailable");
    vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Alexandria";
    app.apiVersion = VK_API_VERSION_1_1;
    if (negotiation.get_application_info) {
        if (auto supplied = negotiation.get_application_info()) app = *supplied;
    }
    if (negotiation.create_instance) {
        api.instance = negotiation.create_instance(vkGetInstanceProcAddr, &app,
            [](void* opaque, const VkInstanceCreateInfo* info) {
                auto& self = *static_cast<VulkanBridge*>(opaque);
                VkInstance instance = VK_NULL_HANDLE;
                if (self.vkCreateInstance(info, nullptr, &instance) != VK_SUCCESS) return VkInstance(VK_NULL_HANDLE);
                return instance;
            }, this);
    } else {
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        const char* extensions[] = {"VK_KHR_surface"};
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = extensions;
        check(vkCreateInstance(&info, nullptr, &api.instance), "create instance");
    }
    if (!api.instance) throw std::runtime_error("Vulkan instance negotiation failed");
#define LOAD_I(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(api.instance, #name)); if (!name) throw std::runtime_error(#name);
    LOAD_I(vkDestroyInstance) LOAD_I(vkEnumeratePhysicalDevices)
    LOAD_I(vkGetPhysicalDeviceQueueFamilyProperties) LOAD_I(vkGetPhysicalDeviceMemoryProperties)
    LOAD_I(vkCreateDevice) LOAD_I(vkGetDeviceProcAddr)
#undef LOAD_I
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(api.instance, &count, nullptr), "enumerate GPUs");
    if (!count) throw std::runtime_error("No Vulkan GPU");
    std::vector<VkPhysicalDevice> gpus(count);
    check(vkEnumeratePhysicalDevices(api.instance, &count, gpus.data()), "enumerate GPUs");
    retro_vulkan_context context{};
    if (negotiation.create_device2) {
        negotiatedDevice = negotiation.create_device2(&context, api.instance, gpus[0], VK_NULL_HANDLE,
            vkGetInstanceProcAddr, [](VkPhysicalDevice gpu, void* opaque, const VkDeviceCreateInfo* info) {
                auto& self = *static_cast<VulkanBridge*>(opaque);
                VkDevice device = VK_NULL_HANDLE;
                if (self.vkCreateDevice(gpu, info, nullptr, &device) != VK_SUCCESS) return VkDevice(VK_NULL_HANDLE);
                return device;
            }, this);
    } else if (negotiation.create_device) {
        VkPhysicalDeviceFeatures features{};
        negotiatedDevice = negotiation.create_device(&context, api.instance, gpus[0], VK_NULL_HANDLE,
            vkGetInstanceProcAddr, nullptr, 0, nullptr, 0, &features);
    }
    if (!negotiatedDevice) {
        // A requested custom device is essential to these experimental cores.
        if (negotiation.create_device || negotiation.create_device2)
            throw std::runtime_error("Core Vulkan device negotiation failed");
        context.gpu = gpus[0];
        vkGetPhysicalDeviceQueueFamilyProperties(context.gpu, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(context.gpu, &count, families.data());
        uint32_t family = 0;
        while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
        if (family == count) throw std::runtime_error("No Vulkan graphics queue");
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.queueCreateInfoCount = 1; info.pQueueCreateInfos = &queue;
        check(vkCreateDevice(context.gpu, &info, nullptr, &context.device), "create device");
        context.queue_family_index = family;
    }
    api.gpu = context.gpu; api.device = context.device; api.queue_index = context.queue_family_index;
#define LOAD_D(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(api.device, #name)); if (!name) throw std::runtime_error(#name);
    LOAD_D(vkDestroyDevice) LOAD_D(vkGetDeviceQueue) LOAD_D(vkDeviceWaitIdle)
    LOAD_D(vkCreateCommandPool) LOAD_D(vkDestroyCommandPool) LOAD_D(vkAllocateCommandBuffers)
    LOAD_D(vkResetCommandBuffer) LOAD_D(vkBeginCommandBuffer) LOAD_D(vkEndCommandBuffer)
    LOAD_D(vkCreateFence) LOAD_D(vkDestroyFence) LOAD_D(vkResetFences) LOAD_D(vkWaitForFences)
    LOAD_D(vkCreateBuffer) LOAD_D(vkDestroyBuffer) LOAD_D(vkGetBufferMemoryRequirements)
    LOAD_D(vkAllocateMemory) LOAD_D(vkFreeMemory) LOAD_D(vkBindBufferMemory) LOAD_D(vkMapMemory) LOAD_D(vkUnmapMemory)
    LOAD_D(vkCmdPipelineBarrier) LOAD_D(vkCmdCopyImageToBuffer) LOAD_D(vkQueueSubmit)
#undef LOAD_D
    api.queue = context.queue;
    if (!api.queue) vkGetDeviceQueue(api.device, api.queue_index, 0, &api.queue);
    api.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    api.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    api.handle = this;
    api.get_instance_proc_addr = vkGetInstanceProcAddr;
    api.get_device_proc_addr = vkGetDeviceProcAddr;
    api.get_sync_index = [](void*) { return 0u; };
    api.get_sync_index_mask = [](void*) { return 1u; };
    api.wait_sync_index = [](void*) {}; // readFrame waits for every submission before returning.
    api.lock_queue = [](void* p) { static_cast<VulkanBridge*>(p)->queueMutex.lock(); };
    api.unlock_queue = [](void* p) { static_cast<VulkanBridge*>(p)->queueMutex.unlock(); };
    api.set_image = [](void* p, const retro_vulkan_image* img, uint32_t n, const VkSemaphore* sem, uint32_t family) {
        auto& s = *static_cast<VulkanBridge*>(p);
        s.hasImage = img != nullptr;
        if (img) s.image = *img;
        s.waits.clear(); if (n) s.waits.assign(sem, sem + n);
        s.sourceFamily = family;
    };
    api.set_command_buffers = [](void* p, uint32_t n, const VkCommandBuffer* cmd) {
        auto& s = *static_cast<VulkanBridge*>(p);
        if (n) s.commands.insert(s.commands.end(), cmd, cmd + n);
    };
    api.set_signal_semaphore = [](void* p, VkSemaphore sem) { static_cast<VulkanBridge*>(p)->signal = sem; };
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; poolInfo.queueFamilyIndex = api.queue_index;
    check(vkCreateCommandPool(api.device, &poolInfo, nullptr, &pool), "create command pool");
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(api.device, &alloc, &command), "allocate command buffer");
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(api.device, &fenceInfo, nullptr, &fence), "create fence");
    LOGI("Experimental Vulkan readback context ready");
}

void VulkanBridge::freeBuffer() {
    if (mapped) vkUnmapMemory(api.device, memory);
    if (buffer) vkDestroyBuffer(api.device, buffer, nullptr);
    if (memory) vkFreeMemory(api.device, memory, nullptr);
    mapped = nullptr; buffer = VK_NULL_HANDLE; memory = VK_NULL_HANDLE; capacity = 0;
}
void VulkanBridge::allocate(size_t bytes) {
    if (capacity >= bytes) return;
    freeBuffer();
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = bytes; info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(api.device, &info, nullptr, &buffer), "create readback buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(api.device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(api.gpu, &properties);
    uint32_t type = 0;
    const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (type < properties.memoryTypeCount && (!(requirements.memoryTypeBits & (1u << type)) ||
        (properties.memoryTypes[type].propertyFlags & flags) != flags)) ++type;
    if (type == properties.memoryTypeCount) throw std::runtime_error("No coherent Vulkan readback memory");
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = requirements.size; alloc.memoryTypeIndex = type;
    check(vkAllocateMemory(api.device, &alloc, nullptr, &memory), "allocate readback memory");
    check(vkBindBufferMemory(api.device, buffer, memory, 0), "bind readback memory");
    check(vkMapMemory(api.device, memory, 0, requirements.size, 0, &mapped), "map readback memory");
    capacity = bytes;
}

const void* VulkanBridge::readFrame(unsigned width, unsigned height, bool duplicate) {
    if (!api.device) return nullptr;
    if (!width || !height || width > 8192 || height > 8192) throw std::runtime_error("Invalid Vulkan frame dimensions");
    bool copy = hasImage && !duplicate;
    const VkFormat format = image.create_info.format;
    if (copy && format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R8G8B8A8_SRGB &&
        format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_B8G8R8A8_SRGB)
        throw std::runtime_error("Unsupported Vulkan readback image format");
    if (copy) allocate(size_t(width) * height * 4);
    check(vkResetCommandBuffer(command, 0), "reset command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "begin readback");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    const bool transfer = copy && commands.empty() && !waits.empty() &&
        sourceFamily != VK_QUEUE_FAMILY_IGNORED && sourceFamily != api.queue_index;
    const auto layout = image.image_layout == VK_IMAGE_LAYOUT_GENERAL ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    if (copy) {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = image.image_layout; barrier.newLayout = layout;
        barrier.srcQueueFamilyIndex = transfer ? sourceFamily : VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = transfer ? api.queue_index : VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.create_info.image; barrier.subresourceRange = image.create_info.subresourceRange;
        barrier.subresourceRange.levelCount = 1; barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, barrier.subresourceRange.baseMipLevel, barrier.subresourceRange.baseArrayLayer, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(command, barrier.image, layout, buffer, 1, &region);
        std::swap(barrier.oldLayout, barrier.newLayout);
        std::swap(barrier.srcQueueFamilyIndex, barrier.dstQueueFamilyIndex);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = buffer; host.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &host, 1, &barrier);
    }
    check(vkEndCommandBuffer(command), "end readback");
    const bool coreCommands = !commands.empty();
    commands.push_back(command);
    std::vector<VkPipelineStageFlags> stages(waits.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    if (!duplicate && !coreCommands) {
        submit.waitSemaphoreCount = waits.size(); submit.pWaitSemaphores = waits.data(); submit.pWaitDstStageMask = stages.data();
    }
    submit.commandBufferCount = commands.size(); submit.pCommandBuffers = commands.data();
    submit.signalSemaphoreCount = signal ? 1 : 0; submit.pSignalSemaphores = signal ? &signal : nullptr;
    check(vkResetFences(api.device, 1, &fence), "reset readback fence");
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        check(vkQueueSubmit(api.queue, 1, &submit, fence), "submit readback");
    }
    check(vkWaitForFences(api.device, 1, &fence, VK_TRUE, UINT64_MAX), "wait readback");
    commands.clear(); waits.clear(); signal = VK_NULL_HANDLE;
    if (!copy) return nullptr;
    pixels.resize(size_t(width) * height);
    std::memcpy(pixels.data(), mapped, pixels.size() * 4);
    if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB)
        for (auto& pixel : pixels) pixel = (pixel & 0xff00ff00u) | ((pixel & 0xffu) << 16) | ((pixel >> 16) & 0xffu);
    return pixels.data();
}

void VulkanBridge::destroy() {
    if (api.device && vkDeviceWaitIdle) vkDeviceWaitIdle(api.device);
    if (api.device) {
        freeBuffer();
        if (fence) vkDestroyFence(api.device, fence, nullptr);
        if (pool) vkDestroyCommandPool(api.device, pool, nullptr);
    }
    if (negotiatedDevice && negotiation.destroy_device) negotiation.destroy_device();
    if (api.device && vkDestroyDevice) vkDestroyDevice(api.device, nullptr);
    if (api.instance && vkDestroyInstance) vkDestroyInstance(api.instance, nullptr);
    if (loader) dlclose(loader);
    api = {}; negotiation = {}; image = {}; hasImage = false; requested = false; negotiatedDevice = false;
    pool = VK_NULL_HANDLE; command = VK_NULL_HANDLE; fence = VK_NULL_HANDLE; loader = nullptr;
    waits.clear(); commands.clear(); pixels.clear(); signal = VK_NULL_HANDLE;
    driverLibrary.clear(); hookDirectory.clear();
}

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <string>

class Window;

#include "vma/vk_mem_alloc.h"

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct FrameSync
{
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
};

class VulkanContext
{
public:
    VulkanContext() = default;
    ~VulkanContext();

    //Lifecycle.
    void createInstance(bool enableValidation);
    void setupDebugMessenger(bool enableValidation);
    void createSurface(Window& window);
    void pickPhysicalDevice();
    void createDevice();
    void createAllocator();
    void createCommandPoolsAndBuffers(uint32_t framesInFlight);
    void createSyncObjects(uint32_t framesInFlight);

    void destroy();

    //Helpers.
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;

    //Getters.
    VkInstance instance() const
    {
        return mInstance;
    }

    VkPhysicalDevice physical() const
    {
        return mPhysical;
    }

    VkDevice device() const
    {
        return mDevice;
    }

    VkSurfaceKHR surface() const
    {
        return mSurface;
    }

    VkQueue graphicsQueue() const
    {
        return mGraphicsQueue;
    }

    VkQueue presentQueue() const
    {
        return mPresentQueue;
    }

    VmaAllocator allocator() const
    {
        return mAllocator;
    }

    const std::vector<FrameSync>& frames() const
    {
        return mFrames;
    }

    uint32_t graphicsFamilyIndex() const
    {
        return mGraphicsFamilyIndex;
    }

    uint32_t presentFamilyIndex() const
    {
        return mPresentFamilyIndex;
    }

    //Resize.
    void waitIdle() const;

private:
    //Debug utils.
    VkDebugUtilsMessengerEXT mDebugMessenger = VK_NULL_HANDLE;

    //Core handles.
    VkInstance mInstance = VK_NULL_HANDLE;
    VkSurfaceKHR mSurface = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysical = VK_NULL_HANDLE;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkQueue mGraphicsQueue = VK_NULL_HANDLE;
    VkQueue mPresentQueue = VK_NULL_HANDLE;
    uint32_t mGraphicsFamilyIndex = 0;
    uint32_t mPresentFamilyIndex = 0;

    //VMA.
    VmaAllocator mAllocator = VK_NULL_HANDLE;

    //Per-frame.
    std::vector<FrameSync> mFrames;

    //Validation.
    bool mEnableValidation = false;

    //Internal helpers.
    bool checkDeviceExtensions(VkPhysicalDevice device) const;
    void getRequiredInstanceExtensions(std::vector<const char*>& out) const;

    //Loading extension functions.
    static VkResult CreateDebugUtilsMessengerEXT(VkInstance inst,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pMessenger);

    static void DestroyDebugUtilsMessengerEXT(VkInstance inst, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* pAllocator);
};
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
    bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
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
    VkInstance instance() const { return m_instance; }
    VkPhysicalDevice physical() const { return m_physical; }
    VkDevice device() const { return m_device; }
    VkSurfaceKHR surface() const { return m_surface; }
    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    VkQueue presentQueue() const { return m_presentQueue; }
    VmaAllocator allocator() const { return m_allocator; }

    const std::vector<FrameSync>& frames() const { return m_frames; }
    uint32_t graphicsFamilyIndex() const { return m_graphicsFamilyIndex; }
    uint32_t presentFamilyIndex() const { return m_presentFamilyIndex; }

    //Resize.
    void waitIdle() const;

private:
    //Debug utils.
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

    //Core handles.
    VkInstance       m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkDevice         m_device = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue          m_presentQueue = VK_NULL_HANDLE;
    uint32_t         m_graphicsFamilyIndex = 0;
    uint32_t         m_presentFamilyIndex = 0;

    //VMA.
    VmaAllocator     m_allocator = VK_NULL_HANDLE;

    //Per-frame.
    std::vector<FrameSync> m_frames;

    //Validation.
    bool m_enableValidation = false;

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
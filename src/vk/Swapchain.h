#pragma once

#include <vulkan/vulkan.h>
#include <vector>

class VulkanContext;
class Window;

struct SwapchainBundle
{
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
};

class Swapchain
{
public:
    Swapchain() = default;
    ~Swapchain() = default;

    void create(VulkanContext& vulkanContext, Window& window);
    void recreate(VulkanContext& vulkanContext, Window& window);
    void destroy(VulkanContext& vulkanContext);

    VkResult acquireNextImage(VulkanContext& vulkanContext, VkSemaphore imageAvailable, uint32_t* outIndex);
    VkResult present(VulkanContext& vulkanContext, VkSemaphore renderFinished, uint32_t imageIndex);

    const SwapchainBundle& bundle() const
    {
        return mSwapchainBundle;
    }

private:
    SwapchainBundle mSwapchainBundle{};

    void createSwapchain(VulkanContext& vulkanContext, Window& window, VkSwapchainKHR oldSwap = VK_NULL_HANDLE);
    void createImageViews(VulkanContext& vulkanContext);
    void createRenderPass(VulkanContext& vulkanContext);
    void createFramebuffers(VulkanContext& vulkanContext);
};
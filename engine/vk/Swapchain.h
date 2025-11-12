#pragma once

#include <vulkan/vulkan.h>
#include <vector>

class VulkanContext;
class Window;

struct SwapchainBundle 
{
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat       imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent{};
    std::vector<VkImage>     images;
    std::vector<VkImageView> imageViews;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
};

class Swapchain 
{
public:
    Swapchain() = default;
    ~Swapchain() = default;

    void create(VulkanContext& vk, Window& window);
    void recreate(VulkanContext& vk, Window& window);
    void destroy(VulkanContext& vk);

    VkResult acquireNextImage(VulkanContext& vk, VkSemaphore imageAvailable, uint32_t* outIndex);
    VkResult present(VulkanContext& vk, VkSemaphore renderFinished, uint32_t imageIndex);

    const SwapchainBundle& bundle() const { return m_sc; }

private:
    SwapchainBundle m_sc{};

    void createSwapchain(VulkanContext& vk, Window& window, VkSwapchainKHR oldSwap = VK_NULL_HANDLE);
    void createImageViews(VulkanContext& vk);
    void createRenderPass(VulkanContext& vk);
    void createFramebuffers(VulkanContext& vk);
};
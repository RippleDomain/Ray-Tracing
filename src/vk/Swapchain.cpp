#include "Swapchain.h"
#include "VulkanContext.h"
#include "../util/Check.h"
#include "../util/Logger.h"
#include "../platform/Window.h"

#include <algorithm>
#include <limits>

struct SwapSupport
{
    VkSurfaceCapabilitiesKHR caps{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> modes;
};

static SwapSupport querySupport(VulkanContext& vulkanContext)
{
    SwapSupport support;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkanContext.physical(), vulkanContext.surface(), &support.caps);

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vulkanContext.physical(), vulkanContext.surface(), &count, nullptr);
    support.formats.resize(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vulkanContext.physical(), vulkanContext.surface(), &count, support.formats.data());

    vkGetPhysicalDeviceSurfacePresentModesKHR(vulkanContext.physical(), vulkanContext.surface(), &count, nullptr);
    support.modes.resize(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vulkanContext.physical(), vulkanContext.surface(), &count, support.modes.data());

    return support;
}

static VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>& available)
{
    for (auto& format : available)
    {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    for (auto& format : available)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    return available[0];
}

static VkPresentModeKHR choosePresent(const std::vector<VkPresentModeKHR>& available)
{
    for (auto mode : available)
    {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, Window& window)
{
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return caps.currentExtent;
    }

    int width = 0;
    int height = 0;
    window.getFramebufferSize(width, height);
    VkExtent2D extent{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);

    return extent;
}

void Swapchain::create(VulkanContext& vulkanContext, Window& window)
{
    createSwapchain(vulkanContext, window, VK_NULL_HANDLE);
    createImageViews(vulkanContext);
    createRenderPass(vulkanContext);
    createFramebuffers(vulkanContext);

    logger::info("Swapchain created: %u images (%ux%u)", static_cast<unsigned>(mSwapchainBundle.images.size()), mSwapchainBundle.extent.width, mSwapchainBundle.extent.height);
}

void Swapchain::recreate(VulkanContext& vulkanContext, Window& window)
{
    vulkanContext.waitIdle();
    destroy(vulkanContext);
    create(vulkanContext, window);
}

void Swapchain::destroy(VulkanContext& vulkanContext)
{
    for (auto framebuffer : mSwapchainBundle.framebuffers)
    {
        vkDestroyFramebuffer(vulkanContext.device(), framebuffer, nullptr);
    }

    mSwapchainBundle.framebuffers.clear();

    if (mSwapchainBundle.renderPass)
    {
        vkDestroyRenderPass(vulkanContext.device(), mSwapchainBundle.renderPass, nullptr);
    }

    mSwapchainBundle.renderPass = VK_NULL_HANDLE;

    for (auto view : mSwapchainBundle.imageViews)
    {
        vkDestroyImageView(vulkanContext.device(), view, nullptr);
    }

    mSwapchainBundle.imageViews.clear();

    if (mSwapchainBundle.swapchain)
    {
        vkDestroySwapchainKHR(vulkanContext.device(), mSwapchainBundle.swapchain, nullptr);
    }

    mSwapchainBundle.swapchain = VK_NULL_HANDLE;
    mSwapchainBundle.images.clear();
}

void Swapchain::createSwapchain(VulkanContext& vulkanContext, Window& window, VkSwapchainKHR oldSwap)
{
    SwapSupport support = querySupport(vulkanContext);
    auto format = chooseFormat(support.formats);
    auto presentMode = choosePresent(support.modes);
    auto extent = chooseExtent(support.caps, window);

    uint32_t imageCount = support.caps.minImageCount + 1;

    if (support.caps.maxImageCount > 0 && imageCount > support.caps.maxImageCount)
    {
        imageCount = support.caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    createInfo.surface = vulkanContext.surface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = format.format;
    createInfo.imageColorSpace = format.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

    uint32_t queueIndices[] = { vulkanContext.graphicsFamilyIndex(), vulkanContext.presentFamilyIndex() };

    if (vulkanContext.graphicsFamilyIndex() != vulkanContext.presentFamilyIndex())
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwap;

    VK_CHECK(vkCreateSwapchainKHR(vulkanContext.device(), &createInfo, nullptr, &mSwapchainBundle.swapchain));
    mSwapchainBundle.imageFormat = format.format;
    mSwapchainBundle.extent = extent;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(vulkanContext.device(), mSwapchainBundle.swapchain, &count, nullptr);
    mSwapchainBundle.images.resize(count);
    vkGetSwapchainImagesKHR(vulkanContext.device(), mSwapchainBundle.swapchain, &count, mSwapchainBundle.images.data());
}

void Swapchain::createImageViews(VulkanContext& vulkanContext)
{
    mSwapchainBundle.imageViews.resize(mSwapchainBundle.images.size());

    for (size_t i = 0; i < mSwapchainBundle.images.size(); ++i)
    {
        VkImageViewCreateInfo createInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

        createInfo.image = mSwapchainBundle.images[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = mSwapchainBundle.imageFormat;
        createInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(vulkanContext.device(), &createInfo, nullptr, &mSwapchainBundle.imageViews[i]));
    }
}

void Swapchain::createRenderPass(VulkanContext& vulkanContext)
{
    VkAttachmentDescription color{};
    color.format = mSwapchainBundle.imageFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Load compute output.
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Compute transition provides this.
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &color;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(vulkanContext.device(), &renderPassInfo, nullptr, &mSwapchainBundle.renderPass));
}

void Swapchain::createFramebuffers(VulkanContext& vulkanContext)
{
    mSwapchainBundle.framebuffers.resize(mSwapchainBundle.imageViews.size());

    for (size_t i = 0; i < mSwapchainBundle.imageViews.size(); ++i)
    {
        VkImageView attachments[] = { mSwapchainBundle.imageViews[i] };
        VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };

        framebufferInfo.renderPass = mSwapchainBundle.renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = mSwapchainBundle.extent.width;
        framebufferInfo.height = mSwapchainBundle.extent.height;
        framebufferInfo.layers = 1;

        VK_CHECK(vkCreateFramebuffer(vulkanContext.device(), &framebufferInfo, nullptr, &mSwapchainBundle.framebuffers[i]));
    }
}

VkResult Swapchain::acquireNextImage(VulkanContext& vulkanContext, VkSemaphore imageAvailable, uint32_t* outIndex)
{
    return vkAcquireNextImageKHR(vulkanContext.device(), mSwapchainBundle.swapchain, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, outIndex);
}

VkResult Swapchain::present(VulkanContext& vulkanContext, VkSemaphore renderFinished, uint32_t imageIndex)
{
    VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    VkSwapchainKHR swapchainHandle = mSwapchainBundle.swapchain;
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(vulkanContext.presentQueue(), &presentInfo);
}
#include "Swapchain.h"
#include "VulkanContext.h"
#include "../core/Check.h"
#include "../core/Logger.h"
#include "../platform/Window.h"

#include <algorithm>
#include <limits>

struct SwapSupport 
{
    VkSurfaceCapabilitiesKHR caps{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> modes;
};

static SwapSupport querySupport(VulkanContext& vk) 
{
    SwapSupport s;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical(), vk.surface(), &s.caps);

    uint32_t c = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical(), vk.surface(), &c, nullptr);
    s.formats.resize(c);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical(), vk.surface(), &c, s.formats.data());

    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical(), vk.surface(), &c, nullptr);
    s.modes.resize(c);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical(), vk.surface(), &c, s.modes.data());

    return s;
}

static VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>& avail) 
{
    for (auto& f : avail) 
    {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return f;
        }
    }

    return avail[0];
}
static VkPresentModeKHR choosePresent(const std::vector<VkPresentModeKHR>& avail) 
{
    for (auto m : avail) if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, Window& window) 
{
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) return caps.currentExtent;

    int w, h; window.getFramebufferSize(w, h);
    VkExtent2D e{ static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    e.width = std::clamp(e.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    e.height = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);

    return e;
}

void Swapchain::create(VulkanContext& vk, Window& window) 
{
    createSwapchain(vk, window, VK_NULL_HANDLE);
    createImageViews(vk);
    createRenderPass(vk);
    createFramebuffers(vk);

    logger::info("Swapchain created: %u images (%ux%u)", (unsigned)m_sc.images.size(), m_sc.extent.width, m_sc.extent.height);
}

void Swapchain::recreate(VulkanContext& vk, Window& window) 
{
    vk.waitIdle();
    destroy(vk);
    create(vk, window);
}

void Swapchain::destroy(VulkanContext& vk) 
{
    for (auto fb : m_sc.framebuffers) vkDestroyFramebuffer(vk.device(), fb, nullptr);
    m_sc.framebuffers.clear();

    if (m_sc.renderPass) vkDestroyRenderPass(vk.device(), m_sc.renderPass, nullptr);
    m_sc.renderPass = VK_NULL_HANDLE;

    for (auto view : m_sc.imageViews) vkDestroyImageView(vk.device(), view, nullptr);
    m_sc.imageViews.clear();

    if (m_sc.swapchain) vkDestroySwapchainKHR(vk.device(), m_sc.swapchain, nullptr);
    m_sc.swapchain = VK_NULL_HANDLE;
    m_sc.images.clear();
}

void Swapchain::createSwapchain(VulkanContext& vk, Window& window, VkSwapchainKHR oldSwap) 
{
    SwapSupport s = querySupport(vk);
    auto fmt = chooseFormat(s.formats);
    auto pm = choosePresent(s.modes);
    auto ext = chooseExtent(s.caps, window);

    uint32_t imageCount = s.caps.minImageCount + 1;
    if (s.caps.maxImageCount > 0 && imageCount > s.caps.maxImageCount) imageCount = s.caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    ci.surface = vk.surface();
    ci.minImageCount = imageCount;
    ci.imageFormat = fmt.format;
    ci.imageColorSpace = fmt.colorSpace;
    ci.imageExtent = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t qIdx[] = { vk.graphicsFamilyIndex(), vk.presentFamilyIndex() };

    if (vk.graphicsFamilyIndex() != vk.presentFamilyIndex()) 
    {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = qIdx;
    }
    else 
    {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform = s.caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = pm;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = oldSwap;

    VK_CHECK(vkCreateSwapchainKHR(vk.device(), &ci, nullptr, &m_sc.swapchain));
    m_sc.imageFormat = fmt.format;
    m_sc.extent = ext;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(vk.device(), m_sc.swapchain, &count, nullptr);
    m_sc.images.resize(count);
    vkGetSwapchainImagesKHR(vk.device(), m_sc.swapchain, &count, m_sc.images.data());
}

void Swapchain::createImageViews(VulkanContext& vk) 
{
    m_sc.imageViews.resize(m_sc.images.size());

    for (size_t i = 0; i < m_sc.images.size(); ++i) 
    {
        VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ci.image = m_sc.images[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = m_sc.imageFormat;
        ci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.baseMipLevel = 0;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.baseArrayLayer = 0;
        ci.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(vk.device(), &ci, nullptr, &m_sc.imageViews[i]));
    }
}

void Swapchain::createRenderPass(VulkanContext& vk) 
{
    VkAttachmentDescription color{};
    color.format = m_sc.imageFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;

    VK_CHECK(vkCreateRenderPass(vk.device(), &rp, nullptr, &m_sc.renderPass));
}

void Swapchain::createFramebuffers(VulkanContext& vk) 
{
    m_sc.framebuffers.resize(m_sc.imageViews.size());

    for (size_t i = 0; i < m_sc.imageViews.size(); ++i) 
    {
        VkImageView attachments[] = { m_sc.imageViews[i] };
        VkFramebufferCreateInfo fci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };

        fci.renderPass = m_sc.renderPass;
        fci.attachmentCount = 1;
        fci.pAttachments = attachments;
        fci.width = m_sc.extent.width;
        fci.height = m_sc.extent.height;
        fci.layers = 1;

        VK_CHECK(vkCreateFramebuffer(vk.device(), &fci, nullptr, &m_sc.framebuffers[i]));
    }
}

VkResult Swapchain::acquireNextImage(VulkanContext& vk, VkSemaphore imageAvailable, uint32_t* outIndex) 
{
    return vkAcquireNextImageKHR(vk.device(), m_sc.swapchain, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, outIndex);
}

VkResult Swapchain::present(VulkanContext& vk, VkSemaphore renderFinished, uint32_t imageIndex) 
{
    VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished;
    pi.swapchainCount = 1;
    VkSwapchainKHR sc = m_sc.swapchain;
    pi.pSwapchains = &sc;
    pi.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(vk.presentQueue(), &pi);
}
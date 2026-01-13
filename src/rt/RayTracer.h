#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <glm/glm.hpp>
#include "vma/vk_mem_alloc.h"

class VulkanContext;
class Swapchain;

// GPU sphere layout.
struct GPUSphere
{
    glm::vec4 centerRadius; // xyz = center, w = radius.
    glm::vec4 albedo; // xyz = albedo, w unused.
    glm::vec4 misc; // x = material (0=lambert,1=metal,2=dielectric), y = fuzz, z = refIdx, w = flags (bit0=checker).
};

// Uniform parameters.
struct GPUParams
{
    glm::vec4 originLens; // xyz origin, w lensRadius.
    glm::vec4 lowerLeft; // xyz lower-left corner, w unused.
    glm::vec4 horizontal; // xyz horizontal, w unused.
    glm::vec4 vertical; // xyz vertical, w unused.
    glm::vec4 u; // camera basis.
    glm::vec4 v;
    glm::vec4 w;
    glm::uvec4 frameSampleDepthCount; // frameIndex, samplesPerFrame, maxDepth, sphereCount.
    glm::vec4 resolution; // x=width, y=height.
};

class RayTracer
{
public:
    RayTracer() = default;
    ~RayTracer() = default;

    void create(VulkanContext& vulkanContext, Swapchain& swapchain);
    void resize(VulkanContext& vulkanContext, Swapchain& swapchain);
    void destroy(VulkanContext& vulkanContext);
    void setCamera(const glm::vec3& pos, const glm::vec3& dir, float focusDist = -1.0f);
    void setSamplesPerPixel(uint32_t spp);
    void setAperture(float aperture);
    void setFocusDistance(float focusDist);
    void setFov(float vfov);
    void setMaxDepth(uint32_t depth);

    // Records commands into an already begun command buffer.
    void render(VulkanContext& vulkanContext, Swapchain& swapchain, VkCommandBuffer commandBuffer, uint32_t swapImageIndex, uint32_t frameIndex);

private:
    void buildScene();
    void createPipeline(VulkanContext& vulkanContext);
    void createDescriptors(VulkanContext& vulkanContext, Swapchain& swapchain);
    void createAccumulationImage(VulkanContext& vulkanContext, const VkExtent2D& extent);
    void uploadScene(VulkanContext& vulkanContext);
    void updateParams(VulkanContext& vulkanContext, const VkExtent2D& extent, uint32_t frameIndex);
    GPUParams makeCameraParams(const VkExtent2D& extent) const;

    VkDescriptorSetLayout mSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
    VkPipeline mPipeline = VK_NULL_HANDLE;
    VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> mDescriptorSets;

    VkImage mAccumImage = VK_NULL_HANDLE;
    VkImageView mAccumView = VK_NULL_HANDLE;
    VmaAllocation mAccumAlloc = VK_NULL_HANDLE;

    VkBuffer mSphereBuffer = VK_NULL_HANDLE;
    VmaAllocation mSphereAlloc = VK_NULL_HANDLE;

    VkBuffer mParamsBuffer = VK_NULL_HANDLE;
    VmaAllocation mParamsAlloc = VK_NULL_HANDLE;

    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    bool mResetAccum = true;

    std::vector<GPUSphere> mSpheres;

    glm::vec3 mCamPos{ 13.0f, 2.0f, 3.0f };
    glm::vec3 mCamDir{ -1.0f, 0.0f, 0.0f };
    float mAperture = 0.05f;
    float mVerticalFov = 20.0f;
    float mFocusDistance = 10.0f;
    uint32_t mSamplesPerPixel = 4;
    uint32_t mMaxDepth = 12;
};
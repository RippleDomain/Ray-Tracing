#include "RayTracer.h"

#include "../vk/VulkanContext.h"
#include "../vk/Swapchain.h"
#include "../util/Check.h"
#include "../util/Logger.h"

#include <shaderc/shaderc.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <fstream>
#include <stdexcept>
#include <array>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace
{
    // Reads an entire text file into a std::string.
    std::string readFileText(const std::string& path)
    {
        std::ifstream inputStream(path, std::ios::binary);

        if (!inputStream)
        {
            throw std::runtime_error("Failed to open file: " + path);
        }

        return std::string((std::istreambuf_iterator<char>(inputStream)), std::istreambuf_iterator<char>());
    }

    std::vector<uint32_t> readFileBinaryWords(const std::string& path)
    {
        std::ifstream inputStream(path, std::ios::binary | std::ios::ate);

        if (!inputStream)
        {
            throw std::runtime_error("Failed to open file: " + path);
        }

        const std::streamsize size = inputStream.tellg();
        if (size <= 0 || (size % 4) != 0)
        {
            throw std::runtime_error("Invalid SPIR-V file size: " + path);
        }

        std::vector<uint32_t> data(static_cast<size_t>(size) / 4);
        inputStream.seekg(0);
        inputStream.read(reinterpret_cast<char*>(data.data()), size);

        if (!inputStream)
        {
            throw std::runtime_error("Failed to read file: " + path);
        }

        return data;
    }

    VkShaderModule compileCompute(VkDevice device, const std::string& path)
    {
        std::vector<uint32_t> spirv;

#if defined(_DEBUG)
        std::string spvPath = path;
        if (spvPath.size() > 5 && spvPath.substr(spvPath.size() - 5) == ".glsl")
        {
            spvPath = spvPath.substr(0, spvPath.size() - 5) + ".spv";
        }
        else
        {
            spvPath += ".spv";
        }

        spirv = readFileBinaryWords(spvPath);
#else
        auto source = readFileText(path);

        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        auto result = compiler.CompileGlslToSpv(source, shaderc_compute_shader, path.c_str(), options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            throw std::runtime_error(result.GetErrorMessage());
        }

        spirv.assign(result.cbegin(), result.cend());
#endif

        VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();

        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &module));

        return module;
    }
}

void RayTracer::buildScene()
{
    mSpheres.clear();

    GPUSphere ground{};
    ground.centerRadius = { 0.0f, -1000.0f, 0.0f, 1000.0f };
    ground.albedo = { 0.75f, 0.8f, 0.9f, 0.0f };
    ground.misc = { 0.0f, 0.0f, 1.0f, 1.0f }; // Lambert with checker flag.
    mSpheres.push_back(ground);

    GPUSphere center{};
    center.centerRadius = { 0.0f, 1.0f, 0.0f, 1.0f };
    center.albedo = { 0.9f, 0.25f, 0.25f, 0.0f }; // Vibrant red.
    center.misc = { 0.0f, 0.0f, 1.0f, 0.0f }; // Lambert.
    mSpheres.push_back(center);

    GPUSphere left{};
    left.centerRadius = { -4.0f, 1.0f, 0.0f, 1.0f };
    left.albedo = { 1.0f, 1.0f, 1.0f, 0.0f }; // Glass stays neutral.
    left.misc = { 2.0f, 0.0f, 1.5f, 0.0f }; // Dielectric, refIdx 1.5.
    mSpheres.push_back(left);

    GPUSphere right{};
    right.centerRadius = { 4.0f, 1.0f, 0.0f, 1.0f };
    right.albedo = { 0.95f, 0.65f, 0.15f, 0.0f }; // Warmer metal.
    right.misc = { 1.0f, 0.03f, 1.0f, 0.0f }; // Metal with small fuzz.
    mSpheres.push_back(right);

    GPUSphere mirror{};
    mirror.centerRadius = { 2.5f, 0.5f, 2.5f, 0.5f };
    mirror.albedo = { 0.95f, 0.95f, 0.98f, 0.0f }; // Bright reflective.
    mirror.misc = { 1.0f, 0.0f, 1.0f, 0.0f }; // Perfect mirror (fuzz=0).
    mSpheres.push_back(mirror);
}

GPUParams RayTracer::makeCameraParams(const VkExtent2D& extent) const
{
    const glm::vec3 lookFrom = mCamPos;
    const glm::vec3 direction = glm::normalize(mCamDir);
    const glm::vec3 lookAt = lookFrom + direction;
    const glm::vec3 vup = { 0.0f, 1.0f, 0.0f };
    float verticalFov = mVerticalFov;
    float aperture = mAperture;

    float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    float theta = glm::radians(verticalFov);
    float halfHeight = tanf(theta * 0.5f);
    float viewportHeight = 2.0f * halfHeight;
    float viewportWidth = aspect * viewportHeight;

    glm::vec3 w = glm::normalize(lookFrom - lookAt);
    glm::vec3 u = glm::normalize(glm::cross(vup, w));
    glm::vec3 v = glm::cross(w, u);

    float focusDistance = mFocusDistance;

    glm::vec3 horizontal = focusDistance * viewportWidth * u;
    glm::vec3 vertical = focusDistance * viewportHeight * v;
    glm::vec3 lowerLeft = lookFrom - horizontal * 0.5f - vertical * 0.5f - focusDistance * w;

    GPUParams params{};
    params.originLens = { lookFrom, aperture * 0.5f };
    params.lowerLeft = { lowerLeft, 0.0f };
    params.horizontal = { horizontal, 0.0f };
    params.vertical = { vertical, 0.0f };
    params.u = { u, 0.0f };
    params.v = { v, 0.0f };
    params.w = { w, 0.0f };
    params.resolution = { static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 0.0f };
    params.invResolution = { 1.0f / static_cast<float>(extent.width), 1.0f / static_cast<float>(extent.height), 0.0f, 0.0f };

    return params;
}

void RayTracer::create(VulkanContext& vulkanContext, Swapchain& swapchain)
{
    const auto& extent = swapchain.bundle().extent;
    mWidth = extent.width;
    mHeight = extent.height;
    mResetAccum = true;
    mAccumInitialized = false;
    mSwapchainImageInitialized.assign(swapchain.bundle().images.size(), false);

    buildScene();
    {
        glm::vec3 lookAt{ 0.0f, 1.0f, 0.0f };
        mCamDir = glm::normalize(lookAt - mCamPos);
        mFocusDistance = glm::length(lookAt - mCamPos);
    }

    uploadScene(vulkanContext);
    createPipeline(vulkanContext);
    createAccumulationImage(vulkanContext, extent);
    createDescriptors(vulkanContext, swapchain);
}

void RayTracer::resize(VulkanContext& vulkanContext, Swapchain& swapchain)
{
    vkDeviceWaitIdle(vulkanContext.device());

    if (mAccumView)
    {
        vkDestroyImageView(vulkanContext.device(), mAccumView, nullptr);
    }
    if (mAccumImage && mAccumAlloc)
    {
        vmaDestroyImage(vulkanContext.allocator(), mAccumImage, mAccumAlloc);
    }

    mAccumImage = VK_NULL_HANDLE;
    mAccumView = VK_NULL_HANDLE;
    mAccumAlloc = VK_NULL_HANDLE;

    if (mDescriptorPool)
    {
        vkDestroyDescriptorPool(vulkanContext.device(), mDescriptorPool, nullptr);
    }
    mDescriptorPool = VK_NULL_HANDLE;
    mDescriptorSets.clear();

    const auto& extent = swapchain.bundle().extent;
    mWidth = extent.width;
    mHeight = extent.height;
    mResetAccum = true;
    mAccumInitialized = false;
    mSwapchainImageInitialized.assign(swapchain.bundle().images.size(), false);

    createAccumulationImage(vulkanContext, extent);
    createDescriptors(vulkanContext, swapchain);
}

void RayTracer::destroy(VulkanContext& vulkanContext)
{
    vkDeviceWaitIdle(vulkanContext.device());

    if (mDescriptorPool)
    {
        vkDestroyDescriptorPool(vulkanContext.device(), mDescriptorPool, nullptr);
    }

    mDescriptorPool = VK_NULL_HANDLE;
    mDescriptorSets.clear();

    if (mPipeline)
    {
        vkDestroyPipeline(vulkanContext.device(), mPipeline, nullptr);
    }
    if (mPipelineLayout)
    {
        vkDestroyPipelineLayout(vulkanContext.device(), mPipelineLayout, nullptr);
    }
    if (mSetLayout)
    {
        vkDestroyDescriptorSetLayout(vulkanContext.device(), mSetLayout, nullptr);
    }

    mPipeline = VK_NULL_HANDLE;
    mPipelineLayout = VK_NULL_HANDLE;
    mSetLayout = VK_NULL_HANDLE;

    if (mAccumView)
    {
        vkDestroyImageView(vulkanContext.device(), mAccumView, nullptr);
    }
    if (mAccumImage && mAccumAlloc)
    {
        vmaDestroyImage(vulkanContext.allocator(), mAccumImage, mAccumAlloc);
    }

    mAccumImage = VK_NULL_HANDLE;
    mAccumView = VK_NULL_HANDLE;
    mAccumAlloc = VK_NULL_HANDLE;

    if (mSphereBuffer && mSphereAlloc)
    {
        vmaDestroyBuffer(vulkanContext.allocator(), mSphereBuffer, mSphereAlloc);
    }
    
    for (size_t i = 0; i < mParamsBuffers.size(); ++i)
    {
        if (mParamsBuffers[i] && mParamsAllocs[i])
        {
            vmaDestroyBuffer(vulkanContext.allocator(), mParamsBuffers[i], mParamsAllocs[i]);
        }
    }

    mSphereBuffer = VK_NULL_HANDLE;
    mSphereAlloc = VK_NULL_HANDLE;
    mParamsBuffers.clear();
    mParamsAllocs.clear();
    mParamsMapped.clear();
}

void RayTracer::setCamera(const glm::vec3& position, const glm::vec3& direction, float focusDistance)
{
    mCamPos = position;
    mCamDir = glm::normalize(direction);

    if (focusDistance > 0.0f)
    {
        mFocusDistance = focusDistance;
    }

    mResetAccum = true;
}

void RayTracer::setSamplesPerPixel(uint32_t samplesPerPixel)
{
    mSamplesPerPixel = std::max(1u, samplesPerPixel);
    mResetAccum = true;
}

void RayTracer::setAperture(float aperture)
{
    mAperture = std::max(0.0f, aperture);
    mResetAccum = true;
}

void RayTracer::setFocusDistance(float focusDistance)
{
    if (focusDistance > 0.0f)
    {
        mFocusDistance = focusDistance;
        mResetAccum = true;
    }
}

void RayTracer::setFov(float verticalFov)
{
    mVerticalFov = std::clamp(verticalFov, 5.0f, 120.0f);
    mResetAccum = true;
}

void RayTracer::setMaxDepth(uint32_t depth)
{
    mMaxDepth = std::max(1u, depth);
    mResetAccum = true;
}

void RayTracer::createPipeline(VulkanContext& vulkanContext)
{
    VkDescriptorSetLayoutBinding accumBinding{};
    accumBinding.binding = 0;
    accumBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    accumBinding.descriptorCount = 1;
    accumBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding swapBinding{};
    swapBinding.binding = 1;
    swapBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    swapBinding.descriptorCount = 1;
    swapBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding sphereBinding{};
    sphereBinding.binding = 2;
    sphereBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sphereBinding.descriptorCount = 1;
    sphereBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding paramsBinding{};
    paramsBinding.binding = 3;
    paramsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    paramsBinding.descriptorCount = 1;
    paramsBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::array<VkDescriptorSetLayoutBinding, 4> bindings
    {
        accumBinding,
        swapBinding,
        sphereBinding,
        paramsBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(vulkanContext.device(), &layoutInfo, nullptr, &mSetLayout));

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mSetLayout;
    VK_CHECK(vkCreatePipelineLayout(vulkanContext.device(), &pipelineLayoutInfo, nullptr, &mPipelineLayout));

    VkShaderModule computeModule = compileCompute(vulkanContext.device(), "shaders/raytrace.comp.glsl");

    VkPipelineShaderStageCreateInfo stageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = computeModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = mPipelineLayout;

    VK_CHECK(vkCreateComputePipelines(vulkanContext.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline));
    vkDestroyShaderModule(vulkanContext.device(), computeModule, nullptr);
}

void RayTracer::createDescriptors(VulkanContext& vulkanContext, Swapchain& swapchain)
{
    const size_t imageCount = swapchain.bundle().images.size();
    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(imageCount * 2);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(imageCount);
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = static_cast<uint32_t>(imageCount);

    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = static_cast<uint32_t>(imageCount);
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(vulkanContext.device(), &poolInfo, nullptr, &mDescriptorPool));

    std::vector<VkDescriptorSetLayout> layouts(imageCount, mSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = mDescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();
    mDescriptorSets.resize(layouts.size());
    VK_CHECK(vkAllocateDescriptorSets(vulkanContext.device(), &allocInfo, mDescriptorSets.data()));

    mParamsBuffers.assign(imageCount, VK_NULL_HANDLE);
    mParamsAllocs.assign(imageCount, VK_NULL_HANDLE);

    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = sizeof(GPUParams);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo paramsAllocInfo{};
    paramsAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    paramsAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    mParamsMapped.assign(imageCount, nullptr);

    for (size_t i = 0; i < mDescriptorSets.size(); ++i)
    {
        VmaAllocationInfo allocationInfo{};
        VK_CHECK(vmaCreateBuffer(vulkanContext.allocator(), &bufferInfo, &paramsAllocInfo, &mParamsBuffers[i], &mParamsAllocs[i], &allocationInfo));
        mParamsMapped[i] = allocationInfo.pMappedData;

        VkDescriptorImageInfo accumInfo{};
        accumInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        accumInfo.imageView = mAccumView;

        VkDescriptorImageInfo swapInfo{};
        swapInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        swapInfo.imageView = swapchain.bundle().imageViews[i];

        VkDescriptorBufferInfo sphereInfo{};
        sphereInfo.buffer = mSphereBuffer;
        sphereInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo paramsInfo{};
        paramsInfo.buffer = mParamsBuffers[i];
        paramsInfo.range = sizeof(GPUParams);

        std::array<VkWriteDescriptorSet, 4> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = mDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &accumInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = mDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &swapInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = mDescriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &sphereInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = mDescriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &paramsInfo;

        vkUpdateDescriptorSets(vulkanContext.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void RayTracer::createAccumulationImage(VulkanContext& vulkanContext, const VkExtent2D& extent)
{
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };

    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { extent.width, extent.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_CHECK(vmaCreateImage(vulkanContext.allocator(), &imageInfo, &allocInfo, &mAccumImage, &mAccumAlloc, nullptr));

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = mAccumImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(vulkanContext.device(), &viewInfo, nullptr, &mAccumView));
}

void RayTracer::uploadScene(VulkanContext& vulkanContext)
{
    VkDeviceSize sphereSize = sizeof(GPUSphere) * mSpheres.size();

    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = sphereSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VK_CHECK(vmaCreateBuffer(vulkanContext.allocator(), &bufferInfo, &allocInfo, &mSphereBuffer, &mSphereAlloc, nullptr));

    void* mappedMemory = nullptr;
    VK_CHECK(vmaMapMemory(vulkanContext.allocator(), mSphereAlloc, &mappedMemory));
    std::memcpy(mappedMemory, mSpheres.data(), static_cast<size_t>(sphereSize));
    vmaUnmapMemory(vulkanContext.allocator(), mSphereAlloc);

}

void RayTracer::updateParams(VulkanContext& vulkanContext, const VkExtent2D& extent, uint32_t frameIndex, uint32_t swapImageIndex)
{
    GPUParams params = makeCameraParams(extent);
    params.frameSampleDepthCount = { frameIndex, mSamplesPerPixel, mMaxDepth, static_cast<uint32_t>(mSpheres.size()) };

    std::memcpy(mParamsMapped[swapImageIndex], &params, sizeof(GPUParams));
    vmaFlushAllocation(vulkanContext.allocator(), mParamsAllocs[swapImageIndex], 0, sizeof(GPUParams));
}

void RayTracer::render(VulkanContext& vulkanContext, Swapchain& swapchain, VkCommandBuffer commandBuffer, uint32_t swapImageIndex, uint32_t frameIndex)
{
    VkExtent2D extent = swapchain.bundle().extent;
    updateParams(vulkanContext, extent, frameIndex, swapImageIndex);

    const bool clearAccum = mResetAccum || frameIndex == 0;

    if (clearAccum)
    {
        VkImageMemoryBarrier accumToClear{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        accumToClear.oldLayout = mAccumInitialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
        accumToClear.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        accumToClear.srcAccessMask = mAccumInitialized
            ? (VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT)
            : 0;
        accumToClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        accumToClear.image = mAccumImage;
        accumToClear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        accumToClear.subresourceRange.levelCount = 1;
        accumToClear.subresourceRange.layerCount = 1;

        VkPipelineStageFlags accumClearSrcStage = mAccumInitialized
            ? (VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            accumClearSrcStage,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &accumToClear);

        VkClearColorValue zero{};
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(commandBuffer, mAccumImage, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        mAccumInitialized = true;
        mResetAccum = false;
    }

    VkImageMemoryBarrier swapBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    swapBarrier.oldLayout = mSwapchainImageInitialized[swapImageIndex] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    swapBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    swapBarrier.srcAccessMask = 0;
    swapBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    swapBarrier.image = swapchain.bundle().images[swapImageIndex];
    swapBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapBarrier.subresourceRange.levelCount = 1;
    swapBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &swapBarrier);

    mSwapchainImageInitialized[swapImageIndex] = true;

    VkImageMemoryBarrier accumToCompute{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    accumToCompute.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    accumToCompute.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    accumToCompute.srcAccessMask = clearAccum
        ? VK_ACCESS_TRANSFER_WRITE_BIT
        : (VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);
    accumToCompute.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    accumToCompute.image = mAccumImage;
    accumToCompute.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    accumToCompute.subresourceRange.levelCount = 1;
    accumToCompute.subresourceRange.layerCount = 1;

    VkPipelineStageFlags accumComputeSrcStage = clearAccum
        ? VK_PIPELINE_STAGE_TRANSFER_BIT
        : (VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdPipelineBarrier(
        commandBuffer,
        accumComputeSrcStage,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &accumToCompute);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineLayout, 0, 1, &mDescriptorSets[swapImageIndex], 0, nullptr);

    uint32_t groupX = (extent.width + 7) / 8;
    uint32_t groupY = (extent.height + 7) / 8;
    vkCmdDispatch(commandBuffer, groupX, groupY, 1);

    // Barrier to make image ready for color attachment (ImGui render pass will load).
    VkImageMemoryBarrier presentBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    presentBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    presentBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    presentBarrier.image = swapchain.bundle().images[swapImageIndex];
    presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    presentBarrier.subresourceRange.levelCount = 1;
    presentBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &presentBarrier);
}
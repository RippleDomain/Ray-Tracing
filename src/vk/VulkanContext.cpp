#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vma/vk_mem_alloc.h"

#include "VulkanContext.h"
#include "../util/Check.h"
#include "../util/Logger.h"
#include "../platform/Window.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <set>
#include <cstring>
#include <cassert>
#include <stdexcept>

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData)
{
    (void)types;
    (void)userData;
    const char* tag = "[VK] ";

    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        tag = "[VK-ERROR] ";
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        tag = "[VK-WARN ] ";
    }

    logger::warn("%s%s", tag, callbackData->pMessage);

    return VK_FALSE;
}

VulkanContext::~VulkanContext()
{
    destroy();
}

void VulkanContext::createInstance(bool enableValidation)
{
    mEnableValidation = enableValidation;

    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "Vulkan Ray Tracer";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Custom";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions;
    getRequiredInstanceExtensions(extensions);

#ifdef VRAYT_DEBUG
    if (enableValidation)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
#endif

    std::vector<const char*> layers;
#ifdef VRAYT_DEBUG
    if (enableValidation)
    {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
#endif

    VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &mInstance));
    logger::info("VkInstance created.");
}

void VulkanContext::setupDebugMessenger(bool enableValidation)
{
#ifndef VRAYT_DEBUG
    (void)enableValidation;
#else
    if (!enableValidation)
    {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;

    VK_CHECK(CreateDebugUtilsMessengerEXT(mInstance, &info, nullptr, &mDebugMessenger));
    logger::info("Debug messenger created.");
#endif
}

void VulkanContext::createSurface(Window& window)
{
    GLFWwindow* glfwWindow = window.handle();
    VK_CHECK(glfwCreateWindowSurface(mInstance, glfwWindow, nullptr, &mSurface));
    logger::info("Surface created.");
}

static bool supportsRayTracing(VkPhysicalDevice device)
{
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, &accelerationFeatures };
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, &rayTracingFeatures };
    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, &bufferDeviceAddressFeatures };
    VkPhysicalDeviceVulkan12Features vulkan12Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &descriptorIndexingFeatures };
    VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &vulkan12Features };

    vkGetPhysicalDeviceFeatures2(device, &features);

    return accelerationFeatures.accelerationStructure &&
        rayTracingFeatures.rayTracingPipeline &&
        bufferDeviceAddressFeatures.bufferDeviceAddress &&
        descriptorIndexingFeatures.runtimeDescriptorArray &&
        descriptorIndexingFeatures.descriptorBindingPartiallyBound;
}

bool VulkanContext::checkDeviceExtensions(VkPhysicalDevice device) const
{
    static const char* required[] =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };

    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> properties(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, properties.data());

    std::set<std::string> needed(required, required + sizeof(required) / sizeof(required[0]));

    for (auto& property : properties)
    {
        needed.erase(property.extensionName);
    }

    return needed.empty();
}

QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& family = families[i];

        if (family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, mSurface, &presentSupport);

        if (presentSupport)
        {
            indices.presentFamily = i;
        }

        if (indices.isComplete())
        {
            break;
        }
    }

    return indices;
}

void VulkanContext::pickPhysicalDevice()
{
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(mInstance, &count, nullptr));

    if (count == 0)
    {
        throw std::runtime_error("No Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(count);
    VK_CHECK(vkEnumeratePhysicalDevices(mInstance, &count, devices.data()));

    for (auto device : devices)
    {
        if (!checkDeviceExtensions(device))
        {
            continue;
        }
        if (!supportsRayTracing(device))
        {
            continue;
        }

        QueueFamilyIndices indices = findQueueFamilies(device);

        if (!indices.isComplete())
        {
            continue;
        }

        mPhysical = device;
        mGraphicsFamilyIndex = indices.graphicsFamily.value();
        mPresentFamilyIndex = indices.presentFamily.value();

        break;
    }

    if (mPhysical == VK_NULL_HANDLE)
    {
        throw std::runtime_error("No suitable device found (ray tracing + swapchain).");
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mPhysical, &properties);
    logger::info("Physical device: %s", properties.deviceName);
}

void VulkanContext::createDevice()
{
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    accelerationFeatures.accelerationStructure = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
    rayTracingFeatures.rayTracingPipeline = VK_TRUE;
    rayTracingFeatures.pNext = &accelerationFeatures;

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
    bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
    bufferDeviceAddressFeatures.pNext = &rayTracingFeatures;

    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
    descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
    descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
    descriptorIndexingFeatures.pNext = &bufferDeviceAddressFeatures;

    VkPhysicalDeviceVulkan12Features vulkan12Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    vulkan12Features.bufferDeviceAddress = VK_TRUE;
    vulkan12Features.descriptorIndexing = VK_TRUE;
    vulkan12Features.pNext = &descriptorIndexingFeatures;

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    std::set<uint32_t> uniqueFamilies = { mGraphicsFamilyIndex, mPresentFamilyIndex };

    for (uint32_t index : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };

        queueCreateInfo.queueFamilyIndex = index;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueInfos.push_back(queueCreateInfo);
    }

    const char* extensions[] =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };

    std::vector<const char*> layers;
#ifdef VRAYT_DEBUG
    if (mEnableValidation)
    {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
#endif

    VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    deviceInfo.pQueueCreateInfos = queueInfos.data();
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(extensions));
    deviceInfo.ppEnabledExtensionNames = extensions;
    deviceInfo.pNext = &vulkan12Features;
    deviceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    deviceInfo.ppEnabledLayerNames = layers.data();

    VK_CHECK(vkCreateDevice(mPhysical, &deviceInfo, nullptr, &mDevice));
    vkGetDeviceQueue(mDevice, mGraphicsFamilyIndex, 0, &mGraphicsQueue);
    vkGetDeviceQueue(mDevice, mPresentFamilyIndex, 0, &mPresentQueue);
    logger::info("Logical device created.");
}

void VulkanContext::createAllocator()
{
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocInfo.instance = mInstance;
    allocInfo.physicalDevice = mPhysical;
    allocInfo.device = mDevice;
    allocInfo.pVulkanFunctions = &functions;
    VK_CHECK(vmaCreateAllocator(&allocInfo, &mAllocator));

    logger::info("VMA allocator created.");
}

void VulkanContext::createCommandPoolsAndBuffers(uint32_t framesInFlight)
{
    mFrames.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; ++i)
    {
        FrameSync& frameSync = mFrames[i];

        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.queueFamilyIndex = mGraphicsFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(mDevice, &poolInfo, nullptr, &frameSync.cmdPool));

        VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocInfo.commandPool = frameSync.cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(mDevice, &allocInfo, &frameSync.cmdBuf));
    }

    logger::info("Per-frame command pools & buffers created.");
}

void VulkanContext::createSyncObjects(uint32_t framesInFlight)
{
    for (uint32_t i = 0; i < framesInFlight; ++i)
    {
        FrameSync& frameSync = mFrames[i];

        VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &frameSync.imageAvailable));
        VK_CHECK(vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &frameSync.renderFinished));

        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(mDevice, &fenceInfo, nullptr, &frameSync.inFlight));
    }

    logger::info("Per-frame sync objects created.");
}

void VulkanContext::waitIdle() const
{
    VK_CHECK(vkDeviceWaitIdle(mDevice));
}

void VulkanContext::destroy()
{
    if (mDevice)
    {
        vkDeviceWaitIdle(mDevice);
    }

    for (auto& frame : mFrames)
    {
        if (frame.inFlight)
        {
            vkDestroyFence(mDevice, frame.inFlight, nullptr);
        }
        if (frame.renderFinished)
        {
            vkDestroySemaphore(mDevice, frame.renderFinished, nullptr);
        }
        if (frame.imageAvailable)
        {
            vkDestroySemaphore(mDevice, frame.imageAvailable, nullptr);
        }
        if (frame.cmdPool)
        {
            vkDestroyCommandPool(mDevice, frame.cmdPool, nullptr);
        }
    }

    mFrames.clear();

    if (mAllocator)
    {
        vmaDestroyAllocator(mAllocator);
        mAllocator = VK_NULL_HANDLE;
    }

    if (mDevice)
    {
        vkDestroyDevice(mDevice, nullptr);
        mDevice = VK_NULL_HANDLE;
    }

    if (mSurface)
    {
        vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
        mSurface = VK_NULL_HANDLE;
    }

#ifdef VRAYT_DEBUG
    if (mDebugMessenger)
    {
        DestroyDebugUtilsMessengerEXT(mInstance, mDebugMessenger, nullptr);
        mDebugMessenger = VK_NULL_HANDLE;
    }
#endif

    if (mInstance)
    {
        vkDestroyInstance(mInstance, nullptr);
        mInstance = VK_NULL_HANDLE;
    }
}

void VulkanContext::getRequiredInstanceExtensions(std::vector<const char*>& out) const
{
    uint32_t glfwCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);

    for (uint32_t i = 0; i < glfwCount; ++i)
    {
        out.push_back(glfwExtensions[i]);
    }
}

VkResult VulkanContext::CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    const VkAllocationCallbacks* allocator,
    VkDebugUtilsMessengerEXT* messenger)
{
    auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

    if (function)
    {
        return function(instance, createInfo, allocator, messenger);
    }

    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void VulkanContext::DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* allocator)
{
    auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

    if (function)
    {
        function(instance, messenger, allocator);
    }
}
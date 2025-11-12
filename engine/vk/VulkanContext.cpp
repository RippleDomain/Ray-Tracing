#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vma/vk_mem_alloc.h"

#include "VulkanContext.h"
#include "../core/Check.h"
#include "../core/Logger.h"
#include "../platform/Window.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <set>
#include <cstring>
#include <cassert>

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData)
{
    (void)types; (void)userData;
    const char* tag = "[VK] ";

    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) tag = "[VK-ERROR] ";
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) tag = "[VK-WARN ] ";

    logger::warn("%s%s", tag, callbackData->pMessage);

    return VK_FALSE;
}

VulkanContext::~VulkanContext() { destroy(); }

void VulkanContext::createInstance(bool enableValidation) 
{
    m_enableValidation = enableValidation;

    VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "Vulkan Ray Tracer";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "Custom";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions;
    getRequiredInstanceExtensions(extensions);
#ifdef VRAYT_DEBUG
    if (enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
#endif

    std::vector<const char*> layers;
#ifdef VRAYT_DEBUG
    if (enableValidation) layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();

    VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance));
    logger::info("VkInstance created");
}

void VulkanContext::setupDebugMessenger(bool enableValidation) 
{
#ifndef VRAYT_DEBUG
    (void)enableValidation;
#else
    if (!enableValidation) return;

    VkDebugUtilsMessengerCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT 
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT 
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = DebugCallback;

    VK_CHECK(CreateDebugUtilsMessengerEXT(m_instance, &info, nullptr, &m_debugMessenger));
    log::info("Debug messenger created");
#endif
}

void VulkanContext::createSurface(Window& window) 
{
    GLFWwindow* w = window.handle();
    VK_CHECK(glfwCreateWindowSurface(m_instance, w, nullptr, &m_surface));
    logger::info("Surface created");
}

static bool supportsRayTracing(VkPhysicalDevice device) 
{
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accel{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR    rt{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, &accel };
    VkPhysicalDeviceBufferDeviceAddressFeatures      bda{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, &rt };
    VkPhysicalDeviceDescriptorIndexingFeatures       di{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, &bda };
    VkPhysicalDeviceVulkan12Features                v12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &di };
    VkPhysicalDeviceFeatures2                        feat2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v12 };

    vkGetPhysicalDeviceFeatures2(device, &feat2);

    return accel.accelerationStructure &&
        rt.rayTracingPipeline &&
        bda.bufferDeviceAddress &&
        di.runtimeDescriptorArray &&
        di.descriptorBindingPartiallyBound;
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
    std::vector<VkExtensionProperties> props(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, props.data());

    std::set<std::string> needed(required, required + sizeof(required) / sizeof(required[0]));

    for (auto& p : props) needed.erase(p.extensionName);

    return needed.empty();
}

QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device) const 
{
    QueueFamilyIndices indices;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> fam(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, fam.data());

    for (uint32_t i = 0; i < count; ++i) 
    {
        const auto& f = fam[i];
        if (f.queueFlags & VK_QUEUE_GRAPHICS_BIT) indices.graphicsFamily = i;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) indices.presentFamily = i;

        if (indices.isComplete()) break;
    }

    return indices;
}

void VulkanContext::pickPhysicalDevice() 
{
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, nullptr));
    if (count == 0) throw std::runtime_error("No Vulkan physical devices found");
    std::vector<VkPhysicalDevice> devices(count);
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()));

    for (auto d : devices) 
    {
        if (!checkDeviceExtensions(d)) continue;
        if (!supportsRayTracing(d)) continue;

        QueueFamilyIndices q = findQueueFamilies(d);

        if (!q.isComplete()) continue;

        m_physical = d;
        m_graphicsFamilyIndex = q.graphicsFamily.value();
        m_presentFamilyIndex = q.presentFamily.value();

        break;
    }

    if (m_physical == VK_NULL_HANDLE) 
    {
        throw std::runtime_error("No suitable device found (ray tracing + swapchain)");
    }

    VkPhysicalDeviceProperties prop{};
    vkGetPhysicalDeviceProperties(m_physical, &prop);
    logger::info("Physical device: %s", prop.deviceName);
}

void VulkanContext::createDevice() 
{
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accel{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    accel.accelerationStructure = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
    rt.rayTracingPipeline = VK_TRUE;
    rt.pNext = &accel;

    VkPhysicalDeviceBufferDeviceAddressFeatures bda{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
    bda.bufferDeviceAddress = VK_TRUE;
    bda.pNext = &rt;

    VkPhysicalDeviceDescriptorIndexingFeatures di{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
    di.runtimeDescriptorArray = VK_TRUE;
    di.descriptorBindingPartiallyBound = VK_TRUE;
    di.pNext = &bda;

    VkPhysicalDeviceVulkan12Features v12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    v12.bufferDeviceAddress = VK_TRUE;
    v12.descriptorIndexing = VK_TRUE;
    v12.pNext = &di;

    float prio = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queues;
    std::set<uint32_t> unique = { m_graphicsFamilyIndex, m_presentFamilyIndex };

    for (uint32_t idx : unique) 
    {
        VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qci.queueFamilyIndex = idx;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;
        queues.push_back(qci);
    }

    const char* exts[] = 
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
    if (m_enableValidation) layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
    dci.pQueueCreateInfos = queues.data();
    dci.enabledExtensionCount = static_cast<uint32_t>(std::size(exts));
    dci.ppEnabledExtensionNames = exts;
    dci.pNext = &v12;
    dci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    dci.ppEnabledLayerNames = layers.data();

    VK_CHECK(vkCreateDevice(m_physical, &dci, nullptr, &m_device));
    vkGetDeviceQueue(m_device, m_graphicsFamilyIndex, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamilyIndex, 0, &m_presentQueue);
    logger::info("Logical device created");
}

void VulkanContext::createAllocator() 
{
    VmaVulkanFunctions fns{};
    fns.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    fns.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo info{};
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    info.instance = m_instance;
    info.physicalDevice = m_physical;
    info.device = m_device;
    info.pVulkanFunctions = &fns;
    VK_CHECK(vmaCreateAllocator(&info, &m_allocator));

    logger::info("VMA allocator created");
}

void VulkanContext::createCommandPoolsAndBuffers(uint32_t framesInFlight) 
{
    m_frames.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; ++i) 
    {
        FrameSync& fr = m_frames[i];

        VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pci.queueFamilyIndex = m_graphicsFamilyIndex;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(m_device, &pci, nullptr, &fr.cmdPool));

        VkCommandBufferAllocateInfo cai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cai.commandPool = fr.cmdPool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(m_device, &cai, &fr.cmdBuf));
    }

    logger::info("Per-frame command pools & buffers created");
}

void VulkanContext::createSyncObjects(uint32_t framesInFlight) 
{
    for (uint32_t i = 0; i < framesInFlight; ++i) 
    {
        FrameSync& fr = m_frames[i];

        VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(m_device, &sci, nullptr, &fr.imageAvailable));
        VK_CHECK(vkCreateSemaphore(m_device, &sci, nullptr, &fr.renderFinished));

        VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(m_device, &fci, nullptr, &fr.inFlight));
    }

    logger::info("Per-frame sync objects created");
}

void VulkanContext::waitIdle() const { VK_CHECK(vkDeviceWaitIdle(m_device)); }

void VulkanContext::destroy() 
{
    if (m_device) vkDeviceWaitIdle(m_device);

    for (auto& fr : m_frames) 
    {
        if (fr.inFlight)       vkDestroyFence(m_device, fr.inFlight, nullptr);
        if (fr.renderFinished) vkDestroySemaphore(m_device, fr.renderFinished, nullptr);
        if (fr.imageAvailable) vkDestroySemaphore(m_device, fr.imageAvailable, nullptr);
        if (fr.cmdPool)        vkDestroyCommandPool(m_device, fr.cmdPool, nullptr);
    }

    m_frames.clear();

    if (m_allocator) { vmaDestroyAllocator(m_allocator); m_allocator = VK_NULL_HANDLE; }

    if (m_device) { vkDestroyDevice(m_device, nullptr); m_device = VK_NULL_HANDLE; }

    if (m_surface) { vkDestroySurfaceKHR(m_instance, m_surface, nullptr); m_surface = VK_NULL_HANDLE; }

#ifdef VRAYT_DEBUG
    if (m_debugMessenger) {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
#endif

    if (m_instance) { vkDestroyInstance(m_instance, nullptr); m_instance = VK_NULL_HANDLE; }
}

void VulkanContext::getRequiredInstanceExtensions(std::vector<const char*>& out) const 
{
    uint32_t glfwCount = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);

    for (uint32_t i = 0; i < glfwCount; ++i) out.push_back(glfwExt[i]);
}

VkResult VulkanContext::CreateDebugUtilsMessengerEXT(
    VkInstance inst, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pMessenger)
{
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT"));

    if (fn) return fn(inst, pCreateInfo, pAllocator, pMessenger);

    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void VulkanContext::DestroyDebugUtilsMessengerEXT(VkInstance inst, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* pAllocator)
{
    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(inst, "vkDestroyDebugUtilsMessengerEXT"));

    if (fn) fn(inst, messenger, pAllocator);
}
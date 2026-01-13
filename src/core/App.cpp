#include "App.h"

#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cmath>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "../util/Logger.h"
#include "../util/Timer.h"
#include "../util/Check.h"
#include "../platform/Window.h"
#include "../vk/VulkanContext.h"
#include "../vk/Swapchain.h"
#include "../rt/RayTracer.h"

static const uint32_t windowWidth = 1280;
static const uint32_t windowHeight = 720;
static const uint32_t maxFramesInFlight = 2;
static const uint32_t imguiMinImageCount = 2;

static VkDescriptorPool createImguiPool(VkDevice device)
{
    VkDescriptorPoolSize poolSizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    VkDescriptorPool pool = VK_NULL_HANDLE;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create ImGui descriptor pool");
    }

    return pool;
}

static void imguiInit(Window& window, VulkanContext& vulkanContext, Swapchain& swapchain, VkDescriptorPool imguiPool)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window.handle(), true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = vulkanContext.instance();
    initInfo.PhysicalDevice = vulkanContext.physical();
    initInfo.Device = vulkanContext.device();
    initInfo.QueueFamily = vulkanContext.graphicsFamilyIndex();
    initInfo.Queue = vulkanContext.graphicsQueue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = imguiPool;
    initInfo.MinImageCount = std::max<uint32_t>(imguiMinImageCount, static_cast<uint32_t>(swapchain.bundle().images.size()));
    initInfo.ImageCount = static_cast<uint32_t>(swapchain.bundle().images.size());
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.PipelineInfoMain.RenderPass = swapchain.bundle().renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = false;
    initInfo.CheckVkResultFn = nullptr;

    ImGui_ImplVulkan_Init(&initInfo);
}

static void imguiShutdown(VkDevice device, VkDescriptorPool pool)
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(device, pool, nullptr);
}

int App::run()
{
    try
    {
        // Window.
        Window window;

        if (!window.create(windowWidth, windowHeight, "Vulkan Ray Tracer"))
        {
            return EXIT_FAILURE;
        }

        // Vulkan core.
        VulkanContext vulkanContext;
        vulkanContext.createInstance(true);
        vulkanContext.setupDebugMessenger(true);
        vulkanContext.createSurface(window);
        vulkanContext.pickPhysicalDevice();
        vulkanContext.createDevice();
        vulkanContext.createAllocator();
        vulkanContext.createCommandPoolsAndBuffers(maxFramesInFlight);
        vulkanContext.createSyncObjects(maxFramesInFlight);

        // Swapchain.
        Swapchain swapchain;
        swapchain.create(vulkanContext, window);

        // ImGui.
        VkDescriptorPool imguiPool = createImguiPool(vulkanContext.device());
        imguiInit(window, vulkanContext, swapchain, imguiPool);

        // Ray tracer.
        RayTracer tracer;
        tracer.create(vulkanContext, swapchain);
        tracer.setSamplesPerPixel(4);
        tracer.setAperture(0.05f);

        uint32_t currentFrame = 0;
        uint32_t sampleFrame = 0;
        Timer fpsTimer;
        double fpsTimeAcc = 0.0;
        int fpsFrames = 0;
        Timer frameTimer;

        // Camera state.
        glm::vec3 camPos{ 13.0f, 2.0f, 3.0f };
        glm::vec3 camDir = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f) - camPos);
        float yaw = glm::degrees(std::atan2(camDir.z, camDir.x));
        float pitch = glm::degrees(std::asin(camDir.y));
        double lastX = windowWidth * 0.5;
        double lastY = windowHeight * 0.5;
        bool firstMouse = true;
        bool cursorCaptured = true;
        window.setCursorMode(GLFW_CURSOR_DISABLED);
        bool cameraPaused = false;
        bool escPrev = false;

        // UI state.
        int uiSpp = 4;
        float uiAperture = 0.05f;
        float uiFocusDist = glm::length(glm::vec3(0.0f, 1.0f, 0.0f) - camPos);
        float uiFov = 20.0f;
        int uiMaxDepth = 12;

        while (!window.shouldClose())
        {
            window.poll();
            double deltaTime = frameTimer.elapsedSeconds();
            frameTimer.reset();
            bool camChanged = false;

            // Toggle pause with ESC.
            bool escPressed = window.keyState(GLFW_KEY_ESCAPE) == GLFW_PRESS;

            if (escPressed && !escPrev)
            {
                cameraPaused = !cameraPaused;

                if (cameraPaused)
                {
                    window.setCursorMode(GLFW_CURSOR_NORMAL);
                    cursorCaptured = false;
                    firstMouse = true;
                }
                else if (!ImGui::GetIO().WantCaptureMouse)
                {
                    window.setCursorMode(GLFW_CURSOR_DISABLED);
                    cursorCaptured = true;
                    firstMouse = true;
                }
            }

            escPrev = escPressed;

            // Handle resize (recreate swapchain on demand).
            if (window.framebufferResized())
            {
                window.clearFramebufferResized();
                swapchain.recreate(vulkanContext, window);
                tracer.resize(vulkanContext, swapchain);
                sampleFrame = 0;

                continue;
            }

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGuiIO& imguiIo = ImGui::GetIO();
            bool uiWantsMouse = imguiIo.WantCaptureMouse;
            bool uiWantsKeyboard = imguiIo.WantCaptureKeyboard;

            // Cursor capture toggle based on UI focus or pause.
            if ((uiWantsMouse || cameraPaused) && cursorCaptured)
            {
                window.setCursorMode(GLFW_CURSOR_NORMAL);
                cursorCaptured = false;
            }
            else if (!uiWantsMouse && !cameraPaused && !cursorCaptured)
            {
                window.setCursorMode(GLFW_CURSOR_DISABLED);
                cursorCaptured = true;
                firstMouse = true;
            }

            // Mouse look.
            if (cursorCaptured && !cameraPaused)
            {
                double cursorX = 0.0;
                double cursorY = 0.0;
                window.getCursorPos(cursorX, cursorY);

                if (firstMouse)
                {
                    lastX = cursorX;
                    lastY = cursorY;
                    firstMouse = false;
                }

                double deltaX = cursorX - lastX;
                double deltaY = lastY - cursorY;
                lastX = cursorX;
                lastY = cursorY;
                const float sensitivity = 0.1f;

                if (deltaX != 0.0 || deltaY != 0.0)
                {
                    yaw += static_cast<float>(deltaX) * sensitivity;
                    pitch += static_cast<float>(deltaY) * sensitivity;
                    pitch = std::clamp(pitch, -89.0f, 89.0f);
                    float yawRad = glm::radians(yaw);
                    float pitchRad = glm::radians(pitch);

                    glm::vec3 newDir
                    {
                        cosf(pitchRad) * cosf(yawRad),
                        sinf(pitchRad),
                        cosf(pitchRad) * sinf(yawRad)
                    };

                    camDir = glm::normalize(newDir);
                    camChanged = true;
                }
            }

            // Keyboard move.
            if (!uiWantsKeyboard && !cameraPaused)
            {
                glm::vec3 right = glm::normalize(glm::cross(camDir, glm::vec3(0.0f, 1.0f, 0.0f)));
                glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
                const float moveSpeed = 5.0f;

                if (window.keyState(GLFW_KEY_W) == GLFW_PRESS)
                {
                    camPos += camDir * moveSpeed * static_cast<float>(deltaTime);
                    camChanged = true;
                }
                if (window.keyState(GLFW_KEY_S) == GLFW_PRESS)
                {
                    camPos -= camDir * moveSpeed * static_cast<float>(deltaTime);
                    camChanged = true;
                }
                if (window.keyState(GLFW_KEY_A) == GLFW_PRESS)
                {
                    camPos -= right * moveSpeed * static_cast<float>(deltaTime);
                    camChanged = true;
                }
                if (window.keyState(GLFW_KEY_D) == GLFW_PRESS)
                {
                    camPos += right * moveSpeed * static_cast<float>(deltaTime);
                    camChanged = true;
                }
                if (window.keyState(GLFW_KEY_SPACE) == GLFW_PRESS)
                {
                    camPos += up * moveSpeed * static_cast<float>(deltaTime);
                    camChanged = true;
                }
                if (window.keyState(GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
                {
                    camPos -= up * moveSpeed * static_cast<float>(deltaTime);
                    camChanged = true;
                }
            }

            if (camChanged)
            {
                tracer.setCamera(camPos, camDir);
                sampleFrame = 0;
            }

            auto& frameSync = vulkanContext.frames()[currentFrame];

            // Wait for GPU.
            VK_CHECK(vkWaitForFences(vulkanContext.device(), 1, &frameSync.inFlight, VK_TRUE, UINT64_MAX));
            VK_CHECK(vkResetFences(vulkanContext.device(), 1, &frameSync.inFlight));

            uint32_t imageIndex = 0;
            VkResult acquireResult = swapchain.acquireNextImage(vulkanContext, frameSync.imageAvailable, &imageIndex);

            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
            {
                swapchain.recreate(vulkanContext, window);
                tracer.resize(vulkanContext, swapchain);
                sampleFrame = 0;

                continue;
            }

            VK_CHECK(acquireResult);

            VK_CHECK(vkResetCommandBuffer(frameSync.cmdBuf, 0));
            VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            VK_CHECK(vkBeginCommandBuffer(frameSync.cmdBuf, &beginInfo));

            tracer.render(vulkanContext, swapchain, frameSync.cmdBuf, imageIndex, sampleFrame);

            // Overlay.
            ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.35f);
            ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("Overlay", nullptr, overlayFlags))
            {
                ImGui::Text("FPS: %.1f", fpsFrames / std::max(0.0001, fpsTimeAcc));
                ImGui::Text("Press ESC to pause camera for UI");
            }

            ImGui::End();

            // Settings.
            ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
            ImGui::Begin("Ray Tracer");

            if (ImGui::SliderInt("Samples", &uiSpp, 1, 32))
            {
                tracer.setSamplesPerPixel(static_cast<uint32_t>(uiSpp));
                sampleFrame = 0;
            }
            if (ImGui::SliderFloat("Aperture", &uiAperture, 0.0f, 0.2f, "%.3f"))
            {
                tracer.setAperture(uiAperture);
                sampleFrame = 0;
            }
            if (ImGui::SliderFloat("Focus Dist", &uiFocusDist, 0.1f, 50.0f, "%.2f"))
            {
                tracer.setFocusDistance(uiFocusDist);
                sampleFrame = 0;
            }
            if (ImGui::SliderFloat("FOV", &uiFov, 10.0f, 90.0f, "%.1f"))
            {
                tracer.setFov(uiFov);
                sampleFrame = 0;
            }
            if (ImGui::SliderInt("Max Depth", &uiMaxDepth, 1, 64))
            {
                tracer.setMaxDepth(static_cast<uint32_t>(uiMaxDepth));
                sampleFrame = 0;
            }

            ImGui::End();

            ImGui::Render();

            const auto& swapchainBundle = swapchain.bundle();
            VkRenderPassBeginInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            renderPassInfo.renderPass = swapchainBundle.renderPass;
            renderPassInfo.framebuffer = swapchainBundle.framebuffers[imageIndex];
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = swapchainBundle.extent;
            renderPassInfo.clearValueCount = 0;
            renderPassInfo.pClearValues = nullptr;

            vkCmdBeginRenderPass(frameSync.cmdBuf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frameSync.cmdBuf);
            vkCmdEndRenderPass(frameSync.cmdBuf);

            VK_CHECK(vkEndCommandBuffer(frameSync.cmdBuf));

            // Submit.
            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &frameSync.imageAvailable;
            submitInfo.pWaitDstStageMask = &waitStage;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &frameSync.cmdBuf;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &frameSync.renderFinished;

            VK_CHECK(vkQueueSubmit(vulkanContext.graphicsQueue(), 1, &submitInfo, frameSync.inFlight));

            // Present.
            VkResult presentResult = swapchain.present(vulkanContext, frameSync.renderFinished, imageIndex);

            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
            {
                swapchain.recreate(vulkanContext, window);
                tracer.resize(vulkanContext, swapchain);
                sampleFrame = 0;
            }
            else
            {
                VK_CHECK(presentResult);
            }

            // FPS.
            fpsTimeAcc += fpsTimer.elapsedSeconds();
            ++fpsFrames;

            if (fpsTimeAcc >= 1.0)
            {
                logger::info("FPS: %d", fpsFrames);
                fpsFrames = 0;
                fpsTimeAcc = 0.0;
            }

            fpsTimer.reset();

            currentFrame = (currentFrame + 1) % maxFramesInFlight;
            ++sampleFrame;
        }

        vulkanContext.waitIdle();
        imguiShutdown(vulkanContext.device(), imguiPool);
        tracer.destroy(vulkanContext);
        swapchain.destroy(vulkanContext);
        vulkanContext.destroy();
        window.destroy();
    }
    catch (const std::exception& error)
    {
        logger::error("Fatal: %s", error.what());

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
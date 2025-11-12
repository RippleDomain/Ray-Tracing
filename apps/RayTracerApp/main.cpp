#include <stdexcept>
#include <vector>
#include <cstdio>
#include <cstdlib>

#include "../../engine/core/Logger.h"
#include "../../engine/core/Timer.h"
#include "../../engine/platform/Window.h"
#include "../../engine/vk/VulkanContext.h"
#include "../../engine/vk/Swapchain.h"
#include "../../engine/core/Check.h"

static const uint32_t WIDTH = 1280;
static const uint32_t HEIGHT = 720;
static const uint32_t MAX_FRAMES_IN_FLIGHT = 2;

int main() 
{
    try 
    {
        //--- Window ---
        Window window;
        if (!window.create(WIDTH, HEIGHT, "Vulkan Ray Tracer")) return EXIT_FAILURE;

        //--- Vulkan base ---
        VulkanContext vk;
        vk.createInstance(true);
        vk.setupDebugMessenger(true);
        vk.createSurface(window);
        vk.pickPhysicalDevice();
        vk.createDevice();
        vk.createAllocator();
        vk.createCommandPoolsAndBuffers(MAX_FRAMES_IN_FLIGHT);
        vk.createSyncObjects(MAX_FRAMES_IN_FLIGHT);

        //--- Swapchain ---
        Swapchain swapchain;
        swapchain.create(vk, window);

        uint32_t currentFrame = 0;
        Timer fpsTimer; double fpsTimeAcc = 0.0; int fpsFrames = 0;

        while (!window.shouldClose()) 
        {
            window.poll();

            //Handle resize (recreate swapchain on demand)
            if (window.framebufferResized()) 
            {
                window.clearFramebufferResized();
                swapchain.recreate(vk, window);
            }

            auto& fr = vk.frames()[currentFrame];

            //Wait for GPU to finish with this frame.
            VK_CHECK(vkWaitForFences(vk.device(), 1, &fr.inFlight, VK_TRUE, UINT64_MAX));
            VK_CHECK(vkResetFences(vk.device(), 1, &fr.inFlight));

            uint32_t imageIndex = 0;
            VkResult acq = swapchain.acquireNextImage(vk, fr.imageAvailable, &imageIndex);

            if (acq == VK_ERROR_OUT_OF_DATE_KHR) 
            {
                swapchain.recreate(vk, window);
                continue;
            }

            VK_CHECK(acq);

            VK_CHECK(vkResetCommandBuffer(fr.cmdBuf, 0));
            VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            VK_CHECK(vkBeginCommandBuffer(fr.cmdBuf, &bi));

            VkClearValue clear{};
            clear.color = { { 0.05f, 0.06f, 0.07f, 1.0f } };

            const auto& scb = swapchain.bundle();
            VkRenderPassBeginInfo rbi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            rbi.renderPass = scb.renderPass;
            rbi.framebuffer = scb.framebuffers[imageIndex];
            rbi.renderArea.offset = { 0, 0 };
            rbi.renderArea.extent = scb.extent;
            rbi.clearValueCount = 1;
            rbi.pClearValues = &clear;

            vkCmdBeginRenderPass(fr.cmdBuf, &rbi, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdEndRenderPass(fr.cmdBuf);

            VK_CHECK(vkEndCommandBuffer(fr.cmdBuf));

            //Submit.
            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &fr.imageAvailable;
            si.pWaitDstStageMask = &waitStage;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &fr.cmdBuf;
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &fr.renderFinished;

            VK_CHECK(vkQueueSubmit(vk.graphicsQueue(), 1, &si, fr.inFlight));

            //Present.
            VkResult pres = swapchain.present(vk, fr.renderFinished, imageIndex);

            if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) 
            {
                swapchain.recreate(vk, window);
            }
            else 
            {
                VK_CHECK(pres);
            }

            //FPS.
            fpsTimeAcc += 0.0;
            ++fpsFrames;

            if (fpsFrames % 120 == 0) 
            {
                logger::info("Frame #%d OK", fpsFrames);
            }

            currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        }

        vk.waitIdle();
        swapchain.destroy(vk);
        vk.destroy();
        window.destroy();
    }
    catch (const std::exception& e) 
    {
        logger::error("Fatal: %s", e.what());

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
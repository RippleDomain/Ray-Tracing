#pragma once

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <string>
#include <sstream>

inline void vkCheck(VkResult result, const char* expr, const char* file, int line)
{
    if (result != VK_SUCCESS)
    {
        std::ostringstream messageStream;
        messageStream << "VkResult(" << result << ") for " << expr << " at " << file << ":" << line;

        throw std::runtime_error(messageStream.str());
    }
}

#define VK_CHECK(x) vkCheck((x), #x, __FILE__, __LINE__)
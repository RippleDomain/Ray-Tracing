#pragma once

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <string>
#include <sstream>

inline void vkCheck(VkResult res, const char* expr, const char* file, int line) 
{
    if (res != VK_SUCCESS) 
    {
        std::ostringstream oss;
        oss << "VkResult(" << res << ") for " << expr << " at " << file << ":" << line;

        throw std::runtime_error(oss.str());
    }
}

#define VK_CHECK(x) vkCheck((x), #x, __FILE__, __LINE__)
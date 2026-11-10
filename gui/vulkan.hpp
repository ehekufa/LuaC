#pragma once
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include "geometry.hpp"

class VulkanRenderer {
public:
    VulkanRenderer() = default;
    ~VulkanRenderer();
    void initialize(GLFWwindow *window, const std::string &shaderDirectory);
    void render(const std::vector<LcVertex> &vertices, const std::vector<LcDraw> &draws,
                int pixelWidth, int pixelHeight);
    void shutdown();

private:
    GLFWwindow *window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    void *vertexMapped_ = nullptr;
    VkDeviceSize vertexCapacity_ = 0;
    VkSemaphore acquireSemaphore_ = VK_NULL_HANDLE;
    VkSemaphore presentSemaphore_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    std::string shaders_;

    void createInstance();
    void selectDevice();
    void createSwapchain();
    void destroySwapchain();
    void createRenderPassAndPipeline();
    void createFrameResources();
    void recreateSwapchain();
    std::vector<char> readShader(const char *name) const;
};

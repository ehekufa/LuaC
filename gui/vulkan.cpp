#include "vulkan.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
void vk(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
void vk(VkResult result, const char *message) {
    if (result != VK_SUCCESS) throw std::runtime_error(message);
}
VkShaderModule shaderModule(VkDevice device, const std::vector<char> &bytes) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes.size();
    info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
    VkShaderModule module;
    vk(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}
}

std::vector<char> VulkanRenderer::readShader(const char *name) const {
    std::string path = shaders_ + "/" + name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open shader: " + path);
    std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<char> bytes(static_cast<size_t>(size));
    file.read(bytes.data(), size);
    return bytes;
}

void VulkanRenderer::createInstance() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "LuaC Studio";
    app.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
    app.pEngineName = "LuaC";
    app.engineVersion = VK_MAKE_VERSION(0, 2, 0);
    app.apiVersion = VK_API_VERSION_1_1;
    uint32_t count = 0;
    const char **extensions = glfwGetRequiredInstanceExtensions(&count);
    std::vector<const char *> enabled(extensions, extensions + count);
#ifdef __APPLE__
    enabled.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
    info.ppEnabledExtensionNames = enabled.data();
#ifdef __APPLE__
    info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    vk(vkCreateInstance(&info, nullptr, &instance_), "Vulkan instance is unavailable");
    vk(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_), "Vulkan surface creation failed");
}

void VulkanRenderer::selectDevice() {
    uint32_t count = 0;
    vk(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices");
    if (!count) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    for (VkPhysicalDevice candidate : devices) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t i = 0; i < familyCount; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                uint32_t extensionCount = 0;
                vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
                std::vector<VkExtensionProperties> extensions(extensionCount);
                vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data());
                bool swapchain = false;
                for (auto &extension : extensions)
                    if (!std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) swapchain = true;
                if (!swapchain) continue;
                physical_ = candidate;
                queueFamily_ = i;
                break;
            }
        }
        if (physical_ != VK_NULL_HANDLE) break;
    }
    if (physical_ == VK_NULL_HANDLE) throw std::runtime_error("no graphics/present Vulkan queue");
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = queueFamily_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    const char *extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &extension;
    vk(vkCreateDevice(physical_, &info, nullptr, &device_), "Vulkan logical device creation failed");
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
}

void VulkanRenderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &capabilities), "surface capabilities");
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data());
    if (!count) throw std::runtime_error("no Vulkan surface formats");
    VkSurfaceFormatKHR selected = formats[0];
    for (auto &format : formats)
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM) { selected = format; break; }
    format_ = selected.format;
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    extent_ = capabilities.currentExtent;
    if (extent_.width == UINT32_MAX) {
        extent_.width = std::max(capabilities.minImageExtent.width,
            std::min(capabilities.maxImageExtent.width, static_cast<uint32_t>(std::max(width, 1))));
        extent_.height = std::max(capabilities.minImageExtent.height,
            std::min(capabilities.maxImageExtent.height, static_cast<uint32_t>(std::max(height, 1))));
    }
    uint32_t imageCount = std::max(2u, capabilities.minImageCount + 1);
    if (capabilities.maxImageCount && imageCount > capabilities.maxImageCount) imageCount = capabilities.maxImageCount;
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = format_;
    info.imageColorSpace = selected.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    vk(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "Vulkan swapchain creation failed");
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());
    views_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = images_[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format_;
        view.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vk(vkCreateImageView(device_, &view, nullptr, &views_[i]), "Vulkan image view creation failed");
    }
}

void VulkanRenderer::createRenderPassAndPipeline() {
    VkAttachmentDescription attachment{};
    attachment.format = format_;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render.attachmentCount = 1;
    render.pAttachments = &attachment;
    render.subpassCount = 1;
    render.pSubpasses = &subpass;
    render.dependencyCount = 1;
    render.pDependencies = &dependency;
    vk(vkCreateRenderPass(device_, &render, nullptr, &renderPass_), "Vulkan render pass creation failed");
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    vk(vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout_), "Vulkan pipeline layout failed");
    auto vertBytes = readShader("shader.vert.spv");
    auto fragBytes = readShader("shader.frag.spv");
    VkShaderModule vert = shaderModule(device_, vertBytes);
    VkShaderModule frag = shaderModule(device_, fragBytes);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binding{0, sizeof(LcVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(LcVertex, x)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LcVertex, r)};
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 2;
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0, 0, 1, 1, 0, 1};
    VkRect2D scissor{{0, 0}, {1, 1}};
    VkPipelineViewportStateCreateInfo view{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    view.viewportCount = 1;
    view.pViewports = &viewport;
    view.scissorCount = 1;
    view.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &view;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamic;
    info.layout = pipelineLayout_;
    info.renderPass = renderPass_;
    info.subpass = 0;
    vk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_), "Vulkan graphics pipeline failed");
    vkDestroyShaderModule(device_, frag, nullptr);
    vkDestroyShaderModule(device_, vert, nullptr);
}

void VulkanRenderer::createFrameResources() {
    framebuffers_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = renderPass_;
        info.attachmentCount = 1;
        info.pAttachments = &views_[i];
        info.width = extent_.width;
        info.height = extent_.height;
        info.layers = 1;
        vk(vkCreateFramebuffer(device_, &info, nullptr, &framebuffers_[i]), "Vulkan framebuffer creation failed");
    }
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queueFamily_;
    vk(vkCreateCommandPool(device_, &pool, nullptr, &commandPool_), "Vulkan command pool failed");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = commandPool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    vk(vkAllocateCommandBuffers(device_, &allocate, &commandBuffer_), "Vulkan command buffer failed");
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vk(vkCreateSemaphore(device_, &semaphore, nullptr, &acquireSemaphore_), "Vulkan semaphore failed");
    vk(vkCreateSemaphore(device_, &semaphore, nullptr, &presentSemaphore_), "Vulkan semaphore failed");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vk(vkCreateFence(device_, &fence, nullptr, &fence_), "Vulkan fence failed");
    vertexCapacity_ = sizeof(LcVertex) * 200000;
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = vertexCapacity_;
    buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk(vkCreateBuffer(device_, &buffer, nullptr, &vertexBuffer_), "Vulkan vertex buffer failed");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device_, vertexBuffer_, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical_, &properties);
    uint32_t memoryType = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((requirements.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & wanted) == wanted) {
            memoryType = i; break;
        }
    }
    if (memoryType == UINT32_MAX) throw std::runtime_error("no host-visible Vulkan memory");
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory.allocationSize = requirements.size;
    memory.memoryTypeIndex = memoryType;
    vk(vkAllocateMemory(device_, &memory, nullptr, &vertexMemory_), "Vulkan vertex memory failed");
    vkBindBufferMemory(device_, vertexBuffer_, vertexMemory_, 0);
    vkMapMemory(device_, vertexMemory_, 0, vertexCapacity_, 0, &vertexMapped_);
}

void VulkanRenderer::initialize(GLFWwindow *window, const std::string &shaderDirectory) {
    window_ = window;
    shaders_ = shaderDirectory;
    createInstance();
    selectDevice();
    createSwapchain();
    createRenderPassAndPipeline();
    createFrameResources();
}

void VulkanRenderer::destroySwapchain() {
    for (auto framebuffer : framebuffers_) vkDestroyFramebuffer(device_, framebuffer, nullptr);
    for (auto view : views_) vkDestroyImageView(device_, view, nullptr);
    framebuffers_.clear(); views_.clear(); images_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void VulkanRenderer::recreateSwapchain() {
    int width = 0, height = 0;
    do { glfwGetFramebufferSize(window_, &width, &height); glfwWaitEvents(); } while ((width == 0 || height == 0) && !glfwWindowShouldClose(window_));
    vkDeviceWaitIdle(device_);
    destroySwapchain();
    createSwapchain();
    framebuffers_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = renderPass_;
        info.attachmentCount = 1;
        info.pAttachments = &views_[i];
        info.width = extent_.width; info.height = extent_.height; info.layers = 1;
        vkCreateFramebuffer(device_, &info, nullptr, &framebuffers_[i]);
    }
}

void VulkanRenderer::render(const std::vector<LcVertex> &vertices, int pixelWidth, int pixelHeight) {
    if (!device_ || vertices.empty() || pixelWidth <= 0 || pixelHeight <= 0) return;
    if (vertices.size() * sizeof(LcVertex) > vertexCapacity_) throw std::runtime_error("too many GUI vertices");
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &fence_);
    uint32_t image = 0;
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquireSemaphore_, VK_NULL_HANDLE, &image);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) throw std::runtime_error("Vulkan acquire failed");
    std::memcpy(vertexMapped_, vertices.data(), vertices.size() * sizeof(LcVertex));
    vkResetCommandBuffer(commandBuffer_, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer_, &begin);
    VkClearValue clear{};
    clear.color = {{0.027f, 0.035f, 0.055f, 1.0f}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = renderPass_;
    pass.framebuffer = framebuffers_[image];
    pass.renderArea = {{0, 0}, extent_};
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer_, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkViewport viewport{0, 0, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
    float size[2]{static_cast<float>(pixelWidth), static_cast<float>(pixelHeight)};
    vkCmdPushConstants(commandBuffer_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(size), size);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &vertexBuffer_, &offset);
    vkCmdDraw(commandBuffer_, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer_);
    vkEndCommandBuffer(commandBuffer_);
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquireSemaphore_;
    submit.pWaitDstStageMask = &wait;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &presentSemaphore_;
    vk(vkQueueSubmit(queue_, 1, &submit, fence_), "Vulkan submit failed");
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &presentSemaphore_;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &image;
    result = vkQueuePresentKHR(queue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) recreateSwapchain();
}

void VulkanRenderer::shutdown() {
    if (device_) vkDeviceWaitIdle(device_);
    if (vertexMapped_) vkUnmapMemory(device_, vertexMemory_);
    if (vertexMemory_) vkFreeMemory(device_, vertexMemory_, nullptr);
    if (vertexBuffer_) vkDestroyBuffer(device_, vertexBuffer_, nullptr);
    if (fence_) vkDestroyFence(device_, fence_, nullptr);
    if (presentSemaphore_) vkDestroySemaphore(device_, presentSemaphore_, nullptr);
    if (acquireSemaphore_) vkDestroySemaphore(device_, acquireSemaphore_, nullptr);
    if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
    destroySwapchain();
    if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
    *this = VulkanRenderer();
}

VulkanRenderer::~VulkanRenderer() { shutdown(); }

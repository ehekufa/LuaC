#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <cstring>
#include <stdexcept>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "LuaCVulkan", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "LuaCVulkan", __VA_ARGS__)

namespace {
struct Renderer {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commands;
    VkSemaphore acquire = VK_NULL_HANDLE;
    VkSemaphore present = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
} r;

void check(VkResult result, const char *message) {
    if (result != VK_SUCCESS) throw std::runtime_error(message);
}

void createInstance(ANativeWindow *window) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "LuaC Studio";
    app.apiVersion = VK_API_VERSION_1_0;
    const char *extensions[]{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = extensions;
    check(vkCreateInstance(&info, nullptr, &r.instance), "vkCreateInstance");
    VkAndroidSurfaceCreateInfoKHR surface{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    surface.window = window;
    check(vkCreateAndroidSurfaceKHR(r.instance, &surface, nullptr, &r.surface), "vkCreateAndroidSurfaceKHR");
}

void createDevice() {
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(r.instance, &count, nullptr), "vkEnumeratePhysicalDevices");
    if (!count) throw std::runtime_error("No Vulkan device");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(r.instance, &count, devices.data());
    for (VkPhysicalDevice candidate : devices) {
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> properties(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, properties.data());
        for (uint32_t i = 0; i < families; ++i) {
            VkBool32 supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, r.surface, &supported);
            if (supported && (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                r.physical = candidate; r.family = i; break;
            }
        }
        if (r.physical != VK_NULL_HANDLE) break;
    }
    if (r.physical == VK_NULL_HANDLE) throw std::runtime_error("No present queue");
    float priority = 1;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = r.family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    const char *extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1; info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = &extension;
    check(vkCreateDevice(r.physical, &info, nullptr, &r.device), "vkCreateDevice");
    vkGetDeviceQueue(r.device, r.family, 0, &r.queue);
}

void createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r.physical, r.surface, &capabilities), "Surface capabilities");
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(r.physical, r.surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(r.physical, r.surface, &count, formats.data());
    if (!count) throw std::runtime_error("No surface formats");
    VkSurfaceFormatKHR selected = formats[0];
    for (auto &f : formats) if (f.format == VK_FORMAT_R8G8B8A8_UNORM) { selected = f; break; }
    r.format = selected.format;
    r.extent = capabilities.currentExtent;
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && imageCount > capabilities.maxImageCount) imageCount = capabilities.maxImageCount;
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = r.surface; info.minImageCount = imageCount;
    info.imageFormat = selected.format; info.imageColorSpace = selected.colorSpace;
    info.imageExtent = r.extent; info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR; info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(r.device, &info, nullptr, &r.swapchain), "vkCreateSwapchainKHR");
    vkGetSwapchainImagesKHR(r.device, r.swapchain, &count, nullptr);
    r.images.resize(count);
    vkGetSwapchainImagesKHR(r.device, r.swapchain, &count, r.images.data());
    r.views.resize(count); r.framebuffers.resize(count); r.commands.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = r.images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = r.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(r.device, &view, nullptr, &r.views[i]), "vkCreateImageView");
    }
    VkAttachmentDescription attachment{};
    attachment.format = r.format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 1; pass.pAttachments = &attachment; pass.subpassCount = 1; pass.pSubpasses = &subpass;
    check(vkCreateRenderPass(r.device, &pass, nullptr, &r.renderPass), "vkCreateRenderPass");
    for (size_t i = 0; i < r.images.size(); ++i) {
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = r.renderPass; framebuffer.attachmentCount = 1;
        framebuffer.pAttachments = &r.views[i]; framebuffer.width = r.extent.width;
        framebuffer.height = r.extent.height; framebuffer.layers = 1;
        check(vkCreateFramebuffer(r.device, &framebuffer, nullptr, &r.framebuffers[i]), "vkCreateFramebuffer");
    }
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex = r.family;
    check(vkCreateCommandPool(r.device, &pool, nullptr, &r.pool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = r.pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = static_cast<uint32_t>(r.commands.size());
    check(vkAllocateCommandBuffers(r.device, &allocate, r.commands.data()), "vkAllocateCommandBuffers");
    for (size_t i = 0; i < r.commands.size(); ++i) {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(r.commands[i], &begin);
        VkClearValue clear{}; clear.color = {{0.025f, 0.15f, 0.18f, 1.0f}};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = r.renderPass; rp.framebuffer = r.framebuffers[i]; rp.renderArea = {{0, 0}, r.extent};
        rp.clearValueCount = 1; rp.pClearValues = &clear;
        vkCmdBeginRenderPass(r.commands[i], &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(r.commands[i]);
        check(vkEndCommandBuffer(r.commands[i]), "vkEndCommandBuffer");
    }
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(r.device, &semaphore, nullptr, &r.acquire);
    vkCreateSemaphore(r.device, &semaphore, nullptr, &r.present);
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(r.device, &fence, nullptr, &r.fence);
}

void initialize(ANativeWindow *window) {
    createInstance(window);
    createDevice();
    createSwapchain();
}

void destroy() {
    if (r.device) vkDeviceWaitIdle(r.device);
    if (r.fence) vkDestroyFence(r.device, r.fence, nullptr);
    if (r.acquire) vkDestroySemaphore(r.device, r.acquire, nullptr);
    if (r.present) vkDestroySemaphore(r.device, r.present, nullptr);
    if (r.pool) vkDestroyCommandPool(r.device, r.pool, nullptr);
    for (auto framebuffer : r.framebuffers) vkDestroyFramebuffer(r.device, framebuffer, nullptr);
    for (auto view : r.views) vkDestroyImageView(r.device, view, nullptr);
    if (r.renderPass) vkDestroyRenderPass(r.device, r.renderPass, nullptr);
    if (r.swapchain) vkDestroySwapchainKHR(r.device, r.swapchain, nullptr);
    if (r.device) vkDestroyDevice(r.device, nullptr);
    if (r.surface) vkDestroySurfaceKHR(r.instance, r.surface, nullptr);
    if (r.instance) vkDestroyInstance(r.instance, nullptr);
    r = Renderer{};
}

void draw() {
    if (!r.device || !r.swapchain) return;
    vkWaitForFences(r.device, 1, &r.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(r.device, 1, &r.fence);
    uint32_t image = 0;
    VkResult result = vkAcquireNextImageKHR(r.device, r.swapchain, 1000000000ull, r.acquire, VK_NULL_HANDLE, &image);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return;
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &r.acquire; submit.pWaitDstStageMask = &wait;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &r.commands[image];
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &r.present;
    if (vkQueueSubmit(r.queue, 1, &submit, r.fence) != VK_SUCCESS) return;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1; present.pWaitSemaphores = &r.present;
    present.swapchainCount = 1; present.pSwapchains = &r.swapchain; present.pImageIndices = &image;
    vkQueuePresentKHR(r.queue, &present);
    vkWaitForFences(r.device, 1, &r.fence, VK_TRUE, UINT64_MAX);
}
}

bool androidVulkanCreate(JNIEnv *env, jobject surface) {
    try {
        destroy();
        ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
        if (!window) return false;
        initialize(window);
        ANativeWindow_release(window);
        LOGI("Vulkan surface initialized: %ux%u", r.extent.width, r.extent.height);
        androidVulkanDraw();
        return true;
    } catch (const std::exception &error) {
        LOGE("Initialization failed: %s", error.what());
        destroy();
        return false;
    }
}
void androidVulkanDraw() { draw(); }
void androidVulkanDestroy() { destroy(); }

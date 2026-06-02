#include "wal/app.hpp"

#include "wal/config.hpp"
#include "wal/io.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <poll.h>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include <linux/input-event-codes.h>

namespace wal {
namespace {

const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

} // namespace

bool QueueFamilyIndices::complete() const
{
    return graphicsFamily.has_value() && presentFamily.has_value();
}

void App::run()
{
    surfaceWidth = defaultWindowWidth;
    surfaceHeight = defaultWindowHeight;

    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

void App::registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    auto* app = static_cast<App*>(data);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = static_cast<wl_compositor*>(wl_registry_bind(
            registry,
            name,
            &wl_compositor_interface,
            std::min(version, 4u)
        ));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)));
    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        app->layerShell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(
            registry,
            name,
            &zwlr_layer_shell_v1_interface,
            std::min(version, 5u)
        ));
    }
}

void App::registryGlobalRemove(void*, wl_registry*, uint32_t) {}

void App::layerSurfaceConfigure(
    void* data,
    zwlr_layer_surface_v1* surface,
    uint32_t serial,
    uint32_t width,
    uint32_t height
)
{
    auto* app = static_cast<App*>(data);
    zwlr_layer_surface_v1_ack_configure(surface, serial);

    const uint32_t nextWidth = width == 0 ? defaultWindowWidth : width;
    const uint32_t nextHeight = height == 0 ? defaultWindowHeight : height;
    if (nextWidth != app->surfaceWidth || nextHeight != app->surfaceHeight) {
        app->surfaceWidth = nextWidth;
        app->surfaceHeight = nextHeight;
        app->framebufferResized = app->configured;
    }
    app->setInputRegion();
    app->configured = true;
}

void App::layerSurfaceClosed(void* data, zwlr_layer_surface_v1*)
{
    static_cast<App*>(data)->running = false;
}

void App::keyboardKeymap(void*, wl_keyboard*, uint32_t, int32_t fd, uint32_t)
{
    close(fd);
}

void App::keyboardEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void App::keyboardLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

void App::keyboardKey(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state)
{
    auto* app = static_cast<App*>(data);
    if (key == KEY_ESC && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        app->running = false;
    }
}

void App::keyboardModifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void App::keyboardRepeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}

void App::pointerEnter(void*, wl_pointer*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t) {}
void App::pointerLeave(void*, wl_pointer*, uint32_t, wl_surface*) {}
void App::pointerMotion(void*, wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t) {}
void App::pointerButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t) {}
void App::pointerAxis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
void App::pointerFrame(void*, wl_pointer*) {}
void App::pointerAxisSource(void*, wl_pointer*, uint32_t) {}
void App::pointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
void App::pointerAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
void App::pointerAxisValue120(void*, wl_pointer*, uint32_t, int32_t) {}
void App::pointerAxisRelativeDirection(void*, wl_pointer*, uint32_t, uint32_t) {}

void App::seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities)
{
    auto* app = static_cast<App*>(data);

    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && app->keyboard == nullptr) {
        app->keyboard = wl_seat_get_keyboard(seat);
        static constexpr wl_keyboard_listener keyboardListener = {
            keyboardKeymap,
            keyboardEnter,
            keyboardLeave,
            keyboardKey,
            keyboardModifiers,
            keyboardRepeatInfo,
        };
        wl_keyboard_add_listener(app->keyboard, &keyboardListener, app);
    } else if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0 && app->keyboard != nullptr) {
        wl_keyboard_release(app->keyboard);
        app->keyboard = nullptr;
    }

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 && app->pointer == nullptr) {
        app->pointer = wl_seat_get_pointer(seat);
        static constexpr wl_pointer_listener pointerListener = {
            pointerEnter,
            pointerLeave,
            pointerMotion,
            pointerButton,
            pointerAxis,
            pointerFrame,
            pointerAxisSource,
            pointerAxisStop,
            pointerAxisDiscrete,
            pointerAxisValue120,
            pointerAxisRelativeDirection,
        };
        wl_pointer_add_listener(app->pointer, &pointerListener, app);
    } else if ((capabilities & WL_SEAT_CAPABILITY_POINTER) == 0 && app->pointer != nullptr) {
        wl_pointer_release(app->pointer);
        app->pointer = nullptr;
    }
}

void App::seatName(void*, wl_seat*, const char*) {}

void App::initWindow()
{
    display = wl_display_connect(nullptr);
    if (display == nullptr) {
        throw std::runtime_error("failed to connect to Wayland display");
    }

    registry = wl_display_get_registry(display);
    static constexpr wl_registry_listener registryListener = {
        registryGlobal,
        registryGlobalRemove,
    };
    wl_registry_add_listener(registry, &registryListener, this);
    wl_display_roundtrip(display);

    if (compositor == nullptr) {
        throw std::runtime_error("Wayland compositor global is unavailable");
    }
    if (layerShell == nullptr) {
        throw std::runtime_error("zwlr_layer_shell_v1 global is unavailable");
    }
    if (seat == nullptr) {
        throw std::runtime_error("Wayland seat global is unavailable");
    }

    static constexpr wl_seat_listener seatListener = {
        seatCapabilities,
        seatName,
    };
    wl_seat_add_listener(seat, &seatListener, this);
    wl_display_roundtrip(display);

    waylandSurface = wl_compositor_create_surface(compositor);
    if (waylandSurface == nullptr) {
        throw std::runtime_error("failed to create Wayland surface");
    }

    layerSurface = zwlr_layer_shell_v1_get_layer_surface(
        layerShell,
        waylandSurface,
        nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "wal"
    );
    if (layerSurface == nullptr) {
        throw std::runtime_error("failed to create layer surface");
    }

    static zwlr_layer_surface_v1_listener layerSurfaceListener = {
        layerSurfaceConfigure,
        layerSurfaceClosed,
    };
    wl_proxy_add_listener(
        reinterpret_cast<wl_proxy*>(layerSurface),
        reinterpret_cast<void (**)(void)>(&layerSurfaceListener),
        this
    );

    zwlr_layer_surface_v1_set_anchor(
        layerSurface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
    );
    zwlr_layer_surface_v1_set_size(layerSurface, 0, 0);
    zwlr_layer_surface_v1_set_margin(layerSurface, 0, 0, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(layerSurface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layerSurface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
    );

    wl_surface_commit(waylandSurface);

    while (!configured && running) {
        if (wl_display_dispatch(display) == -1) {
            throw std::runtime_error("failed while waiting for layer surface configure");
        }
    }
}

void App::setInputRegion()
{
    wl_region* inputRegion = wl_compositor_create_region(compositor);
    if (inputRegion == nullptr) {
        throw std::runtime_error("failed to create Wayland input region");
    }

    wl_surface_set_input_region(waylandSurface, inputRegion);
    wl_region_destroy(inputRegion);
    wl_surface_commit(waylandSurface);
}

void App::initVulkan()
{
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
    createUiVertexBuffer();
    createCommandBuffers();
    createSyncObjects();
}

void App::mainLoop()
{
    while (running) {
        dispatchWaylandEvents();
        drawFrame();
    }

    vkDeviceWaitIdle(device);
}

void App::cleanup()
{
    cleanupSwapchain();

    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);

    if (pointer != nullptr) {
        wl_pointer_release(pointer);
    }
    if (keyboard != nullptr) {
        wl_keyboard_release(keyboard);
    }
    if (layerSurface != nullptr) {
        zwlr_layer_surface_v1_destroy(layerSurface);
    }
    if (waylandSurface != nullptr) {
        wl_surface_destroy(waylandSurface);
    }
    if (seat != nullptr) {
        wl_seat_destroy(seat);
    }
    if (layerShell != nullptr) {
        zwlr_layer_shell_v1_destroy(layerShell);
    }
    if (compositor != nullptr) {
        wl_compositor_destroy(compositor);
    }
    if (registry != nullptr) {
        wl_registry_destroy(registry);
    }
    if (display != nullptr) {
        wl_display_disconnect(display);
    }
}

void App::createInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "wal";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "wal-ui";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    std::array extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan instance");
    }
}

void App::createSurface()
{
    VkWaylandSurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    createInfo.display = display;
    createInfo.surface = waylandSurface;

    if (vkCreateWaylandSurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface");
    }
}

void App::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("failed to find Vulkan-capable GPU");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& candidate : devices) {
        if (isDeviceSuitable(candidate)) {
            physicalDevice = candidate;
            return;
        }
    }

    throw std::runtime_error("failed to find a suitable GPU");
}

void App::createLogicalDevice()
{
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value(),
    };

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device");
    }

    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
}

void App::createSwapchain()
{
    SwapchainSupport support = querySwapchainSupport(physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(support.presentModes);
    VkExtent2D extent = chooseSwapExtent(support.capabilities);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = chooseCompositeAlpha(support.capabilities.supportedCompositeAlpha);
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swapchain");
    }

    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

    swapchainImageFormat = surfaceFormat.format;
    swapchainExtent = extent;
}

void App::createImageViews()
{
    swapchainImageViews.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainImageFormat;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views");
        }
    }
}

void App::createRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass");
    }
}

void App::createGraphicsPipeline()
{
    auto vertShaderCode = readFile(std::string(SHADER_DIR) + "/ui.vert.spv");
    auto fragShaderCode = readFile(std::string(SHADER_DIR) + "/ui.frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(ui::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(ui::Vertex, position);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(ui::Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void App::createFramebuffers()
{
    swapchainFramebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); ++i) {
        VkImageView attachments[] = {swapchainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer");
        }
    }
}

void App::createCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool");
    }
}

void App::createUiVertexBuffer()
{
    rebuildUi();
    if (uiVertices.empty()) {
        return;
    }

    const VkDeviceSize bufferSize = sizeof(ui::Vertex) * uiVertices.size();
    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vertexBuffer,
        vertexBufferMemory
    );

    void* data = nullptr;
    vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
    std::memcpy(data, uiVertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(device, vertexBufferMemory);
}

void App::createCommandBuffers()
{
    commandBuffers.resize(swapchainFramebuffers.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers");
    }

    for (size_t i = 0; i < commandBuffers.size(); ++i) {
        recordCommandBuffer(commandBuffers[i], static_cast<uint32_t>(i));
    }
}

void App::createSyncObjects()
{
    imageAvailableSemaphores.resize(maxFramesInFlight);
    renderFinishedSemaphores.resize(maxFramesInFlight);
    inFlightFences.resize(maxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects");
        }
    }
}

void App::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin command buffer");
    }

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    if (vertexBuffer != VK_NULL_HANDLE && !uiVertices.empty()) {
        VkBuffer vertexBuffers[] = {vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(uiVertices.size()), 1, 0, 0);
    }
    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer");
    }
}

void App::drawFrame()
{
    if (surfaceWidth == 0 || surfaceHeight == 0) {
        return;
    }

    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        device,
        swapchain,
        UINT64_MAX,
        imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swapchain image");
    }

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer");
    }

    VkSwapchainKHR swapchains[] = {swapchain};
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swapchain image");
    }

    currentFrame = (currentFrame + 1) % maxFramesInFlight;
}

void App::recreateSwapchain()
{
    while (surfaceWidth == 0 || surfaceHeight == 0) {
        if (wl_display_dispatch(display) == -1) {
            throw std::runtime_error("failed while waiting for a non-empty surface size");
        }
    }

    vkDeviceWaitIdle(device);

    cleanupSwapchain();

    createSwapchain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
    createUiVertexBuffer();
    createCommandBuffers();
}

void App::cleanupSwapchain()
{
    if (!commandBuffers.empty()) {
        vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
        commandBuffers.clear();
    }

    destroyUiVertexBuffer();

    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    swapchainFramebuffers.clear();

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    graphicsPipeline = VK_NULL_HANDLE;
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
    vkDestroyRenderPass(device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;

    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    swapchainImageViews.clear();

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

void App::destroyUiVertexBuffer()
{
    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemory, nullptr);
        vertexBufferMemory = VK_NULL_HANDLE;
    }
}

void App::rebuildUi()
{
    uiVertices.clear();
}

VkShaderModule App::createShaderModule(const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module");
    }

    return shaderModule;
}

bool App::isDeviceSuitable(VkPhysicalDevice candidate)
{
    QueueFamilyIndices indices = findQueueFamilies(candidate);
    bool extensionsSupported = checkDeviceExtensionSupport(candidate);
    bool swapchainAdequate = false;

    if (extensionsSupported) {
        SwapchainSupport swapchainSupport = querySwapchainSupport(candidate);
        swapchainAdequate = !swapchainSupport.formats.empty() && !swapchainSupport.presentModes.empty();
    }

    return indices.complete() && extensionsSupported && swapchainAdequate;
}

QueueFamilyIndices App::findQueueFamilies(VkPhysicalDevice candidate)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &presentSupport);
        if (presentSupport != VK_FALSE) {
            indices.presentFamily = i;
        }

        if (indices.complete()) {
            break;
        }
    }

    return indices;
}

bool App::checkDeviceExtensionSupport(VkPhysicalDevice candidate)
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

SwapchainSupport App::querySwapchainSupport(VkPhysicalDevice candidate)
{
    SwapchainSupport details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(candidate, surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR App::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& availableFormat : formats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return formats[0];
}

VkPresentModeKHR App::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes)
{
    for (const auto& availablePresentMode : presentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkCompositeAlphaFlagBitsKHR App::chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supportedCompositeAlpha)
{
    if ((supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) != 0) {
        return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }
    if ((supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0) {
        return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }
    if ((supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) != 0) {
        return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }

    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

VkExtent2D App::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = {
        surfaceWidth,
        surfaceHeight,
    };

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

uint32_t App::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find a suitable memory type");
}

void App::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory
)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer");
    }

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocateInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void App::dispatchWaylandEvents()
{
    wl_display_dispatch_pending(display);

    pollfd displayPoll{};
    displayPoll.fd = wl_display_get_fd(display);
    displayPoll.events = POLLIN;

    if (poll(&displayPoll, 1, 0) > 0 && (displayPoll.revents & POLLIN) != 0) {
        if (wl_display_dispatch(display) == -1) {
            running = false;
            return;
        }
    }

    wl_display_flush(display);
}

} // namespace wal

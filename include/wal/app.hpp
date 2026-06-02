#pragma once

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

#include "wal/layer_shell.hpp"
#include "wal/ui.hpp"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wal {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    [[nodiscard]] bool complete() const;
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class App {
public:
    void run();

private:
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_seat* seat = nullptr;
    wl_keyboard* keyboard = nullptr;
    wl_pointer* pointer = nullptr;
    xkb_context* xkbContext = nullptr;
    xkb_keymap* xkbKeymap = nullptr;
    xkb_state* xkbState = nullptr;
    wl_surface* waylandSurface = nullptr;
    zwlr_layer_shell_v1* layerShell = nullptr;
    zwlr_layer_surface_v1* layerSurface = nullptr;

    bool running = true;
    bool configured = false;
    bool framebufferResized = false;
    uint32_t surfaceWidth = 0;
    uint32_t surfaceHeight = 0;
    std::string textFieldValue;
    size_t textCursorIndex = 0;
    size_t textSelectionAnchor = 0;
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    bool mouseSelectingText = false;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
    std::vector<ui::Vertex> uiVertices;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;

    static void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    static void registryGlobalRemove(void* data, wl_registry* registry, uint32_t name);
    static void layerSurfaceConfigure(
        void* data,
        zwlr_layer_surface_v1* surface,
        uint32_t serial,
        uint32_t width,
        uint32_t height
    );
    static void layerSurfaceClosed(void* data, zwlr_layer_surface_v1* surface);
    static void keyboardKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size);
    static void keyboardEnter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys);
    static void keyboardLeave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface);
    static void keyboardKey(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    static void keyboardModifiers(
        void* data,
        wl_keyboard* keyboard,
        uint32_t serial,
        uint32_t modsDepressed,
        uint32_t modsLatched,
        uint32_t modsLocked,
        uint32_t group
    );
    static void keyboardRepeatInfo(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay);
    static void pointerEnter(
        void* data,
        wl_pointer* pointer,
        uint32_t serial,
        wl_surface* surface,
        wl_fixed_t surfaceX,
        wl_fixed_t surfaceY
    );
    static void pointerLeave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface);
    static void pointerMotion(void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t surfaceX, wl_fixed_t surfaceY);
    static void pointerButton(
        void* data,
        wl_pointer* pointer,
        uint32_t serial,
        uint32_t time,
        uint32_t button,
        uint32_t state
    );
    static void pointerAxis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
    static void pointerFrame(void* data, wl_pointer* pointer);
    static void pointerAxisSource(void* data, wl_pointer* pointer, uint32_t axisSource);
    static void pointerAxisStop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis);
    static void pointerAxisDiscrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete);
    static void pointerAxisValue120(void* data, wl_pointer* pointer, uint32_t axis, int32_t value120);
    static void pointerAxisRelativeDirection(void* data, wl_pointer* pointer, uint32_t axis, uint32_t direction);
    static void seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities);
    static void seatName(void* data, wl_seat* seat, const char* name);

    void initWindow();
    void setInputRegion();
    void initVulkan();
    void mainLoop();
    void cleanup();
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createUiVertexBuffer();
    void createCommandBuffers();
    void createSyncObjects();
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void drawFrame();
    void recreateSwapchain();
    void cleanupSwapchain();
    void destroyUiVertexBuffer();
    void rebuildUi();
    void refreshUi();
    void insertText(std::string_view value);
    void deleteBackward();
    void deleteForward();
    void deleteSelection();
    void moveCursor(size_t nextIndex, bool extendSelection);
    void moveCursorByWord(bool forward, bool extendSelection);
    void selectAllText();
    void copySelectionToClipboard() const;
    void cutSelectionToClipboard();
    void pasteFromClipboard();

    [[nodiscard]] ui::Rect textFieldRect() const;
    [[nodiscard]] bool hasTextSelection() const;
    [[nodiscard]] size_t textSelectionStart() const;
    [[nodiscard]] size_t textSelectionEnd() const;
    [[nodiscard]] size_t textIndexAtPointer(float x) const;

    [[nodiscard]] VkShaderModule createShaderModule(const std::vector<char>& code);
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice candidate);
    [[nodiscard]] QueueFamilyIndices findQueueFamilies(VkPhysicalDevice candidate);
    [[nodiscard]] bool checkDeviceExtensionSupport(VkPhysicalDevice candidate);
    [[nodiscard]] SwapchainSupport querySwapchainSupport(VkPhysicalDevice candidate);
    [[nodiscard]] VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    [[nodiscard]] VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes);
    [[nodiscard]] VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supportedCompositeAlpha);
    [[nodiscard]] VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory
    );
    void dispatchWaylandEvents();
};

} // namespace wal

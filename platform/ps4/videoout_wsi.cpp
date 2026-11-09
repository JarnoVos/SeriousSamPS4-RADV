/* RADV VideoOut swapchain for the existing Serious Engine Vulkan backend.
 *
 * RADV is linked in and has no window system. The engine keeps calling the
 * global vk* entry points (see radv_vk_dispatch.cpp). This file is the surface
 * and the swapchain: one 1280x720 RGBA target, which is also the only mode
 * reported to the menu. 1920x1080 waits until that picture is right.
 *
 * The engine's PS4 path stores VK_NULL_HANDLE as the surface. That handle is
 * this one screen.
 */
#include <vulkan/vulkan.h>

#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);
extern "C" int ac_ps4_submit_flip(int video, unsigned buffer, unsigned mode, int64_t arg);

enum { kSwapW = 1280, kSwapH = 720, kMaxImages = 2, kBanks = 2 };
static const VkDeviceSize kAlign = 2u << 20;

struct Swapchain {
    VkDevice device;
    uint32_t imageCount;
    VkFormat format;
    VkImage images[kMaxImages];
    VkDeviceMemory memory[kMaxImages];
    int bufferSet;
    int firstBuffer;
    uint32_t nextImage;
    uint32_t flipMode;
};

static VkInstance g_instance = VK_NULL_HANDLE;
static VkPhysicalDevice g_physical = VK_NULL_HANDLE;
static int g_video = -1;
static OrbisKernelEqueue g_flipQueue;
static uint64_t g_flips = 0;
static bool g_bankUsed[kBanks];
static Swapchain *g_retired[4];
static int g_retiredCount;

static void Log(const char *fmt, ...) {
    fprintf(stderr, "PS4 RADV: ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fflush(stderr);
}

static void Stop(const char *what) {
    Log("stopping: %s\n", what);
    sceSystemServiceLoadExec("exit", nullptr);
    _exit(1);
}

extern "C" PFN_vkVoidFunction ss_radv_resolve(const char *name) {
    PFN_vkVoidFunction fn = vk_icdGetInstanceProcAddr(g_instance, name);
    if (!fn)
        Stop(name);
    return fn;
}

template <typename T>
static T Icd(const char *name) {
    return (T)ss_radv_resolve(name);
}

extern "C" void ss_ps4_radv_extent(int *width, int *height) {
    *width = kSwapW;
    *height = kSwapH;
}

extern "C" void vk_ps4_log_open(const char *path) {
    FILE *log = fopen(path ? path : "/data/serioussam/vulkan.log", "w");
    if (!log)
        return;
    fputs("PS4 RADV: Vulkan log opened\n", log);
    fclose(log);
}

/* SDL's replaced video file still names the PS4 driver. The engine selects dummy. */
struct SSVideoBoot {
    const char *name;
    const char *desc;
    int (*available)(void);
    void *(*create)(int);
};
static int SSVideoUnavailable(void) { return 0; }
extern "C" struct SSVideoBoot PS4_bootstrap = {"PS4", "unused", SSVideoUnavailable, 0};

static bool Listed(const VkExtensionProperties *list, uint32_t count, const char *name) {
    for (uint32_t i = 0; i < count; i++)
        if (strcmp(list[i].extensionName, name) == 0)
            return true;
    return false;
}

template <typename Known>
static uint32_t KnownExtensions(uint32_t count, const char *const *names, Known known, const char **kept, uint32_t cap) {
    uint32_t n = 0;
    if (!names)
        return 0;
    for (uint32_t i = 0; i < count && n < cap; i++) {
        if (known(names[i]))
            kept[n++] = names[i];
        else
            Log("extension %s is provided here, not by RADV\n", names[i]);
    }
    return n;
}

extern "C" void ss_ps4_load_modules(void);

static bool OpenVideo() {
    if (g_video >= 0)
        return true;
    ss_ps4_load_modules();
    sceUserServiceInitialize(nullptr);
    g_video = sceVideoOutOpen(0xFF, ORBIS_VIDEO_OUT_BUS_MAIN, 0, nullptr);
    if (g_video < 0) {
        Log("sceVideoOutOpen %#x\n", g_video);
        return false;
    }
    sceKernelCreateEqueue(&g_flipQueue, "ss flip");
    sceVideoOutAddFlipEvent(g_flipQueue, g_video, nullptr);
    OrbisVideoOutResolutionStatus status;
    memset(&status, 0, sizeof(status));
    if (sceVideoOutGetResolutionStatus(g_video, &status) == 0)
        Log("VideoOut %ux%u, swapchain locked to %ux%u\n", status.width, status.height, kSwapW, kSwapH);
    return true;
}

static uint32_t HostMemory(uint32_t typeBits) {
    VkPhysicalDeviceMemoryProperties props;
    Icd<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(g_physical, &props);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return UINT32_MAX;
}

static void FreeImages(Swapchain *sc) {
    auto destroyImage = Icd<PFN_vkDestroyImage>("vkDestroyImage");
    auto freeMemory = Icd<PFN_vkFreeMemory>("vkFreeMemory");
    for (uint32_t i = 0; i < sc->imageCount; i++) {
        if (sc->images[i])
            destroyImage(sc->device, sc->images[i], nullptr);
        if (sc->memory[i])
            freeMemory(sc->device, sc->memory[i], nullptr);
    }
    delete sc;
}

static void ReleaseRetired() {
    int i = 0;
    while (i < g_retiredCount) {
        Swapchain *sc = g_retired[i];
        if (sceVideoOutUnregisterBuffers(g_video, sc->bufferSet) < 0) {
            i++;
            continue;
        }
        g_bankUsed[sc->firstBuffer / kMaxImages] = false;
        sc->bufferSet = -1;
        g_retired[i] = g_retired[--g_retiredCount];
        FreeImages(sc);
    }
}

static void FreeSwapchain(Swapchain *sc) {
    if (sc->bufferSet >= 0) {
        if (sceVideoOutUnregisterBuffers(g_video, sc->bufferSet) < 0) {
            if (g_retiredCount < 4)
                g_retired[g_retiredCount++] = sc;
            return;
        }
        g_bankUsed[sc->firstBuffer / kMaxImages] = false;
        sc->bufferSet = -1;
    }
    FreeImages(sc);
}

static PFN_vkVoidFunction WsiProc(const char *name);

extern "C" {

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *layer, uint32_t *count,
                                                                      VkExtensionProperties *props) {
    auto real = (PFN_vkEnumerateInstanceExtensionProperties)vk_icdGetInstanceProcAddr(
        VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    uint32_t n = 0;
    real(layer, &n, nullptr);
    if (n > 127)
        n = 127;
    VkExtensionProperties list[128];
    if (n)
        real(layer, &n, list);
    VkExtensionProperties extra;
    memset(&extra, 0, sizeof(extra));
    strncpy(extra.extensionName, VK_KHR_SURFACE_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE - 1);
    extra.specVersion = 25;
    list[n++] = extra;
    if (!props) {
        *count = n;
        return VK_SUCCESS;
    }
    uint32_t copy = *count < n ? *count : n;
    memcpy(props, list, copy * sizeof(VkExtensionProperties));
    *count = copy;
    return copy < n ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *info, const VkAllocationCallbacks *alloc,
                                                VkInstance *instance) {
    auto realEnum = (PFN_vkEnumerateInstanceExtensionProperties)vk_icdGetInstanceProcAddr(
        VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    auto real = (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    uint32_t have = 0;
    realEnum(nullptr, &have, nullptr);
    VkExtensionProperties listed[128];
    if (have > 128)
        have = 128;
    if (have)
        realEnum(nullptr, &have, listed);
    const char *names[64];
    uint32_t nameCount = KnownExtensions(info->enabledExtensionCount, info->ppEnabledExtensionNames,
                                         [&](const char *name) { return Listed(listed, have, name); }, names, 64);
    VkInstanceCreateInfo copy = *info;
    copy.enabledExtensionCount = nameCount;
    copy.ppEnabledExtensionNames = nameCount ? names : nullptr;
    VkResult result = real(&copy, alloc, instance);
    if (result == VK_SUCCESS)
        g_instance = *instance;
    else
        Log("vkCreateInstance %d\n", (int)result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physical, const char *layer,
                                                                    uint32_t *count, VkExtensionProperties *props) {
    auto real = Icd<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
    uint32_t n = 0;
    real(physical, layer, &n, nullptr);
    if (n > 127)
        n = 127;
    VkExtensionProperties list[128];
    if (n)
        real(physical, layer, &n, list);
    VkExtensionProperties extra;
    memset(&extra, 0, sizeof(extra));
    strncpy(extra.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE - 1);
    extra.specVersion = 70;
    list[n++] = extra;
    if (!props) {
        *count = n;
        return VK_SUCCESS;
    }
    uint32_t copy = *count < n ? *count : n;
    memcpy(props, list, copy * sizeof(VkExtensionProperties));
    *count = copy;
    return copy < n ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo *info,
                                              const VkAllocationCallbacks *alloc, VkDevice *device) {
    auto realEnum = Icd<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
    auto real = Icd<PFN_vkCreateDevice>("vkCreateDevice");
    uint32_t have = 0;
    realEnum(physical, nullptr, &have, nullptr);
    if (have > 128)
        have = 128;
    VkExtensionProperties listed[128];
    if (have)
        realEnum(physical, nullptr, &have, listed);
    const char *names[64];
    uint32_t nameCount = KnownExtensions(info->enabledExtensionCount, info->ppEnabledExtensionNames,
                                         [&](const char *name) { return Listed(listed, have, name); }, names, 64);
    VkDeviceCreateInfo copy = *info;
    copy.enabledExtensionCount = nameCount;
    copy.ppEnabledExtensionNames = nameCount ? names : nullptr;
    g_physical = physical;
    VkResult result = real(physical, &copy, alloc, device);
    if (result != VK_SUCCESS)
        Log("vkCreateDevice %d\n", (int)result);
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks *) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t family, VkSurfaceKHR,
                                                                    VkBool32 *supported) {
    if (!supported)
        return VK_ERROR_SURFACE_LOST_KHR;
    *supported = family == 0 ? VK_TRUE : VK_FALSE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice, VkSurfaceKHR,
                                                                        VkSurfaceCapabilitiesKHR *caps) {
    if (!caps)
        return VK_ERROR_SURFACE_LOST_KHR;
    memset(caps, 0, sizeof(*caps));
    caps->minImageCount = 2;
    caps->maxImageCount = 2;
    caps->currentExtent = caps->minImageExtent = caps->maxImageExtent = {kSwapW, kSwapH};
    caps->maxImageArrayLayers = 1;
    caps->supportedTransforms = caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    caps->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    caps->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t *count,
                                                                    VkSurfaceFormatKHR *formats) {
    if (!count)
        return VK_ERROR_SURFACE_LOST_KHR;
    if (!formats) {
        *count = 1;
        return VK_SUCCESS;
    }
    if (!*count)
        return VK_INCOMPLETE;
    *count = 1;
    *formats = {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t *count,
                                                                         VkPresentModeKHR *modes) {
    if (!count)
        return VK_ERROR_SURFACE_LOST_KHR;
    if (!modes) {
        *count = 1;
        return VK_SUCCESS;
    }
    if (!*count)
        return VK_INCOMPLETE;
    *count = 1;
    *modes = VK_PRESENT_MODE_FIFO_KHR;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *info,
                                                    const VkAllocationCallbacks *, VkSwapchainKHR *swapchain) {
    if (!OpenVideo())
        return VK_ERROR_INITIALIZATION_FAILED;
    if (info->imageExtent.width != kSwapW || info->imageExtent.height != kSwapH)
        Log("swapchain request %ux%u stays %ux%u\n", info->imageExtent.width, info->imageExtent.height, kSwapW, kSwapH);
    if (info->imageFormat != VK_FORMAT_R8G8B8A8_UNORM)
        Log("swapchain format %d; images are R8G8B8A8\n", (int)info->imageFormat);

    auto sc = new Swapchain();
    memset(sc, 0, sizeof(*sc));
    sc->device = device;
    sc->format = VK_FORMAT_R8G8B8A8_UNORM;
    sc->imageCount = 2;
    sc->bufferSet = -1;
    sc->flipMode = ORBIS_VIDEO_OUT_FLIP_VSYNC;

    auto createImage = Icd<PFN_vkCreateImage>("vkCreateImage");
    auto getRequirements = Icd<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements");
    auto allocateMemory = Icd<PFN_vkAllocateMemory>("vkAllocateMemory");
    auto mapMemory = Icd<PFN_vkMapMemory>("vkMapMemory");
    auto bindMemory = Icd<PFN_vkBindImageMemory>("vkBindImageMemory");
    auto getLayout = Icd<PFN_vkGetImageSubresourceLayout>("vkGetImageSubresourceLayout");
    void *addresses[kMaxImages] = {};
    uint32_t pitch = kSwapW;

    for (uint32_t i = 0; i < sc->imageCount; i++) {
        VkImageCreateInfo image = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = sc->format;
        image.extent = {kSwapW, kSwapH, 1};
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_LINEAR;
        image.usage = info->imageUsage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult result = createImage(device, &image, nullptr, &sc->images[i]);
        if (result != VK_SUCCESS) {
            Log("vkCreateImage %d\n", (int)result);
            FreeSwapchain(sc);
            return result;
        }
        VkMemoryRequirements req;
        getRequirements(device, sc->images[i], &req);
        VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size + kAlign;
        alloc.memoryTypeIndex = HostMemory(req.memoryTypeBits);
        result = allocateMemory(device, &alloc, nullptr, &sc->memory[i]);
        uint8_t *base = nullptr;
        if (result == VK_SUCCESS)
            result = mapMemory(device, sc->memory[i], 0, VK_WHOLE_SIZE, 0, (void **)&base);
        if (result != VK_SUCCESS) {
            Log("swapchain memory %d\n", (int)result);
            FreeSwapchain(sc);
            return result;
        }
        VkDeviceSize offset = ((uintptr_t)base + kAlign - 1) & ~(kAlign - 1);
        offset -= (uintptr_t)base;
        offset = (offset + req.alignment - 1) & ~(req.alignment - 1);
        bindMemory(device, sc->images[i], sc->memory[i], offset);
        VkImageSubresource sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
        VkSubresourceLayout layout;
        getLayout(device, sc->images[i], &sub, &layout);
        addresses[i] = base + offset + layout.offset;
        pitch = (uint32_t)(layout.rowPitch / 4);
    }

    OrbisVideoOutBufferAttribute attribute;
    sceVideoOutSetBufferAttribute(&attribute, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                  ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, 0, kSwapW, kSwapH, pitch);
    ReleaseRetired();
    int registered = -1;
    for (int bank = 0; bank < kBanks && sc->bufferSet < 0; bank++) {
        if (g_bankUsed[bank])
            continue;
        sc->firstBuffer = bank * kMaxImages;
        registered = sceVideoOutRegisterBuffers(g_video, sc->firstBuffer, addresses, (int)sc->imageCount, &attribute);
        if (registered >= 0) {
            sc->bufferSet = registered;
            g_bankUsed[bank] = true;
        }
    }
    if (sc->bufferSet < 0) {
        Log("sceVideoOutRegisterBuffers %#x\n", registered);
        sc->bufferSet = -1;
        FreeSwapchain(sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    Log("swapchain %ux%u pitch %u buffers %d\n", kSwapW, kSwapH, pitch, sc->firstBuffer);
    *swapchain = (VkSwapchainKHR)sc;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *) {
    if (!swapchain)
        return;
    Icd<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device);
    FreeSwapchain((Swapchain *)swapchain);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice, VkSwapchainKHR swapchain, uint32_t *count, VkImage *images) {
    auto sc = (Swapchain *)swapchain;
    if (!images) {
        *count = sc->imageCount;
        return VK_SUCCESS;
    }
    uint32_t n = *count < sc->imageCount ? *count : sc->imageCount;
    memcpy(images, sc->images, n * sizeof(VkImage));
    *count = n;
    return n < sc->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
}

static void WaitOffScreen(uint32_t imageCount) {
    if (g_flips < imageCount - 1)
        return;
    const int64_t needed = (int64_t)(g_flips - (imageCount - 2));
    for (int tries = 0; tries < 40; tries++) {
        OrbisVideoOutFlipStatus status;
        memset(&status, 0, sizeof(status));
        if (sceVideoOutGetFlipStatus(g_video, &status) < 0)
            return;
        if (status.flipArg >= needed)
            return;
        OrbisKernelEvent event;
        int got = 0;
        OrbisKernelUseconds timeout = 5000;
        sceKernelWaitEqueue(g_flipQueue, &event, 1, &got, &timeout);
    }
    Log("flip wait timed out at %llu\n", (unsigned long long)g_flips);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t, VkSemaphore,
                                                     VkFence fence, uint32_t *index) {
    auto sc = (Swapchain *)swapchain;
    WaitOffScreen(sc->imageCount);
    *index = sc->nextImage;
    sc->nextImage = (sc->nextImage + 1) % sc->imageCount;
    if (!fence)
        return VK_SUCCESS;
    VkQueue queue = VK_NULL_HANDLE;
    Icd<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(device, 0, 0, &queue);
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    auto real = (PFN_vkQueueSubmit)vk_icdGetInstanceProcAddr(g_instance, "vkQueueSubmit");
    return real(queue, 1, &submit, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *info) {
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < info->swapchainCount; i++) {
        auto sc = (Swapchain *)info->pSwapchains[i];
        const uint32_t buffer = (uint32_t)sc->firstBuffer + info->pImageIndices[i];
        const int64_t arg = (int64_t)(g_flips + 1);
        int flipped = ac_ps4_submit_flip(g_video, buffer, sc->flipMode, arg);
        if (flipped != 0) {
            Icd<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(queue);
            flipped = sceVideoOutSubmitFlip(g_video, (int)buffer, sc->flipMode, arg);
        }
        if (flipped < 0) {
            Log("flip %#x\n", flipped);
            result = VK_ERROR_SURFACE_LOST_KHR;
        } else {
            g_flips++;
            if (g_flips == 1)
                sceSystemServiceHideSplashScreen();
            if (g_retiredCount)
                ReleaseRetired();
        }
        if (info->pResults)
            info->pResults[i] = flipped < 0 ? VK_ERROR_SURFACE_LOST_KHR : VK_SUCCESS;
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo *submits, VkFence fence) {
    auto real = (PFN_vkQueueSubmit)vk_icdGetInstanceProcAddr(g_instance, "vkQueueSubmit");
    if (!real)
        Stop("vkQueueSubmit");
    VkSubmitInfo stack[8];
    VkSubmitInfo *copy = stack;
    VkSubmitInfo *heap = nullptr;
    if (count > 8) {
        heap = (VkSubmitInfo *)malloc(count * sizeof(VkSubmitInfo));
        if (!heap)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        copy = heap;
    }
    for (uint32_t i = 0; i < count; i++) {
        copy[i] = submits[i];
        copy[i].waitSemaphoreCount = 0;
        copy[i].pWaitSemaphores = nullptr;
        copy[i].pWaitDstStageMask = nullptr;
        copy[i].signalSemaphoreCount = 0;
        copy[i].pSignalSemaphores = nullptr;
    }
    VkResult result = real(queue, count, count ? copy : nullptr, fence);
    free(heap);
    return result;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *name) {
    (void)device;
    if (strcmp(name, "vkGetDeviceProcAddr") == 0)
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (PFN_vkVoidFunction fn = WsiProc(name))
        return fn;
    return vk_icdGetInstanceProcAddr(g_instance, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name) {
    if (strcmp(name, "vkGetInstanceProcAddr") == 0)
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (strcmp(name, "vkGetDeviceProcAddr") == 0)
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (PFN_vkVoidFunction fn = WsiProc(name))
        return fn;
    return vk_icdGetInstanceProcAddr(instance ? instance : g_instance, name);
}

} /* extern "C" */

static PFN_vkVoidFunction WsiProc(const char *name) {
#define ENTRY(fn) if (strcmp(name, "vk" #fn) == 0) return (PFN_vkVoidFunction)&vk##fn
    ENTRY(EnumerateInstanceExtensionProperties);
    ENTRY(CreateInstance);
    ENTRY(EnumerateDeviceExtensionProperties);
    ENTRY(CreateDevice);
    ENTRY(DestroySurfaceKHR);
    ENTRY(GetPhysicalDeviceSurfaceSupportKHR);
    ENTRY(GetPhysicalDeviceSurfaceCapabilitiesKHR);
    ENTRY(GetPhysicalDeviceSurfaceFormatsKHR);
    ENTRY(GetPhysicalDeviceSurfacePresentModesKHR);
    ENTRY(CreateSwapchainKHR);
    ENTRY(DestroySwapchainKHR);
    ENTRY(GetSwapchainImagesKHR);
    ENTRY(AcquireNextImageKHR);
    ENTRY(QueuePresentKHR);
    ENTRY(QueueSubmit);
#undef ENTRY
    return nullptr;
}

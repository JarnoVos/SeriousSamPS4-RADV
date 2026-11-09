/* Frozen OpenGNM adapter. This is the driver the boot build still links.
 * The RADV swapchain lives in videoout_wsi.cpp and is not part of this build. */
/* Private WSI adapter for the native ICD's fixed 1280x720 VideoOut swapchain.
 * There is no SDL/native surface object; this client uses VK_NULL_HANDLE. */
#include <vulkan/vulkan.h>
#include <string.h>
extern "C" {
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t family, VkSurfaceKHR surface, VkBool32 *supported) {
    if(surface || !supported) return VK_ERROR_SURFACE_LOST_KHR;
    *supported=family==0; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *caps) {
    if(surface || !caps) return VK_ERROR_SURFACE_LOST_KHR;
    memset(caps,0,sizeof(*caps)); caps->minImageCount=2; caps->maxImageCount=2;
    caps->currentExtent=caps->minImageExtent=caps->maxImageExtent={1280,720};
    caps->maxImageArrayLayers=1;
    caps->supportedTransforms=caps->currentTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    caps->supportedCompositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    caps->supportedUsageFlags=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR surface, uint32_t *count, VkSurfaceFormatKHR *formats) {
    if(surface || !count) return VK_ERROR_SURFACE_LOST_KHR;
    if(!formats) { *count=1; return VK_SUCCESS; }
    if(!*count) return VK_INCOMPLETE;
    *count=1; *formats={VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR surface, uint32_t *count, VkPresentModeKHR *modes) {
    if(surface || !count) return VK_ERROR_SURFACE_LOST_KHR;
    if(!modes) { *count=1; return VK_SUCCESS; }
    if(!*count) return VK_INCOMPLETE;
    *count=1; *modes=VK_PRESENT_MODE_FIFO_KHR; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks*) {}
}

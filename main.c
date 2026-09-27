#include <assert.h>
#include <cglm/cglm.h>
#include <cglm/struct.h>
#include <cglm/struct/affine-pre.h>
#include <cglm/struct/affine.h>
#include <cglm/struct/cam.h>
#include <cglm/struct/clipspace/persp_rh_zo.h>
#include <cglm/struct/mat4.h>
#include <cglm/util.h>
#include <inttypes.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_WIDTH 1000
#define SCREEN_HEIGHT 1000
#define FRAMES_IN_FLIGHT 2

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

void throw(char *msg) {
  printf("%s\n", msg);
  exit(1);
}

char *readFile(const char *filename, size_t *outSize) {
  FILE *file = fopen(filename, "rb"); // "b" = binary, same role as std::ios::binary
  if (file == NULL) {
    throw("failed to open file!\n");
  }

  // seek to end to get size (same trick as ate + tellg)
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    throw("failed to seek file!\n");
  }

  long fileSize = ftell(file);
  if (fileSize < 0) {
    fclose(file);
    throw("failed to get file size!\n");
  }

  char *buffer = malloc((size_t)fileSize);
  if (buffer == NULL) {
    fclose(file);
    throw("failed to allocate file buffer!\n");
  }

  // seek back to beginning
  fseek(file, 0, SEEK_SET);

  size_t bytesRead = fread(buffer, 1, (size_t)fileSize, file);
  if (bytesRead != (size_t)fileSize) {
    free(buffer);
    fclose(file);
    throw("failed to read entire file!\n");
  }

  fclose(file);

  *outSize = (size_t)fileSize;
  return buffer;
}

typedef struct {
  mat4s model;
  mat4s view;
  mat4s projection;
} UniformBufferObject;

typedef struct {
  float pos[3];
  float col[3];
} Vertex;

const Vertex vertices[5] = {
    {{
         0.0f,
         0.0f,
         1.0f,
     },
     {1.0f, 0.0f, 0.0f}},
    {{
         0.0f,
         1.0f,
         0.0f,
     },
     {0.0f, 1.0f, 0.0f}},
    {{
         0.0f,
         -1.0f,
         0.0f,
     },
     {0.0f, 1.0f, 0.0f}},
    {{
         1.0f,
         0.0f,
         0.0f,
     },
     {0.0f, 1.0f, 0.0f}},
    {{
         -1.0f,
         0.0f,
         0.0f,
     },
     {0.0f, 1.0f, 0.0f}},
};

uint32_t indices[] = {
    0, 1, 3, // side face: apex, +Y, +X
    0, 3, 2, // side face: apex, +X, -Y
    0, 2, 4, // side face: apex, -Y, -X
    0, 4, 1, // side face: apex, -X, +Y
};
typedef struct {
  VkVertexInputAttributeDescription *attribs;
  int count;
} Vertex_Attribute_Description_result;

VkVertexInputBindingDescription getVertexBindingDescription() {
  VkVertexInputBindingDescription result = {0};
  result.binding = 0;
  result.stride = sizeof(Vertex);
  result.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  return result;
}

Vertex_Attribute_Description_result getVertexAtrributeDescription() {
  Vertex_Attribute_Description_result result;
  result.count = 2;

  result.attribs = malloc(sizeof(VkVertexInputAttributeDescription) * 2);
  VkVertexInputAttributeDescription first = {0};
  first.location = 0;
  first.binding = 0;
  first.format = VK_FORMAT_R32G32B32_SFLOAT;
  first.offset = offsetof(Vertex, pos);

  VkVertexInputAttributeDescription second = {0};
  second.location = 1;
  second.binding = 0;
  second.format = VK_FORMAT_R32G32B32_SFLOAT;
  second.offset = offsetof(Vertex, col);
  result.attribs[0] = first;
  result.attribs[1] = second;
  return result;
}

typedef struct {
  GLFWwindow *window;
  VkInstance instance;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  VkQueue graphicsQueue;
  VkQueue presentQueue;
  VkSurfaceKHR surface;
  VkSwapchainKHR swapChain;
  VkImage *swapChainImages;
  uint32_t swapChainImageCount;
  VkSurfaceFormatKHR swapChainSurfaceFormat;
  VkExtent2D swapChainExtent;
  VkImageView *swapChainImageViews;
  uint32_t swapChainImageViewCount;
  VkDescriptorSetLayout descriptorLayout;
  VkPipelineLayout pipelineLayout;
  VkPipeline graphicsPipeline;
  VkCommandPool commandPool;
  VkCommandBuffer *commandBuffers;
  uint32_t queueIndex;
  VkSemaphore *presentCompleteSemaphore;
  VkSemaphore *renderFinishedSemaphore;
  VkFence *inFlightFences;
  uint32_t frameIndex;
  bool framebufferResized;
  VkBuffer vertexBuffer;
  VkBuffer *uniformBuffers;
  VkDeviceMemory *uniformBufferMemory;
  uint32_t uniformBuffersCount;
  VkDeviceMemory vertexBufferMemory;
  void **uniformBuffersMapped;
  VkBuffer indexBuffer;
  VkDeviceMemory indexBufferMemory;
  VkDescriptorPool descriptorPool;
  VkDescriptorSet *descriptorSets;
  VkImage depthImage;
  VkDeviceMemory depthImageMemory;
  VkImageView depthImageView;
  float lastTime;
} App;

App *app;

const char *validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
const uint32_t vLayersCount = 1;

const char *requiredDeviceExtension[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
const uint32_t deviceExtensionCount = 1;

bool checkValidationLayerSupport() {
  uint32_t layerCount;
  vkEnumerateInstanceLayerProperties(&layerCount, NULL);

  VkLayerProperties *avaliableLayers = malloc(sizeof(VkLayerProperties) * layerCount);
  VkResult result = vkEnumerateInstanceLayerProperties(&layerCount, avaliableLayers);
  if (result != VK_SUCCESS) {
    throw("failed to get validation layers from enumerate instance layer properties\n");
  }

  for (int i = 0; i < vLayersCount; i++) {
    const char *layerName = validationLayers[i];
    bool layerFound = false;

    for (int j = 0; j < layerCount; j++) {
      const char *layerPropName = avaliableLayers[j].layerName;
      printf("%s, %s\n", layerName, layerPropName);
      if (strcmp(layerName, layerPropName) == 0) {
        layerFound = true;
        break;
      }
    }
    if (!layerFound) {
      return false;
    }
  }
  free(avaliableLayers);
  return true;
}

typedef struct {
  uint32_t graphicsFamily;
  uint32_t presentFamily;
  bool isSetGraphicsFamily;
  bool isSetPresentFamily;
} QueueFamilyIndices;
typedef struct {
  VkBuffer buffer;
  VkDeviceMemory memory;
} Create_Buffer_Return_Object;

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
  QueueFamilyIndices indices = {.isSetGraphicsFamily = false};

  VkBool32 presentSupport = false;

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);
  VkQueueFamilyProperties *queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

  for (uint32_t i = 0; i < queueFamilyCount; i++) {
    VkQueueFamilyProperties queueFamiliy = queueFamilies[i];
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, app->surface, &presentSupport);
    if (queueFamiliy.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphicsFamily = i;
      indices.isSetGraphicsFamily = true;
    }
    if (presentSupport) {
      indices.presentFamily = i;
      indices.isSetPresentFamily = true;
      app->queueIndex = i;
    }
  }

  free(queueFamilies);
  return indices;
}
static void framebufferResizeCallback(GLFWwindow *window, int width, int height) { app->framebufferResized = true; }
VkExtent2D chooseSwapExtent(VkSurfaceCapabilitiesKHR capabilities) {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }
  int width, height;
  glfwGetFramebufferSize(app->window, &width, &height);
  if (width > capabilities.maxImageExtent.width) {
    width = capabilities.maxImageExtent.width;
  }
  if (width < capabilities.minImageExtent.width) {
    width = capabilities.minImageExtent.width;
  }
  if (height > capabilities.maxImageExtent.height) {
    height = capabilities.maxImageExtent.height;
  }
  if (width < capabilities.minImageExtent.height) {
    width = capabilities.minImageExtent.height;
  }
  VkExtent2D result;
  result.height = height;
  result.width = width;
  return result;
}
uint32_t chooseSwapMinImageCount(VkSurfaceCapabilitiesKHR capabilities) {
  uint32_t minImageCount = 3u > capabilities.minImageCount ? 3u : capabilities.minImageCount;
  if ((0 < capabilities.maxImageCount) && (capabilities.maxImageCount < minImageCount)) {
    minImageCount = capabilities.maxImageCount;
  }
  return minImageCount;
}
VkSurfaceFormatKHR chooseSwapSurfaceFormat(VkSurfaceFormatKHR *avaliableFormats, int count) {
  VkSurfaceFormatKHR result = avaliableFormats[0];

  for (int i = 0; i < count; i++) {
    VkSurfaceFormatKHR format = avaliableFormats[i];
    if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && format.format == VK_FORMAT_B8G8R8A8_SRGB) {
      result = format;
      break;
    }
  }
  return result;
}
VkPresentModeKHR chooseSwapPresentMode(VkPresentModeKHR *avaliablePresentModes, int count) {
  VkPresentModeKHR result = VK_PRESENT_MODE_FIFO_KHR;
  for (int i = 0; i < count; i++) {
    VkPresentModeKHR presentMode = avaliablePresentModes[i];
    if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
      result = presentMode;
    }
  }
  return result;
}
int rateDeviceSuitabilty(VkPhysicalDevice device) {
  int score = 0;
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(device, &properties);

  if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
    score += 100;
  }

  QueueFamilyIndices indices = findQueueFamilies(device);
  if (!indices.isSetGraphicsFamily || !indices.isSetPresentFamily) {
    return -1;
  }
  return score;
}
VkShaderModule createShaderModule(const char *code, size_t size) {
  VkShaderModuleCreateInfo shaderCreateInfo = {0};
  shaderCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shaderCreateInfo.codeSize = size;
  shaderCreateInfo.pCode = (uint32_t *)code;
  VkShaderModule result;
  vkCreateShaderModule(app->device, &shaderCreateInfo, NULL, &result);
  return result;
}
void transition_image_layout(uint32_t imageIndex, VkImageLayout old_layout, VkImageLayout new_layout,
                             VkAccessFlags2 src_access_mask, VkAccessFlags2 dst_access_mask,
                             VkPipelineStageFlags2 src_stage_mask, VkPipelineStageFlags2 dst_stage_mask

) {
  VkImageMemoryBarrier2 barrier = {0};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.srcStageMask = src_stage_mask;
  barrier.srcAccessMask = src_access_mask;
  barrier.dstStageMask = dst_stage_mask;
  barrier.dstAccessMask = dst_access_mask;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = app->swapChainImages[imageIndex];
  barrier.subresourceRange = (VkImageSubresourceRange){.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                       .baseMipLevel = 0,
                                                       .levelCount = 1,
                                                       .baseArrayLayer = 0,
                                                       .layerCount = 1};
  VkDependencyFlags d_flags = {0};

  VkDependencyInfo dependency_info = (VkDependencyInfo){
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .dependencyFlags = d_flags,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(app->commandBuffers[app->frameIndex], &dependency_info);
}
uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties = {0};
  vkGetPhysicalDeviceMemoryProperties(app->physicalDevice, &memProperties);
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  throw("unable to find suitable device memory\n");
  return 0;
}
Create_Buffer_Return_Object createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                         VkMemoryPropertyFlags properties) {
  VkBufferCreateInfo bufferInfo = {0};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;

  VkBuffer buffer;
  vkCreateBuffer(app->device, &bufferInfo, VK_NULL_HANDLE, &buffer);

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(app->device, buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

  VkDeviceMemory deviceMemory;
  vkAllocateMemory(app->device, &allocInfo, VK_NULL_HANDLE, &deviceMemory);

  vkBindBufferMemory(app->device, buffer, deviceMemory, 0);

  Create_Buffer_Return_Object result;
  result.buffer = buffer;
  result.memory = deviceMemory;
  return result;
}
VkFormat findSupportedFormat(VkFormat *candidates, uint32_t candidateCount, VkImageTiling tiling,
                             VkFormatFeatureFlags features) {
  for (uint32_t i = 0; i < candidateCount; i++) {
    VkFormatProperties props = {0};
    vkGetPhysicalDeviceFormatProperties(app->physicalDevice, candidates[i], &props);
    if (((tiling == VK_IMAGE_TILING_LINEAR) && (props.linearTilingFeatures & features)) ||
        (tiling == VK_IMAGE_TILING_OPTIMAL) && (props.optimalTilingFeatures & features)) {
      return candidates[i];
    }
  }
  throw("unable to find supported format");
  return candidates[0];
}
VkFormat findDepthFormat() {
  VkFormat candidates[3] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
  return findSupportedFormat(candidates, 3, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}
void initWindow() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  app->window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "VulkanApp", NULL, NULL);
  glfwSetFramebufferSizeCallback(app->window, framebufferResizeCallback);
}
void createSurface() {
  if (glfwCreateWindowSurface(app->instance, app->window, NULL, &app->surface) != VK_SUCCESS) {
    throw("failed to create surface\n");
  }
}
void createInstance() {
  if (enableValidationLayers && !checkValidationLayerSupport()) {
    throw("validation layers not avaliable\n");
  }
  VkApplicationInfo appInfo = {0};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Hello triangle";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 3, 0);
  appInfo.engineVersion = VK_MAKE_VERSION(1, 3, 0);
  appInfo.pEngineName = "No engine";
  appInfo.apiVersion = VK_API_VERSION_1_4;

  VkInstanceCreateInfo createInfo = {0};
  createInfo.pApplicationInfo = &appInfo;
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

  uint32_t glfwExtensionCount = 0;
  const char **glfwExtensions;
  glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  createInfo.enabledExtensionCount = glfwExtensionCount;
  createInfo.ppEnabledExtensionNames = glfwExtensions;

  uint32_t extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);

  VkExtensionProperties *extensions = malloc(sizeof(VkExtensionProperties) * extensionCount);
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, extensions);

  for (uint32_t i = 0; i < extensionCount; i++) {
    printf("%s\n", extensions[i].extensionName);
  }

  if (enableValidationLayers) {
    createInfo.enabledLayerCount = vLayersCount;
    createInfo.ppEnabledLayerNames = validationLayers;
  } else {
    createInfo.enabledLayerCount = 0;
  }
  if (vkCreateInstance(&createInfo, NULL, &app->instance) != VK_SUCCESS) {
    printf("failed to create instance\n");
    exit(-1);
  }

  free(extensions);
}
void pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(app->instance, &deviceCount, NULL);

  if (deviceCount == 0) {
    throw("failed to find physical devices\n");
  }

  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  int deviceScore = 0;
  VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
  vkEnumeratePhysicalDevices(app->instance, &deviceCount, devices);

  for (uint32_t i = 0; i < deviceCount; i++) {
    int devScore = rateDeviceSuitabilty(devices[i]);
    if (devScore >= deviceScore) {
      physicalDevice = devices[i];
      deviceScore = devScore;
    }
  }

  if (physicalDevice == VK_NULL_HANDLE) {
    throw("failed to find suitable GPU\n");
  }
  app->physicalDevice = physicalDevice;
  free(devices);
}
void createLogicalDevice() {
  QueueFamilyIndices indices = findQueueFamilies(app->physicalDevice);
  int queueCount = 0;
  if (indices.presentFamily == indices.graphicsFamily) {
    queueCount = 1;
  } else {
    queueCount = 2;
  }
  VkDeviceQueueCreateInfo *queueCreateInfos = malloc(sizeof(VkDeviceQueueCreateInfo) * queueCount);

  float queuePriority = 1.0f;
  // dodgy implementation
  for (int i = 0; i < queueCount; i++) {
    VkDeviceQueueCreateInfo queueCreateInfo = {0};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = i == 0 ? indices.graphicsFamily : indices.presentFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos[i] = queueCreateInfo;
  }

  VkPhysicalDeviceVulkan13Features features13 = {0};
  features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;

  VkPhysicalDeviceVulkan11Features features11 = {0};
  features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
  features11.shaderDrawParameters = VK_TRUE;
  features11.pNext = &features13;

  VkPhysicalDeviceFeatures2 features2 = {0};
  features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features2.pNext = &features11;

  VkDeviceCreateInfo createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.pNext = &features2;
  createInfo.pQueueCreateInfos = queueCreateInfos;
  createInfo.queueCreateInfoCount = queueCount;
  createInfo.enabledExtensionCount = deviceExtensionCount;
  createInfo.ppEnabledExtensionNames = requiredDeviceExtension;

  if (vkCreateDevice(app->physicalDevice, &createInfo, NULL, &app->device) != VK_SUCCESS) {
    throw("failed to create logical device \n");
  }
  vkGetDeviceQueue(app->device, indices.graphicsFamily, 0, &app->graphicsQueue);
  vkGetDeviceQueue(app->device, indices.presentFamily, 0, &app->presentQueue);
  free(queueCreateInfos);
}
void createSwapChain() {
  VkSurfaceCapabilitiesKHR surfaceCapabilities = {0};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physicalDevice, app->surface, &surfaceCapabilities);
  app->swapChainExtent = chooseSwapExtent(surfaceCapabilities);

  uint32_t presentModeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(app->physicalDevice, app->surface, &presentModeCount, NULL);
  VkPresentModeKHR *avaliablePresentModes = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(app->physicalDevice, app->surface, &presentModeCount,
                                            avaliablePresentModes);

  uint32_t surfaceFormatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(app->physicalDevice, app->surface, &surfaceFormatCount, NULL);
  VkSurfaceFormatKHR *avaliableSurfaceFormats = malloc(sizeof(VkSurfaceFormatKHR) * surfaceFormatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(app->physicalDevice, app->surface, &surfaceFormatCount, avaliableSurfaceFormats);
  app->swapChainSurfaceFormat = chooseSwapSurfaceFormat(avaliableSurfaceFormats, surfaceFormatCount);

  uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);
  app->swapChainImageCount = minImageCount;
  app->swapChainImages = malloc(sizeof(VkImage) * minImageCount);

  printf("actual image count %u\n", app->swapChainImageCount);
  VkSwapchainCreateInfoKHR swapChainCreateInfo = {0};
  swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapChainCreateInfo.surface = app->surface;
  swapChainCreateInfo.minImageCount = minImageCount;
  swapChainCreateInfo.imageFormat = app->swapChainSurfaceFormat.format;
  swapChainCreateInfo.imageColorSpace = app->swapChainSurfaceFormat.colorSpace;
  swapChainCreateInfo.imageExtent = app->swapChainExtent;
  swapChainCreateInfo.imageArrayLayers = 1;
  swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapChainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
  swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapChainCreateInfo.presentMode = chooseSwapPresentMode(avaliablePresentModes, presentModeCount);
  swapChainCreateInfo.clipped = true;
  swapChainCreateInfo.oldSwapchain = NULL;

  vkCreateSwapchainKHR(app->device, &swapChainCreateInfo, NULL, &app->swapChain);
  vkGetSwapchainImagesKHR(app->device, app->swapChain, &app->swapChainImageCount, NULL);
  app->swapChainImages = realloc(app->swapChainImages, sizeof(VkImage) * app->swapChainImageCount);
  vkGetSwapchainImagesKHR(app->device, app->swapChain, &app->swapChainImageCount, app->swapChainImages);

  free(avaliableSurfaceFormats);
  free(avaliablePresentModes);
}
void createImageViews() {
  VkImageViewCreateInfo imageViewCreateInfo = {0};
  imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewCreateInfo.format = app->swapChainSurfaceFormat.format;
  imageViewCreateInfo.subresourceRange =
      (VkImageSubresourceRange){.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1};

  app->swapChainImageViewCount = app->swapChainImageCount;
  app->swapChainImageViews = malloc(sizeof(VkImageView) * app->swapChainImageViewCount);

  for (uint32_t i = 0; i < app->swapChainImageCount; i++) {
    imageViewCreateInfo.image = app->swapChainImages[i];
    VkImageView imageView;
    vkCreateImageView(app->device, &imageViewCreateInfo, NULL, &imageView);
    app->swapChainImageViews[i] = imageView;
  }
}
void createDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding uboLayoutBinding = {0};
  uboLayoutBinding.binding = 0;
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.descriptorCount = 1;
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkDescriptorSetLayoutCreateInfo createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  createInfo.bindingCount = 1;
  createInfo.pBindings = &uboLayoutBinding;

  vkCreateDescriptorSetLayout(app->device, &createInfo, VK_NULL_HANDLE, &app->descriptorLayout);
}
void createGraphicsPipeline() {
  size_t codeSize;
  char *shaderCode = readFile("slang.spv", &codeSize);
  VkShaderModule shader = createShaderModule(shaderCode, codeSize);

  VkPipelineShaderStageCreateInfo vertShaderStageInfo = {0};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = shader;
  vertShaderStageInfo.pName = "vertMain";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo = {0};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = shader;
  fragShaderStageInfo.pName = "fragMain";

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

  VkVertexInputBindingDescription bindingDescription = getVertexBindingDescription();
  Vertex_Attribute_Description_result attributeDescription = getVertexAtrributeDescription();

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount = attributeDescription.count;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescription.attribs;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewportState = {0};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer = {0};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisampling = {0};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.sampleShadingEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {0};
  colorBlendAttachment.blendEnable = VK_FALSE;
  colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo colorBlending = {0};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState = {0};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.pushConstantRangeCount = 0;
  pipelineLayoutInfo.pSetLayouts = &app->descriptorLayout;
  pipelineLayoutInfo.setLayoutCount = 1;

  vkCreatePipelineLayout(app->device, &pipelineLayoutInfo, NULL, &app->pipelineLayout);

  VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo = {0};
  pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  pipelineRenderingCreateInfo.colorAttachmentCount = 1;
  pipelineRenderingCreateInfo.pColorAttachmentFormats = &app->swapChainSurfaceFormat.format;

  VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo = {0};
  graphicsPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  graphicsPipelineCreateInfo.stageCount = 2;
  graphicsPipelineCreateInfo.pStages = shaderStages;
  graphicsPipelineCreateInfo.pVertexInputState = &vertexInputInfo;
  graphicsPipelineCreateInfo.pViewportState = &viewportState;
  graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssembly;
  graphicsPipelineCreateInfo.pRasterizationState = &rasterizer;
  graphicsPipelineCreateInfo.pMultisampleState = &multisampling;
  graphicsPipelineCreateInfo.pColorBlendState = &colorBlending;
  graphicsPipelineCreateInfo.pDynamicState = &dynamicState;
  graphicsPipelineCreateInfo.layout = app->pipelineLayout;
  graphicsPipelineCreateInfo.renderPass = NULL;
  graphicsPipelineCreateInfo.pNext = &pipelineRenderingCreateInfo;

  vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, VK_NULL_HANDLE,
                            &app->graphicsPipeline);
  free(shaderCode);
  free(attributeDescription.attribs);
}
void createCommandPool() {
  VkCommandPoolCreateInfo poolInfo = {0};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = app->queueIndex;

  vkCreateCommandPool(app->device, &poolInfo, VK_NULL_HANDLE, &app->commandPool);
}
void createCommandBuffers() {
  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = app->commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 2;

  vkAllocateCommandBuffers(app->device, &allocInfo, app->commandBuffers);
}
void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
  VkCommandBufferAllocateInfo bufferInfo = {0};
  bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  bufferInfo.commandPool = app->commandPool;
  bufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  bufferInfo.commandBufferCount = 1;

  VkCommandBuffer copyCommandBuffer = {0};
  vkAllocateCommandBuffers(app->device, &bufferInfo, &copyCommandBuffer);
  VkCommandBufferBeginInfo beginInfo;
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(copyCommandBuffer, &beginInfo);

  VkBufferCopy copyRegion = {0};
  copyRegion.size = size;
  copyRegion.srcOffset = 0;
  copyRegion.dstOffset = 0;

  vkCmdCopyBuffer(copyCommandBuffer, src, dst, 1, &copyRegion);

  vkEndCommandBuffer(copyCommandBuffer);

  VkSubmitInfo submitInfo;
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &copyCommandBuffer;

  vkQueueSubmit(app->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(app->graphicsQueue);

  vkFreeCommandBuffers(app->device, app->commandPool, 1, &copyCommandBuffer);
}
void createVertexBuffer() {
  VkDeviceSize allocSize = sizeof(vertices[0]) * 5;
  Create_Buffer_Return_Object stagingBuffer =
      createBuffer(allocSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  void *data;
  vkMapMemory(app->device, stagingBuffer.memory, 0, allocSize, 0, &data);
  memcpy(data, vertices, allocSize);
  vkUnmapMemory(app->device, stagingBuffer.memory);

  Create_Buffer_Return_Object vertexBuffer =
      createBuffer(allocSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  copyBuffer(stagingBuffer.buffer, vertexBuffer.buffer, allocSize);
  app->vertexBufferMemory = vertexBuffer.memory;
  app->vertexBuffer = vertexBuffer.buffer;
}
void createIndexBuffer() {
  VkDeviceSize bufferSize = sizeof(indices);
  Create_Buffer_Return_Object stagingBuffer =
      createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  void *data;
  vkMapMemory(app->device, stagingBuffer.memory, 0, bufferSize, 0, &data);
  memcpy(data, indices, bufferSize);
  vkUnmapMemory(app->device, stagingBuffer.memory);

  Create_Buffer_Return_Object indexBuffer =
      createBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  copyBuffer(stagingBuffer.buffer, indexBuffer.buffer, bufferSize);
  app->indexBufferMemory = indexBuffer.memory;
  app->indexBuffer = indexBuffer.buffer;
}
void createTextureImage() {
  int texWidth, texHeight, texChannels;
  stbi_uc *pixels = stbi_load("textures.texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
  if (!pixels) {
    throw("failed to load texture image\n");
  }
}
void createDepthResources() { VkFormat depthFormat = findDepthFormat(); }
void createUniformBuffers() {
  app->uniformBuffersCount = FRAMES_IN_FLIGHT;
  for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    Create_Buffer_Return_Object buffer =
        createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    vkMapMemory(app->device, buffer.memory, 0, bufferSize, 0, &app->uniformBuffersMapped[i]);
    app->uniformBuffers[i] = buffer.buffer;
    app->uniformBufferMemory[i] = buffer.memory;
  }
}
void createDescriptorPools() {
  VkDescriptorPoolSize poolSize = {0};
  poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSize.descriptorCount = FRAMES_IN_FLIGHT;
  VkDescriptorPoolCreateInfo pInfo = {0};
  pInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pInfo.maxSets = FRAMES_IN_FLIGHT;
  pInfo.poolSizeCount = 1;
  pInfo.pPoolSizes = &poolSize;

  vkCreateDescriptorPool(app->device, &pInfo, VK_NULL_HANDLE, &app->descriptorPool);
}
void createDescriptorSets() {
  VkDescriptorSetLayout layouts[FRAMES_IN_FLIGHT];
  for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
    layouts[i] = app->descriptorLayout;
  }
  VkDescriptorSetAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = app->descriptorPool;
  allocInfo.descriptorSetCount = FRAMES_IN_FLIGHT;
  allocInfo.pSetLayouts = layouts;

  vkAllocateDescriptorSets(app->device, &allocInfo, app->descriptorSets);

  for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
    VkDescriptorBufferInfo bufferInfo = {0};
    bufferInfo.buffer = app->uniformBuffers[i];
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet descriptorWrite = {0};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = app->descriptorSets[i];
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(app->device, 1, &descriptorWrite, 0, VK_NULL_HANDLE);
  }
}
void createSyncObjects() {
  VkSemaphoreCreateInfo semCreateInfo = {0};
  semCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fenceCreateInfo = {0};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  vkCreateSemaphore(app->device, &semCreateInfo, VK_NULL_HANDLE, &app->presentCompleteSemaphore[0]);
  vkCreateSemaphore(app->device, &semCreateInfo, VK_NULL_HANDLE, &app->presentCompleteSemaphore[1]);

  vkCreateSemaphore(app->device, &semCreateInfo, VK_NULL_HANDLE, &app->renderFinishedSemaphore[0]);
  vkCreateSemaphore(app->device, &semCreateInfo, VK_NULL_HANDLE, &app->renderFinishedSemaphore[1]);

  vkCreateFence(app->device, &fenceCreateInfo, VK_NULL_HANDLE, &app->inFlightFences[0]);
  vkCreateFence(app->device, &fenceCreateInfo, VK_NULL_HANDLE, &app->inFlightFences[1]);
}
void cleanupSwapchain() {
  for (int i = 0; i < app->swapChainImageViewCount; i++) {
    vkDestroyImageView(app->device, app->swapChainImageViews[i], VK_NULL_HANDLE);
  }
  vkDestroySwapchainKHR(app->device, app->swapChain, VK_NULL_HANDLE);
  free(app->swapChainImages);
  free(app->swapChainImageViews);
  app->swapChainImageViews = NULL;
  app->swapChainImages = NULL;
}
void recreateSwapchain() {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(app->window, &width, &height);
  while ((width == 0 || height == 0) && !glfwWindowShouldClose(app->window)) {
    glfwGetFramebufferSize(app->window, &width, &height);
    glfwWaitEvents();
  }
  if (glfwWindowShouldClose(app->window)) {
    return;
  }

  vkDeviceWaitIdle(app->device);
  cleanupSwapchain();
  createSwapChain();
  createImageViews();
}
void recordCommandBuffer(uint32_t imageIndex) {
  VkCommandBufferBeginInfo bInfo = {0};
  bInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(app->commandBuffers[app->frameIndex], &bInfo);

  transition_image_layout(imageIndex, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          (VkAccessFlags){}, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
  VkClearValue clearColour = {0};
  clearColour.color = (VkClearColorValue){{0.0f, 0.0f, 0.0f, 1.0f}};

  VkRenderingAttachmentInfo attachmentInfo = {0};
  attachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  attachmentInfo.imageView = app->swapChainImageViews[imageIndex];
  attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachmentInfo.clearValue = clearColour;

  VkRenderingInfo renderingInfo = {0};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = (VkRect2D){.offset = {0, 0}, .extent = app->swapChainExtent};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &attachmentInfo;

  VkViewport viewport = {0};
  viewport.width = (float)app->swapChainExtent.width;
  viewport.height = -(float)(app->swapChainExtent.height);
  viewport.x = 0;
  viewport.y = (float)app->swapChainExtent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor = {0};
  scissor.extent = app->swapChainExtent;
  VkDeviceSize offsets[] = {0};

  vkCmdBeginRendering(app->commandBuffers[app->frameIndex], &renderingInfo);
  vkCmdBindPipeline(app->commandBuffers[app->frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
  vkCmdSetViewport(app->commandBuffers[app->frameIndex], 0, 1, &viewport);
  vkCmdSetScissor(app->commandBuffers[app->frameIndex], 0, 1, &scissor);
  vkCmdBindVertexBuffers(app->commandBuffers[app->frameIndex], 0, 1, &app->vertexBuffer, offsets);
  vkCmdBindIndexBuffer(app->commandBuffers[app->frameIndex], app->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdBindDescriptorSets(app->commandBuffers[app->frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipelineLayout, 0,
                          1, &app->descriptorSets[app->frameIndex], 0, VK_NULL_HANDLE);
  vkCmdDrawIndexed(app->commandBuffers[app->frameIndex], 12, 1, 0, 0, 0);
  vkCmdEndRendering(app->commandBuffers[app->frameIndex]);

  transition_image_layout(imageIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, (VkAccessFlags){},
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

  vkEndCommandBuffer(app->commandBuffers[app->frameIndex]);
}
void updateUniformBuffer(uint32_t currentImage) {
  float currentTime = glfwGetTime();

  UniformBufferObject ubo = {0};
  vec3s worldPos = {0};
  worldPos.z += sinf(glm_rad(currentTime * 100.0f));
  ubo.model = glms_mat4_identity();
  ubo.model = glms_translate_make(worldPos);
  ubo.model = glms_rotate(ubo.model, currentTime * glm_rad(90), (vec3s){{0.0f, 0.0f, 1.0f}});
  ubo.view = glms_lookat((vec3s){{2.0f, 2.0f, 2.0f}}, (vec3s){{0.0f, 0.0f, 0.0f}}, (vec3s){{0.0f, 0.0f, 1.0f}});
  float aspectRatio = (float)app->swapChainExtent.width / (float)app->swapChainExtent.height;
  ubo.projection = glms_perspective_rh_zo(glm_rad(45), aspectRatio, 0.1f, 10.0f);

  memcpy(app->uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}
void drawFrame() {
  VkResult f_result = vkWaitForFences(app->device, 1, &app->inFlightFences[app->frameIndex], VK_TRUE, UINT32_MAX);
  if (f_result != VK_SUCCESS) {
    throw("failed to wait for fences\n");
  }

  uint32_t imageIndex;
  VkResult result = vkAcquireNextImageKHR(app->device, app->swapChain, UINT32_MAX,
                                          app->presentCompleteSemaphore[app->frameIndex], VK_NULL_HANDLE, &imageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapchain();
    return;
  }

  vkResetFences(app->device, 1, &app->inFlightFences[app->frameIndex]);
  recordCommandBuffer(imageIndex);
  updateUniformBuffer(app->frameIndex);
  vkQueueWaitIdle(app->presentQueue);

  VkPipelineStageFlags waitDestinationStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submitInfo = {0};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &app->presentCompleteSemaphore[app->frameIndex];
  submitInfo.pWaitDstStageMask = &waitDestinationStageMask;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &app->commandBuffers[app->frameIndex];
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &app->renderFinishedSemaphore[app->frameIndex];

  vkQueueSubmit(app->graphicsQueue, 1, &submitInfo, app->inFlightFences[app->frameIndex]);
  VkPresentInfoKHR presentInfoKHR = {0};
  presentInfoKHR.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfoKHR.waitSemaphoreCount = 1;
  presentInfoKHR.pWaitSemaphores = &app->renderFinishedSemaphore[app->frameIndex];
  presentInfoKHR.swapchainCount = 1;
  presentInfoKHR.pSwapchains = &app->swapChain;
  presentInfoKHR.pImageIndices = &imageIndex;

  result = vkQueuePresentKHR(app->presentQueue, &presentInfoKHR);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || app->framebufferResized) {
    recreateSwapchain();
    app->framebufferResized = false;
  }
  app->frameIndex = (app->frameIndex + 1) % FRAMES_IN_FLIGHT;
}
void initVulkan() {
  createInstance();
  createSurface();
  pickPhysicalDevice();
  createLogicalDevice();
  createSwapChain();
  createImageViews();
  createDescriptorSetLayout();
  createGraphicsPipeline();
  createCommandPool();
  createVertexBuffer();
  createIndexBuffer();
  createUniformBuffers();
  createDescriptorPools();
  createDescriptorSets();
  createCommandBuffers();
  createSyncObjects();
}
void cleanup() {
  cleanupSwapchain();
  vkDestroySurfaceKHR(app->instance, app->surface, NULL);
  vkDestroyDevice(app->device, NULL);
  vkDestroyInstance(app->instance, NULL);
  glfwDestroyWindow(app->window);
  glfwTerminate();
}
void mainLoop() {
  while (!glfwWindowShouldClose(app->window)) {
    glfwPollEvents();
    drawFrame();
  }
  vkDeviceWaitIdle(app->device);
  cleanup();
}

int main() {
  app = malloc(sizeof(App));
  app->framebufferResized = false;
  app->inFlightFences = malloc(sizeof(VkFence) * FRAMES_IN_FLIGHT);
  app->presentCompleteSemaphore = malloc(sizeof(VkSemaphore) * FRAMES_IN_FLIGHT);
  app->renderFinishedSemaphore = malloc(sizeof(VkSemaphore) * FRAMES_IN_FLIGHT);
  app->commandBuffers = malloc(sizeof(VkCommandBuffer) * FRAMES_IN_FLIGHT);
  app->uniformBuffers = malloc(sizeof(VkBuffer) * FRAMES_IN_FLIGHT);
  app->uniformBufferMemory = malloc(sizeof(VkDeviceMemory) * FRAMES_IN_FLIGHT);
  app->uniformBuffersMapped = malloc(sizeof(void *) * FRAMES_IN_FLIGHT);
  app->descriptorSets = malloc(sizeof(VkDescriptorSet) * FRAMES_IN_FLIGHT);
  app->frameIndex = 0;
  app->lastTime = 0.0f;
  initWindow();
  initVulkan();
  mainLoop();
  return 0;
}

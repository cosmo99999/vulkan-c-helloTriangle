#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

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
  VkPipelineLayout pipelineLayout;
  VkPipeline graphicsPipeline;
  VkCommandPool commandPool;
  VkCommandBuffer commandBuffer;
  uint32_t queueIndex;
  VkSemaphore presentCompleteSemaphore;
  VkSemaphore renderFinishedSemaphore;
  VkFence drawFence;
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
  vkCmdPipelineBarrier2(app->commandBuffer, &dependency_info);
}
void initWindow() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  app->window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "VulkanApp", NULL, NULL);
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

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
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
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
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
}
void createCommandPool() {
  VkCommandPoolCreateInfo poolInfo = {0};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = app->queueIndex;

  vkCreateCommandPool(app->device, &poolInfo, VK_NULL_HANDLE, &app->commandPool);
}
void createCommandBuffer() {
  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = app->commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  vkAllocateCommandBuffers(app->device, &allocInfo, &app->commandBuffer);
}
void createSyncObjects() {
  VkSemaphoreCreateInfo semCreateInfo = {0};
  semCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fenceCreateInfo = {0};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  vkCreateSemaphore(app->device, &semCreateInfo, VK_NULL_HANDLE, &app->presentCompleteSemaphore);
  vkCreateSemaphore(app->device, &semCreateInfo, VK_NULL_HANDLE, &app->renderFinishedSemaphore);
  vkCreateFence(app->device, &fenceCreateInfo, VK_NULL_HANDLE, &app->drawFence);
}
void recordCommandBuffer(uint32_t imageIndex) {
  VkCommandBufferBeginInfo bInfo = {0};
  bInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(app->commandBuffer, &bInfo);
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
  viewport.width = app->swapChainExtent.width;
  viewport.height = app->swapChainExtent.height;
  VkRect2D scissor = {0};
  scissor.extent = app->swapChainExtent;

  vkCmdBeginRendering(app->commandBuffer, &renderingInfo);
  vkCmdBindPipeline(app->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
  vkCmdSetViewport(app->commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(app->commandBuffer, 0, 1, &scissor);
  vkCmdDraw(app->commandBuffer, 3, 0, 0, 0);
  vkCmdEndRendering(app->commandBuffer);

  transition_image_layout(imageIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, (VkAccessFlags){},
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

  vkEndCommandBuffer(app->commandBuffer);
}

void drawFrame() {
  VkResult f_result = vkWaitForFences(app->device, 1, &app->drawFence, VK_TRUE, UINT32_MAX);
  if (f_result != VK_SUCCESS) {
    throw("failed to wait for fences\n");
  }
  vkResetFences(app->device, 1, &app->drawFence);

  uint32_t imageIndex;
  vkAcquireNextImageKHR(app->device, app->swapChain, UINT32_MAX, app->presentCompleteSemaphore, VK_NULL_HANDLE,
                        &imageIndex);
  recordCommandBuffer(imageIndex);
  vkQueueWaitIdle(app->presentQueue);

  VkPipelineStageFlags waitDestinationStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submitInfo = {0};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &app->presentCompleteSemaphore;
  submitInfo.pWaitDstStageMask = &waitDestinationStageMask;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &app->commandBuffer;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &app->renderFinishedSemaphore;

  vkQueueSubmit(app->graphicsQueue, 1, &submitInfo, app->drawFence);
  VkPresentInfoKHR presentInfoKHR = {0};
  presentInfoKHR.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfoKHR.waitSemaphoreCount = 1;
  presentInfoKHR.pWaitSemaphores = &app->renderFinishedSemaphore;
  presentInfoKHR.swapchainCount = 1;
  presentInfoKHR.pSwapchains = &app->swapChain;
  presentInfoKHR.pImageIndices = &imageIndex;

  VkResult result = vkQueuePresentKHR(app->presentQueue, &presentInfoKHR);
  switch (result) {
  case VK_SUCCESS: {
    break;
  }
  case VK_SUBOPTIMAL_KHR: {
    printf("sub optimal draw result \n");
    break;
  }
  default: {
    throw("unexpected draw result\n");
    break;
  }
  }
}
void initVulkan() {
  createInstance();
  createSurface();
  pickPhysicalDevice();
  createLogicalDevice();
  createSwapChain();
  createImageViews();
  createGraphicsPipeline();
  createCommandPool();
  createCommandBuffer();
  createSyncObjects();
}
void cleanup() {
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
  initWindow();
  initVulkan();
  mainLoop();
  return 0;
}

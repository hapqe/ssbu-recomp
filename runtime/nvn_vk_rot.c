/* Android pre-rotation (2026-10-21). Included by nvn_vk_draw.c: render thread only, uses that file's device (V), loader
 * functions, memory and command helpers.
 *
 * A phone held in landscape has a portrait display. With the identity transform the compositor turned every frame itself:
 * SurfaceFlinger composed our layer on the GPU (CLIENT composition, ROT_90), beside the game's own GPU work and a frame
 * later. Now (V.rot, nvn_vk.c swapchain_create) the swapchain has the display's transform and portrait shape; the frame is
 * drawn as before into landscape images of our own that stand in V.images (the game's picture, the menu, the progress
 * bar: V.extent stays landscape), and rot_draw turns it into the swapchain's image with one triangle just before the
 * present. One landscape image, two views and one descriptor set per swapchain image; made again with the swapchain. */
#include "rot_spv.h"

static struct {
    VkSwapchainKHR sc;                             /* the swapchain the images below were made for */
    VkImage img[16]; VMem mem[16]; VkImageView src[16], dst[16]; VkDescriptorSet set[16];
    VkFormat fmt; VkPipeline pipe; VkPipelineLayout layout; VkDescriptorSetLayout dsl; VkDescriptorPool pool; VkSampler smp;
} g_rot;

static void rot_drop(void) {                       /* nvn_vk.c swapchain_destroy: the device is idle */
    for (int i = 0; i < 16; i++) {
        if (g_rot.src[i]) vkDestroyImageView(V.device, g_rot.src[i], NULL);
        if (g_rot.dst[i]) vkDestroyImageView(V.device, g_rot.dst[i], NULL);
        if (g_rot.img[i]) { vkDestroyImage(V.device, g_rot.img[i], NULL); vmem_free(g_rot.mem[i]); }
        g_rot.src[i] = g_rot.dst[i] = NULL; g_rot.img[i] = NULL; g_rot.set[i] = NULL;
    }
    if (g_rot.pool) vkResetDescriptorPool(V.device, g_rot.pool, 0);
    g_rot.sc = NULL;
}
static void rot_pipeline(VkFormat fmt) {
    if (g_rot.pipe && g_rot.fmt == fmt) return;
    if (g_rot.pipe) { vkDestroyPipeline(V.device, g_rot.pipe, NULL); g_rot.pipe = NULL; }      /* only after a new swapchain: the device was idle */
    if (!g_rot.layout) {
        VkDescriptorSetLayoutBinding b = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
        VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.bindingCount = 1; dl.pBindings = &b;
        VKC(vkCreateDescriptorSetLayout(V.device, &dl, NULL, &g_rot.dsl));
        VkPushConstantRange pc = {VK_SHADER_STAGE_VERTEX_BIT, 0, 4};
        VkPipelineLayoutCreateInfo li = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.setLayoutCount = 1; li.pSetLayouts = &g_rot.dsl; li.pushConstantRangeCount = 1; li.pPushConstantRanges = &pc;
        VKC(vkCreatePipelineLayout(V.device, &li, NULL, &g_rot.layout));
        VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
        VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 16; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        VKC(vkCreateDescriptorPool(V.device, &pi, NULL, &g_rot.pool));
        VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_NEAREST; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;     /* pixel for pixel */
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VKC(vkCreateSampler(V.device, &si, NULL, &g_rot.smp));
    }
    VkShaderModuleCreateInfo mi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkShaderModule vs, fs;
    mi.codeSize = sizeof k_rot_vert_spv; mi.pCode = k_rot_vert_spv; VKC(vkCreateShaderModule(V.device, &mi, NULL, &vs));
    mi.codeSize = sizeof k_rot_frag_spv; mi.pCode = k_rot_frag_spv; VKC(vkCreateShaderModule(V.device, &mi, NULL, &fs));
    VkPipelineShaderStageCreateInfo st[2];
    memset(st, 0, sizeof st);
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vs; st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fs; st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState ba;
    memset(&ba, 0, sizeof ba);
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &ba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo rinfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rinfo.colorAttachmentCount = 1; rinfo.pColorAttachmentFormats = &fmt;
    VkGraphicsPipelineCreateInfo gi = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gi.pNext = &rinfo; gi.stageCount = 2; gi.pStages = st; gi.pVertexInputState = &vi; gi.pInputAssemblyState = &ia; gi.pViewportState = &vp;
    gi.pRasterizationState = &rs; gi.pMultisampleState = &ms; gi.pDepthStencilState = &ds; gi.pColorBlendState = &cb; gi.pDynamicState = &dy; gi.layout = g_rot.layout;
    VKC(vkCreateGraphicsPipelines(V.device, NULL, 1, &gi, NULL, &g_rot.pipe));
    vkDestroyShaderModule(V.device, vs, NULL);
    vkDestroyShaderModule(V.device, fs, NULL);
    g_rot.fmt = fmt;
}
/* the present job, once the image to draw into is known: the landscape images of this swapchain in V.images */
static void rot_images(void) {
    if (!V.rot || g_rot.sc == V.swapchain) return;
    rot_drop();
    rot_pipeline(V.format.format);
    for (uint32_t i = 0; i < V.image_count && i < 16; i++) {
        VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        VkMemoryRequirements rq;
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = V.format.format; ci.extent.width = V.extent.width; ci.extent.height = V.extent.height; ci.extent.depth = 1;
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VKC(vkCreateImage(V.device, &ci, NULL, &g_rot.img[i]));
        vkGetImageMemoryRequirements(V.device, g_rot.img[i], &rq);
        g_rot.mem[i] = vmem_alloc(rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        VKC(vkBindImageMemory(V.device, g_rot.img[i], g_rot.mem[i].mem, g_rot.mem[i].off));
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = V.format.format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
        vi.image = g_rot.img[i]; VKC(vkCreateImageView(V.device, &vi, NULL, &g_rot.src[i]));
        vi.image = V.sc_images[i]; VKC(vkCreateImageView(V.device, &vi, NULL, &g_rot.dst[i]));
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = g_rot.pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &g_rot.dsl;
        VKC(vkAllocateDescriptorSets(V.device, &ai, &g_rot.set[i]));
        VkDescriptorImageInfo ii = {g_rot.smp, g_rot.src[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wr.dstSet = g_rot.set[i]; wr.descriptorCount = 1; wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.pImageInfo = &ii;
        vkUpdateDescriptorSets(V.device, 1, &wr, 0, NULL);
        V.images[i] = g_rot.img[i];
    }
    g_rot.sc = V.swapchain;
    printf("nvn_vk: the swapchain is the display's %ux%u, turned %s: the frame is drawn %ux%u and turned into it\n", (unsigned)V.extent.height,
           (unsigned)V.extent.width, V.rot == 1 ? "clockwise" : "anticlockwise", (unsigned)V.extent.width, (unsigned)V.extent.height);
}
/* the present job: the landscape image (TRANSFER_DST, everything drawn) turned into the swapchain's image, left in PRESENT_SRC */
static void rot_draw(uint32_t index) {
    uint32_t W = V.extent.height, H = V.extent.width;      /* the swapchain image: portrait */
    image_barrier(V.images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    image_barrier(V.sc_images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    ca.imageView = g_rot.dst[index]; ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; ca.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent.width = W; ri.renderArea.extent.height = H; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca;
    CKPT_OP("pre-rotation pass (swapchain image, index)", V.sc_images[index], index, W, H);
    vkCmdBeginRendering(R.cmd, &ri);
    vkCmdBindPipeline(R.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_rot.pipe);
    vkCmdBindDescriptorSets(R.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_rot.layout, 0, 1, &g_rot.set[index], 0, NULL);
    int32_t cw = V.rot == 1;
    vkCmdPushConstants(R.cmd, g_rot.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof cw, &cw);
    VkViewport vp = {0, 0, (float)W, (float)H, 0, 1};
    VkRect2D sc = {{0, 0}, {W, H}};
    vkCmdSetViewport(R.cmd, 0, 1, &vp);
    vkCmdSetScissor(R.cmd, 0, 1, &sc);
    vkCmdDraw(R.cmd, 3, 1, 0, 0);
    vkCmdEndRendering(R.cmd);
    image_barrier(V.sc_images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    R.pipe_bound = NULL; R.dyn_dirty = R.vi_dirty = 1; R.dyn3_dirty = 1;      /* the game's state tracking: nothing of ours stays bound for it */
}

/* 2026-10-22: THE FRAME DISPLAY. Included by nvn_vk_draw.c after the dot font (k_dot_ch, k_dot_gl): render thread only,
 * uses that file's device (V), memory and command helpers.
 *
 * Two lines in the top left corner of the picture, over a dark box: frames presented per second, then the mean time
 * between them and the longest one of the last half second ("59.8 FPS" / "16.7 MS MAX 21"). The frames per second are
 * green from 58, yellow from 45, red below. On when SSBU_FPS=1, or with graphics.ini frame_time_overlay = on in a build
 * without the settings menu (Android, a build without Dear ImGui: the menu draws its own box, menu.cpp draw_overlay);
 * SSBU_FPS=0 turns it off. The Android launcher's "Show frame rate" sets SSBU_FPS=1.
 *
 * Cost: the text is drawn by the CPU into a staging buffer of FPS_W x FPS_H pixels twice a second (three buffers in turn,
 * so that one a frame in flight copies from is never written), copied into one small image, and that image is blitted
 * over the presented frame scaled up (nearest) with one vkCmdBlitImage per frame. */
#define FPS_W 128
#define FPS_H 18
int menu_enabled(void);                            /* menu.cpp / menu_stub.c */
static struct {
    int on, made, cur, tw, have;
    VkImage img; VMem mem; VkBuffer buf[3]; VMem bmem[3];
    LONG64 t0, f0, tlast, flast; double gap_max;
} g_fps = {.on = -1};

static int fps_on(void) {
    if (g_fps.on == -1) {
        const char *e = getenv("SSBU_FPS");
        g_fps.on = e && e[0] ? atoi(e) != 0 : -2;      /* -2: graphics.ini decides, read at every frame (the menu can change it) */
        if (g_fps.on == 1) printf("nvn_vk: frame display on (SSBU_FPS=1): frames per second and frame time in the top left corner\n");
    }
    if (g_fps.on >= 0) return g_fps.on;
    return gfx_options()->overlay && !menu_enabled();
}
static void fps_text(uint32_t *px, int y, const char *s, uint32_t col) {
    for (int x = 1; *s && x + 5 <= FPS_W; s++, x += 6) {
        const char *q = strchr(k_dot_ch, *s);
        if (*s == ' ' || !q) continue;
        const uint8_t *g = k_dot_gl[q - k_dot_ch];
        for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) if (g[r] >> (4 - c) & 1) px[(y + r) * FPS_W + x + c] = col;
    }
}
/* the present job, the swapchain image (landscape) in TRANSFER_DST with the game's picture in it */
static void fps_draw(uint32_t index) {
    if (!fps_on()) return;
    LARGE_INTEGER fq, now;
    QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&now);
    LONG64 f = g_vk_frames;
    if (!g_fps.t0) { g_fps.t0 = g_fps.tlast = now.QuadPart; g_fps.f0 = g_fps.flast = f; }
    if (f != g_fps.flast) {                        /* a new frame of the game: the time since the one before */
        double gap = (double)(now.QuadPart - g_fps.tlast) * 1000.0 / (double)fq.QuadPart / (double)(f - g_fps.flast);
        if (gap > g_fps.gap_max) g_fps.gap_max = gap;
        g_fps.tlast = now.QuadPart; g_fps.flast = f;
    }
    if (!g_fps.buf[0]) for (int i = 0; i < 3; i++) g_fps.buf[i] = vbuf_new((uint64_t)FPS_W * FPS_H * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &g_fps.bmem[i]);
    if (!g_fps.made) {
        VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        VkMemoryRequirements rq;
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = VK_FORMAT_R8G8B8A8_UNORM; ci.extent.width = FPS_W; ci.extent.height = FPS_H; ci.extent.depth = 1;
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VKC(vkCreateImage(V.device, &ci, NULL, &g_fps.img));
        vkGetImageMemoryRequirements(V.device, g_fps.img, &rq);
        g_fps.mem = vmem_alloc(rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        VKC(vkBindImageMemory(V.device, g_fps.img, g_fps.mem.mem, g_fps.mem.off));
        g_fps.made = 1;
    }
    if (now.QuadPart - g_fps.t0 >= fq.QuadPart / 2) {      /* twice a second: new text */
        double secs = (double)(now.QuadPart - g_fps.t0) / (double)fq.QuadPart, fps = (double)(f - g_fps.f0) / secs;
        char l0[24], l1[24];
        snprintf(l0, sizeof l0, "%.1f FPS", fps);
        snprintf(l1, sizeof l1, "%.1f MS MAX %.0f", f > g_fps.f0 ? secs * 1000.0 / (double)(f - g_fps.f0) : 0.0, g_fps.gap_max);
        g_fps.t0 = now.QuadPart; g_fps.f0 = f; g_fps.gap_max = 0;
        int next = (g_fps.cur + 1) % 3, n = (int)(strlen(l0) > strlen(l1) ? strlen(l0) : strlen(l1));
        uint32_t *px = (uint32_t *)g_fps.bmem[next].map;
        for (int i = 0; i < FPS_W * FPS_H; i++) px[i] = 0xFF141414u;      /* R G B A in memory order */
        fps_text(px, 1, l0, fps >= 58.0 ? 0xFF60E060u : fps >= 45.0 ? 0xFF40D0FFu : 0xFF6060FFu);
        fps_text(px, 10, l1, 0xFFEBEBEBu);
        g_fps.tw = n * 6 + 1 < FPS_W ? n * 6 + 1 : FPS_W;
        image_barrier(g_fps.img, VK_IMAGE_ASPECT_COLOR_BIT, g_fps.have ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy cp;
        memset(&cp, 0, sizeof cp);
        cp.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; cp.imageSubresource.layerCount = 1;
        cp.imageExtent.width = FPS_W; cp.imageExtent.height = FPS_H; cp.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(R.cmd, g_fps.buf[next], g_fps.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
        image_barrier(g_fps.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        g_fps.cur = next; g_fps.have = 1;
    }
    if (!g_fps.have) return;                       /* the first half second: nothing measured yet */
    void display_fit(int sw, int sh, int dw, int dh, int out[4]);
    int W = (int)V.extent.width, H = (int)V.extent.height, d4[4], dot = H / 360 > 1 ? H / 360 : 1;
    { int pw, ph; pic_shape(&pw, &ph); display_fit(pw, ph, W, H, d4); }      /* in the picture, not in a black bar */
    int x = d4[0] + dot * 4, y = d4[1] + dot * 4;
    if (x + g_fps.tw * dot > W || y + FPS_H * dot > H) return;
    VkImageMemoryBarrier ib;                       /* the picture's blit is written before ours writes over a part of it */
    memset(&ib, 0, sizeof ib);
    ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; ib.srcAccessMask = ib.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ib.oldLayout = ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = V.images[index]; ib.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; ib.subresourceRange.levelCount = 1; ib.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(R.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
    VkImageBlit b;
    memset(&b, 0, sizeof b);
    b.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; b.srcSubresource.layerCount = 1; b.dstSubresource = b.srcSubresource;
    b.srcOffsets[1].x = g_fps.tw; b.srcOffsets[1].y = FPS_H; b.srcOffsets[1].z = 1;
    b.dstOffsets[0].x = x; b.dstOffsets[0].y = y; b.dstOffsets[1].x = x + g_fps.tw * dot; b.dstOffsets[1].y = y + FPS_H * dot; b.dstOffsets[1].z = 1;
    CKPT_OP("frame display blit (swapchain image, index)", V.images[index], index, 0, 0);
    vkCmdBlitImage(R.cmd, g_fps.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, V.images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_NEAREST);
}

/* port/vk_rt_experiment/RenderLayerManager.cpp
 *
 * Host-side staging + GPU upload for the per-frame quad batch.
 * Buffers are allocated DEVICE_LOCAL with the storage and AS-build-
 * input usage bits so the BLAS builder can ingest them directly,
 * and uploaded from a HOST_VISIBLE staging buffer via vkCmdCopyBuffer.
 *
 * The host-staged vertices come out of beginFrame() into vectors;
 * drawSprite / drawBgQuad append; flushToBuffers does one staging
 * upload + one vkCmdCopyBuffer per buffer type.
 */

#include "RenderLayerManager.h"

#include <cstring>

namespace tmc_vkrt {

RenderLayerManager::RenderLayerManager(Engine& eng)
    : mEngine(eng) {
    /* Reserve enough headroom for a typical frame (a few hundred
     * sprites + background quads). Avoids realloc churn within a
     * frame. */
    mVertices.reserve(4 * 512);
    mIndices.reserve(6 * 512);
}

RenderLayerManager::~RenderLayerManager() {
    freeBuffers();
}

void RenderLayerManager::beginFrame() {
    mVertices.clear();
    mIndices.clear();
}

void RenderLayerManager::drawSprite(Layer layer, float x, float y, float w, float h,
                                    float uvU0, float uvV0, float uvU1, float uvV1,
                                    bool emissive) {
    emitQuad(layer, x, y, w, h, uvU0, uvV0, uvU1, uvV1, emissive);
}

void RenderLayerManager::drawBgQuad(Layer layer,
                                    float uvU0, float uvV0, float uvU1, float uvV1) {
    /* Full GBA frame at 240×160 (the engine's logical screen size).
     * The orthographic camera maps this 1:1 to the storage image. */
    emitQuad(layer, 0.0f, 0.0f, 240.0f, 160.0f, uvU0, uvV0, uvU1, uvV1, false);
}

void RenderLayerManager::emitQuad(Layer layer, float x, float y, float w, float h,
                                  float uvU0, float uvV0, float uvU1, float uvV1,
                                  bool emissive) {
    const float z = kLayerZ[(size_t)layer];
    const float e = emissive ? 1.0f : 0.0f;

    /* Vertex order: top-left, top-right, bottom-right, bottom-left
     * (counter-clockwise when viewed from -Z, which is where the
     * camera sits). Indices in kQuadIndices triangulate this. */
    const uint32_t base = (uint32_t)mVertices.size();
    mVertices.push_back({ {x,       y,       z}, {uvU0, uvV0}, e });
    mVertices.push_back({ {x + w,   y,       z}, {uvU1, uvV0}, e });
    mVertices.push_back({ {x + w,   y + h,   z}, {uvU1, uvV1}, e });
    mVertices.push_back({ {x,       y + h,   z}, {uvU0, uvV1}, e });

    for (uint32_t k = 0; k < 6; ++k) {
        mIndices.push_back(base + kQuadIndices[k]);
    }
}

void RenderLayerManager::ensurePersistentBuffers() {
    if (mPersistentReady) return;

    const VkBufferUsageFlags kVtxUsage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const VkBufferUsageFlags kIdxUsage =
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const VkMemoryPropertyFlags kHostProps =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    const VkDeviceSize vCapBytes = kMaxQuads * 4u * sizeof(Vertex);
    const VkDeviceSize iCapBytes = kMaxQuads * 6u * sizeof(uint32_t);

    /* Device-local destination buffers (read by BLAS + rchit). */
    mEngine.allocateBuffer(vCapBytes, kVtxUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           &mBuffers.vertexBuffer, &mBuffers.vertexMemory);
    mEngine.allocateBuffer(iCapBytes, kIdxUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           &mBuffers.indexBuffer, &mBuffers.indexMemory);

    /* Persistent host-visible staging — mapped once, written each frame. */
    mEngine.allocateBuffer(vCapBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           kHostProps, &mStagingVertex, &mStagingVertexMem);
    mEngine.allocateBuffer(iCapBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           kHostProps, &mStagingIndex,  &mStagingIndexMem);
    check(vkMapMemory(mEngine.device(), mStagingVertexMem, 0, vCapBytes, 0, &mStagingVertexMap),
          "vkMapMemory (persistent vertex staging)");
    check(vkMapMemory(mEngine.device(), mStagingIndexMem,  0, iCapBytes, 0, &mStagingIndexMap),
          "vkMapMemory (persistent index staging)");

    /* Cache device addresses — stable for the lifetime of the buffers. */
    VkBufferDeviceAddressInfo bda{};
    bda.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bda.buffer = mBuffers.vertexBuffer;
    mBuffers.vertexAddr = vkGetBufferDeviceAddress(mEngine.device(), &bda);
    bda.buffer = mBuffers.indexBuffer;
    mBuffers.indexAddr  = vkGetBufferDeviceAddress(mEngine.device(), &bda);

    mPersistentReady = true;
}

void RenderLayerManager::flushToBuffers() {
    if (mVertices.empty()) {
        mBuffers.vertexCount = 0;
        mBuffers.indexCount  = 0;
        return;
    }

    ensurePersistentBuffers();

    const VkDeviceSize vSize = mVertices.size() * sizeof(Vertex);
    const VkDeviceSize iSize = mIndices.size()  * sizeof(uint32_t);
    const VkDeviceSize vCap  = kMaxQuads * 4u * sizeof(Vertex);
    const VkDeviceSize iCap  = kMaxQuads * 6u * sizeof(uint32_t);
    if (vSize > vCap || iSize > iCap) {
        std::fprintf(stderr, "[layers] WARN: %zu quads exceeds kMaxQuads=%u — clipping\n",
                     mVertices.size() / 4, kMaxQuads);
        return;
    }

    /* Memcpy into the persistent staging — host-coherent so no flush. */
    std::memcpy(mStagingVertexMap, mVertices.data(), (size_t)vSize);
    std::memcpy(mStagingIndexMap,  mIndices.data(),  (size_t)iSize);

    /* One-shot copy from staging → device-local.  oneShot still uses
     * a queue submit + idle-wait internally, but it's one round trip
     * instead of the old alloc+map+copy+free per buffer. */
    const VkDeviceSize vCopySize = vSize;
    const VkDeviceSize iCopySize = iSize;
    mEngine.oneShot([&](VkCommandBuffer cmd) {
        VkBufferCopy vCopy{};
        vCopy.size = vCopySize;
        vkCmdCopyBuffer(cmd, mStagingVertex, mBuffers.vertexBuffer, 1, &vCopy);
        VkBufferCopy iCopy{};
        iCopy.size = iCopySize;
        vkCmdCopyBuffer(cmd, mStagingIndex, mBuffers.indexBuffer, 1, &iCopy);
    });

    mBuffers.vertexCount = (uint32_t)mVertices.size();
    mBuffers.indexCount  = (uint32_t)mIndices.size();
}

void RenderLayerManager::freeBuffers() {
    if (mBuffers.vertexBuffer) vkDestroyBuffer(mEngine.device(), mBuffers.vertexBuffer, nullptr);
    if (mBuffers.vertexMemory) vkFreeMemory(mEngine.device(), mBuffers.vertexMemory, nullptr);
    if (mBuffers.indexBuffer)  vkDestroyBuffer(mEngine.device(), mBuffers.indexBuffer,  nullptr);
    if (mBuffers.indexMemory)  vkFreeMemory(mEngine.device(), mBuffers.indexMemory,  nullptr);
    mBuffers = GeometryBuffers{};

    /* Slice 11 persistent staging — release on full teardown only,
     * not between frames.  freeBuffers() is called from the dtor and
     * the (removed) per-frame realloc path. */
    if (mStagingVertexMap) vkUnmapMemory(mEngine.device(), mStagingVertexMem);
    if (mStagingVertex)    vkDestroyBuffer(mEngine.device(), mStagingVertex, nullptr);
    if (mStagingVertexMem) vkFreeMemory(mEngine.device(), mStagingVertexMem, nullptr);
    if (mStagingIndexMap)  vkUnmapMemory(mEngine.device(), mStagingIndexMem);
    if (mStagingIndex)     vkDestroyBuffer(mEngine.device(), mStagingIndex, nullptr);
    if (mStagingIndexMem)  vkFreeMemory(mEngine.device(), mStagingIndexMem, nullptr);
    mStagingVertex = mStagingIndex = VK_NULL_HANDLE;
    mStagingVertexMem = mStagingIndexMem = VK_NULL_HANDLE;
    mStagingVertexMap = mStagingIndexMap = nullptr;
    mPersistentReady = false;
}

}  /* namespace tmc_vkrt */

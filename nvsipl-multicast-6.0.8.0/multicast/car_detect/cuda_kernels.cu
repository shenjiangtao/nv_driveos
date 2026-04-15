// Copyright (c) 2023 NVIDIA Corporation. All rights reserved.
//
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software, related documentation and any
// modifications thereto. Any use, reproduction, disclosure or distribution
// of this software and related documentation without an express license
// agreement from NVIDIA Corporation is strictly prohibited.

#include "cuda_kernels.h"

#define ROUND_DOWN_2(num) ((num) & (~1))

__forceinline__ __device__ static float clampF(float x, float lower, float upper)
{
    return x < lower ? lower : (x > upper ? upper : x);
}

__forceinline__ __device__ static int clampI(int x, int lower, int upper)
{
    return x < lower ? lower : (x > upper ? upper : x);
}

static __global__ void imageTransformKernel(cudaTextureObject_t texSrcLuma,
                                            cudaTextureObject_t texSrcChroma,
                                            void *dpTsr,
                                            int nTsrWidth,
                                            int nTsrHeight,
                                            int nTsrStride,
                                            float fxScale,
                                            float fyScale,
                                            int nBatchSize,
                                            float scaleFactor,
                                            bool enableInt8)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    int px = x * 4, py = y * 2;
    if ((px + 3) >= nTsrWidth || (py + 1) >= nTsrHeight)
        return;

    uchar4 luma2x01, luma2x23, uv2;

    for (int i = blockIdx.z; i < nBatchSize; i += gridDim.z) {
        *(uchar4 *)&luma2x01 =
            make_uchar4(tex2D<uint8_t>(texSrcLuma, px * fxScale, (py + i * nTsrHeight) * fyScale),
                        tex2D<uint8_t>(texSrcLuma, (px + 1) * fxScale, (py + i * nTsrHeight) * fyScale),
                        tex2D<uint8_t>(texSrcLuma, (px + 2) * fxScale, (py + i * nTsrHeight) * fyScale),
                        tex2D<uint8_t>(texSrcLuma, (px + 3) * fxScale, (py + i * nTsrHeight) * fyScale));

        *(uchar4 *)&luma2x23 =
            make_uchar4(tex2D<uint8_t>(texSrcLuma, px * fxScale, ((py + i * nTsrHeight) + 1) * fyScale),
                        tex2D<uint8_t>(texSrcLuma, (px + 1) * fxScale, ((py + i * nTsrHeight) + 1) * fyScale),
                        tex2D<uint8_t>(texSrcLuma, (px + 2) * fxScale, ((py + i * nTsrHeight) + 1) * fyScale),
                        tex2D<uint8_t>(texSrcLuma, (px + 3) * fxScale, ((py + i * nTsrHeight) + 1) * fyScale));

        *(uchar4 *)&uv2 = make_uchar4(
            tex2D<uint8_t>(texSrcChroma, ROUND_DOWN_2(int(px * fxScale)), (y + i * nTsrHeight / 2) * fyScale),
            tex2D<uint8_t>(texSrcChroma, ROUND_DOWN_2(int(px * fxScale)) + 1, (y + i * nTsrHeight / 2) * fyScale),
            tex2D<uint8_t>(texSrcChroma, ROUND_DOWN_2(int((px + 2) * fxScale)), (y + i * nTsrHeight / 2) * fyScale),
            tex2D<uint8_t>(texSrcChroma, ROUND_DOWN_2(int((px + 2) * fxScale)) + 1,
                           (y + i * nTsrHeight / 2) * fyScale));

        float2 add00, add01, add02, add03;

        add00.x = 1.1644f * luma2x01.x;
        add01.x = 1.1644f * luma2x01.y;
        add00.y = 1.1644f * luma2x01.z;
        add01.y = 1.1644f * luma2x01.w;

        add02.x = 1.1644f * luma2x23.x;
        add03.x = 1.1644f * luma2x23.y;
        add02.y = 1.1644f * luma2x23.z;
        add03.y = 1.1644f * luma2x23.w;

        float2 add1, add2, add3;

        add1.x = 2.0172f * (uv2.x - 128.0f);
        add1.y = 2.0172f * (uv2.z - 128.0f);

        add2.x = (-0.3918f) * (uv2.x - 128.0f) + (-0.8130f) * (uv2.y - 128.0f);
        add2.y = (-0.3918f) * (uv2.z - 128.0f) + (-0.8130f) * (uv2.w - 128.0f);

        add3.x = 1.5960f * (uv2.y - 128.0f);
        add3.y = 1.5960f * (uv2.w - 128.0f);

        float r00 = scaleFactor * clampF(add00.x + add3.x, 0.0f, 255.0f);
        float r01 = scaleFactor * clampF(add01.x + add3.x, 0.0f, 255.0f);
        float r02 = scaleFactor * clampF(add00.y + add3.y, 0.0f, 255.0f);
        float r03 = scaleFactor * clampF(add01.y + add3.y, 0.0f, 255.0f);
        float r10 = scaleFactor * clampF(add02.x + add3.x, 0.0f, 255.0f);
        float r11 = scaleFactor * clampF(add03.x + add3.x, 0.0f, 255.0f);
        float r12 = scaleFactor * clampF(add02.y + add3.y, 0.0f, 255.0f);
        float r13 = scaleFactor * clampF(add03.y + add3.y, 0.0f, 255.0f);

        float g00 = scaleFactor * clampF(add00.x + add2.x, 0.0f, 255.0f);
        float g01 = scaleFactor * clampF(add01.x + add2.x, 0.0f, 255.0f);
        float g02 = scaleFactor * clampF(add00.y + add2.y, 0.0f, 255.0f);
        float g03 = scaleFactor * clampF(add01.y + add2.y, 0.0f, 255.0f);
        float g10 = scaleFactor * clampF(add02.x + add2.x, 0.0f, 255.0f);
        float g11 = scaleFactor * clampF(add03.x + add2.x, 0.0f, 255.0f);
        float g12 = scaleFactor * clampF(add02.y + add2.y, 0.0f, 255.0f);
        float g13 = scaleFactor * clampF(add03.y + add2.y, 0.0f, 255.0f);

        float b00 = scaleFactor * clampF(add00.x + add1.x, 0.0f, 255.0f);
        float b01 = scaleFactor * clampF(add01.x + add1.x, 0.0f, 255.0f);
        float b02 = scaleFactor * clampF(add00.y + add1.y, 0.0f, 255.0f);
        float b03 = scaleFactor * clampF(add01.y + add1.y, 0.0f, 255.0f);
        float b10 = scaleFactor * clampF(add02.x + add1.x, 0.0f, 255.0f);
        float b11 = scaleFactor * clampF(add03.x + add1.x, 0.0f, 255.0f);
        float b12 = scaleFactor * clampF(add02.y + add1.y, 0.0f, 255.0f);
        float b13 = scaleFactor * clampF(add03.y + add1.y, 0.0f, 255.0f);

        int pitch_offset_0 = i * nTsrStride * nTsrWidth * nTsrHeight + py * nTsrWidth * nTsrStride + px * nTsrStride;
        int pitch_offset_1 =
            i * nTsrStride * nTsrWidth * nTsrHeight + (py + 1) * nTsrWidth * nTsrStride + px * nTsrStride;

        if (enableInt8) {
            *(uchar3 *)&((int8_t *)dpTsr)[pitch_offset_0] =
                make_uchar3(clampI(int(roundf(r00)), -128, 127), clampI(int(roundf(g00)), -128, 127),
                            clampI(int(roundf(b00)), -128, 127));
            *(uchar3 *)&((int8_t *)dpTsr)[pitch_offset_0 + 1 * nTsrStride] =
                make_uchar3(clampI(int(roundf(r01)), -128, 127), clampI(int(roundf(g01)), -128, 127),
                            clampI(int(roundf(b01)), -128, 127));
            *(uchar3 *)&((int8_t *)dpTsr)[pitch_offset_0 + 2 * nTsrStride] =
                make_uchar3(clampI(int(roundf(r02)), -128, 127), clampI(int(roundf(g02)), -128, 127),
                            clampI(int(roundf(b02)), -128, 127));
            *(uchar3 *)&((int8_t *)dpTsr)[pitch_offset_0 + 3 * nTsrStride] =
                make_uchar3(clampI(int(roundf(r03)), -128, 127), clampI(int(roundf(g03)), -128, 127),
                            clampI(int(roundf(b03)), -128, 127));

            *(uchar3 *)&((int8_t *)dpTsr)[pitch_offset_1] =
                make_uchar3(clampI(int(roundf(r10)), -128, 127), clampI(int(roundf(g10)), -128, 127),
                            clampI(int(roundf(b10)), -128, 127));
            *(uchar3 *)&((int8_t *)dpTsr)[pitch_offset_1 + 1 * nTsrStride] =
                make_uchar3(clampI(int(roundf(r11)), -128, 127), clampI(int(roundf(g11)), -128, 127),
                            clampI(int(roundf(b11)), -128, 127));
            *(uchar3 *)&((int8_t *)dpTsr)[pitch_offset_1 + 2 * nTsrStride] =
                make_uchar3(clampI(int(roundf(r12)), -128, 127), clampI(int(roundf(g12)), -128, 127),
                            clampI(int(roundf(b12)), -128, 127));
            *(uchar3 *)&((int8_t *)dpTsr)[pitch_offset_1 + 3 * nTsrStride] =
                make_uchar3(clampI(int(roundf(r13)), -128, 127), clampI(int(roundf(g13)), -128, 127),
                            clampI(int(roundf(b13)), -128, 127));
        } else {
            ((half *)dpTsr)[pitch_offset_0] = __float2half(r00);
            ((half *)dpTsr)[pitch_offset_0 + 1] = __float2half(g00);
            ((half *)dpTsr)[pitch_offset_0 + 2] = __float2half(b00);
            ((half *)dpTsr)[pitch_offset_0 + 1 * nTsrStride] = __float2half(r01);
            ((half *)dpTsr)[pitch_offset_0 + 1 * nTsrStride + 1] = __float2half(g01);
            ((half *)dpTsr)[pitch_offset_0 + 1 * nTsrStride + 2] = __float2half(b01);
            ((half *)dpTsr)[pitch_offset_0 + 2 * nTsrStride] = __float2half(r02);
            ((half *)dpTsr)[pitch_offset_0 + 2 * nTsrStride + 1] = __float2half(g02);
            ((half *)dpTsr)[pitch_offset_0 + 2 * nTsrStride + 2] = __float2half(b02);
            ((half *)dpTsr)[pitch_offset_0 + 3 * nTsrStride] = __float2half(r03);
            ((half *)dpTsr)[pitch_offset_0 + 3 * nTsrStride + 1] = __float2half(g03);
            ((half *)dpTsr)[pitch_offset_0 + 3 * nTsrStride + 2] = __float2half(b03);

            ((half *)dpTsr)[pitch_offset_1] = __float2half(r10);
            ((half *)dpTsr)[pitch_offset_1 + 1] = __float2half(g10);
            ((half *)dpTsr)[pitch_offset_1 + 2] = __float2half(b10);
            ((half *)dpTsr)[pitch_offset_1 + 1 * nTsrStride] = __float2half(r11);
            ((half *)dpTsr)[pitch_offset_1 + 1 * nTsrStride + 1] = __float2half(g11);
            ((half *)dpTsr)[pitch_offset_1 + 1 * nTsrStride + 2] = __float2half(b11);
            ((half *)dpTsr)[pitch_offset_1 + 2 * nTsrStride] = __float2half(r12);
            ((half *)dpTsr)[pitch_offset_1 + 2 * nTsrStride + 1] = __float2half(g12);
            ((half *)dpTsr)[pitch_offset_1 + 2 * nTsrStride + 2] = __float2half(b12);
            ((half *)dpTsr)[pitch_offset_1 + 3 * nTsrStride] = __float2half(r13);
            ((half *)dpTsr)[pitch_offset_1 + 3 * nTsrStride + 1] = __float2half(g13);
            ((half *)dpTsr)[pitch_offset_1 + 3 * nTsrStride + 2] = __float2half(b13);
        }
    }
}

/* Input format is NV12, output is scaled RGB */
bool nv12toRgbBatched(uint8_t *dpSrc,
                      int nSrcPitch,
                      int nSrcWidth,
                      int nSrcHeight,
                      void *dpTsr,
                      int nTsrWidth,
                      int nTsrHeight,
                      int nTsrStride,
                      double &ratio_x,
                      double &ratio_y,
                      int nBatchSize,
                      float scaleFactor,
                      bool enableInt8,
                      cudaStream_t stream)
{

    /* Calculate the scaling ratio of the frame / object crop. This will be
   * required later for rescaling the detector output boxes to input resolution.
   */
    ratio_x = 1.0 * nTsrWidth / nSrcWidth;
    ratio_y = 1.0 * nTsrHeight / nSrcHeight;

    cudaResourceDesc resDesc = {};
    resDesc.resType = cudaResourceTypePitch2D;
    resDesc.res.pitch2D.devPtr = dpSrc;
    resDesc.res.pitch2D.desc = cudaCreateChannelDesc<uint8_t>();
    resDesc.res.pitch2D.width = nSrcWidth;
    resDesc.res.pitch2D.height = nSrcHeight * nBatchSize;
    resDesc.res.pitch2D.pitchInBytes = nSrcPitch;

    cudaTextureDesc texDesc = {};
    texDesc.filterMode = cudaFilterModePoint;
    texDesc.readMode = cudaReadModeElementType;

    cudaTextureObject_t texLuma = 0;
    checkCudaErrors(cudaCreateTextureObject(&texLuma, &resDesc, &texDesc, NULL));

    resDesc.res.pitch2D.devPtr = dpSrc + nSrcWidth * nSrcHeight * nBatchSize;
    resDesc.res.pitch2D.desc = cudaCreateChannelDesc<uint8_t>();
    resDesc.res.pitch2D.width = nSrcWidth;
    resDesc.res.pitch2D.height = nSrcHeight / 2 * nBatchSize;

    cudaTextureObject_t texChroma = 0;
    checkCudaErrors(cudaCreateTextureObject(&texChroma, &resDesc, &texDesc, NULL));

    dim3 dimBlock(32, 32, 1);

    unsigned int blockDimZ = nBatchSize;

    // Restricting blocks in Z-dim till 32 to not launch too many blocks
    blockDimZ = (blockDimZ > 32) ? 32 : blockDimZ;

    dim3 dimGrid((nTsrWidth / 4 + dimBlock.x - 1) / dimBlock.x, (nTsrHeight / 2 + dimBlock.y - 1) / dimBlock.y,
                 blockDimZ);

    float fxScale = 1.0f * nSrcWidth / nTsrWidth;
    float fyScale = 1.0f * nSrcHeight / nTsrHeight;

    imageTransformKernel<<<dimGrid, dimBlock, 0, stream>>>(texLuma, texChroma, dpTsr, nTsrWidth, nTsrHeight, nTsrStride,
                                                           fxScale, fyScale, nBatchSize, scaleFactor, enableInt8);

    auto err = cudaGetLastError();
    if (cudaSuccess != err) {
        fprintf(stderr, "CUDA kernel failed : %s\n", cudaGetErrorString(err));
        return false;
    }

    checkCudaErrors(cudaDestroyTextureObject(texLuma));
    checkCudaErrors(cudaDestroyTextureObject(texChroma));

    return true;
}

/* Input format is NV12 block-linear, output is scaled RGB */
bool nv12bltoRgbBatched(const cudaArray_t *yuvInPlanes,
                        int nSrcPitch,
                        int nSrcWidth,
                        int nSrcHeight,
                        void *dpTsr,
                        int nTsrWidth,
                        int nTsrHeight,
                        int nTsrStride,
                        double &ratio_x,
                        double &ratio_y,
                        int nBatchSize,
                        float scaleFactor,
                        bool enableInt8,
                        cudaStream_t stream)
{

    /* Calculate the scaling ratio of the frame / object crop. This will be
   * required later for rescaling the detector output boxes to input resolution.
   */
    ratio_x = 1.0 * nTsrWidth / nSrcWidth;
    ratio_y = 1.0 * nTsrHeight / nSrcHeight;

    cudaResourceDesc resDesc = {};
    memset(&resDesc, 0, sizeof(cudaResourceDesc));
    resDesc.resType = cudaResourceTypeArray;
    resDesc.res.array.array = yuvInPlanes[0];

    cudaTextureDesc texDesc = {};
    texDesc.filterMode = cudaFilterModePoint;
    texDesc.readMode = cudaReadModeElementType;

    cudaTextureObject_t texLuma = 0;
    checkCudaErrors(cudaCreateTextureObject(&texLuma, &resDesc, &texDesc, NULL));

    memset(&resDesc, 0, sizeof(cudaResourceDesc));
    resDesc.resType = cudaResourceTypeArray;
    resDesc.res.array.array = yuvInPlanes[1];

    cudaTextureObject_t texChroma = 0;
    checkCudaErrors(cudaCreateTextureObject(&texChroma, &resDesc, &texDesc, NULL));

    dim3 dimBlock(32, 32, 1);

    unsigned int blockDimZ = nBatchSize;

    // Restricting blocks in Z-dim till 32 to not launch too many blocks
    blockDimZ = (blockDimZ > 32) ? 32 : blockDimZ;

    dim3 dimGrid((nTsrWidth / 4 + dimBlock.x - 1) / dimBlock.x, (nTsrHeight / 2 + dimBlock.y - 1) / dimBlock.y,
                 blockDimZ);

    float fxScale = 1.0f * nSrcWidth / nTsrWidth;
    float fyScale = 1.0f * nSrcHeight / nTsrHeight;

    imageTransformKernel<<<dimGrid, dimBlock, 0, stream>>>(texLuma, texChroma, dpTsr, nTsrWidth, nTsrHeight, nTsrStride,
                                                           fxScale, fyScale, nBatchSize, scaleFactor, enableInt8);

    auto err = cudaGetLastError();
    if (cudaSuccess != err) {
        fprintf(stderr, "CUDA kernel failed : %s\n", cudaGetErrorString(err));
        return false;
    }

    checkCudaErrors(cudaDestroyTextureObject(texLuma));
    checkCudaErrors(cudaDestroyTextureObject(texChroma));

    return true;
}
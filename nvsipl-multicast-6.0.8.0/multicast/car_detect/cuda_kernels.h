// Copyright (c) 2023 NVIDIA Corporation. All rights reserved.
//
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software, related documentation and any
// modifications thereto. Any use, reproduction, disclosure or distribution
// of this software and related documentation without an express license
// agreement from NVIDIA Corporation is strictly prohibited.

#ifndef KERNELS_H
#define KERNELS_H

#include "Common.hpp"

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
                      cudaStream_t stream);
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
                        cudaStream_t stream);
#endif
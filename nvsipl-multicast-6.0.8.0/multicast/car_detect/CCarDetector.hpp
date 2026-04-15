// Copyright (c) 2023 NVIDIA Corporation. All rights reserved.
//
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software, related documentation and any
// modifications thereto. Any use, reproduction, disclosure or distribution
// of this software and related documentation without an express license
// agreement from NVIDIA Corporation is strictly prohibited.

#ifndef CCARDETECTOR_HPP
#define CCARDETECTOR_HPP

#include "CCudlaTask.hpp"
#include "Common.hpp"
#include <atomic>
#include <memory>

class CCarDetector
{
  public:
    CCarDetector();
    virtual ~CCarDetector(void);
    bool Init(uint32_t id, cudaStream_t stream);
    bool Process(const cudaArray_t *inputBuf, uint32_t inputImageWidth, uint32_t inputImageHeight, bool fileDump);

  private:
    NvInferInitParams m_initParams;
    std::unique_ptr<CCudlaTask> m_upCudlaTask;
    cudaStream_t m_streamDLA;
    uint32_t m_id;
    std::atomic<bool> m_Processing;
    bool m_init_success;
};
#endif

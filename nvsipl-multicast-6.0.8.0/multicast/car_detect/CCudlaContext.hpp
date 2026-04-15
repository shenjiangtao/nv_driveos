// Copyright (c) 2023 NVIDIA Corporation. All rights reserved.
//
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software, related documentation and any
// modifications thereto. Any use, reproduction, disclosure or distribution
// of this software and related documentation without an express license
// agreement from NVIDIA Corporation is strictly prohibited.

#ifndef CUDLA_CONTEXT_HPP
#define CUDLA_CONTEXT_HPP

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>

#include "Common.hpp"

class CCudlaContext
{
  public:
    CCudlaContext(uint32_t id, const std::string &loadableFilePath);
    ~CCudlaContext();

    bool Initialize();
    bool BufferPrep();
    bool GetTensorSizeAndDim(const std::string &tensorName, uint64_t &tensorSize, Dims32 &d);
    bool BufferRegister(void *in_buf, void *bbox_buf, void *coverage_buf);
    bool SubmitDLATask(cudaStream_t m_stream);
    void CleanUp();

  private:
    bool ReadDLALoadable();

    uint32_t m_id = 0;
    std::string m_loadableFilePath;

    cudlaDevHandle m_DevHandle;
    cudlaModule m_ModuleHandle;
    unsigned char *m_LoadableData;
    uint32_t m_NumInputTensors;
    uint32_t m_NumOutputTensors;
    cudlaModuleTensorDescriptor *m_InputTensorDesc;
    cudlaModuleTensorDescriptor *m_OutputTensorDesc;
    void **m_InputBufferGPU;
    void **m_OutputBufferGPU;
    uint64_t **m_InputBufferRegisteredPtr;
    uint64_t **m_OutputBufferRegisteredPtr;

    size_t m_File_size;
    bool m_Initialized = false;
    std::unordered_map<std::string, uint64_t> m_TensorSizeTable;
    std::unordered_map<std::string, Dims32> m_TensorDimsTable;
};

#endif

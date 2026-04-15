// Copyright (c) 2023 NVIDIA Corporation. All rights reserved.
//
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software, related documentation and any
// modifications thereto. Any use, reproduction, disclosure or distribution
// of this software and related documentation without an express license
// agreement from NVIDIA Corporation is strictly prohibited.

#include "CCudlaContext.hpp"
#include "CUtils.hpp"

CCudlaContext::CCudlaContext(uint32_t id, const std::string &loadableFilePath)
    : m_id(id)
    , m_loadableFilePath(loadableFilePath)
    , m_DevHandle()
    , m_ModuleHandle()
    , m_LoadableData(nullptr)
    , m_NumInputTensors(0)
    , m_NumOutputTensors(0)
    , m_InputTensorDesc(nullptr)
    , m_OutputTensorDesc(nullptr)
    , m_InputBufferGPU(nullptr)
    , m_OutputBufferGPU(nullptr)
    , m_InputBufferRegisteredPtr(nullptr)
    , m_OutputBufferRegisteredPtr(nullptr)
    , m_File_size(0)
    , m_Initialized(false)
    , m_TensorSizeTable()
    , m_TensorDimsTable()
{
}

bool CCudlaContext::ReadDLALoadable()
{
    FILE *fp = NULL;
    struct stat st;
    size_t actually_read = 0;

    // Read loadable into buffer.
    fp = fopen(m_loadableFilePath.c_str(), "rb");
    if (fp == NULL) {
        LOG_WARN("Cannot open file %s \n", m_loadableFilePath.c_str());
        return false;
    }

    if (stat(m_loadableFilePath.c_str(), &st) != 0) {
        LOG_WARN("Cannot stat file\n");
        return false;
    }

    m_File_size = st.st_size;

    m_LoadableData = (unsigned char *)malloc(m_File_size);
    if (m_LoadableData == NULL) {
        LOG_WARN("Cannot Allocate memory for loadable\n");
        return false;
    }

    actually_read = fread(m_LoadableData, 1, m_File_size, fp);
    if (actually_read != m_File_size) {
        free(m_LoadableData);
        LOG_WARN("Read wrong size\n");
        return false;
    }
    fclose(fp);
    return true;
}

bool CCudlaContext::Initialize()
{
    if (!m_Initialized) {
        if (!ReadDLALoadable()) {
            return false;
        }
        cudlaStatus err;

        uint64_t cudla_num = 0;
        err = cudlaDeviceGetCount(&cudla_num);
        CHK_CUDLASTATUS_AND_RETURN(err, "cuDLA device get count");
        LOG_INFO("cuDLA count is %u\n", cudla_num);
        if (cudla_num == 0) {
            LOG_WARN("cuDLA device count ZERO\n");
            return false;
        }

        uint64_t dev_id = 0;
        dev_id = m_id & 0x1; // camera 1/3 run DLA0, camera 2/4 run DLA1

        err = cudlaCreateDevice(dev_id, &m_DevHandle, CUDLA_CUDA_DLA);
        CHK_CUDLASTATUS_AND_RETURN(err, "cuDLA create device");

        err = cudlaModuleLoadFromMemory(m_DevHandle, m_LoadableData, m_File_size, &m_ModuleHandle, 0);
        CHK_CUDLASTATUS_AND_RETURN(err, "cudlaModuleLoadFromMemory");

        m_Initialized = true;
        LOG_INFO("DLA CTX INIT !!!\n");
    }

    return true;
}

bool CCudlaContext::GetTensorSizeAndDim(const std::string &tensorName, uint64_t &tensorSize, Dims32 &d)
{
    if (m_TensorSizeTable.find(tensorName) != m_TensorSizeTable.end() &&
        m_TensorDimsTable.find(tensorName) != m_TensorDimsTable.end()) {
        tensorSize = m_TensorSizeTable[tensorName];
        d = m_TensorDimsTable[tensorName];
        return true;
    }
    return false;
}

bool CCudlaContext::BufferPrep()
{
    if (!m_Initialized) {
        return false;
    }
    // Get tensor attributes.
    cudlaStatus err;
    cudlaModuleAttribute attribute;

    err = cudlaModuleGetAttributes(m_ModuleHandle, CUDLA_NUM_INPUT_TENSORS, &attribute);
    CHK_CUDLASTATUS_AND_RETURN(err, "getting numInputTensors");

    m_NumInputTensors = attribute.numInputTensors;

    err = cudlaModuleGetAttributes(m_ModuleHandle, CUDLA_NUM_OUTPUT_TENSORS, &attribute);
    CHK_CUDLASTATUS_AND_RETURN(err, "getting numOutputTensors");

    m_NumOutputTensors = attribute.numOutputTensors;

    m_InputTensorDesc = (cudlaModuleTensorDescriptor *)malloc(sizeof(cudlaModuleTensorDescriptor) * m_NumInputTensors);
    m_OutputTensorDesc =
        (cudlaModuleTensorDescriptor *)malloc(sizeof(cudlaModuleTensorDescriptor) * m_NumOutputTensors);
    if ((m_InputTensorDesc == NULL) || (m_OutputTensorDesc == NULL)) {
        LOG_ERR("Error in allocating memory for TensorDesc\n");
        CleanUp();
        return false;
    }

    attribute.inputTensorDesc = m_InputTensorDesc;
    err = cudlaModuleGetAttributes(m_ModuleHandle, CUDLA_INPUT_TENSOR_DESCRIPTORS, &attribute);
    CHK_CUDLASTATUS_AND_RETURN(err, "getting input tensor descriptor");

    for (uint32_t ii = 0; ii < m_NumInputTensors; ii++) {
        m_TensorSizeTable.insert(std::make_pair(m_InputTensorDesc[ii].name, m_InputTensorDesc[ii].size));
        Dims32 d;
        d.nbDims = 4;
        d.d[0] = m_InputTensorDesc[ii].n;
        d.d[1] = m_InputTensorDesc[ii].c;
        d.d[2] = m_InputTensorDesc[ii].h;
        d.d[3] = m_InputTensorDesc[ii].w;
        m_TensorDimsTable.insert(std::make_pair(m_InputTensorDesc[ii].name, d));
    }

    attribute.outputTensorDesc = m_OutputTensorDesc;
    err = cudlaModuleGetAttributes(m_ModuleHandle, CUDLA_OUTPUT_TENSOR_DESCRIPTORS, &attribute);
    CHK_CUDLASTATUS_AND_RETURN(err, "getting getting output tensor descriptor");

    for (uint32_t ii = 0; ii < m_NumOutputTensors; ii++) {
        m_TensorSizeTable.insert(std::make_pair(m_OutputTensorDesc[ii].name, m_OutputTensorDesc[ii].size));
        Dims32 d;
        d.nbDims = 4;
        d.d[0] = m_OutputTensorDesc[ii].n;
        d.d[1] = m_OutputTensorDesc[ii].c;
        d.d[2] = m_OutputTensorDesc[ii].h;
        d.d[3] = m_OutputTensorDesc[ii].w;
        m_TensorDimsTable.insert(std::make_pair(m_OutputTensorDesc[ii].name, d));
    }

    // Allocate memory on GPU.
    m_InputBufferGPU = (void **)malloc(sizeof(void *) * m_NumInputTensors);
    if (m_InputBufferGPU == NULL) {
        LOG_ERR("Error in allocating memory for input buffer GPU array\n");
        CleanUp();
        return false;
    }

    m_OutputBufferGPU = (void **)malloc(sizeof(void *) * m_NumOutputTensors);
    if (m_OutputBufferGPU == NULL) {
        LOG_ERR("Error in allocating memory for output buffer GPU array\n");
        CleanUp();
        return false;
    }

    // Register the CUDA-allocated buffers.
    m_InputBufferRegisteredPtr = (uint64_t **)malloc(sizeof(uint64_t *) * m_NumInputTensors);
    m_OutputBufferRegisteredPtr = (uint64_t **)malloc(sizeof(uint64_t *) * m_NumOutputTensors);

    if ((m_InputBufferRegisteredPtr == NULL) || (m_OutputBufferRegisteredPtr == NULL)) {
        LOG_ERR("Error in allocating memory for BufferRegisteredPtr\n");
        CleanUp();
        return false;
    }

    return true;
}

bool CCudlaContext::BufferRegister(void *in_buf, void *bbox_buf, void *coverage_buf)
{
    m_InputBufferGPU[0] = in_buf;
    m_OutputBufferGPU[0] = bbox_buf;
    m_OutputBufferGPU[1] = coverage_buf;

    for (uint32_t ii = 0; ii < m_NumInputTensors; ii++) {
        cudlaStatus err = cudlaMemRegister(m_DevHandle, (uint64_t *)(m_InputBufferGPU[ii]), m_InputTensorDesc[0].size,
                                           &(m_InputBufferRegisteredPtr[0]), 0);
        if (err != cudlaSuccess) {
            LOG_ERR("Error in registering input tensor memory %u\n", ii);
            CleanUp();
            return false;
        }
    }

    for (uint32_t ii = 0; ii < m_NumOutputTensors; ii++) {
        cudlaStatus err = cudlaMemRegister(m_DevHandle, (uint64_t *)(m_OutputBufferGPU[ii]),
                                           m_OutputTensorDesc[ii].size, &(m_OutputBufferRegisteredPtr[ii]), 0);
        if (err != cudlaSuccess) {
            LOG_ERR("Error in registering output tensor memory %u\n", ii);
            CleanUp();
            return false;
        }
    }

    // Print for showing the cuDLA enable.
    LOG_INFO("ALL CUDLA MEMORY REGISTERED SUCCESSFULLY \n");
    return true;
}

bool CCudlaContext::SubmitDLATask(cudaStream_t m_stream)
{
    if (m_InputBufferGPU == NULL || m_OutputBufferGPU == NULL)
        return false;

    cudlaTask task;
    task.moduleHandle = m_ModuleHandle;
    task.inputTensor = m_InputBufferRegisteredPtr;
    task.outputTensor = m_OutputBufferRegisteredPtr;
    task.numOutputTensors = m_NumOutputTensors;
    task.numInputTensors = m_NumInputTensors;
    task.waitEvents = NULL;
    task.signalEvents = NULL;

    // Enqueue a cuDLA task.
    cudlaStatus err = cudlaSubmitTask(m_DevHandle, &task, 1, m_stream, 0);

    CHK_CUDLASTATUS_AND_RETURN(err, "submitting task");

    return true;
}

void CCudlaContext::CleanUp()
{
    if (m_InputTensorDesc != nullptr) {
        free(m_InputTensorDesc);
        m_InputTensorDesc = nullptr;
    }
    if (m_OutputTensorDesc != nullptr) {
        free(m_OutputTensorDesc);
        m_OutputTensorDesc = nullptr;
    }

    if (m_LoadableData != nullptr) {
        free(m_LoadableData);
        m_LoadableData = nullptr;
    }

    if (m_ModuleHandle != nullptr) {
        cudlaModuleUnload(m_ModuleHandle, 0);
        m_ModuleHandle = nullptr;
    }

    if (m_DevHandle != nullptr) {
        cudlaDestroyDevice(m_DevHandle);
        m_DevHandle = nullptr;
    }

    if (m_InputBufferGPU != nullptr) {
        free(m_InputBufferGPU);
        m_InputBufferGPU = nullptr;
    }

    if (m_OutputBufferGPU != nullptr) {
        free(m_OutputBufferGPU);
        m_OutputBufferGPU = nullptr;
    }

    if (m_InputBufferRegisteredPtr != nullptr) {
        free(m_InputBufferRegisteredPtr);
        m_InputBufferRegisteredPtr = nullptr;
    }

    if (m_OutputBufferRegisteredPtr != nullptr) {
        free(m_OutputBufferRegisteredPtr);
        m_OutputBufferRegisteredPtr = nullptr;
    }

    m_NumInputTensors = 0;
    m_NumOutputTensors = 0;
    LOG_INFO("DLA CTX CLEAN UP !!!\n");
}

CCudlaContext::~CCudlaContext()
{
    cudlaStatus err;
    for (uint32_t ii = 0; ii < m_NumInputTensors; ii++) {
        err = cudlaMemUnregister(m_DevHandle, m_InputBufferRegisteredPtr[ii]);
        if (err != cudlaSuccess) {
            LOG_ERR("Error in unregistering input tensor memory %u\n", ii);
        }
    }

    for (uint32_t ii = 0; ii < m_NumOutputTensors; ii++) {
        err = cudlaMemUnregister(m_DevHandle, m_OutputBufferRegisteredPtr[ii]);
        if (err != cudlaSuccess) {
            LOG_ERR("Error in unregistering output tensor memory %u\n", ii);
        }
    }
}
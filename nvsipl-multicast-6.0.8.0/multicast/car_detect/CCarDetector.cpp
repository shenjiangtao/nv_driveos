// Copyright (c) 2023 NVIDIA Corporation. All rights reserved.
//
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software, related documentation and any
// modifications thereto. Any use, reproduction, disclosure or distribution
// of this software and related documentation without an express license
// agreement from NVIDIA Corporation is strictly prohibited.

#include "CCarDetector.hpp"
#include "CCudlaTask.hpp"
#include "CUtils.hpp"
#include "cuda_kernels.h"
#include <iostream>

CCarDetector::CCarDetector()
    : m_initParams()
    , m_upCudlaTask(nullptr)
    , m_streamDLA(nullptr)
    , m_id(0)
    , m_Processing(false)
    , m_init_success(false)
{
}

CCarDetector::~CCarDetector(void)
{
    if (m_Processing == true) {
        // only the cudla task need to destroy
        (void)m_upCudlaTask->DestroyCudla();
    }
}

bool CCarDetector::Init(uint32_t id, cudaStream_t stream)
{
    m_id = id;
    int cudaDeviceId = 0;
    int numOfGPUs = 0;
    checkCudaErrors(cudaGetDeviceCount(&numOfGPUs));
    LOG_INFO("%d GPUs found\n", numOfGPUs);
    if (!numOfGPUs) {
        LOG_ERR("No GPUs found!!\n");
        return false;
    }

    int major = 0, minor = 0;
    checkCudaErrors(cudaDeviceGetAttribute(&major, cudaDevAttrComputeCapabilityMajor, cudaDeviceId));
    checkCudaErrors(cudaDeviceGetAttribute(&minor, cudaDevAttrComputeCapabilityMinor, cudaDeviceId));
    LOG_INFO("GPU Device %d: with compute capability %d.%d\n", cudaDeviceId, major, minor);

    LOG_INFO(">>> Use GPU Device %d\n", cudaDeviceId);
    checkCudaErrors(cudaSetDevice(cudaDeviceId));

    // Implicit create GPU context
    checkCudaErrors(cudaFree(0));
    // stream to run
    m_streamDLA = stream;

    // the parameter of the model
    // User need to modify here
    m_initParams.networkMode = NvInferNetworkMode_FP16; // Or NvInferNetworkMode_INT8
    // no support INT8 because need calibration file
    m_initParams.fp16ModelCacheFilePath = "./resnet10_fp16.bin";
    m_initParams.networkScaleFactor = 0.0039215697906911373;
    m_initParams.scoreThresh = 0.8;
    m_initParams.nmsThresh = 0.2;
    m_initParams.groupThresh = 1;
    m_initParams.groupIouThresh = 0.7;

    // onnx
    m_initParams.inputImageLayerName = "data'";           // caffe "input_1'";
    m_initParams.outputBboxLayerName = "Layer7_bbox'";    // caffe "conv2d_bbox'";
    m_initParams.outputCoverageLayerName = "Layer7_cov'"; // caffe "conv2d_cov/Sigmoid'";

    std::vector<std::vector<std::string>> tmp_labels{{"Car"}, {"Bicycle"}, {"Person"}, {"Roadsign"}};
    m_initParams.labels = tmp_labels;

    m_upCudlaTask.reset(new CCudlaTask(m_initParams, m_id, m_streamDLA));
    if (m_upCudlaTask == nullptr) {
        LOG_WARN("CudlaTask memory allocation failed\n");
        return false;
    }

    // check the cudla task init success
    m_init_success = m_upCudlaTask->Init();
    if (!m_init_success) {
        LOG_WARN("WARN: cudla task init failed! run without inference!\n");
        return false;
    }

    return true;
}

bool CCarDetector::Process(const cudaArray_t *inputBuf,
                           uint32_t inputImageWidth,
                           uint32_t inputImageHeight,
                           bool fileDump)
{
    if (m_init_success) {
        // change the cudla processing status
        m_Processing = true;
        auto res = m_upCudlaTask->ProcessCudla(inputBuf, inputImageWidth, inputImageHeight, fileDump);
        // finish
        m_Processing = false;
        return res;
    } else {
        return false;
    }
}

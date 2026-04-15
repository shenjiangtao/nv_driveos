// Copyright (c) 2023 NVIDIA Corporation. All rights reserved.
//
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software, related documentation and any
// modifications thereto. Any use, reproduction, disclosure or distribution
// of this software and related documentation without an express license
// agreement from NVIDIA Corporation is strictly prohibited.

#ifndef CCUDLA_TASK_HPP
#define CCUDLA_TASK_HPP

#include "CCudlaContext.hpp"
#include "Common.hpp"
#include <algorithm>

class CCudlaTask
{
  public:
    CCudlaTask(NvInferInitParams &init_params, uint32_t id, cudaStream_t m_streamDLA);
    ~CCudlaTask();
    bool Init();
    bool ProcessCudla(const cudaArray_t *input_frame, int inputImageWidth, int inputImageHeight, bool fileDump);
    bool DestroyCudla();

  private:
    bool InitializeCudla();
    uint32_t m_id;
    cudaStream_t m_streamDLA;
    /** NvInferContext to be used for inferencing. */
    CCudlaContext *dla_ctx;

    bool m_Registed;
    /* Dimensions of network input. */
    unsigned int m_NetWidth;
    unsigned int m_NetHeight;
    unsigned int m_NetChannels;

    uint64_t m_NetworkInputTensorSize;
    void *m_NetworkInputTensor;
    Dims32 m_NetworkInputLayerDim;

    uint64_t m_OutputBboxTensorSize;
    void *m_OutputBboxTensor;
    Dims32 m_OutputBboxLayerDim;

    uint64_t m_OutputCoverageTensorSize;
    void *m_OutputCoverageTensor;
    Dims32 m_OutputCoverageLayerDim;

    std::vector<std::vector<std::string>> m_Labels;

    const float ThresHold = 1e-8;

    float m_NetworkInputTensorScale;
    float m_OutputBboxTensorScale;
    float m_OutputCoverageTensorScale;

    float m_NetworkScaleFactor;

    NvInferNetworkMode m_NetworkMode;
    std::string m_Int8ModelCacheFilePath;
    std::string m_Fp16ModelCacheFilePath;
    std::string m_CalibrationTableFile;
    std::unordered_map<std::string, float> mPerTensorDynamicRangeMap;

    float m_scoreThresh;
    float m_nmsThresh;
    float m_groupIouThresh;
    unsigned int m_groupThresh;

    std::string m_NetworkInputLayerName;
    std::string m_OutputCoverageLayerName;
    std::string m_OutputBboxLayerName;

    std::ofstream ofs;
    std::string m_name{ "CudlaTask" };

    bool PostProcess(double scale_ratio_x, double scale_ratio_y, bool dumpFile);
    bool GetIOSizesAndDims();
    bool GetIODynamicRange();
    bool ReadPerTensorDynamicRangeValues();
    bool NmsCpu(std::vector<Bndbox> bndboxes, std::vector<Bndbox> &nms_pred);
    void ParseBoundingBox(const int8_t *outputBboxBuffer,
                          const int8_t *outputCoverageBuffer,
                          std::vector<Bndbox> &rectList,
                          unsigned int classIndex,
                          unsigned int strideC);
    void ParseBoundingBox(const half *outputBboxBuffer,
                          const half *outputCoverageBuffer,
                          std::vector<Bndbox> &rectList,
                          unsigned int classIndex,
                          unsigned int strideC);
};

#endif // CCUDLA_TASK_H

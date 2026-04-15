// Copyright (c) 2023 NVIDIA Corporation. All rights reserved.
//
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software, related documentation and any
// modifications thereto. Any use, reproduction, disclosure or distribution
// of this software and related documentation without an express license
// agreement from NVIDIA Corporation is strictly prohibited.

#include "CCudlaTask.hpp"
#include "CUtils.hpp"
#include "cuda_kernels.h"

#define NV12BL_RGB_RESULT_CHECK 0

CCudlaTask::CCudlaTask(NvInferInitParams &init_params, uint32_t id, cudaStream_t stream)
    : m_id(id)
    , m_streamDLA(stream)
    , m_Registed(false)
    , m_Labels(init_params.labels)
    , m_NetworkScaleFactor(init_params.networkScaleFactor)
    , m_NetworkMode(init_params.networkMode)
    , m_Int8ModelCacheFilePath(init_params.int8ModelCacheFilePath)
    , m_Fp16ModelCacheFilePath(init_params.fp16ModelCacheFilePath)
    , m_CalibrationTableFile(init_params.int8CalibrationFilePath)
    , m_scoreThresh(init_params.scoreThresh)
    , m_nmsThresh(init_params.nmsThresh)
    , m_groupIouThresh(init_params.groupIouThresh)
    , m_groupThresh(init_params.groupThresh)
    , m_NetworkInputLayerName(init_params.inputImageLayerName)
    , m_OutputCoverageLayerName(init_params.outputCoverageLayerName)
    , m_OutputBboxLayerName(init_params.outputBboxLayerName)
{
}

bool CCudlaTask::Init()
{
    if (m_OutputBboxLayerName.empty()) {
        LOG_ERR("Error: Output BBox layer name should be provided for detectors.\n");
        return false;
    }

    if (m_OutputCoverageLayerName.empty()) {
        LOG_ERR("Error: Output coverage layer name should be provided.\n");
        return false;
    }

    if (!InitializeCudla()) {
        return false;
    }

    return true;
}

CCudlaTask::~CCudlaTask()
{
    if (ofs.is_open()) {
        ofs.close();
    }
}

bool CCudlaTask::ReadPerTensorDynamicRangeValues()
{
    std::ifstream iDynamicRangeStream(m_CalibrationTableFile);
    if (!iDynamicRangeStream) {
        LOG_ERR("Could not find per-tensor scales file: %s\n", m_CalibrationTableFile.c_str());
        return false;
    }

    std::string line;

    while (std::getline(iDynamicRangeStream, line)) {
        auto colonPos = line.find_last_of(':');
        if (colonPos != std::string::npos) {
            // Scales should be stored in calibration cache
            // as 32-bit floating numbers encoded as 32-bit integers
            int32_t scalesAsInt = std::stoi(line.substr(colonPos + 2, 8), nullptr, 16);
            auto const tensorName = line.substr(0, colonPos);
            float *f_p = reinterpret_cast<float *>(&scalesAsInt);
            mPerTensorDynamicRangeMap[tensorName] = *f_p;
        }
    }

    return true;
}

bool CCudlaTask::GetIODynamicRange()
{
    if (!ReadPerTensorDynamicRangeValues()) {
        return false;
    }

    if (mPerTensorDynamicRangeMap.find(m_NetworkInputLayerName.substr(0, m_NetworkInputLayerName.length() - 1)) !=
            mPerTensorDynamicRangeMap.end() &&
        mPerTensorDynamicRangeMap.find(m_OutputBboxLayerName.substr(0, m_OutputBboxLayerName.length() - 1)) !=
            mPerTensorDynamicRangeMap.end() &&
        mPerTensorDynamicRangeMap.find(m_OutputCoverageLayerName.substr(0, m_OutputCoverageLayerName.length() - 1)) !=
            mPerTensorDynamicRangeMap.end()) {
        m_NetworkInputTensorScale =
            mPerTensorDynamicRangeMap.at(m_NetworkInputLayerName.substr(0, m_NetworkInputLayerName.length() - 1));
        m_OutputBboxTensorScale =
            mPerTensorDynamicRangeMap.at(m_OutputBboxLayerName.substr(0, m_OutputBboxLayerName.length() - 1));
        m_OutputCoverageTensorScale =
            mPerTensorDynamicRangeMap.at(m_OutputCoverageLayerName.substr(0, m_OutputCoverageLayerName.length() - 1));
        return true;
    }

    return false;
}

bool CCudlaTask::GetIOSizesAndDims()
{
    if (!m_NetworkInputLayerName.empty()) {
        if (!dla_ctx->GetTensorSizeAndDim(m_NetworkInputLayerName, m_NetworkInputTensorSize, m_NetworkInputLayerDim)) {
            LOG_ERR("Input layer not found. Please check input layer name in config.\n");
            return false;
        }
        m_NetChannels = m_NetworkInputLayerDim.d[1];
        m_NetHeight = m_NetworkInputLayerDim.d[2];
        m_NetWidth = m_NetworkInputLayerDim.d[3];
        // For int8, Data format is HWC4, input channel is marked as 4.
        // For fp16, Data format is CHW16, input channel is marked as 3.
        if (m_NetChannels != (m_NetworkMode == NvInferNetworkMode_INT8 ? 4 : 3)) {
            LOG_ERR("m_NetChannels error! \n");
            return false;
        }

        m_NetworkInputTensor = NULL;
    }

    if (!m_OutputBboxLayerName.empty()) {
        if (!dla_ctx->GetTensorSizeAndDim(m_OutputBboxLayerName, m_OutputBboxTensorSize, m_OutputBboxLayerDim)) {
            LOG_ERR("Output BBOX layer not found. Please check output BBOX layer name in config.");
            return false;
        }
        // according to the sample resnet model
        m_OutputBboxTensor = NULL;
    }

    if (!m_OutputCoverageLayerName.empty()) {
        if (!dla_ctx->GetTensorSizeAndDim(m_OutputCoverageLayerName, m_OutputCoverageTensorSize,
                                          m_OutputCoverageLayerDim)) {
            LOG_ERR("Output Coverage layer not found. Please check output Coverage layer name in config.\n");
            return false;
        }
        // according to the sample resnet model
        m_OutputCoverageTensor = NULL;
    }

    return true;
}

bool CCudlaTask::InitializeCudla()
{
    dla_ctx = new CCudlaContext(m_id, m_NetworkMode == NvInferNetworkMode_INT8 ? m_Int8ModelCacheFilePath
                                                                               : m_Fp16ModelCacheFilePath);
    if (!dla_ctx->Initialize()) {
        LOG_WARN("Error: CCudlaContext create false.\n");
        return false;
    }

    if (!dla_ctx->BufferPrep()) {
        LOG_WARN("Error: Failed to prepare buffer.\n");
        return false;
    }

    if (!GetIOSizesAndDims()) {
        LOG_WARN("Error: Failed to get buffer size and dimension information for I/O layers.\n");
        return false;
    }

    if (m_NetworkMode == NvInferNetworkMode_INT8) {
        if (!GetIODynamicRange()) {
            LOG_WARN("Error: Failed to get dynamic ranges setting for I/O layers.\n");
            return false;
        }
    }

    checkCudaErrors(cudaMallocManaged(&m_NetworkInputTensor, m_NetworkInputTensorSize));
    checkCudaErrors(cudaMallocManaged(&m_OutputBboxTensor, m_OutputBboxTensorSize));
    checkCudaErrors(cudaMallocManaged(&m_OutputCoverageTensor, m_OutputCoverageTensorSize));
    checkCudaErrors(cudaStreamAttachMemAsync(m_streamDLA, m_NetworkInputTensor));
    checkCudaErrors(cudaStreamAttachMemAsync(m_streamDLA, m_OutputBboxTensor));
    checkCudaErrors(cudaStreamAttachMemAsync(m_streamDLA, m_OutputCoverageTensor));

    return true;
}

bool CCudlaTask::ProcessCudla(const cudaArray_t *inputImageBuffer,
                              int inputImageWidth,
                              int inputImageHeight,
                              bool fileDump)
{
    double scale_ratio_x, scale_ratio_y;

    switch (m_NetworkMode) {
        case NvInferNetworkMode_FP16:
            nv12bltoRgbBatched(inputImageBuffer, inputImageWidth, inputImageWidth, inputImageHeight,
                               m_NetworkInputTensor, m_NetWidth, m_NetHeight, 16, scale_ratio_x, scale_ratio_y, 1,
                               m_NetworkScaleFactor, false, m_streamDLA);
            break;
        case NvInferNetworkMode_INT8:
            nv12bltoRgbBatched(inputImageBuffer, inputImageWidth, inputImageWidth, inputImageHeight,
                               m_NetworkInputTensor, m_NetWidth, m_NetHeight, 4, scale_ratio_x, scale_ratio_y, 1,
                               m_NetworkScaleFactor / m_NetworkInputTensorScale, true, m_streamDLA);
#if NV12BL_RGB_RESULT_CHECK
            // Warning!
            // The cuda_kernel result is very strange
            // Because of the cuDLA need padding for int8 chw4
            // you should dump to four channels, and open the image with rgba
            {
                checkCudaErrors(cudaStreamSynchronize(m_streamDLA));
                // Dump transformed image
                FILE *file = fopen("rgb_bl_uint8.out", "wb");
                size_t s = 1 * m_NetWidth * m_NetHeight * 4 * sizeof(uint8_t);
                auto true_s = fwrite(m_NetworkInputTensor, 1, s, file);
                if (true_s != s) {
                    printf(">>> dump rgb out error.%lu, %lu\n", s, true_s);
                }
                fflush(file);
                fclose(file);
                printf(">>> dump rgb out.\n");
            }
#endif
            break;
        default:
            LOG_ERR("Error: Unsupported network input Mode.\n");
            return false;
    }

    if (!m_Registed) {
        if (!dla_ctx->BufferRegister(m_NetworkInputTensor, m_OutputBboxTensor, m_OutputCoverageTensor)) {
            return false;
        }
        m_Registed = true;
    }

    if (!dla_ctx->SubmitDLATask(m_streamDLA)) {
        return false;
    }
    checkCudaErrors(cudaStreamSynchronize(m_streamDLA));
    // Post-process detection boxes and types on cpu
    PostProcess(scale_ratio_x, scale_ratio_y, fileDump);

    return true;
}

bool CCudlaTask::NmsCpu(std::vector<Bndbox> bndboxes, std::vector<Bndbox> &nms_pred)
{
    std::sort(bndboxes.begin(), bndboxes.end(),
              [](Bndbox boxes1, Bndbox boxes2) { return boxes1.score > boxes2.score; });
    std::vector<int> suppressed(bndboxes.size(), 0);
    for (size_t i = 0; i < bndboxes.size(); i++) {
        if (suppressed[i] == 1) {
            continue;
        }
        unsigned int times = 0;
        for (size_t j = i + 1; j < bndboxes.size(); j++) {
            if (suppressed[j] == 1) {
                continue;
            }

            int sa = (bndboxes[i].xMax - bndboxes[i].xMin) * (bndboxes[i].yMax - bndboxes[i].yMin);
            int sb = (bndboxes[j].xMax - bndboxes[j].xMin) * (bndboxes[j].yMax - bndboxes[j].yMin);

            int xMin_inter = std::max(bndboxes[i].xMin, bndboxes[j].xMin);
            int yMin_inter = std::max(bndboxes[i].yMin, bndboxes[j].yMin);
            int xMax_inter = std::min(bndboxes[i].xMax, bndboxes[j].xMax);
            int yMax_inter = std::min(bndboxes[i].yMax, bndboxes[j].yMax);

            int s_overlap = 0;
            if (xMin_inter < xMax_inter && yMin_inter < yMax_inter)
                s_overlap = (xMax_inter - xMin_inter) * (yMax_inter - yMin_inter);

            float iou = float(s_overlap) / std::max(float(sa + sb - s_overlap), ThresHold);

            if (s_overlap > 0 && iou >= m_groupIouThresh) {
                times++;
            }

            if (iou >= m_nmsThresh) {
                suppressed[j] = 1;
            }
        }
        if (times >= m_groupThresh) {
            nms_pred.emplace_back(bndboxes[i]);
        }
    }
    return true;
}

bool CCudlaTask::PostProcess(double scale_ratio_x, double scale_ratio_y, bool fileDump)
{
    NvInferFrameOutput frameOutput;
    for (size_t c = 0; c < m_OutputCoverageLayerDim.d[1]; c++) {
        std::vector<Bndbox> rect_list, nms_pred;
        switch (m_NetworkMode) {
            case NvInferNetworkMode_FP16:
                ParseBoundingBox((half *)m_OutputBboxTensor, (half *)m_OutputCoverageTensor, rect_list, c, 16);
                break;
            case NvInferNetworkMode_INT8:
                ParseBoundingBox((int8_t *)m_OutputBboxTensor, (int8_t *)m_OutputCoverageTensor, rect_list, c, 32);
                break;
            default:
                PLOG_ERR("parseBoundingBox Error: Unsupported network mode.\n");
                return false;
        }
        NmsCpu(rect_list, nms_pred);
        rect_list.clear();
        for (auto &rect : nms_pred) {
            NvInferObject object;
            object.left = rect.xMin;
            object.top = rect.yMin;
            object.width = rect.xMax - rect.xMin;
            object.height = rect.yMax - rect.yMin;
            object.classIndex = c;
            if (c < m_Labels.size() && m_Labels[c].size() > 0)
                object.label = m_Labels[c][0];
            frameOutput.objects.push_back(object);
        }
    }

    if (fileDump && !ofs.is_open()) {
        std::string file_name = "./cuDLA_detection_" + std::to_string(m_id) + ".txt";
        ofs.open(file_name, std::ios::out);
    }

    PLOG_INFO("[cuDLA %u  Detection] - %u objects were detected \n", m_id, frameOutput.objects.size());

    for (unsigned int i = 0; i < frameOutput.objects.size(); i++) {
        NvInferObject &obj = frameOutput.objects[i];

        /* Scale the bounding boxes proportionally based on how the object/frame was
     * scaled during input. */
        obj.left /= scale_ratio_x;
        obj.top /= scale_ratio_y;
        obj.width /= scale_ratio_x;
        obj.height /= scale_ratio_y;

        PLOG_INFO("[ %u %u %u %u ] object[%u]: %s \n", (unsigned)obj.left, (unsigned)obj.top, (unsigned)obj.width,
                  (unsigned)obj.height, i, obj.label.c_str());

        if (fileDump && ofs.is_open()) {
            ofs << obj.classIndex << " ";
            ofs << obj.left << " ";
            ofs << obj.top << " ";
            ofs << obj.width << " ";
            ofs << obj.height << " ";
            ofs << "\n";
        }
    }
    return true;
}

bool CCudlaTask::DestroyCudla()
{
    if (dla_ctx != NULL) {
        dla_ctx->CleanUp();
    }
    checkCudaErrors(cudaFree(m_NetworkInputTensor));
    checkCudaErrors(cudaFree(m_OutputBboxTensor));
    checkCudaErrors(cudaFree(m_OutputCoverageTensor));
    return true;
}

void CCudlaTask::ParseBoundingBox(const half *outputBboxBuffer,
                                  const half *outputCoverageBuffer,
                                  std::vector<Bndbox> &rectList,
                                  unsigned int classIndex,
                                  unsigned int strideC)
{
    int gridC = m_OutputCoverageLayerDim.d[1];
    int gridH = m_OutputCoverageLayerDim.d[2];
    int gridW = m_OutputCoverageLayerDim.d[3];

    int targetShape[2] = { gridW, gridH };
    float bboxNorm[2] = { 35.0, 35.0 };
    float gcCenters0[targetShape[0]];
    float gcCenters1[targetShape[1]];

    for (int i = 0; i < targetShape[0]; i++) {
        gcCenters0[i] = (float)(i * 16 + 0.5);
        gcCenters0[i] /= (float)bboxNorm[0];
    }
    for (int i = 0; i < targetShape[1]; i++) {
        gcCenters1[i] = (float)(i * 16 + 0.5);
        gcCenters1[i] /= (float)bboxNorm[1];
    }

    for (int h = 0; h < gridH; h++) {
        for (int w = 0; w < gridW; w++) {
            for (int c = 0; c < gridC; c++) {
                int i = int(c / strideC) * gridH * gridW * strideC + h * gridW * strideC + w * strideC + c % strideC;

                float score = outputCoverageBuffer[i + classIndex];
                if (score < m_scoreThresh)
                    continue;

                int rectX1, rectY1, rectX2, rectY2;
                float rectX1Float, rectY1Float, rectX2Float, rectY2Float;

                /* Centering and normalization of the rectangle. */
                rectX1Float = outputBboxBuffer[i + classIndex * 4 + 0] - gcCenters0[w];
                rectY1Float = outputBboxBuffer[i + classIndex * 4 + 1] - gcCenters1[h];
                rectX2Float = outputBboxBuffer[i + classIndex * 4 + 2] + gcCenters0[w];
                rectY2Float = outputBboxBuffer[i + classIndex * 4 + 3] + gcCenters1[h];

                rectX1Float *= (float)(-bboxNorm[0]);
                rectY1Float *= (float)(-bboxNorm[1]);
                rectX2Float *= (float)(bboxNorm[0]);
                rectY2Float *= (float)(bboxNorm[1]);

                rectX1 = (int)rectX1Float;
                rectY1 = (int)rectY1Float;
                rectX2 = (int)rectX2Float;
                rectY2 = (int)rectY2Float;

                if (rectX1 < 0)
                    rectX1 = 0;
                if (rectX2 < 0)
                    rectX2 = 0;
                if (rectY1 < 0)
                    rectY1 = 0;
                if (rectY2 < 0)
                    rectY2 = 0;

                /* Clip parsed rectangles to frame bounds. */
                if (rectX1 >= (int)m_NetWidth)
                    rectX1 = m_NetWidth - 1;
                if (rectX2 >= (int)m_NetWidth)
                    rectX2 = m_NetWidth - 1;
                if (rectY1 >= (int)m_NetHeight)
                    rectY1 = m_NetHeight - 1;
                if (rectY2 >= (int)m_NetHeight)
                    rectY2 = m_NetHeight - 1;

                rectList.push_back(Bndbox(rectX1, rectY1, rectX2, rectY2, score));
            }
        }
    }
}

void CCudlaTask::ParseBoundingBox(const int8_t *outputBboxBuffer,
                                  const int8_t *outputCoverageBuffer,
                                  std::vector<Bndbox> &rectList,
                                  unsigned int classIndex,
                                  unsigned int strideC)
{
    int gridC = m_OutputCoverageLayerDim.d[1];
    int gridH = m_OutputCoverageLayerDim.d[2];
    int gridW = m_OutputCoverageLayerDim.d[3];

    int targetShape[2] = { gridW, gridH };
    float bboxNorm[2] = { 35.0, 35.0 };
    float gcCenters0[targetShape[0]];
    float gcCenters1[targetShape[1]];

    for (int i = 0; i < targetShape[0]; i++) {
        gcCenters0[i] = (float)(i * 16 + 0.5);
        gcCenters0[i] /= (float)bboxNorm[0];
    }
    for (int i = 0; i < targetShape[1]; i++) {
        gcCenters1[i] = (float)(i * 16 + 0.5);
        gcCenters1[i] /= (float)bboxNorm[1];
    }

    for (int h = 0; h < gridH; h++) {
        for (int w = 0; w < gridW; w++) {
            for (int c = 0; c < gridC; c++) {
                int i = int(c / strideC) * gridH * gridW * strideC + h * gridW * strideC + w * strideC + c % strideC;

                float score = outputCoverageBuffer[i + classIndex] * m_OutputCoverageTensorScale;
                if (score < m_scoreThresh)
                    continue;

                int rectX1, rectY1, rectX2, rectY2;
                float rectX1Float, rectY1Float, rectX2Float, rectY2Float;

                /* Centering and normalization of the rectangle. */
                rectX1Float = outputBboxBuffer[i + classIndex * 4 + 0] * m_OutputBboxTensorScale - gcCenters0[w];
                rectY1Float = outputBboxBuffer[i + classIndex * 4 + 1] * m_OutputBboxTensorScale - gcCenters1[h];
                rectX2Float = outputBboxBuffer[i + classIndex * 4 + 2] * m_OutputBboxTensorScale + gcCenters0[w];
                rectY2Float = outputBboxBuffer[i + classIndex * 4 + 3] * m_OutputBboxTensorScale + gcCenters1[h];

                rectX1Float *= (float)(-bboxNorm[0]);
                rectY1Float *= (float)(-bboxNorm[1]);
                rectX2Float *= (float)(bboxNorm[0]);
                rectY2Float *= (float)(bboxNorm[1]);

                rectX1 = (int)rectX1Float;
                rectY1 = (int)rectY1Float;
                rectX2 = (int)rectX2Float;
                rectY2 = (int)rectY2Float;

                if (rectX1 < 0)
                    rectX1 = 0;
                if (rectX2 < 0)
                    rectX2 = 0;
                if (rectY1 < 0)
                    rectY1 = 0;
                if (rectY2 < 0)
                    rectY2 = 0;

                /* Clip parsed rectangles to frame bounds. */
                if (rectX1 >= (int)m_NetWidth)
                    rectX1 = m_NetWidth - 1;
                if (rectX2 >= (int)m_NetWidth)
                    rectX2 = m_NetWidth - 1;
                if (rectY1 >= (int)m_NetHeight)
                    rectY1 = m_NetHeight - 1;
                if (rectY2 >= (int)m_NetHeight)
                    rectY2 = m_NetHeight - 1;

                rectList.push_back(Bndbox(rectX1, rectY1, rectX2, rectY2, score));
            }
        }
    }
}

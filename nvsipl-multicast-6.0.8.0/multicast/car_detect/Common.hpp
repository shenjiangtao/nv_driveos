// Copyright (c) 2023 NVIDIA Corporation. All rights reserved.
//
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software, related documentation and any
// modifications thereto. Any use, reproduction, disclosure or distribution
// of this software and related documentation without an express license
// agreement from NVIDIA Corporation is strictly prohibited.

#ifndef COMMON_HPP
#define COMMON_HPP

#include <iostream>
#include <stdint.h>
#include <vector>

#include <cuda_fp16.h>
#include <cuda_runtime_api.h>
#include <cudla.h>

#define EXIT_SUCCESS 0 /* Successful exit status. */
#define EXIT_FAILURE 1 /* Failing exit status.    */
#define EXIT_WAIVED 2  /* WAIVED exit status.     */

#define checkCudaErrors(status)                                                                                   \
    {                                                                                                             \
        if (status != 0) {                                                                                        \
            std::cout << "Cuda failure: " << cudaGetErrorString(status) << " at line " << __LINE__ << " in file " \
                      << __FILE__ << " error status: " << status << std::endl;                                    \
            return false;                                                                                         \
        }                                                                                                         \
    }

class Dims32
{
  public:
    //! The maximum rank (number of dimensions) supported for a tensor.
    static constexpr size_t MAX_DIMS{ 8 };
    //! The rank (number of dimensions).
    size_t nbDims;
    //! The extent of each dimension.
    size_t d[MAX_DIMS];
};

enum NvInferNetworkMode
{
    NvInferNetworkMode_FP16,
    NvInferNetworkMode_INT8
};

struct NvInferInitParams
{
    NvInferNetworkMode networkMode;

    std::string int8CalibrationFilePath;
    std::string int8ModelCacheFilePath;
    std::string fp16ModelCacheFilePath;
    std::string int8TrtEngineFilePath;

    std::vector<std::vector<std::string>> labels;
    std::string testImageFilePath;

    float networkScaleFactor; /* Normalization factor to scale the input pixels. */

    std::string inputImageLayerName;
    std::string outputCoverageLayerName;
    std::string outputBboxLayerName;

    double scoreThresh;
    double nmsThresh;
    double groupIouThresh;
    unsigned int groupThresh;
};

struct NvInferObject
{
    unsigned int left;
    unsigned int top;
    unsigned int width;
    unsigned int height;
    int classIndex;
    std::string label;
};

struct NvInferFrameOutput
{
    std::vector<NvInferObject> objects;
};

struct Bndbox
{
    int xMin;
    int yMin;
    int xMax;
    int yMax;
    float score;
    Bndbox(){};
    Bndbox(int xMin_, int yMin_, int xMax_, int yMax_, float score_)
        : xMin(xMin_)
        , yMin(yMin_)
        , xMax(xMax_)
        , yMax(yMax_)
        , score(score_)
    {
    }
};

#endif
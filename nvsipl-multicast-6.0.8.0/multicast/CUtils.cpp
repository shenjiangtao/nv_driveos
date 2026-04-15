/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "CUtils.hpp"
#include <cstring>

using namespace std;

// Log utils
CLogger &CLogger::GetInstance()
{
    static CLogger instance;
    return instance;
}

void CLogger::SetLogLevel(LogLevel level)
{
    m_level = (level > LEVEL_DBG) ? LEVEL_DBG : level;
}

CLogger::LogLevel CLogger::GetLogLevel(void)
{
    return m_level;
}

void CLogger::SetLogStyle(LogStyle style)
{
    m_style = (style > LOG_STYLE_FUNCTION_LINE) ? LOG_STYLE_FUNCTION_LINE : style;
}

void CLogger::LogLevelMessageVa(
    LogLevel level, const char *functionName, uint32_t lineNumber, const char *prefix, const char *format, va_list ap)
{
    char str[256] = {
        '\0',
    };

    if (level > m_level) {
        return;
    }

    strcpy(str, "nvsipl_multicast: ");
    switch (level) {
        case LEVEL_NONE:
            break;
        case LEVEL_ERR:
            strcat(str, "ERROR: ");
            break;
        case LEVEL_WARN:
            strcat(str, "WARNING: ");
            break;
        case LEVEL_INFO:
            break;
        case LEVEL_DBG:
            // Empty
            break;
    }

    if (strlen(prefix) != 0) {
        strcat(str, prefix);
    }

    vsnprintf(str + strlen(str), sizeof(str) - strlen(str), format, ap);

    if (m_style == LOG_STYLE_NORMAL) {
        if (strlen(str) != 0 && str[strlen(str) - 1] == '\n') {
            strcat(str, "\n");
        }
    } else if (m_style == LOG_STYLE_FUNCTION_LINE) {
        if (strlen(str) != 0 && str[strlen(str) - 1] == '\n') {
            str[strlen(str) - 1] = 0;
        }
        snprintf(str + strlen(str), sizeof(str) - strlen(str), " at %s():%d\n", functionName, lineNumber);
    }

    cout << str;
}

void CLogger::LogLevelMessage(LogLevel level, const char *functionName, uint32_t lineNumber, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    LogLevelMessageVa(level, functionName, lineNumber, "", format, ap);
    va_end(ap);
}

void CLogger::LogLevelMessage(LogLevel level, std::string functionName, uint32_t lineNumber, std::string format, ...)
{
    va_list ap;
    va_start(ap, format);
    LogLevelMessageVa(level, functionName.c_str(), lineNumber, "", format.c_str(), ap);
    va_end(ap);
}

void CLogger::PLogLevelMessage(
    LogLevel level, const char *functionName, uint32_t lineNumber, std::string prefix, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    LogLevelMessageVa(level, functionName, lineNumber, prefix.c_str(), format, ap);
    va_end(ap);
}

void CLogger::PLogLevelMessage(
    LogLevel level, std::string functionName, uint32_t lineNumber, std::string prefix, std::string format, ...)
{
    va_list ap;
    va_start(ap, format);
    LogLevelMessageVa(level, functionName.c_str(), lineNumber, prefix.c_str(), format.c_str(), ap);
    va_end(ap);
}

void CLogger::LogMessageVa(const char *format, va_list ap)
{
    char str[128] = {
        '\0',
    };
    vsnprintf(str, sizeof(str), format, ap);
    cout << str;
}

void CLogger::LogMessage(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    LogMessageVa(format, ap);
    va_end(ap);
}

void CLogger::LogMessage(std::string format, ...)
{
    va_list ap;
    va_start(ap, format);
    LogMessageVa(format.c_str(), ap);
    va_end(ap);
}

/* Loads NITO file for given camera module.
 The function assumes the .nito files to be named same as camera module name.
 */
SIPLStatus LoadNITOFile(std::string folderPath, std::string moduleName, std::vector<uint8_t> &nito)
{
    // Set up blob file
    string nitoFilePath = (folderPath != "") ? folderPath : "/usr/share/camera/";
    string nitoFile = nitoFilePath + moduleName + ".nito";

    string moduleNameLower{};
    for (auto &c : moduleName) {
        moduleNameLower.push_back(std::tolower(c));
    }
    string nitoFileLower = nitoFilePath + moduleNameLower + ".nito";
    string nitoFileDefault = nitoFilePath + "default.nito";

    // Open NITO file
    auto fp = fopen(nitoFile.c_str(), "rb");
    if (fp == nullptr) {
        LOG_INFO("File \"%s\" not found\n", nitoFile.c_str());
        // Try lower case name
        fp = fopen(nitoFileLower.c_str(), "rb");
        if (fp == nullptr) {
            LOG_INFO("File \"%s\" not found\n", nitoFileLower.c_str());
            LOG_ERR("Unable to open NITO file for module \"%s\", image quality is not supported!\n",
                    moduleName.c_str());
            return NVSIPL_STATUS_BAD_ARGUMENT;
        } else {
            LOG_MSG("nvsipl_multicast: Opened NITO file for module \"%s\"\n", moduleName.c_str());
        }
    } else {
        LOG_MSG("nvsipl_multicast: Opened NITO file for module \"%s\"\n", moduleName.c_str());
    }

    // Check file size
    fseek(fp, 0, SEEK_END);
    auto fsize = ftell(fp);
    rewind(fp);

    if (fsize <= 0U) {
        LOG_ERR("NITO file for module \"%s\" is of invalid size\n", moduleName.c_str());
        fclose(fp);
        return NVSIPL_STATUS_BAD_ARGUMENT;
    }

    /* allocate blob memory */
    nito.resize(fsize);

    /* load nito */
    auto result = (long int)fread(nito.data(), 1, fsize, fp);
    if (result != fsize) {
        LOG_ERR("Fail to read data from NITO file for module \"%s\", image quality is not supported!\n",
                moduleName.c_str());
        nito.resize(0);
        fclose(fp);
        return NVSIPL_STATUS_BAD_ARGUMENT;
    }
    /* close file */
    fclose(fp);

    LOG_INFO("data from NITO file loaded for module \"%s\"\n", moduleName.c_str());

    return NVSIPL_STATUS_OK;
}

const char *NvSciBufAttrKeyToString(NvSciBufAttrKey key)
{
    switch (key) {
        case NvSciBufGeneralAttrKey_Types:
            return "NvSciBufGeneralAttrKey_Types";
        case NvSciBufGeneralAttrKey_NeedCpuAccess:
            return "NvSciBufGeneralAttrKey_NeedCpuAccess";
        case NvSciBufGeneralAttrKey_RequiredPerm:
            return "NvSciBufGeneralAttrKey_RequiredPerm";
        case NvSciBufGeneralAttrKey_EnableCpuCache:
            return "NvSciBufGeneralAttrKey_EnableCpuCache";
        case NvSciBufGeneralAttrKey_GpuId:
            return "NvSciBufGeneralAttrKey_GpuId";
        case NvSciBufGeneralAttrKey_CpuNeedSwCacheCoherency:
            return "NvSciBufGeneralAttrKey_CpuNeedSwCacheCoherency";
        case NvSciBufGeneralAttrKey_ActualPerm:
            return "NvSciBufGeneralAttrKey_ActualPerm";
        case NvSciBufGeneralAttrKey_VidMem_GpuId:
            return "NvSciBufGeneralAttrKey_VidMem_GpuId";
        case NvSciBufGeneralAttrKey_EnableGpuCache:
            return "NvSciBufGeneralAttrKey_EnableGpuCache";
        case NvSciBufGeneralAttrKey_GpuSwNeedCacheCoherency:
            return "NvSciBufGeneralAttrKey_GpuSwNeedCacheCoherency";
        case NvSciBufGeneralAttrKey_EnableGpuCompression:
            return "NvSciBufGeneralAttrKey_EnableGpuCompression";
        case NvSciBufRawBufferAttrKey_Size:
            return "NvSciBufRawBufferAttrKey_Size";
        case NvSciBufRawBufferAttrKey_Align:
            return "NvSciBufRawBufferAttrKey_Align";
        case NvSciBufImageAttrKey_Layout:
            return "NvSciBufImageAttrKey_Layout";
        case NvSciBufImageAttrKey_TopPadding:
            return "NvSciBufImageAttrKey_TopPadding";
        case NvSciBufImageAttrKey_BottomPadding:
            return "NvSciBufImageAttrKey_BottomPadding";
        case NvSciBufImageAttrKey_LeftPadding:
            return "NvSciBufImageAttrKey_LeftPadding";
        case NvSciBufImageAttrKey_RightPadding:
            return "NvSciBufImageAttrKey_RightPadding";
        case NvSciBufImageAttrKey_VprFlag:
            return "NvSciBufImageAttrKey_VprFlag";
        case NvSciBufImageAttrKey_Size:
            return "NvSciBufImageAttrKey_Size";
        case NvSciBufImageAttrKey_Alignment:
            return "NvSciBufImageAttrKey_Alignment";
        case NvSciBufImageAttrKey_PlaneCount:
            return "NvSciBufImageAttrKey_PlaneCount";
        case NvSciBufImageAttrKey_PlaneColorFormat:
            return "NvSciBufImageAttrKey_PlaneColorFormat";
        case NvSciBufImageAttrKey_PlaneColorStd:
            return "NvSciBufImageAttrKey_PlaneColorStd";
        case NvSciBufImageAttrKey_PlaneBaseAddrAlign:
            return "NvSciBufImageAttrKey_PlaneBaseAddrAlign";
        case NvSciBufImageAttrKey_PlaneWidth:
            return "NvSciBufImageAttrKey_PlaneWidth";
        case NvSciBufImageAttrKey_PlaneHeight:
            return "NvSciBufImageAttrKey_PlaneHeight";
        case NvSciBufImageAttrKey_ScanType:
            return "NvSciBufImageAttrKey_ScanType";
        case NvSciBufImageAttrKey_PlaneBitsPerPixel:
            return "NvSciBufImageAttrKey_PlaneBitsPerPixel";
        case NvSciBufImageAttrKey_PlaneOffset:
            return "NvSciBufImageAttrKey_PlaneOffset";
        case NvSciBufImageAttrKey_PlaneDatatype:
            return "NvSciBufImageAttrKey_PlaneDatatype";
        case NvSciBufImageAttrKey_PlaneChannelCount:
            return "NvSciBufImageAttrKey_PlaneChannelCount";
        case NvSciBufImageAttrKey_PlaneSecondFieldOffset:
            return "NvSciBufImageAttrKey_PlaneSecondFieldOffset";
        case NvSciBufImageAttrKey_PlanePitch:
            return "NvSciBufImageAttrKey_PlanePitch";
        case NvSciBufImageAttrKey_PlaneAlignedHeight:
            return "NvSciBufImageAttrKey_PlaneAlignedHeight";
        case NvSciBufImageAttrKey_PlaneAlignedSize:
            return "NvSciBufImageAttrKey_PlaneAlignedSize";
        case NvSciBufImageAttrKey_ImageCount:
            return "NvSciBufImageAttrKey_ImageCount";
        case NvSciBufImageAttrKey_SurfType:
            return "NvSciBufImageAttrKey_SurfType";
        case NvSciBufImageAttrKey_SurfMemLayout:
            return "NvSciBufImageAttrKey_SurfMemLayout";
        case NvSciBufImageAttrKey_SurfSampleType:
            return "NvSciBufImageAttrKey_SurfSampleType";
        case NvSciBufImageAttrKey_SurfBPC:
            return "NvSciBufImageAttrKey_SurfBPC";
        case NvSciBufImageAttrKey_SurfComponentOrder:
            return "NvSciBufImageAttrKey_SurfComponentOrder";
        case NvSciBufImageAttrKey_SurfWidthBase:
            return "NvSciBufImageAttrKey_SurfWidthBase";
        case NvSciBufImageAttrKey_SurfHeightBase:
            return "NvSciBufImageAttrKey_SurfHeightBase";
        case NvSciBufTensorAttrKey_DataType:
            return "NvSciBufTensorAttrKey_DataType";
        case NvSciBufTensorAttrKey_NumDims:
            return "NvSciBufTensorAttrKey_NumDims";
        case NvSciBufTensorAttrKey_SizePerDim:
            return "NvSciBufTensorAttrKey_SizePerDim";
        case NvSciBufTensorAttrKey_AlignmentPerDim:
            return "NvSciBufTensorAttrKey_AlignmentPerDim";
        case NvSciBufTensorAttrKey_StridesPerDim:
            return "NvSciBufTensorAttrKey_StridesPerDim";
        case NvSciBufTensorAttrKey_PixelFormat:
            return "NvSciBufTensorAttrKey_PixelFormat";
        case NvSciBufTensorAttrKey_BaseAddrAlign:
            return "NvSciBufTensorAttrKey_BaseAddrAlign";
        case NvSciBufTensorAttrKey_Size:
            return "NvSciBufTensorAttrKey_Size";
        case NvSciBufArrayAttrKey_DataType:
            return "NvSciBufArrayAttrKey_DataType";
        case NvSciBufArrayAttrKey_Stride:
            return "NvSciBufArrayAttrKey_Stride";
        case NvSciBufArrayAttrKey_Capacity:
            return "NvSciBufArrayAttrKey_Capacity";
        case NvSciBufArrayAttrKey_Size:
            return "NvSciBufArrayAttrKey_Size";
        case NvSciBufArrayAttrKey_Alignment:
            return "NvSciBufArrayAttrKey_Alignment";
        case NvSciBufPyramidAttrKey_NumLevels:
            return "NvSciBufPyramidAttrKey_NumLevels";
        case NvSciBufPyramidAttrKey_Scale:
            return "NvSciBufPyramidAttrKey_Scale";
        case NvSciBufPyramidAttrKey_LevelOffset:
            return "NvSciBufPyramidAttrKey_LevelOffset";
        case NvSciBufPyramidAttrKey_LevelSize:
            return "NvSciBufPyramidAttrKey_LevelSize";
        case NvSciBufPyramidAttrKey_Alignment:
            return "NvSciBufPyramidAttrKey_Alignment";
        default:
            return "Unknown Attribute";
    }
}

/*
 * Copy if the source buffer is valid and the size of the dest buffer greater or equal to the size
 * of the source buffer.
 */
#define NVSCIBUFATTR_COPY(attrKey, nvsciDst, nvsciSrc, length)                                                 \
    {                                                                                                          \
        if (nvsciSrc) {                                                                                        \
            if (sizeof(nvsciDst) >= length) {                                                                  \
                const void *src = (const void *)nvsciSrc;                                                      \
                void *dst = (void *)&(nvsciDst);                                                               \
                memcpy(dst, src, length);                                                                      \
            } else {                                                                                           \
                LOG_ERR("Retrieved attribute(%s) length is out of range Length:%d, Expected range: 0 to %d\n", \
                        NvSciBufAttrKeyToString(attrKey), length, sizeof(nvsciDst));                           \
                return NVSIPL_STATUS_ERROR;                                                                    \
            }                                                                                                  \
        } else {                                                                                               \
            LOG_WARN("Retrieved attribute(%s) doesn't exist.\n", NvSciBufAttrKeyToString(attrKey));            \
        }                                                                                                      \
    }

SIPLStatus PopulateBufAttr(const NvSciBufObj &sciBufObj, BufferAttrs &bufAttrs)
{
    NvSciError err = NvSciError_Success;
    NvSciBufAttrList bufAttrList;

    NvSciBufAttrKeyValuePair imgAttrs[] = {
        { NvSciBufImageAttrKey_Size, NULL, 0 },                     //0
        { NvSciBufImageAttrKey_Layout, NULL, 0 },                   //1
        { NvSciBufImageAttrKey_PlaneCount, NULL, 0 },               //2
        { NvSciBufImageAttrKey_PlaneWidth, NULL, 0 },               //3
        { NvSciBufImageAttrKey_PlaneHeight, NULL, 0 },              //4
        { NvSciBufImageAttrKey_PlanePitch, NULL, 0 },               //5
        { NvSciBufImageAttrKey_PlaneBitsPerPixel, NULL, 0 },        //6
        { NvSciBufImageAttrKey_PlaneAlignedHeight, NULL, 0 },       //7
        { NvSciBufImageAttrKey_PlaneAlignedSize, NULL, 0 },         //8
        { NvSciBufImageAttrKey_PlaneChannelCount, NULL, 0 },        //9
        { NvSciBufImageAttrKey_PlaneOffset, NULL, 0 },              //10
        { NvSciBufImageAttrKey_PlaneColorFormat, NULL, 0 },         //11
        { NvSciBufImageAttrKey_TopPadding, NULL, 0 },               //12
        { NvSciBufImageAttrKey_BottomPadding, NULL, 0 },            //13
        { NvSciBufGeneralAttrKey_CpuNeedSwCacheCoherency, NULL, 0 } //14
    };

    err = NvSciBufObjGetAttrList(sciBufObj, &bufAttrList);
    CHK_NVSCISTATUS_AND_RETURN(err, "NvSciBufObjGetAttrList");
    err = NvSciBufAttrListGetAttrs(bufAttrList, imgAttrs, sizeof(imgAttrs) / sizeof(imgAttrs[0]));
    CHK_NVSCISTATUS_AND_RETURN(err, "NvSciBufAttrListGetAttrs");

    bufAttrs.size = *(static_cast<const uint64_t *>(imgAttrs[0].value));
    bufAttrs.layout = *(static_cast<const NvSciBufAttrValImageLayoutType *>(imgAttrs[1].value));
    bufAttrs.planeCount = *(static_cast<const uint32_t *>(imgAttrs[2].value));
    bufAttrs.needSwCacheCoherency = *(static_cast<const bool *>(imgAttrs[14].value));

    NVSCIBUFATTR_COPY(NvSciBufImageAttrKey_PlaneWidth, bufAttrs.planeWidths, imgAttrs[3].value, imgAttrs[3].len);

    NVSCIBUFATTR_COPY(NvSciBufImageAttrKey_PlaneHeight, bufAttrs.planeHeights, imgAttrs[4].value, imgAttrs[4].len);

    NVSCIBUFATTR_COPY(NvSciBufImageAttrKey_PlanePitch, bufAttrs.planePitches, imgAttrs[5].value, imgAttrs[5].len);

    NVSCIBUFATTR_COPY(NvSciBufImageAttrKey_PlaneBitsPerPixel, bufAttrs.planeBitsPerPixels, imgAttrs[6].value,
                      imgAttrs[6].len);

    NVSCIBUFATTR_COPY(NvSciBufImageAttrKey_PlaneAlignedHeight, bufAttrs.planeAlignedHeights, imgAttrs[7].value,
                      imgAttrs[7].len);

    NVSCIBUFATTR_COPY(NvSciBufImageAttrKey_PlaneAlignedSize, bufAttrs.planeAlignedSizes, imgAttrs[8].value,
                      imgAttrs[8].len);

    NVSCIBUFATTR_COPY(NvSciBufImageAttrKey_PlaneChannelCount, bufAttrs.planeChannelCounts, imgAttrs[9].value,
                      imgAttrs[9].len);

    NVSCIBUFATTR_COPY(NvSciBufImageAttrKey_PlaneOffset, bufAttrs.planeOffsets, imgAttrs[10].value, imgAttrs[10].len);

    NVSCIBUFATTR_COPY(NvSciBufImageAttrKey_PlaneColorFormat, bufAttrs.planeColorFormats, imgAttrs[11].value,
                      imgAttrs[11].len);

    NVSCIBUFATTR_COPY(NvSciBufImageAttrKey_TopPadding, bufAttrs.topPadding, imgAttrs[12].value, imgAttrs[12].len);

    NVSCIBUFATTR_COPY(NvSciBufImageAttrKey_BottomPadding, bufAttrs.bottomPadding, imgAttrs[13].value, imgAttrs[13].len);

    //Print sciBuf attributes
    LOG_DBG("========PopulateBufAttr========\n");
    LOG_DBG("size=%lu, layout=%u, planeCount=%u\n", bufAttrs.size, bufAttrs.layout, bufAttrs.planeCount);
    for (auto i = 0U; i < bufAttrs.planeCount; i++) {
        LOG_DBG(
            "plane %u: planeWidth=%u, planeHeight=%u, planePitch=%u, planeBitsPerPixels=%u, planeAlignedHeight=%u\n", i,
            bufAttrs.planeWidths[i], bufAttrs.planeHeights[i], bufAttrs.planePitches[i], bufAttrs.planeBitsPerPixels[i],
            bufAttrs.planeAlignedHeights[i]);
        LOG_DBG("plane %u: planeAlignedSize=%lu, planeOffset=%lu, planeColorFormat=%u, planeChannelCount=%u\n", i,
                bufAttrs.planeAlignedSizes[i], bufAttrs.planeOffsets[i], bufAttrs.planeColorFormats[i],
                bufAttrs.planeChannelCounts[i]);
    }

    return NVSIPL_STATUS_OK;
}

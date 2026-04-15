/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/* STL Headers */
#include <unistd.h>
#include <cstring>
#include <iostream>

#include "CSiplCamera.hpp"
#include "platform/ar0820.hpp"

#if !NVMEDIA_QNX
#include <fstream>
#else // NVMEDIA_QNX
#include "nvdtcommon.h"
#endif // !NVMEDIA_QNX

#if NVMEDIA_QNX
SIPLStatus CSiplCamera::GetDTPropAsString(const void *node, const char *const name, char val[], const uint32_t size)
{
    CHK_PTR_AND_RETURN_BADARG(node, "node");
    CHK_PTR_AND_RETURN_BADARG(name, "name");
    CHK_PTR_AND_RETURN_BADARG(val, "val");

    if (size == 0U) {
        LOG_ERR("size cannot be zero\n");
        return NVSIPL_STATUS_BAD_ARGUMENT;
    }

    uint32_t propLengthBytes = 0U;
    void const *const val_str = nvdt_node_get_prop(node, name, &propLengthBytes);
    CHK_PTR_AND_RETURN_BADARG(val_str, "val_str");
    if (propLengthBytes == 0U) {
        LOG_ERR("Property string cannot be zero-length\n");
        return NVSIPL_STATUS_ERROR;
    }

    if (propLengthBytes > size) {
        LOG_ERR("Property string exceeds maximum length\n");
        return NVSIPL_STATUS_ERROR;
    }

    memcpy(&val[0], val_str, static_cast<std::size_t>(propLengthBytes));

    if (val[(propLengthBytes - 1U)] != '\0') {
        LOG_ERR("Failed to parse property string\n");
        return NVSIPL_STATUS_ERROR;
    }

    return NVSIPL_STATUS_OK;
}
#endif // NVMEDIA_QNX

SIPLStatus CSiplCamera::CheckSKU(const std::string &findStr, bool &bFound)
{
#if !NVMEDIA_QNX
    std::string sTargetModelNode = "/proc/device-tree/model";
    std::ifstream fs;
    fs.open(sTargetModelNode, std::ifstream::in);
    if (!fs.is_open()) {
        LOG_ERR("%s: cannot open board node %s\n", __func__, sTargetModelNode.c_str());
        return NVSIPL_STATUS_ERROR;
    }

    // Read the file in to the string.
    std::string nodeString;
    fs >> nodeString;

    if (strstr(nodeString.c_str(), findStr.c_str())) {
        bFound = true;
    }

    if (fs.is_open()) {
        fs.close();
    }
#else  // NVMEDIA_QNX
    /* Get handle for DTB */
    if (NVDT_SUCCESS != nvdt_open()) {
        LOG_ERR("nvdt_open failed\n");
        return NVSIPL_STATUS_OUT_OF_MEMORY;
    }

    /* Check the Model */
    const void *modelNode;
    modelNode = nvdt_get_node_by_path("/");
    if (modelNode == NULL) {
        LOG_ERR("No node for model\n");
        (void)nvdt_close();
        return NVSIPL_STATUS_OUT_OF_MEMORY;
    }

    char name[20];
    SIPLStatus status =
        GetDTPropAsString(modelNode, "model", &name[0], static_cast<uint32_t>(sizeof(name) / sizeof(name[0])));
    if (status != NVSIPL_STATUS_OK) {
        (void)nvdt_close();
        return status;
    }

    if (strstr(name, findStr.c_str())) {
        bFound = true;
    }
    /* close nvdt once done */
    (void)nvdt_close();
#endif // !NVMEDIA_QNX

    return NVSIPL_STATUS_OK;
}

SIPLStatus CSiplCamera::UpdatePlatformCfgPerBoardModel(PlatformCfg *platformCfg)
{
    CHK_PTR_AND_RETURN_BADARG(platformCfg, "platformCfg");

    /* GPIO power control is required for Drive Orin (P3663) but not Firespray (P3710)
     * If using another platform (something customer-specific, for example) the GPIO
     * field may need to be modified
     */
    bool isP3663 = false;
    SIPLStatus status = CheckSKU("3663", isP3663);
    CHK_STATUS_AND_RETURN(status, "CheckSKU");
    if (isP3663) {
        std::vector<uint32_t> gpios = { 7 };
        CHK_PTR_AND_RETURN_BADARG(platformCfg->deviceBlockList, "deviceBlockList");
        platformCfg->deviceBlockList[0].gpios = gpios;
    }

    return status;
}

SIPLStatus CSiplCamera::Setup(CAppConfig *pAppConfig)
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    CHK_PTR_AND_RETURN(pAppConfig, "pAppConfig");
    m_pAppConfig = pAppConfig;

    LOG_INFO("Checking SIPL version\n");
    NvSIPLVersion oVer;
    NvSIPLGetVersion(oVer);

    LOG_INFO("NvSIPL library version: %u.%u.%u\n", oVer.uMajor, oVer.uMinor, oVer.uPatch);
    LOG_INFO("NVSIPL header version: %u %u %u\n", NVSIPL_MAJOR_VER, NVSIPL_MINOR_VER, NVSIPL_PATCH_VER);
    if (oVer.uMajor != NVSIPL_MAJOR_VER || oVer.uMinor != NVSIPL_MINOR_VER || oVer.uPatch != NVSIPL_PATCH_VER) {
        LOG_ERR("NvSIPL library and header version mismatch\n");
    }

    m_pPlatformCfg = m_pAppConfig->GetPlatformCfg();
    CHK_PTR_AND_RETURN(m_pPlatformCfg, "upAppConfig->GetPlatformCfg");

    status = UpdatePlatformCfgPerBoardModel(m_pPlatformCfg);
    CHK_STATUS_AND_RETURN(status, "UpdatePlatformCfgPerBoardModel");

    // for each sensor
    for (auto d = 0u; d != m_pPlatformCfg->numDeviceBlocks; d++) {
        const auto& db = m_pPlatformCfg->deviceBlockList[d];
        for (auto m = 0u; m != db.numCameraModules; m++) {
            m_vCameraModules.push_back(db.cameraModuleInfoList[m]);
        }
    }

    return NVSIPL_STATUS_OK;
}

SIPLStatus CSiplCamera::GetPipelineCfg(uint32_t sensorId, NvSIPLPipelineConfiguration &pipeCfg)
{
    if (m_pAppConfig->IsYUVSensor(sensorId)) {
        pipeCfg.captureOutputRequested = true;
        pipeCfg.isp0OutputRequested = false;
        pipeCfg.isp1OutputRequested = false;
    } else if (m_pAppConfig->IsMultiElementsEnabled()) {
        pipeCfg.captureOutputRequested = false;
        pipeCfg.isp0OutputRequested = true;
        pipeCfg.isp1OutputRequested = true;
        LOG_MSG("Enable ISP1 output for multiple elements\n");
    } else {
        pipeCfg.captureOutputRequested = false;
        pipeCfg.isp0OutputRequested = true;
        pipeCfg.isp1OutputRequested = false;
    }
    pipeCfg.isp2OutputRequested = false;
    return NVSIPL_STATUS_OK;
}

SIPLStatus CSiplCamera::GetOutputTypeList(NvSIPLPipelineConfiguration &pipeCfg,
                                          std::vector<INvSIPLClient::ConsumerDesc::OutputType> &outputList)
{
    if (pipeCfg.captureOutputRequested) {
        outputList.push_back(INvSIPLClient::ConsumerDesc::OutputType::ICP);
    }
    if (pipeCfg.isp0OutputRequested) {
        outputList.push_back(INvSIPLClient::ConsumerDesc::OutputType::ISP0);
    }
    if (pipeCfg.isp1OutputRequested) {
        outputList.push_back(INvSIPLClient::ConsumerDesc::OutputType::ISP1);
    }
    if (pipeCfg.isp2OutputRequested) {
        outputList.push_back(INvSIPLClient::ConsumerDesc::OutputType::ISP2);
    }
    return NVSIPL_STATUS_OK;
}

SIPLStatus CSiplCamera::Init(ICallback *callback)
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    // Camera Master setup
    m_upCamera = INvSIPLCamera::GetInstance();
    CHK_PTR_AND_RETURN(m_upCamera, "INvSIPLCamera::GetInstance()");

    NvSIPLDeviceBlockQueues deviceBlockQueues;

    status = m_upCamera->SetPlatformCfg(m_pPlatformCfg, deviceBlockQueues);
    CHK_STATUS_AND_RETURN(status, "INvSIPLCamera::SetPlatformCfg");

    for (const auto &module : m_vCameraModules) {
        uint32_t uSensorId = module.sensorInfo.id;
        NvSIPLPipelineConfiguration pipelineCfg{};
        NvSIPLPipelineQueues pipelineQueues{};
        std::vector<INvSIPLClient::ConsumerDesc::OutputType> eOutputList;

        GetPipelineCfg(module.sensorInfo.vcInfo.inputFormat, pipelineCfg);
        GetOutputTypeList(pipelineCfg, eOutputList);
        status = m_upCamera->SetPipelineCfg(uSensorId, pipelineCfg, pipelineQueues);
        CHK_STATUS_AND_RETURN(status, "INvSIPLCamera::SetPipelineConfig");

        INvSIPLFrameCompletionQueue *frameCompletionQueue[MAX_OUTPUTS_PER_SENSOR];

        frameCompletionQueue[(uint32_t)INvSIPLClient::ConsumerDesc::OutputType::ICP] =
            pipelineQueues.captureCompletionQueue;
        frameCompletionQueue[(uint32_t)INvSIPLClient::ConsumerDesc::OutputType::ISP0] =
            pipelineQueues.isp0CompletionQueue;
        frameCompletionQueue[(uint32_t)INvSIPLClient::ConsumerDesc::OutputType::ISP1] =
            pipelineQueues.isp1CompletionQueue;

        //Register handlers
        auto upFrameCompletionQueueHandler =
            std::unique_ptr<CPipelineFrameQueueHandler>(new CPipelineFrameQueueHandler());
        CHK_PTR_AND_RETURN(upFrameCompletionQueueHandler, "Frame Completion Queue handler creation");

        std::vector<std::pair<INvSIPLClient::ConsumerDesc::OutputType, INvSIPLFrameCompletionQueue *>>
            pFrameCompletionQueue;
        for (auto outputType : eOutputList) {

            pFrameCompletionQueue.push_back(std::make_pair(outputType, frameCompletionQueue[(uint32_t)outputType]));

            auto upNotificationHandler =
                std::unique_ptr<CPipelineNotificationHandler>(new CPipelineNotificationHandler());
            CHK_PTR_AND_RETURN(upNotificationHandler, "Notification handler creation");

            status = upNotificationHandler->Init(uSensorId, pipelineQueues.notificationQueue, m_pAppConfig);
            CHK_STATUS_AND_RETURN(status, "Notification Handler Init");

            m_vupNotificationHandler.push_back(move(upNotificationHandler));
        }

        upFrameCompletionQueueHandler->Init(uSensorId, pFrameCompletionQueue, callback);
        CHK_STATUS_AND_RETURN(status, "Frame Completion Queues Handler Init");

        m_vupFrameCompletionQueueHandler.push_back(move(upFrameCompletionQueueHandler));
    }

    LOG_INFO("m_upCamera Init\n");
    status = m_upCamera->Init();
    CHK_STATUS_AND_RETURN(status, "INvSIPLCamera::Init()");

    for (auto d = 0u; d != m_pPlatformCfg->numDeviceBlocks; d++) {
        auto upDeviceBlockNotifyHandler =
            std::unique_ptr<CDeviceBlockNotificationHandler>(new CDeviceBlockNotificationHandler());
        CHK_PTR_AND_RETURN(upDeviceBlockNotifyHandler, "Device Block Notification handler creation");

        status = upDeviceBlockNotifyHandler->Init(d, &m_pPlatformCfg->deviceBlockList[d],
                                                  deviceBlockQueues.notificationQueue[d], m_upCamera.get(),
                                                  m_pAppConfig->IsErrorIgnored());
        CHK_STATUS_AND_RETURN(status, "Device Block Notification Handler Init");

        m_vupDeviceBlockNotifyHandler.push_back(move(upDeviceBlockNotifyHandler));
    }

    return NVSIPL_STATUS_OK;
}

SIPLStatus CSiplCamera::DeInit()
{
    bool bDeviceBlockError = false;
    bool bPipelineError = false;
    SIPLStatus status = NVSIPL_STATUS_OK;

    for (auto &notificationHandler : m_vupDeviceBlockNotifyHandler) {
        LOG_INFO("Deinitializing devblk notificationHandler: %u\n", notificationHandler->m_uDevBlkIndex);
        bDeviceBlockError |= notificationHandler->IsDeviceBlockInError();
        notificationHandler->Deinit();
    }

    for (auto &notificationHandler : m_vupNotificationHandler) {
        LOG_INFO("Deinitializing pipeline notificationHandler: %u\n", notificationHandler->m_uSensor);
        bPipelineError |= notificationHandler->IsPipelineInError();
        notificationHandler->Deinit();
    }

    for (auto &frameCompletionQueueHandler : m_vupFrameCompletionQueueHandler) {
        LOG_INFO("Deinitializing frameCompletionQueueHandler: %u\n", frameCompletionQueueHandler->m_uSensor);
        frameCompletionQueueHandler->Deinit();
    }

    status = m_upCamera->Deinit();
    CHK_STATUS_AND_RETURN(status, "INvSIPLCamera::Deinit");

    LOG_DBG("Release m_upCamera");
    m_upCamera.reset(nullptr);

    m_vupFrameCompletionQueueHandler.clear();
    m_vupNotificationHandler.clear();
    m_vupDeviceBlockNotifyHandler.clear();

    return status;
}

SIPLStatus CSiplCamera::RegisterAutoControlPlugin()
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    for (const auto &module : m_vCameraModules) {
        uint32_t uSensorId = module.sensorInfo.id;
        if (m_pAppConfig->IsYUVSensor(uSensorId)) {
            continue;
        }
        //load nito
        std::vector<uint8_t> blob;
        status = LoadNITOFile(m_pAppConfig->GetNitoFolderPath(), module.name, blob);
        CHK_STATUS_AND_RETURN(status, "Load NITO file");

        LOG_INFO("RegisterAutoControl.\n");
        status = m_upCamera->RegisterAutoControlPlugin(uSensorId, NV_PLUGIN, nullptr, blob);
        CHK_STATUS_AND_RETURN(status, "INvSIPLCamera::RegisterAutoControlPlugin");
    }

    return NVSIPL_STATUS_OK;
}

SIPLStatus CSiplCamera::Start()
{
    const SIPLStatus status = m_upCamera->Start();
    CHK_STATUS_AND_RETURN(status, "INvSIPLCamera::Start");

    return NVSIPL_STATUS_OK;
}

SIPLStatus CSiplCamera::Stop()
{
    const SIPLStatus status = m_upCamera->Stop();
    CHK_STATUS_AND_RETURN(status, "INvSIPLCamera::Stop");

    return NVSIPL_STATUS_OK;
}

/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "CMaster.hpp"
#include "CIpcProducerChannel.hpp"
#include "CIpcConsumerChannel.hpp"

/* NvSIPL Headers */
#include "NvSIPLVersion.hpp" // Version
#include "NvSIPLTrace.hpp"   // Trace
#if !NV_IS_SAFETY
#include "NvSIPLQuery.hpp"      // Query
#include "NvSIPLQueryTrace.hpp" // Query Trace
#endif

#if !NVMEDIA_QNX
#include <fstream>
#else // NVMEDIA_QNX
#include "nvdtcommon.h"
#endif // !NVMEDIA_QNX

using namespace std;
using namespace nvsipl;

#define SECONDS_PER_ITERATION (2)

void CMaster::SetLogLevel(uint32_t verbosity)
{
    LOG_INFO("Setting verbosity level: %u\n", verbosity);

#if !NV_IS_SAFETY
    INvSIPLQueryTrace::GetInstance()->SetLevel((INvSIPLQueryTrace::TraceLevel)verbosity);
    INvSIPLTrace::GetInstance()->SetLevel((INvSIPLTrace::TraceLevel)verbosity);
#endif // !NV_IS_SAFETY
    CLogger::GetInstance().SetLogLevel((CLogger::LogLevel)verbosity);
}

bool CMaster::IsProducerResident()
{
    return m_producerResident;
}

SIPLStatus CMaster::InitStream()
{
    LOG_DBG("Enter: CMaster::InitStream()\n");
    auto sciErr = NvSciBufModuleOpen(&m_sciBufModule);
    CHK_NVSCISTATUS_AND_RETURN(sciErr, "NvSciBufModuleOpen");

    sciErr = NvSciSyncModuleOpen(&m_sciSyncModule);
    CHK_NVSCISTATUS_AND_RETURN(sciErr, "NvSciSyncModuleOpen");

    if (m_pAppConfig->GetCommType() != CommType_IntraProcess) {
        auto sciErr = NvSciIpcInit();
        CHK_NVSCISTATUS_AND_RETURN(sciErr, "NvSciIpcInit");
    }

    if (m_pAppConfig->IsStitchingDisplayEnabled() || m_pAppConfig->IsDPMSTDisplayEnabled()) {
        m_spWFDController = std::make_shared<COpenWFDController>();
        CHK_PTR_AND_RETURN(m_spWFDController, "COpenWFDController");
        auto status = m_spWFDController->InitResource(m_pAppConfig->IsDPMSTDisplayEnabled() ? MAX_NUM_WFD_PORTS : 1U);
        CHK_STATUS_AND_RETURN(status, " m_spWFDController->InitResource()");
    }

    LOG_INFO("RegisterSource.\n");
    for (auto &module : m_upSiplCamera->m_vCameraModules) {
        unique_ptr<CProfiler> upProfiler = unique_ptr<CProfiler>(new CProfiler());
        CHK_PTR_AND_RETURN(upProfiler, "Profiler creation");
        CProfiler *pProfiler = upProfiler.get();
        SensorInfo *pSensorInfo = &module.sensorInfo;

        auto outType = INvSIPLClient::ConsumerDesc::OutputType::ISP0;
        if (m_pAppConfig->IsYUVSensor(pSensorInfo->id)) {
            outType = INvSIPLClient::ConsumerDesc::OutputType::ICP;
        }

        upProfiler->Init(module.sensorInfo.id, outType);
        m_vupProfilers.push_back(move(upProfiler));

        if (nullptr == m_upDisplaychannel && m_pAppConfig->IsStitchingDisplayEnabled()) {
            m_upDisplaychannel = CreateDisplayChannel(pSensorInfo, pProfiler);
            CHK_PTR_AND_RETURN(m_upDisplaychannel, "Display CreateChannel");

            auto status = m_upDisplaychannel->CreateBlocks(pProfiler);
            CHK_STATUS_AND_RETURN(status, "Display CreateBlocks");
        }

        m_upChannels[pSensorInfo->id] = CreateChannel(pSensorInfo, pProfiler, m_upDisplaychannel);
        CHK_PTR_AND_RETURN(m_upChannels[pSensorInfo->id], "Master CreateChannel");
        m_upChannels[pSensorInfo->id]->Init();

        auto status = m_upChannels[pSensorInfo->id]->CreateBlocks(pProfiler);
        CHK_STATUS_AND_RETURN(status, "Master CreateBlocks");
    }

    for (auto i = 0U; i < MAX_NUM_SENSORS; i++) {
        if (nullptr != m_upChannels[i]) {
            auto status = m_upChannels[i]->Connect();
            CHK_STATUS_AND_RETURN(status, "CMaster: Channel connect.");

            status = m_upChannels[i]->InitBlocks();
            CHK_STATUS_AND_RETURN(status, "InitBlocks");

            status = m_upChannels[i]->Reconcile();
            CHK_STATUS_AND_RETURN(status, "Channel Reconcile");
        }
    }

    if (m_upDisplaychannel) {
        auto status = InitStitchingToDisplay();
        CHK_STATUS_AND_RETURN(status, "InitStitchingToDisplay");
    }
    LOG_DBG("Exit: CMaster::InitStream()\n");

    return NVSIPL_STATUS_OK;
}

SIPLStatus CMaster::DeInitStream()
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    LOG_DBG("Enter: CMaster::DeInitStream()\n");
    if (m_upDisplaychannel) {
        m_upDisplaychannel->Deinit();
        m_upDisplaychannel.reset();
    }

    for (auto i = 0U; i < MAX_NUM_SENSORS; i++) {
        if (nullptr != m_upChannels[i]) {
            m_upChannels[i]->Deinit();
            m_upChannels[i].reset();
        }
    }

    if (m_spWFDController) {
        m_spWFDController->DeInit();
        m_spWFDController.reset();
    }

    m_vupProfilers.clear();

    if (m_sciBufModule != nullptr) {
        NvSciBufModuleClose(m_sciBufModule);
    }

    if (m_sciSyncModule != nullptr) {
        NvSciSyncModuleClose(m_sciSyncModule);
    }

    if (m_pAppConfig->GetCommType() != CommType_IntraProcess) {
        NvSciIpcDeinit();
    }
    LOG_DBG("Exit: CMaster::DeInitStream()\n");

    return status;
}

SIPLStatus CMaster::PreInit(CAppConfig *pAppConfig)
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    LOG_DBG("Enter: CMaster::PreInit()\n");
    CHK_PTR_AND_RETURN(pAppConfig, "pAppConfig");
    m_pAppConfig = pAppConfig;

    m_producerResident =
        (pAppConfig->GetCommType() == CommType_IntraProcess) || (pAppConfig->GetEntityType() == EntityType_Producer);
    LOG_INFO("commType: %u, entityType: %u, producerResident: %u\n", pAppConfig->GetCommType(),
             pAppConfig->GetEntityType(), m_producerResident);

    m_upSiplCamera = std::make_unique<CSiplCamera>();
    m_upSiplCamera->Setup(m_pAppConfig);
    LOG_DBG("Exit: CMaster::PreInit()\n");

    return status;
}

SIPLStatus CMaster::PostDeInit()
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    LOG_DBG("Enter: CMaster::PostDeInit()\n");
    m_upSiplCamera.reset(nullptr);
    LOG_DBG("Exit: CMaster::PostDeInit()\n");

    return status;
}

SIPLStatus CMaster::Init()
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    LOG_DBG("Enter: CMaster::Init()\n");
    if (m_producerResident) {
        status = m_upSiplCamera->Init(this);
        CHK_STATUS_AND_RETURN(status, "CSiplCamera::Init");
    }

    status = InitStream();
    CHK_STATUS_AND_RETURN(status, "CMaster::InitStream");

    if (m_producerResident) {
        status = m_upSiplCamera->RegisterAutoControlPlugin();
        CHK_STATUS_AND_RETURN(status, "CSiplCamera::RegisterAutoControlPlugin");
    }

    LOG_DBG("Exit: CMaster::Init()\n");

    return status;
}

SIPLStatus CMaster::DeInit()
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    LOG_DBG("Enter: CMaster::DeInit()\n");
    status = DeInitStream();
    CHK_STATUS_AND_RETURN(status, "CMaster::DeInitStream");

    if (m_producerResident) {
        status = m_upSiplCamera->DeInit();
    }
    LOG_DBG("Exit: CMaster::DeInit()\n");

    return status;
}

SIPLStatus CMaster::Start()
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    LOG_DBG("Enter: CMaster::Start()\n");
    status = StartStream();
    CHK_STATUS_AND_RETURN(status, "CMaster::StartStream");

    m_bMonitorThreadQuit = false;
    m_upMonitorThread.reset(new std::thread(&CMaster::MonitorThreadFunc, this));

    if (m_producerResident) {
        LOG_INFO("StartPipeline().\n");
        status = m_upSiplCamera->Start();
        CHK_STATUS_AND_RETURN(status, "CSiplCamera::Start");
    }
    LOG_DBG("Exit: CMaster::Start()\n");

    return NVSIPL_STATUS_OK;
}

SIPLStatus CMaster::Stop()
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    LOG_DBG("Enter: CMaster::Stop()\n");
    if (m_producerResident) {
        status = m_upSiplCamera->Stop();
        CHK_STATUS_AND_RETURN(status, "CSiplCamera::Stop");
    }

    if (m_upMonitorThread != nullptr) {
        m_bMonitorThreadQuit = true;
        m_upMonitorThread->join();
        m_upMonitorThread.reset();
    }

    StopStream();
    LOG_DBG("Exit: CMaster::Stop()\n");

    return status;
}

void CMaster::Quit(SIPLStatus status)
{
    GraceQuit(status);
}

SIPLStatus CMaster::Resume()
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    LOG_DBG("Enter: CMaster::Resume()\n");
    if ((m_PMStatus == PM_RESUME_PREPARE) || (m_PMStatus == PM_POST_RESUME))
        return status;

    m_PMStatus = PM_RESUME_PREPARE;
    status = Init();
    CHK_STATUS_AND_RETURN(status, "CMaster::Init");
    status = Start();
    CHK_STATUS_AND_RETURN(status, "CMaster::Start");
    m_PMStatus = PM_POST_RESUME;

    LOG_DBG("Exit: CMaster::Resume()\n");
    return status;
}

SIPLStatus CMaster::Suspend()
{
    SIPLStatus status = NVSIPL_STATUS_OK;

    LOG_DBG("Enter: CMaster::Suspend()\n");
    if ((m_PMStatus == PM_SUSPEND_PREPARE) || (m_PMStatus == PM_POST_SUSPEND))
        return status;

    m_PMStatus = PM_SUSPEND_PREPARE;
    status = Stop();
    CHK_STATUS_AND_RETURN(status, "CMaster::Stop");
    status = DeInit();
    CHK_STATUS_AND_RETURN(status, "CMaster::DeInit");
    m_PMStatus = PM_POST_SUSPEND;
    LOG_DBG("Exit: CMaster::Suspend()\n");

    return status;
}

SIPLStatus CMaster::StartStream(void)
{
    LOG_DBG("Enter: CMaster::StartStream()\n");

    if (m_upDisplaychannel) {
        m_upDisplaychannel->Start();
    }

    for (auto i = 0U; i < MAX_NUM_SENSORS; i++) {
        if (nullptr != m_upChannels[i]) {
            m_upChannels[i]->Start();
        }
    }
    LOG_DBG("Exit: CMaster::StartStream()\n");

    return NVSIPL_STATUS_OK;
}

void CMaster::StopStream(void)
{
    LOG_DBG("Enter: CMaster::StopStream()\n");

    if (m_upDisplaychannel) {
        m_upDisplaychannel->Stop();
    }

    for (auto i = 0U; i < MAX_NUM_SENSORS; i++) {
        if (nullptr != m_upChannels[i]) {
            m_upChannels[i]->Stop();
        }
    }
    LOG_DBG("Exit: CMaster::StopStream()\n");
}

void CMaster::MonitorThreadFunc()
{
    uint64_t uFrameCountDelta = 0u;
    uint64_t uTimeElapsedSum = 0u;
    uint32_t uRunDurationSec = m_pAppConfig->GetRunDurationSec();

    LOG_DBG("Enter: MonitorThreadFunc()\n");
    // Wait for quit
    while (!m_bMonitorThreadQuit) {
        // Wait for SECONDS_PER_ITERATION
        auto oStartTime = chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::seconds(SECONDS_PER_ITERATION));
        auto uTimeElapsedMs = chrono::duration<double, std::milli>(chrono::steady_clock::now() - oStartTime).count();
        uTimeElapsedSum += uTimeElapsedMs;
        cout << "Output" << endl;

        for (auto &prof : m_vupProfilers) {
            prof->m_profData.profDataMut.lock();
            uFrameCountDelta = prof->m_profData.uFrameCount - prof->m_profData.uPrevFrameCount;
            prof->m_profData.uPrevFrameCount = prof->m_profData.uFrameCount;
            prof->m_profData.profDataMut.unlock();
            auto fps = uFrameCountDelta / (uTimeElapsedMs / 1000.0);
            string profName =
                "Sensor" + to_string(prof->m_uSensor) + "_Out" + to_string(int(prof->m_outputType)) + "\t";
            cout << profName << "Frame rate (fps):\t\t" << fps << endl;
        }
        cout << endl;

        if (m_producerResident) {
            // Check for any asynchronous fatal errors reported by pipeline threads in the library
            for (auto &notificationHandler : m_upSiplCamera->m_vupNotificationHandler) {
                if (notificationHandler->IsPipelineInError()) {
                    LOG_ERR("Pipeline failure\n");
                    Quit(NVSIPL_STATUS_ERROR);
                }
            }

            // Check for any asynchronous errors reported by the device blocks
            for (auto &notificationHandler : m_upSiplCamera->m_vupDeviceBlockNotifyHandler) {
                if (notificationHandler->IsDeviceBlockInError()) {
                    LOG_ERR("Device Block failure\n");
                    Quit(NVSIPL_STATUS_ERROR);
                }
            }
            if ((uRunDurationSec != 0) && (uTimeElapsedSum / 1000 >= uRunDurationSec))
                Quit();
        }
    }
    LOG_DBG("Exit: MonitorThreadFunc()\n");
}

SIPLStatus CMaster::OnFrameAvailable(uint32_t uSensor, NvSIPLBuffers &siplBuffers)
{
    if (uSensor >= MAX_NUM_SENSORS) {
        LOG_ERR("%s: Invalid sensor id: %u\n", __func__, uSensor);
        return NVSIPL_STATUS_BAD_ARGUMENT;
    }

    if (m_pAppConfig->GetCommType() == CommType_IntraProcess) {
        CSingleProcessChannel *pSingleProcessChannel =
            dynamic_cast<CSingleProcessChannel *>(m_upChannels[uSensor].get());
        return pSingleProcessChannel->Post(siplBuffers);
    } else if (m_pAppConfig->GetEntityType() == EntityType_Producer) {
        CIpcProducerChannel *pIpcProducerChannel = dynamic_cast<CIpcProducerChannel *>(m_upChannels[uSensor].get());
        return pIpcProducerChannel->Post(siplBuffers);
    } else {
        LOG_WARN("Received unexpected OnFrameAvailable, commType: %u, EntityType: %u\n", m_pAppConfig->GetCommType(),
                 m_pAppConfig->GetEntityType());
        return NVSIPL_STATUS_ERROR;
    }
}

std::unique_ptr<CChannel> CMaster::CreateChannel(SensorInfo *pSensorInfo,
                                                 CProfiler *pProfiler,
                                                 std::unique_ptr<CDisplayChannel> &upDisplayChannel)
{
    switch (m_pAppConfig->GetCommType()) {
        default:
        case CommType_IntraProcess:
            return make_unique<CSingleProcessChannel>(
                m_sciBufModule, m_sciSyncModule, pSensorInfo, m_pAppConfig, m_upSiplCamera->m_upCamera.get(),
                nullptr == upDisplayChannel ? nullptr : upDisplayChannel->GetDisplayProducer(), m_spWFDController);
        case CommType_InterProcess:
            if (m_pAppConfig->GetEntityType() == EntityType_Producer) {
                return make_unique<CP2pProducerChannel>(m_sciBufModule, m_sciSyncModule, pSensorInfo, m_pAppConfig,
                                                        m_upSiplCamera->m_upCamera.get());
            } else {
                return make_unique<CP2pConsumerChannel>(m_sciBufModule, m_sciSyncModule, pSensorInfo, m_pAppConfig);
            }
        case CommType_InterChip:
            if (m_pAppConfig->GetEntityType() == EntityType_Producer) {
                return make_unique<CC2cProducerChannel>(m_sciBufModule, m_sciSyncModule, pSensorInfo, m_pAppConfig,
                                                        m_upSiplCamera->m_upCamera.get());
            } else {
                return make_unique<CC2cConsumerChannel>(m_sciBufModule, m_sciSyncModule, pSensorInfo, m_pAppConfig);
            }
    }
}

std::unique_ptr<CDisplayChannel> CMaster::CreateDisplayChannel(SensorInfo *pSensorInfo, CProfiler *pProfiler)
{
    return std::unique_ptr<CDisplayChannel>(
        new CDisplayChannel(m_sciBufModule, m_sciSyncModule, pSensorInfo, m_pAppConfig, m_spWFDController));
}

SIPLStatus CMaster::InitStitchingToDisplay()
{
    auto status = m_upDisplaychannel->Connect();
    CHK_STATUS_AND_RETURN(status, "CMaster: Display channel connect.");

    status = m_upDisplaychannel->InitBlocks();
    CHK_STATUS_AND_RETURN(status, "Display channel InitBlocks");

    status = m_upDisplaychannel->Reconcile();
    CHK_STATUS_AND_RETURN(status, "Display channel Reconcile");

    return NVSIPL_STATUS_OK;
}

SIPLStatus CMaster::AttachConsumer()
{
    if ((m_pAppConfig->GetCommType() != CommType_InterProcess) && (m_pAppConfig->GetCommType() != CommType_InterChip)) {
        LOG_WARN("Only IPC(P2P or C2C) Producer support late attach by now\n");
        return NVSIPL_STATUS_ERROR;
    }

    if ((m_pAppConfig->GetEntityType() != EntityType_Producer)) {
        LOG_WARN("Only Producer support late attach by now\n");
        return NVSIPL_STATUS_ERROR;
    }

    for (auto i = 0U; i < MAX_NUM_SENSORS; i++) {
        if (nullptr != m_upChannels[i]) {
            CIpcProducerChannel *pIpcProducerChannel = dynamic_cast<CIpcProducerChannel *>(m_upChannels[i].get());
            pIpcProducerChannel->AttachLateConsumers();
        }
    }
    return NVSIPL_STATUS_OK;
}

SIPLStatus CMaster::DetachConsumer()
{
    if ((m_pAppConfig->GetCommType() != CommType_InterProcess) && (m_pAppConfig->GetCommType() != CommType_InterChip)) {
        LOG_WARN("Only IPC(P2P or C2C) Producer support late attach by now\n");
        return NVSIPL_STATUS_ERROR;
    }

    if ((m_pAppConfig->GetEntityType() != EntityType_Producer)) {
        LOG_WARN("Only Producer support late attach by now\n");
        return NVSIPL_STATUS_ERROR;
    }

    for (auto i = 0U; i < MAX_NUM_SENSORS; i++) {
        if (nullptr != m_upChannels[i]) {
            CIpcProducerChannel *pIpcProducerChannel = dynamic_cast<CIpcProducerChannel *>(m_upChannels[i].get());
            pIpcProducerChannel->DetachLateConsumers();
        }
    }
    return NVSIPL_STATUS_OK;
}

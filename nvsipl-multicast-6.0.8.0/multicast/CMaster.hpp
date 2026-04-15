/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/* STL Headers */
#include <cstring>
#include <iostream>
#include <unistd.h>

#include "CAppConfig.hpp"
#include "CChannel.hpp"
#include "CDisplayChannel.hpp"

#include "COpenWFDController.hpp"
#include "CSingleProcessChannel.hpp"
#include "CUtils.hpp"
#include "NvSIPLCamera.hpp"
#include "NvSIPLPipelineMgr.hpp"
#include "CSiplCamera.hpp"

#include "nvscibuf.h"
#include "nvscistream.h"
#include "nvscisync.h"

#ifndef CMASTER_HPP
#define CMASTER_HPP

using namespace std;

enum PMStatus
{
    PM_SUSPEND_PREPARE = 0,
    PM_POST_SUSPEND,
    PM_RESUME_PREPARE,
    PM_POST_RESUME
};

void GraceQuit(SIPLStatus status);

/** CMaster class */
class CMaster : public CSiplCamera::ICallback
{
  public:
    std::atomic<bool> bIgnoreError;
    SIPLStatus OnFrameAvailable(uint32_t uSensor, NvSIPLBuffers &siplBuffers);
    void Quit(SIPLStatus status = NVSIPL_STATUS_OK);
    SIPLStatus Resume();
    SIPLStatus Suspend();
    SIPLStatus PreInit(CAppConfig *pAppConfig);
    SIPLStatus Init();
    SIPLStatus Start();
    SIPLStatus Stop();
    SIPLStatus DeInit();
    SIPLStatus PostDeInit();
    void SetLogLevel(uint32_t verbosity);
    bool IsProducerResident();
    SIPLStatus AttachConsumer();
    SIPLStatus DetachConsumer();

  private:
    void MonitorThreadFunc();
    SIPLStatus StartStream();
    void StopStream();
    SIPLStatus StopPipeline();
    void DeinitPipeline();
    SIPLStatus InitStream();
    SIPLStatus DeInitStream();
    std::unique_ptr<CChannel>
    CreateChannel(SensorInfo *pSensorInfo, CProfiler *pProfiler, std::unique_ptr<CDisplayChannel> &upDisplayChannel);
    std::unique_ptr<CDisplayChannel> CreateDisplayChannel(SensorInfo *pSensorInfo, CProfiler *pProfiler);
    SIPLStatus InitStitchingToDisplay();

  private:
    CAppConfig *m_pAppConfig{ nullptr };
    NvSciSyncModule m_sciSyncModule{ nullptr };
    NvSciBufModule m_sciBufModule{ nullptr };
    unique_ptr<CChannel> m_upChannels[MAX_NUM_SENSORS]{ nullptr };
    std::unique_ptr<CDisplayChannel> m_upDisplaychannel{ nullptr };
    std::unique_ptr<CSiplCamera> m_upSiplCamera{ nullptr };
    std::shared_ptr<COpenWFDController> m_spWFDController{ nullptr };
    std::atomic<bool> m_bMonitorThreadQuit{ false };
    PMStatus m_PMStatus = PM_POST_SUSPEND;
    vector<unique_ptr<CProfiler>> m_vupProfilers;
    bool m_producerResident;
    std::unique_ptr<std::thread> m_upMonitorThread = nullptr;
};

#endif //CMASTER_HPP

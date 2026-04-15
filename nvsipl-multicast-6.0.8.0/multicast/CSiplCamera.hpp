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
#include <vector>
#include <thread>

/* NvSIPL Headers */
#include "NvSIPLVersion.hpp" // Version
#include "NvSIPLTrace.hpp"   // Trace
#if !NV_IS_SAFETY
#include "NvSIPLQuery.hpp"      // Query
#include "NvSIPLQueryTrace.hpp" // Query Trace
#endif
#include "NvSIPLCommon.hpp"      // Common
#include "NvSIPLCamera.hpp"      // Camera
#include "NvSIPLPipelineMgr.hpp" // Pipeline manager
#include "NvSIPLClient.hpp"      // Client
#include "CUtils.hpp"
#include "CAppConfig.hpp"

#ifndef CSIPLCAMERA_HPP
#define CSIPLCAMERA_HPP

#define EVENT_QUEUE_TIMEOUT_US (1000000U)
constexpr unsigned long IMAGE_QUEUE_TIMEOUT_US = 1000000U;

using namespace std;
using namespace nvsipl;

class CDeviceBlockNotificationHandler;
class CPipelineNotificationHandler;
class CPipelineFrameQueueHandler;

/** CMaster class */
class CSiplCamera
{
  public:
    class ICallback
    {
      public:
        virtual SIPLStatus OnFrameAvailable(uint32_t uSensor, NvSIPLBuffers &siplBuffers) = 0;

      protected:
        ICallback() = default;
        virtual ~ICallback() = default;
    };

    SIPLStatus Setup(CAppConfig *pAppConfig);
    SIPLStatus Init(ICallback *callback);
    SIPLStatus DeInit();
    SIPLStatus Start();
    SIPLStatus Stop();
    SIPLStatus RegisterAutoControlPlugin();

  private:
    static SIPLStatus GetDTPropAsString(const void *node, const char *const name, char val[], const uint32_t size);
    static SIPLStatus UpdatePlatformCfgPerBoardModel(PlatformCfg *platformCfg);
    static SIPLStatus CheckSKU(const std::string &findStr, bool &bFound);
    static bool IsProducerResident(CAppConfig *pAppConfig);
    SIPLStatus GetPipelineCfg(uint32_t sensorId, NvSIPLPipelineConfiguration &pipeCfg);
    SIPLStatus GetOutputTypeList(NvSIPLPipelineConfiguration &pipeCfg,
                                std::vector<INvSIPLClient::ConsumerDesc::OutputType> &outputList);

  public:
    std::vector<CameraModuleInfo> m_vCameraModules;
    unique_ptr<INvSIPLCamera> m_upCamera{ nullptr };
    vector<std::unique_ptr<CPipelineFrameQueueHandler>> m_vupFrameCompletionQueueHandler;
    vector<std::unique_ptr<CPipelineNotificationHandler>> m_vupNotificationHandler;
    vector<std::unique_ptr<CDeviceBlockNotificationHandler>> m_vupDeviceBlockNotifyHandler;

  private:
    CAppConfig *m_pAppConfig = nullptr;
    PlatformCfg *m_pPlatformCfg = nullptr;
};

class CDeviceBlockNotificationHandler : public NvSIPLPipelineNotifier
{
  public:
    uint32_t m_uDevBlkIndex = -1U;

    SIPLStatus Init(uint32_t uDevBlkIndex,
                    const DeviceBlockInfo *pDeviceBlockInfo,
                    INvSIPLNotificationQueue *notificationQueue,
                    INvSIPLCamera *camera,
                    bool bIgnoreError)
    {
        if (notificationQueue == nullptr) {
            LOG_ERR("Invalid Notification Queue\n");
            return NVSIPL_STATUS_BAD_ARGUMENT;
        }
        m_uDevBlkIndex = uDevBlkIndex;
        m_pDeviceBlockInfo = pDeviceBlockInfo;
        m_pNotificationQueue = notificationQueue;
        m_pCamera = camera;
        m_bIgnoreError = bIgnoreError;

        SIPLStatus status = m_pCamera->GetMaxErrorSize(m_uDevBlkIndex, m_uErrorSize);
        if (status != NVSIPL_STATUS_OK) {
            LOG_ERR("DeviceBlock: %u, GetMaxErrorSize failed\n", m_uDevBlkIndex);
            return status;
        }

        if (m_uErrorSize != 0U) {
            m_oDeserializerErrorInfo.upErrorBuffer.reset(new uint8_t[m_uErrorSize]);
            m_oDeserializerErrorInfo.bufferSize = m_uErrorSize;

            m_oSerializerErrorInfo.upErrorBuffer.reset(new uint8_t[m_uErrorSize]);
            m_oSerializerErrorInfo.bufferSize = m_uErrorSize;

            m_oSensorErrorInfo.upErrorBuffer.reset(new uint8_t[m_uErrorSize]);
            m_oSensorErrorInfo.bufferSize = m_uErrorSize;
        }

        m_upThread.reset(new std::thread(EventQueueThreadFunc, this));
        return NVSIPL_STATUS_OK;
    }

    void Deinit()
    {
        m_bQuit = true;
        if (m_upThread != nullptr) {
            m_upThread->join();
            m_upThread.reset();
        }
    }

    bool IsDeviceBlockInError()
    {
        return m_bInError;
    }

    virtual ~CDeviceBlockNotificationHandler()
    {
        Deinit();
    }

  private:
    void HandleDeserializerError()
    {
        bool isRemoteError{ false };
        uint8_t linkErrorMask{ 0U };

        /* Get detailed error information (if error size is non-zero) and
         * information about remote error and link error. */
        SIPLStatus status = m_pCamera->GetDeserializerErrorInfo(
            m_uDevBlkIndex, (m_uErrorSize > 0) ? &m_oDeserializerErrorInfo : nullptr, isRemoteError, linkErrorMask);
        if (status != NVSIPL_STATUS_OK) {
            LOG_ERR("DeviceBlock: %u, GetDeserializerErrorInfo failed\n", m_uDevBlkIndex);
            m_bInError = true;
            return;
        }

        if ((m_uErrorSize > 0) && (m_oDeserializerErrorInfo.sizeWritten != 0)) {
            cout << "DeviceBlock[" << m_uDevBlkIndex << "] Deserializer Error Buffer: ";
            for (uint32_t i = 0; i < m_oDeserializerErrorInfo.sizeWritten; i++) {
                cout << m_oDeserializerErrorInfo.upErrorBuffer[i] << " ";
            }
            cout << "\n";

            m_bInError = true;
        }

        if (isRemoteError) {
            cout << "DeviceBlock[" << m_uDevBlkIndex << "] Deserializer Remote Error.\n";
            for (uint32_t i = 0; i < m_pDeviceBlockInfo->numCameraModules; i++) {
                HandleCameraModuleError(m_pDeviceBlockInfo->cameraModuleInfoList[i].sensorInfo.id);
            }
        }

        if (linkErrorMask != 0U) {
            LOG_ERR("DeviceBlock: %u, Deserializer link error. mask: %u\n", m_uDevBlkIndex, linkErrorMask);
            m_bInError = true;
        }
    }

    void HandleCameraModuleError(uint32_t index)
    {
        if (m_uErrorSize > 0) {
            /* Get detailed error information. */
            SIPLStatus status = m_pCamera->GetModuleErrorInfo(index, &m_oSerializerErrorInfo, &m_oSensorErrorInfo);
            if (status != NVSIPL_STATUS_OK) {
                LOG_ERR("index: %u, GetModuleErrorInfo failed\n", index);
                m_bInError = true;
                return;
            }

            if (m_oSerializerErrorInfo.sizeWritten != 0) {
                cout << "Pipeline[" << index << "] Serializer Error Buffer: ";
                for (uint32_t i = 0; i < m_oSerializerErrorInfo.sizeWritten; i++) {
                    cout << m_oSerializerErrorInfo.upErrorBuffer[i] << " ";
                }
                cout << "\n";
                m_bInError = true;
            }

            if (m_oSensorErrorInfo.sizeWritten != 0) {
                cout << "Pipeline[" << index << "] Sensor Error Buffer: ";
                for (uint32_t i = 0; i < m_oSensorErrorInfo.sizeWritten; i++) {
                    cout << m_oSensorErrorInfo.upErrorBuffer[i] << " ";
                }
                cout << "\n";
                m_bInError = true;
            }
        }
    }

    bool isTrueGPIOInterrupt(const uint32_t *gpioIdxs, uint32_t numGpioIdxs)
    {
        /*
         * Get disambiguated GPIO interrupt event codes, to determine whether
         * true interrupts or propagation functionality fault occurred.
         */

        bool true_interrupt = false;

        for (uint32_t i = 0U; i < numGpioIdxs; i++) {
            SIPLGpioEvent code;
            SIPLStatus status = m_pCamera->GetErrorGPIOEventInfo(m_uDevBlkIndex, gpioIdxs[i], code);
            if (status == NVSIPL_STATUS_NOT_SUPPORTED) {
                LOG_INFO("GetErrorGPIOEventInfo is not supported by OS backend currently!\n");
                /* Allow app to fetch detailed error info, same as in case of true interrupt. */
                return true;
            } else if (status != NVSIPL_STATUS_OK) {
                LOG_ERR("DeviceBlock: %u, GetErrorGPIOEventInfo failed\n", m_uDevBlkIndex);
                m_bInError = true;
                return false;
            }

            /*
             * If no error condition code is returned, and at least one GPIO has
             * NVSIPL_GPIO_EVENT_INTR status, return true.
             */
            if (code == NVSIPL_GPIO_EVENT_INTR) {
                true_interrupt = true;
            } else if (code != NVSIPL_GPIO_EVENT_NOTHING) {
                // GPIO functionality fault (treat as fatal)
                m_bInError = true;
                return false;
            }
        }

        return true_interrupt;
    }

    //! Notifier function
    void OnEvent(NotificationData &oNotificationData)
    {
        switch (oNotificationData.eNotifType) {
            case NOTIF_ERROR_DESERIALIZER_FAILURE:
                LOG_ERR("DeviceBlock: %u, NOTIF_ERROR_DESERIALIZER_FAILURE\n", m_uDevBlkIndex);
                if (!m_bIgnoreError) {
                    if (isTrueGPIOInterrupt(oNotificationData.gpioIdxs, oNotificationData.numGpioIdxs)) {
                        HandleDeserializerError();
                    }
                }
                break;
            case NOTIF_ERROR_SERIALIZER_FAILURE:
                LOG_ERR("DeviceBlock: %u, NOTIF_ERROR_SERIALIZER_FAILURE\n", m_uDevBlkIndex);
                if (!m_bIgnoreError) {
                    for (uint32_t i = 0; i < m_pDeviceBlockInfo->numCameraModules; i++) {
                        if ((oNotificationData.uLinkMask &
                             (1 << (m_pDeviceBlockInfo->cameraModuleInfoList[i].linkIndex))) != 0) {
                            if (isTrueGPIOInterrupt(oNotificationData.gpioIdxs, oNotificationData.numGpioIdxs)) {
                                HandleCameraModuleError(m_pDeviceBlockInfo->cameraModuleInfoList[i].sensorInfo.id);
                            }
                        }
                    }
                }
                break;
            case NOTIF_ERROR_SENSOR_FAILURE:
                LOG_ERR("DeviceBlock: %u, NOTIF_ERROR_SENSOR_FAILURE\n", m_uDevBlkIndex);
                if (!m_bIgnoreError) {
                    for (uint32_t i = 0; i < m_pDeviceBlockInfo->numCameraModules; i++) {
                        if ((oNotificationData.uLinkMask &
                             (1 << (m_pDeviceBlockInfo->cameraModuleInfoList[i].linkIndex))) != 0) {
                            if (isTrueGPIOInterrupt(oNotificationData.gpioIdxs, oNotificationData.numGpioIdxs)) {
                                HandleCameraModuleError(m_pDeviceBlockInfo->cameraModuleInfoList[i].sensorInfo.id);
                            }
                        }
                    }
                }
                break;
            case NOTIF_ERROR_INTERNAL_FAILURE:
                LOG_ERR("DeviceBlock: %u, NOTIF_ERROR_INTERNAL_FAILURE\n", m_uDevBlkIndex);
                if (!m_bIgnoreError && (oNotificationData.uLinkMask != 0U)) {
                    for (uint32_t i = 0; i < m_pDeviceBlockInfo->numCameraModules; i++) {
                        if ((oNotificationData.uLinkMask &
                             (1 << (m_pDeviceBlockInfo->cameraModuleInfoList[i].linkIndex))) != 0) {
                            if (isTrueGPIOInterrupt(oNotificationData.gpioIdxs, oNotificationData.numGpioIdxs)) {
                                HandleCameraModuleError(m_pDeviceBlockInfo->cameraModuleInfoList[i].sensorInfo.id);
                            }
                        }
                    }
                } else {
                    m_bInError = true;
                }
            default:
                LOG_WARN("DeviceBlock: %u, Unknown/Invalid notification\n", m_uDevBlkIndex);
                break;
        }
        return;
    }

    static void EventQueueThreadFunc(CDeviceBlockNotificationHandler *pThis)
    {
        SIPLStatus status = NVSIPL_STATUS_OK;
        NotificationData notificationData;

        if ((pThis == nullptr) || (pThis->m_pNotificationQueue == nullptr)) {
            LOG_ERR("Invalid thread data\n");
            return;
        }

        pthread_setname_np(pthread_self(), "DevBlkEvent");

        while (!pThis->m_bQuit) {
            status = pThis->m_pNotificationQueue->Get(notificationData, EVENT_QUEUE_TIMEOUT_US);
            if (status == NVSIPL_STATUS_OK) {
                pThis->OnEvent(notificationData);
            } else if (status == NVSIPL_STATUS_TIMED_OUT) {
                LOG_DBG("Queue timeout\n");
            } else if (status == NVSIPL_STATUS_EOF) {
                LOG_DBG("Queue shutdown\n");
                pThis->m_bQuit = true;
            } else {
                LOG_ERR("Unexpected queue return status\n");
                pThis->m_bQuit = true;
            }
        }
    }

    bool m_bQuit = false;
    bool m_bInError = false;
    std::unique_ptr<std::thread> m_upThread = nullptr;
    INvSIPLNotificationQueue *m_pNotificationQueue = nullptr;
    const DeviceBlockInfo *m_pDeviceBlockInfo;
    INvSIPLCamera *m_pCamera = nullptr;
    bool m_bIgnoreError = false;

    size_t m_uErrorSize{};
    SIPLErrorDetails m_oDeserializerErrorInfo{};
    SIPLErrorDetails m_oSerializerErrorInfo{};
    SIPLErrorDetails m_oSensorErrorInfo{};
};

class CPipelineNotificationHandler : public NvSIPLPipelineNotifier
{
  public:
    uint32_t m_uSensor = -1U;

    //! Initializes the Pipeline Notification Handler
    SIPLStatus Init(uint32_t uSensor, INvSIPLNotificationQueue *notificationQueue, CAppConfig *pAppConfig)
    {
        if (notificationQueue == nullptr) {
            LOG_ERR("Invalid Notification Queue\n");
            return NVSIPL_STATUS_BAD_ARGUMENT;
        }
        m_uSensor = uSensor;
        m_pNotificationQueue = notificationQueue;
        m_pAppConfig = pAppConfig;
        m_upThread.reset(new std::thread(EventQueueThreadFunc, this));
        return NVSIPL_STATUS_OK;
    }

    void Deinit()
    {
        m_bQuit = true;
        if (m_upThread != nullptr) {
            m_upThread->join();
            m_upThread.reset();
        }
    }

    //! Returns true to pipeline encountered any fatal error.
    bool IsPipelineInError(void)
    {
        return m_bInError;
    }

    //! Get number of frame drops for a given sensor
    uint32_t GetNumFrameDrops(uint32_t uSensor)
    {
        if (uSensor >= MAX_NUM_SENSORS) {
            LOG_ERR("Invalid index used to request number of frame drops\n");
            return -1;
        }
        return m_uNumFrameDrops;
    }

    //! Reset frame drop counter
    void ResetFrameDropCounter(void)
    {
        m_uNumFrameDrops = 0U;
    }

    virtual ~CPipelineNotificationHandler()
    {
        Deinit();
    }

  private:
    //! Notifier function
    void OnEvent(NotificationData &oNotificationData)
    {
        switch (oNotificationData.eNotifType) {
            case NOTIF_INFO_ICP_PROCESSING_DONE:
                LOG_INFO("Pipeline: %u, NOTIF_INFO_ICP_PROCESSING_DONE\n", oNotificationData.uIndex);
                break;
            case NOTIF_INFO_ISP_PROCESSING_DONE:
                LOG_INFO("Pipeline: %u, NOTIF_INFO_ISP_PROCESSING_DONE\n", oNotificationData.uIndex);
                break;
            case NOTIF_INFO_ACP_PROCESSING_DONE:
                LOG_INFO("Pipeline: %u, NOTIF_INFO_ACP_PROCESSING_DONE\n", oNotificationData.uIndex);
                break;
            case NOTIF_INFO_ICP_AUTH_SUCCESS:
                LOG_DBG("Pipeline: %u, ICP_AUTH_SUCCESS frame=%lu\n", oNotificationData.uIndex,
                        oNotificationData.frameSeqNumber);
                break;
            case NOTIF_INFO_CDI_PROCESSING_DONE:
                LOG_INFO("Pipeline: %u, NOTIF_INFO_CDI_PROCESSING_DONE\n", oNotificationData.uIndex);
                break;
            case NOTIF_WARN_ICP_FRAME_DROP:
                LOG_WARN("Pipeline: %u, NOTIF_WARN_ICP_FRAME_DROP\n", oNotificationData.uIndex);
                m_uNumFrameDrops++;
                break;
            case NOTIF_WARN_ICP_FRAME_DISCONTINUITY:
                LOG_WARN("Pipeline: %u, NOTIF_WARN_ICP_FRAME_DISCONTINUITY\n", oNotificationData.uIndex);
                break;
            case NOTIF_WARN_ICP_CAPTURE_TIMEOUT:
                LOG_WARN("Pipeline: %u, NOTIF_WARN_ICP_CAPTURE_TIMEOUT\n", oNotificationData.uIndex);
                break;
            case NOTIF_ERROR_ICP_BAD_INPUT_STREAM:
                LOG_ERR("Pipeline: %u, NOTIF_ERROR_ICP_BAD_INPUT_STREAM\n", oNotificationData.uIndex);
                if (!m_pAppConfig->IsErrorIgnored()) {
                    m_bInError = true; // Treat this as fatal error only if link recovery is not enabled.
                }
                break;
            case NOTIF_ERROR_ICP_CAPTURE_FAILURE:
                LOG_ERR("Pipeline: %u, NOTIF_ERROR_ICP_CAPTURE_FAILURE\n", oNotificationData.uIndex);
                m_bInError = true;
                break;
            case NOTIF_ERROR_ICP_EMB_DATA_PARSE_FAILURE:
                LOG_ERR("Pipeline: %u, NOTIF_ERROR_ICP_EMB_DATA_PARSE_FAILURE\n", oNotificationData.uIndex);
                m_bInError = true;
                break;
            case NOTIF_ERROR_ISP_PROCESSING_FAILURE:
                LOG_ERR("Pipeline: %u, NOTIF_ERROR_ISP_PROCESSING_FAILURE\n", oNotificationData.uIndex);
                m_bInError = true;
                break;
            case NOTIF_ERROR_ACP_PROCESSING_FAILURE:
                LOG_ERR("Pipeline: %u, NOTIF_ERROR_ACP_PROCESSING_FAILURE\n", oNotificationData.uIndex);
                m_bInError = true;
                break;
            case NOTIF_ERROR_CDI_SET_SENSOR_CTRL_FAILURE:
                LOG_ERR("Pipeline: %u, NOTIF_ERROR_CDI_SET_SENSOR_CTRL_FAILURE\n", oNotificationData.uIndex);
                if (!m_pAppConfig->IsErrorIgnored()) {
                    m_bInError = true; // Treat this as fatal error only if link recovery is not enabled.
                }
                break;
            case NOTIF_ERROR_INTERNAL_FAILURE:
                LOG_ERR("Pipeline: %u, NOTIF_ERROR_INTERNAL_FAILURE\n", oNotificationData.uIndex);
                m_bInError = true;
                break;
            case NOTIF_ERROR_ICP_AUTH_FAILURE:
                LOG_ERR("Pipeline: %u, ICP_AUTH_FAILURE frame=%lu\n", oNotificationData.uIndex,
                        oNotificationData.frameSeqNumber);
                break;
            default:
                LOG_WARN("Pipeline: %u, Unknown/Invalid notification\n", oNotificationData.uIndex);
                break;
        }

        return;
    }

    static void EventQueueThreadFunc(CPipelineNotificationHandler *pThis)
    {
        SIPLStatus status = NVSIPL_STATUS_OK;
        NotificationData notificationData;

        if ((pThis == nullptr) || (pThis->m_pNotificationQueue == nullptr)) {
            LOG_ERR("Invalid thread data\n");
            return;
        }

        pthread_setname_np(pthread_self(), "PipelineEvent");

        while (!pThis->m_bQuit) {
            status = pThis->m_pNotificationQueue->Get(notificationData, EVENT_QUEUE_TIMEOUT_US);
            if (status == NVSIPL_STATUS_OK) {
                pThis->OnEvent(notificationData);
            } else if (status == NVSIPL_STATUS_TIMED_OUT) {
                LOG_DBG("Queue timeout\n");
            } else if (status == NVSIPL_STATUS_EOF) {
                LOG_DBG("Queue shutdown\n");
                pThis->m_bQuit = true;
            } else {
                LOG_ERR("Unexpected queue return status\n");
                pThis->m_bQuit = true;
            }
        }
    }

    uint32_t m_uNumFrameDrops = 0U;
    bool m_bInError = false;
    std::unique_ptr<std::thread> m_upThread = nullptr;
    INvSIPLNotificationQueue *m_pNotificationQueue = nullptr;
    bool m_bQuit = false;
    CAppConfig *m_pAppConfig = nullptr;
};

class CPipelineFrameQueueHandler
{
  public:
    //! Initializes the Pipeline Frame Queue Handler
    SIPLStatus Init(uint32_t uSensor,
                    std::vector<std::pair<INvSIPLClient::ConsumerDesc::OutputType, INvSIPLFrameCompletionQueue *>>
                        pFrameCompletionQueue,
                    CSiplCamera::ICallback *callback)
    {
        m_uSensor = uSensor;
        m_callback = callback;
        m_pFrameCompletionQueue = pFrameCompletionQueue;
        LOG_DBG("FrameQueueHandler, callback: %p, m_pFrameCompletionQueue: %d\n", callback,
                m_pFrameCompletionQueue.size());
        m_upThread.reset(new std::thread(FrameCompletionQueueThreadFunc, this));
        return NVSIPL_STATUS_OK;
    }

    void Deinit()
    {
        m_bQuit = true;
        if (m_upThread != nullptr) {
            m_upThread->join();
            m_upThread.reset();
        }
    }

    static void FrameCompletionQueueThreadFunc(CPipelineFrameQueueHandler *pThis)
    {
        SIPLStatus status = NVSIPL_STATUS_OK;

        pthread_setname_np(pthread_self(), "FrameQueue");

        if (pThis->m_pFrameCompletionQueue.empty()) {
            return;
        }

        NvSIPLBuffers pBuffers;
        INvSIPLClient::INvSIPLBuffer *pbuf = nullptr;
        pBuffers.resize(pThis->m_pFrameCompletionQueue.size());

        while (!pThis->m_bQuit) {
            for (uint32_t index = 0U; index < pThis->m_pFrameCompletionQueue.size(); ++index) {
                status = pThis->m_pFrameCompletionQueue[index].second->Get(pbuf, IMAGE_QUEUE_TIMEOUT_US);

                if (status == NVSIPL_STATUS_OK) {
                    pBuffers[index].first = pThis->m_pFrameCompletionQueue[index].first;
                    pBuffers[index].second = pbuf;
                } else {
                    break;
                }
            }

            if (NVSIPL_STATUS_OK == status) {
                status = pThis->m_callback->OnFrameAvailable(pThis->m_uSensor, pBuffers);
                if (status != NVSIPL_STATUS_OK) {
                    LOG_ERR("OnFrameAvailable failed. (status:%u)\n", status);
                    pThis->m_bQuit = true;
                }
            }

            for (uint32_t i = 0; i < pBuffers.size(); ++i) {
                if (pBuffers[i].second != nullptr) {
                    pBuffers[i].second->Release();
                    pBuffers[i].second = nullptr;
                }
            }

            if (NVSIPL_STATUS_OK == status) {
                LOG_DBG("CPipelineFrameQueueHandler Queue goes to next frame\n");
            } else if (NVSIPL_STATUS_TIMED_OUT == status) {
                LOG_DBG("CPipelineFrameQueueHandler Queue timeout\n");
            } else if (NVSIPL_STATUS_EOF == status) {
                LOG_DBG("CPipelineFrameQueueHandler Queue shutdown\n");
                pThis->m_bQuit = true;
                return;
            } else {
                LOG_ERR("Unexpected queue return status: %u\n", status);
                pThis->m_bQuit = true;
                return;
            }
        }
    }

    virtual ~CPipelineFrameQueueHandler()
    {
        Deinit();
    }

    uint32_t m_uSensor = -1U;
    std::unique_ptr<std::thread> m_upThread = nullptr;
    std::vector<std::pair<INvSIPLClient::ConsumerDesc::OutputType, INvSIPLFrameCompletionQueue *>>
        m_pFrameCompletionQueue;
    bool m_bQuit = false;
    CSiplCamera::ICallback *m_callback = nullptr;
};

#endif //CSiplCamera_HPP

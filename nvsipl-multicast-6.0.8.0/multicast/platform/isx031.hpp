/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef ISX031_HPP
#define ISX031_HPP

static PlatformCfg platformCfgIsx031 = {
    .platform = "ISX031_YUYV_CPHY_x4",
    .platformConfig = "ISX031_YUYV_CPHY_x4",
    .description = "ISX031 YUYV module in 4 lane CPHY mode",
    .numDeviceBlocks = 1U,
    .deviceBlockList = { { .csiPort = NVSIPL_CAP_CSI_INTERFACE_TYPE_CSI_AB,
                           .phyMode = NVSIPL_CAP_CSI_CPHY_MODE,
                           .i2cDevice = 0U,
                           .deserInfo = { .name = "MAX96712",
#if !NV_IS_SAFETY
                                          .description = "Maxim 96712 Aggregator",
#endif // !NV_IS_SAFETY
                                          .i2cAddress = 0x29,
                                          .errGpios = {},
                                          .useCDIv2API = true },
                           .numCameraModules = 2U,
                           .cameraModuleInfoList = { { .name = "ISX031_YUYV",
#if !NV_IS_SAFETY
                                                       .description = "Sony ISX031 YUYV module - 120-deg FOV, "
                                                                      "MIPI-ISX031, MAX9295",
#endif // !NV_IS_SAFETY
                                                       .linkIndex = 2U,
                                                       .isSimulatorModeEnabled = false,
                                                       .serInfo = { .name = "MAX9295",
#if !NV_IS_SAFETY
                                                                    .description = "Maxim 9295 Serializer",
#endif // !NV_IS_SAFETY
                                                                    .i2cAddress = 0x40,
#if !NV_IS_SAFETY
                                                                    .longCable = false,
#endif // !NV_IS_SAFETY
                                                                    .errGpios = {},
                                                                    .useCDIv2API = true,
                                                                    .serdesGPIOPinMappings = {} },
                                                       .sensorInfo = { .id = 0U,
                                                                       .name = "ISX031",
#if !NV_IS_SAFETY
                                                                       .description = "Sony ISX031 Sensor",
#endif // !NV_IS_SAFETY
                                                                       .i2cAddress = 0x1A,
                                                                       .vcInfo = { .cfa = NVSIPL_PIXEL_ORDER_YUYV,
                                                                                   .embeddedTopLines = 1U,
                                                                                   .embeddedBottomLines = 0U,
                                                                                   .inputFormat =
                                                                                       NVSIPL_CAP_INPUT_FORMAT_TYPE_YUV422,
                                                                                   .resolution = { .width = 1920U,
                                                                                                   .height = 1536U },
                                                                                   .fps = 30.0,
                                                                                   .isEmbeddedDataTypeEnabled = false },
                                                                       .isTriggerModeEnabled = true,
                                                                       .errGpios = {},
                                                                       .useCDIv2API = true } },
                                                     { .name = "ISX031_YUYV",
#if !NV_IS_SAFETY
                                                       .description = "Sony ISX031 YUYV module - 120-deg FOV, "
                                                                      "MIPI-ISX031, MAX9295",
#endif // !NV_IS_SAFETY
                                                       .linkIndex = 3U,
                                                       .isSimulatorModeEnabled = false,
                                                       .serInfo = { .name = "MAX9295",
#if !NV_IS_SAFETY
                                                                    .description = "Maxim 9295 Serializer",
#endif // !NV_IS_SAFETY
                                                                    .i2cAddress = 0x40,
#if !NV_IS_SAFETY
                                                                    .longCable = false,
#endif // !NV_IS_SAFETY
                                                                    .errGpios = {},
                                                                    .useCDIv2API = true,
                                                                    .serdesGPIOPinMappings = {} },
                                                       .sensorInfo = { .id = 1U,
                                                                       .name = "ISX031",
#if !NV_IS_SAFETY
                                                                       .description = "Sony ISX031 Sensor",
#endif // !NV_IS_SAFETY
                                                                       .i2cAddress = 0x1A,
                                                                       .vcInfo = { .cfa = NVSIPL_PIXEL_ORDER_YUYV,
                                                                                   .embeddedTopLines = 1U,
                                                                                   .embeddedBottomLines = 0U,
                                                                                   .inputFormat =
                                                                                       NVSIPL_CAP_INPUT_FORMAT_TYPE_YUV422,
                                                                                   .resolution = { .width = 1920U,
                                                                                                   .height = 1536U },
                                                                                   .fps = 30.0,
                                                                                   .isEmbeddedDataTypeEnabled = false },
                                                                       .isTriggerModeEnabled = true,
                                                                       .errGpios = {},
                                                                       .useCDIv2API = true } } },
                           .desI2CPort = 0U,
                           .desTxPort = UINT32_MAX,
                           .pwrPort = 0U,
                           .dphyRate = { 2500000U, 2500000U },
                           .cphyRate = { 2000000U, 2000000U },
#if !NV_IS_SAFETY
                           .isPassiveModeEnabled = false,
#endif // !NV_IS_SAFETY
                           .isGroupInitProg = true,
                           .gpios = {},
#if !NV_IS_SAFETY
                           .isPwrCtrlDisabled = false,
                           .longCables = { false, false, false, false },
#endif // !NV_IS_SAFETY
                           .resetAll = false } }
};

#endif // ISX031_HPP

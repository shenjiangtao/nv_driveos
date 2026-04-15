# Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../../../make/nvdefs.mk

TARGETS = nvsipl_multicast

CXXFLAGS := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) -I. -I../../../../../include/nvmedia_6x/
CXXFLAGS += -std=c++14 -fexceptions -frtti -fPIC
CPPFLAGS := $(NV_PLATFORM_CPPFLAGS) $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CXXFLAGS)
CPPFLAGS += -I./platform -frtti
LDFLAGS := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

ifeq ($(NV_PLATFORM_SAFETY),1)
CPPFLAGS += -DNV_IS_SAFETY=1
endif

OBJS := CPoolManager.o
OBJS += CProducer.o
OBJS += CSIPLProducer.o
OBJS += CConsumer.o
OBJS += CCudaConsumer.o
OBJS += CClientCommon.o
OBJS += CEncConsumer.o
OBJS += CUtils.o
OBJS += CDisplayConsumer.o
OBJS += CStitchingConsumer.o
OBJS += CDisplayProducer.o
OBJS += COpenWFDController.o
OBJS += CCmdLineParser.o
OBJS += CFactory.o
OBJS += CAppConfig.o
OBJS += CPeerValidator.o
OBJS += CLateConsumerHelper.o
OBJS += CMaster.o
OBJS += CSiplCamera.o
OBJS += main.o

LDLIBS += -lnvsipl
ifeq ($(NV_PLATFORM_SAFETY),0)
LDLIBS += -lnvsipl_query
endif
LDLIBS += -lnvmedia_iep_sci
LDLIBS += -lnvscistream
LDLIBS += -lnvmedia2d
LDLIBS += -lnvscibuf
LDLIBS += -lnvscisync
LDLIBS += -lnvscievent
LDLIBS += -lnvsciipc
LDLIBS += -lnvscicommon
LDLIBS += -lcuda
LDLIBS += -ltegrawfd
ifeq ($(NV_PLATFORM_OS),QNX)
  CPPFLAGS += -DNVMEDIA_QNX
  LDLIBS += $(NV_PLATFORM_CUDA_LIB)/libcudart_static.a
  LDLIBS += -lnvdtcommon
  LDLIBS += -lsocket
else
  LDLIBS += -L$(NV_PLATFORM_CUDA_TOOLKIT)/targets/aarch64-linux/lib/ -lcudart
  LDLIBS += -L$(NV_PLATFORM_CUDA_TOOLKIT)/targets/aarch64-linux/lib/stubs/ -lcudla
  LDLIBS += -lpthread
endif

ifneq ($(NV_PLATFORM_OS),QNX)
  CAR_DETECT_PATH := car_detect
  OBJS += $(CAR_DETECT_PATH)/CCarDetector.o
  OBJS += $(CAR_DETECT_PATH)/CCudlaContext.o
  OBJS += $(CAR_DETECT_PATH)/CCudlaTask.o
  CU_OBJS += $(CAR_DETECT_PATH)/cuda_kernels.o
  CU_SRCS += $(CAR_DETECT_PATH)/cuda_kernels.cu
  CPPFLAGS += -I$(CAR_DETECT_PATH)
  LDLIBS += -lcudla
  NVCC := $(NV_PLATFORM_CUDA_TOOLKIT)/bin/nvcc -ccbin ${CC}
  CU_INCLUDES += -I$(CAR_DETECT_PATH)
  TARGET_SIZE := 64
  NVCCFLAGS   := -m${TARGET_SIZE}
endif

.PHONY: default
default: $(TARGETS)

ifneq ($(NV_PLATFORM_OS),QNX)
$(CU_OBJS): $(CU_SRCS)
	$(NVCC) $(CU_INCLUDES) $(NVCCFLAGS) -o $@ -c $<
endif

$(TARGETS): $(OBJS) $(CU_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

ifeq ($(NV_PLATFORM_OS),Linux)
default: pm_service
pm_service: utils/pm_service.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)
clean clobber:
	rm -rf $(OBJS) $(TARGETS) utils/pm_service.o pm_service
else
clean clobber:
	rm -rf $(OBJS) $(TARGETS)
endif

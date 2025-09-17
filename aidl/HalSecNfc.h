/******************************************************************************
 *
 *  Copyright (C) 2023 Samsung Electronics
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *
 ******************************************************************************/

#ifndef __NFC_SEC_HAL__
#define __NFC_SEC_HAL__

#include <aidl/android/hardware/nfc/INfc.h>
#include <aidl/android/hardware/nfc/NfcConfig.h>
#include <aidl/android/hardware/nfc/NfcEvent.h>
#include <aidl/android/hardware/nfc/NfcStatus.h>
#include <aidl/android/hardware/nfc/PresenceCheckAlgorithm.h>
#include <aidl/android/hardware/nfc/ProtocolDiscoveryConfig.h>

#include "hardware_nfc.h"

using NfcConfig = aidl::android::hardware::nfc::NfcConfig;
using NfcEvent = aidl::android::hardware::nfc::NfcEvent;
using NfcStatus = aidl::android::hardware::nfc::NfcStatus;
using PresenceCheckAlgorithm =
    aidl::android::hardware::nfc::PresenceCheckAlgorithm;
using ProtocolDiscoveryConfig =
    aidl::android::hardware::nfc::ProtocolDiscoveryConfig;


int nfc_hal_init(void);

void nfc_hal_deinit(void);

int nfc_hal_open(nfc_stack_callback_t* p_cback,
                   nfc_stack_data_callback_t* p_data_cback);

int nfc_hal_write(uint16_t data_len, const uint8_t* p_data);

int nfc_hal_core_initialized();

int nfc_hal_pre_discover();

int nfc_hal_close();

int nfc_hal_control_granted();

int nfc_hal_power_cycle();

int nfc_hal_factory_reset();

int nfc_hal_closeForPowerOffCase();

void nfc_hal_setLogging(bool enable);     // for setEnableVerboseLogging(in boolean enable)

bool nfc_hal_isLoggingEnabled();      // for isVerboseLoggingEnabled

int nfc_hal_core_initialized_for_aidl();

void nfc_hal_enableAidl(bool enable);

#endif  // __NFC_SEC_HAL__

/*
 *    Copyright (C) 2013 SAMSUNG S.LSI
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at:
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */
#include <cutils/properties.h>
#include <errno.h>
#include <string.h>

#include "device.h"
#include "hal.h"
#include "osi.h"
#include "util.h"

#include "config.h"

using namespace android::hardware::nfc::V1_1;
using android::hardware::nfc::V1_1::NfcEvent;
tNFC_HAL_CB nfc_hal_info;

bool sending_nci_packet = false;
static bool aidl_hal_enabled = false;

#ifdef FIXED_CONNECTIVITY_PIPE_WORKAROUND

extern int nfc_hal_nci_version;

#define HCI_HOST_TERMINAL 0x01
#define HCI_HOST_UICC 0x02
#define HCI_HOST_ESE 0x03
#define HCI_HOST_UICC2 0x04

static uint8_t g_nfcc_uicc_connectivity_pipe_id = 0x00;
static uint8_t g_nfcc_ese_connectivity_pipe_id = 0x00;
static uint8_t g_stack_uicc_connectivity_pipe_id = 0x00;
static uint8_t g_stack_ese_connectivity_pipe_id = 0x00;

void load_persist_connectivity_pipe_ids()
{
    char pipe_id_str[PROPERTY_VALUE_MAX]={0};
    unsigned int pipe_id;
    unsigned long num = 0;

    if (GetNumValue(NAME_OFF_HOST_ESE_PIPE_ID, &num, sizeof(num))) {
        g_stack_ese_connectivity_pipe_id = num;
    }
    if (GetNumValue(NAME_OFF_HOST_SIM_PIPE_ID, &num, sizeof(num))) {
        g_stack_uicc_connectivity_pipe_id = num;
    }
    ALOGD("stack uicc connectivity pipe id: 0x%02x",g_stack_uicc_connectivity_pipe_id);
    ALOGD("stack ese connectivity pipe id: 0x%02x",g_stack_ese_connectivity_pipe_id);

    property_get("persist.nfc.nfcc.uicc.transaction.pipe.id", pipe_id_str, "");
    if(strlen(pipe_id_str) >0 ){
        sscanf(pipe_id_str,"%02x", &pipe_id);
        g_nfcc_uicc_connectivity_pipe_id = (uint8_t)pipe_id;
        ALOGD("persist.nfc.nfcc.uicc.transaction.pipe.id: %s:0x%02x",pipe_id_str,g_nfcc_uicc_connectivity_pipe_id);
    }
    property_get("persist.nfc.nfcc.ese.transaction.pipe.id", pipe_id_str, "");
    if(strlen(pipe_id_str) >0 ){
        sscanf(pipe_id_str,"%02x", &pipe_id);
        g_nfcc_ese_connectivity_pipe_id = (uint8_t)pipe_id;
        ALOGD("persist.nfc.nfcc.ese.transaction.pipe.id: %s:0x%02x",pipe_id_str,g_nfcc_ese_connectivity_pipe_id);
    }
}

void update_connectivity_pipe_id(uint8_t dir, tNFC_NCI_PKT* pkt)
{
    //make sure packet is data for hci static connection on nci 2.0 or above
    if(nfc_hal_nci_version < NCI_VER_2_0 ||
        NCI_MT(pkt) != NCI_MT_DATA ||
        NCI_GID(pkt) != 0x01)
    {
        return;
    }
    uint8_t pipe_id;
    uint8_t new_pipe_id = 0x00;

    if(dir == 1 && //recv from nfcc
            NCI_PBF(pkt) == 0 && //last fragment
            NCI_LEN(pkt) == 7 && //len is for 7 for ADM_NOTIFY_PIPE_CREATED
            NCI_PAYLOAD(pkt)[0] == 0x81 && //HCP header, last fragment,static pipe for administion gate
            NCI_PAYLOAD(pkt)[1] == 0x12 && //HCP message header, instruction type command, instruction ADM_NOTIFY_PIPE_CREATED
            NCI_PAYLOAD(pkt)[4] == HCI_HOST_TERMINAL && //Destination Host ID
            NCI_PAYLOAD(pkt)[5] == 0x41) //Destination Gate ID: Connectivity gate (0x41)
    {
        uint8_t source_host = NCI_PAYLOAD(pkt)[2];
        char pipe_id_str[16];
        pipe_id = NCI_PAYLOAD(pkt)[6] & 0x7F;

        sprintf(pipe_id_str, "%02x",pipe_id);
        if(source_host == HCI_HOST_UICC){
            g_nfcc_uicc_connectivity_pipe_id = pipe_id;
            property_set("persist.nfc.nfcc.uicc.transaction.pipe.id", pipe_id_str);
            OSI_logt("save connectivity pipe id 0x%02x for uicc", pipe_id);
            new_pipe_id = g_stack_uicc_connectivity_pipe_id;
        }
        else if(source_host == HCI_HOST_ESE){
            g_nfcc_ese_connectivity_pipe_id = pipe_id;
            property_set("persist.nfc.nfcc.ese.transaction.pipe.id", pipe_id_str);
            OSI_logt("save connectivity pipe id 0x%02x for ese", pipe_id);
            new_pipe_id = g_stack_ese_connectivity_pipe_id;
        }
        if(new_pipe_id != 0x00){
            NCI_PAYLOAD(pkt)[6] = (NCI_PAYLOAD(pkt)[6] & 0x80) | (new_pipe_id & 0x7F);
            OSI_logt("update connectivity pipe id from:0x%02x to:0x%02x", pipe_id, (NCI_PAYLOAD(pkt)[6] & 0x7F));
        }
        return;
    }

    pipe_id = (NCI_PAYLOAD(pkt)[0] & 0x7F);
    if(dir == 0){ //send to nfcc
        if(pipe_id == g_stack_uicc_connectivity_pipe_id){
            new_pipe_id = g_nfcc_uicc_connectivity_pipe_id & 0x7F;
        }
        else if(pipe_id == g_stack_ese_connectivity_pipe_id){
            new_pipe_id = g_nfcc_ese_connectivity_pipe_id & 0x7F;
        }
    }
    else{ //recv from nfcc
        if(pipe_id == g_nfcc_uicc_connectivity_pipe_id){
            new_pipe_id = g_stack_uicc_connectivity_pipe_id & 0x7F;
        }
        else if(pipe_id == g_nfcc_ese_connectivity_pipe_id){
            new_pipe_id = g_stack_ese_connectivity_pipe_id & 0x7F;
        }
    }
    if(new_pipe_id != 0x00){
         NCI_PAYLOAD(pkt)[0] = (NCI_PAYLOAD(pkt)[0] & 0x80) | (new_pipe_id & 0x7F);
         OSI_logt("dir: %s, update connectivity pipe id from:0x%02x to:0x%02x", (dir == 0? "send":"recv"), pipe_id, (NCI_PAYLOAD(pkt)[0] & 0x7F));
    }

}
#endif

/*************************************
 * Generic device handling.
 *************************************/
bool nfc_stack_cback(nfc_event_t event, nfc_status_t event_status) {
  OSI_logt("!");
  if (!nfc_hal_info.stack_cback) return false;

  nfc_hal_info.stack_cback(event, event_status);
  return true;
}

bool nfc_data_callback(tNFC_NCI_PKT* pkt) {
  uint8_t* data = (uint8_t*)pkt;
  size_t len = NCI_LEN(pkt) + NCI_HDR_SIZE;

  OSI_logt("!");
  if (!nfc_hal_info.data_cback) return false;

  if (((data[0] >> 4) == 4) && (sending_nci_packet == true)) {
    OSI_logt("clear sendig_nci_packet");
    sending_nci_packet = false;
  }

#ifdef FIXED_CONNECTIVITY_PIPE_WORKAROUND
  update_connectivity_pipe_id(1, pkt);
#endif

  nfc_hal_info.data_cback(len, data);
  return true;
}

int nfc_hal_init(void) {
  char valueStr[PROPERTY_VALUE_MAX] = {0};
  bool data_trace = false;
  int trace_level = 0;
  int ret;

  OSI_set_debug_level(2);
  OSI_init();

  OSI_logt("enter; ========================================");

  sending_nci_packet = false;

  /* don't print log at user binary */
  ret = property_get("ro.build.type", valueStr, "");
  if (!strncmp("user", valueStr, PROPERTY_VALUE_MAX)) {
    property_get("ro.debug_level", valueStr, "");
    if (strncmp("0x4f4c", valueStr, PROPERTY_VALUE_MAX)) {
      trace_level = 2;
      data_trace = true;
    }
  } else {
    if (!get_config_int(cfg_name_table[CFG_TRACE_LEVEL], &trace_level))
      trace_level = 0;

    if (get_config_int(cfg_name_table[CFG_DATA_TRACE], &ret))
      if (ret > 0) data_trace = true;
  }

  OSI_set_debug_level(trace_level);

  memset(&nfc_hal_info, 0, sizeof(nfc_hal_info));
  // contenxt init
  nfc_hal_info.state = HAL_STATE_INIT;
  nfc_hal_info.stack_cback = NULL;
  nfc_hal_info.data_cback = NULL;
  nfc_hal_info.nci_last_pkt = (tNFC_NCI_PKT*)OSI_mem_get(NCI_CTRL_SIZE);
  nfc_hal_info.nci_fragment_pkt = NULL;
  nfc_hal_info.msg_task = OSI_task_allocate("hal_task", nfc_hal_task);
  nfc_hal_info.nci_timer = OSI_timer_allocate("nci_timer");
  nfc_hal_info.sleep_timer = OSI_timer_allocate("sleep_timer");
  nfc_hal_info.msg_q = OSI_queue_allocate("msg_q");
  nfc_hal_info.nci_q = OSI_queue_allocate("nci_q");

  setSleepTimeout(SET_SLEEP_TIME_CFG, 5000);

  if (!nfc_hal_info.msg_task || !nfc_hal_info.nci_timer ||
      !nfc_hal_info.sleep_timer || !nfc_hal_info.msg_q ||
      !nfc_hal_info.nci_q) {
    nfc_hal_deinit();
    return -EPERM;
  }

  if (device_init(data_trace)) {
    nfc_hal_deinit();
    return -EPERM;
  }

  OSI_logt("succeed;");
  return 0;
}

void nfc_hal_deinit(void) {
  OSI_logt("enter;");

  device_close();

  nfc_hal_info.state = HAL_STATE_DEINIT;
  OSI_task_kill(nfc_hal_info.msg_task);
  nfc_hal_info.stack_cback = NULL;
  nfc_hal_info.data_cback = NULL;
  OSI_mem_free((tOSI_MEM_HANDLER)nfc_hal_info.nci_last_pkt);
  nfc_hal_info.nci_last_pkt = NULL;
  OSI_mem_free((tOSI_MEM_HANDLER)nfc_hal_info.nci_fragment_pkt);
  nfc_hal_info.nci_fragment_pkt = NULL;
  OSI_timer_free(nfc_hal_info.nci_timer);
  OSI_timer_free(nfc_hal_info.sleep_timer);
  OSI_queue_free(nfc_hal_info.msg_q);
  OSI_queue_free(nfc_hal_info.nci_q);

  OSI_deinit();
  OSI_logt("exit;");
}

int nfc_hal_open(nfc_stack_callback_t* p_cback,
                 nfc_stack_data_callback_t* p_data_cback) {
  tNFC_HAL_MSG* msg;

  OSI_logt("enter;");

  if (nfc_hal_info.state == HAL_STATE_POSTINIT) {
    OSI_logt("SAMSUNG Hal already open");
    msg = (tNFC_HAL_MSG*)OSI_mem_get(HAL_EVT_SIZE);
    if (msg != NULL) {
      nfc_hal_info.stack_cback = p_cback;
      nfc_hal_info.data_cback = p_data_cback;
      nfc_hal_info.state = HAL_STATE_OPEN;
      msg->event = HAL_EVT_OPEN;
      OSI_queue_put(nfc_hal_info.msg_q, (void*)msg);
    }
    return 0;
  }

  nfc_hal_init();
#ifdef FIXED_CONNECTIVITY_PIPE_WORKAROUND
  load_persist_connectivity_pipe_ids();
#endif

  if (device_open()) return -EPERM;

  if (OSI_OK != OSI_task_run(nfc_hal_info.msg_task)) {
    nfc_hal_deinit();
    return -EPERM;
  }

  nfc_hal_info.stack_cback = p_cback;
  nfc_hal_info.data_cback = p_data_cback;
  nfc_hal_info.state = HAL_STATE_OPEN;

  msg = (tNFC_HAL_MSG*)OSI_mem_get(HAL_EVT_SIZE);
  if (msg != NULL) {
    msg->event = HAL_EVT_OPEN;
    OSI_queue_put(nfc_hal_info.msg_q, (void*)msg);
  }
  OSI_logt("exit;");
  return 0;
}

int nfc_hal_close() {
  tNFC_HAL_MSG* msg;

  OSI_logt("enter;");

  if (nfc_hal_info.state == HAL_STATE_CLOSE) {
    OSI_logt("SAMSUNG HAL already closed");
    return 1;
  }

  msg = (tNFC_HAL_MSG*)OSI_mem_get(HAL_EVT_SIZE);
  if (msg != NULL) {
    msg->event = HAL_EVT_TERMINATE;
    OSI_queue_put(nfc_hal_info.msg_q, (void*)msg);
  }
  OSI_task_stop(nfc_hal_info.msg_task);

  device_sleep();
  device_close();

  nfc_hal_info.state = HAL_STATE_CLOSE;

  OSI_logd("nfc_hal_close : Send HAL_NFC_CLOSE_CPLT_EVT(ok)");
  nfc_stack_cback(HAL_NFC_CLOSE_CPLT_EVT, HAL_NFC_STATUS_OK);

  OSI_deinit();

  OSI_logt("exit;");
  return 0;
}

int nfc_hal_write(uint16_t data_len, const uint8_t* p_data) {
  tNFC_HAL_MSG* msg;
  size_t size = (size_t)data_len;

  OSI_logt("enter;");
  if ((sending_nci_packet == true) && ((p_data[0] >> 4) == 2)
      && !(nfc_hal_info.flag & HAL_FLAG_ALREADY_INIT)) {
    OSI_logt("Don't send NCI");
    return size;
  }

  msg = (tNFC_HAL_MSG*)OSI_mem_get(size + HAL_EVT_SIZE);
  if (msg != NULL) {
    msg->event = HAL_EVT_WRITE;
    memcpy((uint8_t*)&msg->nci_packet, p_data, size);

    if ((sending_nci_packet == false) && ((p_data[0] >> 4) == 2))
      sending_nci_packet = true;
  }
#ifdef FIXED_CONNECTIVITY_PIPE_WORKAROUND
  update_connectivity_pipe_id(0, &msg->nci_packet);
#endif
  // changed OIS_queue_put() sequence to meet VTS Replay
  if (OSI_queue_put(nfc_hal_info.msg_q, (void*)msg) == -1)
    sending_nci_packet = false;

  OSI_logt("exit;");
  return size;
}

int nfc_hal_core_initialized(uint8_t* p_core_init_rsp_params) {
  tNFC_HAL_MSG* msg;
  OSI_logt("enter;");

  if(p_core_init_rsp_params != nullptr) {
  size_t size = (size_t)p_core_init_rsp_params[2] + 3;

  msg = (tNFC_HAL_MSG*)OSI_mem_get(size + HAL_EVT_SIZE);
  if (msg != NULL) {
    msg->event = HAL_EVT_CORE_INIT;
    memcpy((uint8_t*)&msg->nci_packet, p_core_init_rsp_params, size);

    OSI_queue_put(nfc_hal_info.msg_q, (void*)msg);
  }
  }
  else {
    OSI_logt("p_core_init_rsp_params is null;");
  }

  OSI_logt("exit;");

  return 0;
}

int nfc_hal_pre_discover() {
  OSI_logt("enter;");
  /*
  tNFC_HAL_MSG *msg;
  msg = (tNFC_HAL_MSG *)OSI_mem_get(HAL_EVT_SIZE);
  if (msg != NULL) {
    msg->event = HAL_EVT_PRE_DISCOVER;
    OSI_queue_put(nfc_hal_info.msg_q, (void *)msg);
  }
  */
  OSI_logd("%s:aidl_hal_enabled=%d",__func__, aidl_hal_enabled);
  OSI_logt("exit;");
  if(aidl_hal_enabled){
    /*Tell AIDL HAL not to wait for HAL_NFC_PRE_DISCOVER_CPLT_EVT*/
    return NFC_STATUS_FAILED;
  }

  /*return success is required for HIDL HAL VTS */
  return 0;
}

int nfc_hal_control_granted() {
  tNFC_HAL_MSG* msg;

  OSI_logt("enter;");

  msg = (tNFC_HAL_MSG*)OSI_mem_get(HAL_EVT_SIZE);
  if (msg != NULL) {
    msg->event = HAL_EVT_CONTROL_GRANTED;
    OSI_queue_put(nfc_hal_info.msg_q, (void*)msg);
  }
  OSI_logt("exit;");
  return 0;
}

int nfc_hal_power_cycle() {
  OSI_logt("enter;");

  tNFC_HAL_MSG* msg;
  if (nfc_hal_info.state == HAL_STATE_CLOSE) {
    OSI_logt("SAMSUNG Hal already closed, ignoring power cycle");
    return NFC_STATUS_FAILED;
  }

  msg = (tNFC_HAL_MSG*)OSI_mem_get(HAL_EVT_SIZE);
  if (msg != NULL) {
    msg->event = HAL_EVT_POWER_CYCLE;
    OSI_queue_put(nfc_hal_info.msg_q, (void*)msg);
  }

  OSI_logt("exit;");
  return 0;
}

void setSleepTimeout(int option, uint32_t timeout) {
  nfc_hal_info.flag &= ~HAL_FLAG_PROP_ONE_TIMER;
  nfc_hal_info.cfg.override_timeout = 0;

  if (option == SET_SLEEP_TIME_CFG) {
    if (!get_config_int(cfg_name_table[CFG_SLEEP_TIMEOUT],
                        (int*)&nfc_hal_info.cfg.sleep_timeout))
      nfc_hal_info.cfg.sleep_timeout = timeout;
  } else if (option == SET_SLEEP_TIME_ONCE) {
    nfc_hal_info.cfg.override_timeout = timeout;
    nfc_hal_info.flag |= HAL_FLAG_PROP_ONE_TIMER;
  } else if (option == SET_SLEEP_TIME_FORCE)
    nfc_hal_info.cfg.sleep_timeout = timeout;
  else
    ALOGE("Unknown option: %d", option);

  if (nfc_hal_info.flag & HAL_FLAG_PROP_ONE_TIMER)
    OSI_logd("Override timeout is %d ms", nfc_hal_info.cfg.override_timeout);
  OSI_logd("Sleep timeout is %d ms", nfc_hal_info.cfg.sleep_timeout);
}

#ifdef INFC_1_1
int nfc_hal_factory_reset(void) {
  OSI_logt("enter;");
  // TO DO impl
  OSI_logt("exit;");

  return 0;
}

int nfc_hal_closeForPowerOffCase(void) {
  OSI_logt("enter;");
#ifdef NFC_SEC_ESE_COLDRESET
  device_shutdown();
#endif
  //TO DO impl
  nfc_hal_close();
  OSI_logt("exit;");

  return 0;
}

void nfc_hal_getVendorConfig(android::hardware::nfc::V1_1::NfcConfig& config) {
  OSI_logt("v1_1 enter;");
  const int MAX_CONFIG_STRING_LEN = 260;
  unsigned long num = 0;
  std::array<uint8_t, MAX_CONFIG_STRING_LEN> buffer;
  buffer.fill(0);
  long retlen = 0;
  memset(&config, 0x00, sizeof(NfcConfig));
  config.nfaPollBailOutMode = false;
  if (GetNumValue(NAME_ISO_DEP_MAX_TRANSCEIVE, &num, sizeof(num))) {
    config.maxIsoDepTransceiveLength = num;
  }
  if (GetNumValue(NAME_DEFAULT_OFFHOST_ROUTE, &num, sizeof(num))) {
    config.defaultOffHostRoute = num;
  }
  if (GetNumValue(NAME_DEFAULT_NFCF_ROUTE, &num, sizeof(num))) {
    config.defaultOffHostRouteFelica = num;
  }
  if (GetNumValue(NAME_DEFAULT_SYS_CODE_ROUTE, &num, sizeof(num))) {
    config.defaultSystemCodeRoute = num;
  }
  if (GetNumValue(NAME_DEFAULT_SYS_CODE_PWR_STATE, &num, sizeof(num))) {
    config.defaultSystemCodePowerState = num;
  }
  if (GetNumValue(NAME_DEFAULT_ROUTE, &num, sizeof(num))) {
    config.defaultRoute = num;
    OSI_logt("mDefaultRoute is %d ", (int)num);
  }
  if (GetByteArrayValue(NAME_DEVICE_HOST_WHITE_LIST, (char*)buffer.data(),
                        buffer.size(), &retlen)) {
    config.hostWhitelist.resize(retlen);
    for (int i = 0; i < retlen; i++) config.hostWhitelist[i] = buffer[i];
  }
  if (GetNumValue(NAME_OFF_HOST_ESE_PIPE_ID, &num, sizeof(num))) {
    config.offHostESEPipeId = num;
  }
  if (GetNumValue(NAME_OFF_HOST_SIM_PIPE_ID, &num, sizeof(num))) {
    config.offHostSIMPipeId = num;
  }
  if (GetByteArrayValue(NAME_NFA_PROPRIETARY_CFG, (char*)buffer.data(),
                        buffer.size(), &retlen)) {
    config.nfaProprietaryCfg.protocol18092Active = (uint8_t)buffer[0];
    config.nfaProprietaryCfg.protocolBPrime = (uint8_t)buffer[1];
    config.nfaProprietaryCfg.protocolDual = (uint8_t)buffer[2];
    config.nfaProprietaryCfg.protocol15693 = (uint8_t)buffer[3];
    config.nfaProprietaryCfg.protocolKovio = (uint8_t)buffer[4];
    config.nfaProprietaryCfg.protocolMifare = (uint8_t)buffer[5];
    config.nfaProprietaryCfg.discoveryPollKovio = (uint8_t)buffer[6];
    config.nfaProprietaryCfg.discoveryPollBPrime = (uint8_t)buffer[7];
    config.nfaProprietaryCfg.discoveryListenBPrime = (uint8_t)buffer[8];
  } else {
    memset(&config.nfaProprietaryCfg, 0xFF, sizeof(ProtocolDiscoveryConfig));
  }
  if ((GetNumValue(NAME_PRESENCE_CHECK_ALGORITHM, &num, sizeof(num))) &&
      (num <= 5)) {
    config.presenceCheckAlgorithm = (PresenceCheckAlgorithm)num;
  }
  OSI_logt("exit;");
}

void nfc_hal_getVendorConfig_1_2(
    android::hardware::nfc::V1_2::NfcConfig& config) {
  OSI_logt("v1_2 enter;");
  const int MAX_CONFIG_STRING_LEN = 260;
  unsigned long num = 0;
  std::array<uint8_t, MAX_CONFIG_STRING_LEN> buffer;

  buffer.fill(0);
  long retlen = 0;

  memset(&config, 0x00, sizeof(android::hardware::nfc::V1_2::NfcConfig));

  nfc_hal_getVendorConfig(config.v1_1);

  if (GetByteArrayValue(NAME_OFFHOST_ROUTE_UICC, (char*)buffer.data(),
                        buffer.size(), &retlen)) {
    config.offHostRouteUicc.resize(retlen);
    for (int i = 0; i < retlen; i++) {
      config.offHostRouteUicc[i] = buffer[i];
    }
  }
  if (GetByteArrayValue(NAME_OFFHOST_ROUTE_ESE, (char*)buffer.data(),
                        buffer.size(), &retlen)) {
    config.offHostRouteEse.resize(retlen);
    for (int i = 0; i < retlen; i++) {
      config.offHostRouteEse[i] = buffer[i];
    }
  }
  if (GetNumValue(NAME_DEFAULT_ISODEP_ROUTE, &num, sizeof(num))) {
    config.defaultIsoDepRoute = num;
  }

  OSI_logt("exit;");
}

// AIDL INfc
// for setEnableVerboseLogging(in boolean enable)
void nfc_hal_setLogging(bool enable) {
  if(!enable)
    OSI_set_debug_level(0);
  else
    OSI_set_debug_level(2);
}

// for isVerboseLoggingEnabled
bool nfc_hal_isLoggingEnabled() {
  if(osi_debug_level == 0x00)
    return false;

  return true;
}

int nfc_hal_core_initialized_for_aidl() {
  OSI_logt("enter;");
  tNFC_HAL_MSG *msg;
  msg = (tNFC_HAL_MSG *)OSI_mem_get(HAL_EVT_SIZE);
  if (msg != NULL) {
    OSI_logt("send  HAL_EVT_CORE_INIT");
    msg->event = HAL_EVT_CORE_INIT;
    OSI_queue_put(nfc_hal_info.msg_q, (void *)msg);
    return 0;
  }
  else
    OSI_logt("nfc_hal_core_initialized_for_aidl : msg is null;");

  OSI_logt("exit;");

  return 0;
}

void nfc_hal_enableAidl(bool enable){
  OSI_logt("%s:enable=%d",__func__, enable);
  aidl_hal_enabled = enable;
}

#endif

/*
 *    Copyright (C) 2016~2024 SAMSUNG S.LSI
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
 *   Author: S.LSI NFC
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <openssl/sha.h>
#include <log/log.h>

#define POWER_DRIVER "/dev/sec-nfc"
#define TRANS_DRIVER "/dev/sec-nfc"

/* ioctl */
#define SEC_NFC_MAGIC   'S'
#define SEC_NFC_GET_MODE        _IOW(SEC_NFC_MAGIC, 0, unsigned int)
#define SEC_NFC_SET_MODE        _IOW(SEC_NFC_MAGIC, 1, unsigned int)
#define SEC_NFC_SLEEP           _IOW(SEC_NFC_MAGIC, 2, unsigned int)
#define SEC_NFC_WAKEUP          _IOW(SEC_NFC_MAGIC, 3, unsigned int)
#define SEC_NFC_COLD_RESET      _IOW(SEC_NFC_MAGIC, 5, unsigned int)


typedef enum {
    NFC_DEV_MODE_OFF = 0,
    NFC_DEV_MODE_ON,
    NFC_DEV_MODE_BOOTLOADER,
} eNFC_DEV_MODE;

void OSI_delay (uint32_t timeout);
int check_nfc(void);
int do_coldreset();
int check_nfc_hal();
void loop_console();
void loop_stress_test(int erase);

#define FEATURE_DRIVER 0
#define FEATURE_HAL_TAG 1
#define FEATURE_HAL_UICC_SWP 2
#define FEATURE_HAL_ESE_SWP 3
#define FEATURE_HAL_OPEN 4
#define FEATURE_HAL_CE_HOST 5
#define FEATURE_HAL_CE_UICC 6
#define FEATURE_HAL_CE_ESE 7
#define FEATURE_FLASH_ERASE 8
#define FEATURE_PRBS_TEST 10
#define FEATURE_SRAM_TEST 11
#define FEATURE_COLDRESET 12
#define FEATURE_HAL_FACTORY 13
#define FEATURE_HAL_VENDOR 14

#define FEATURE_CONSOLE 100
static int verbose = 1;
#define log_dbg(fmt,...)    { if (verbose) printf(fmt, ##__VA_ARGS__); ALOG(LOG_DEBUG, "sec_nfc_test", fmt, ##__VA_ARGS__);}

static int feature_to_test;
static int g_uicc_slot = -1;
static unsigned char g_clock_set = 0x12;
static unsigned char g_get_ese_cplc = 0;
static unsigned char g_stress_test = 0;
static unsigned char g_erase_flash_in_stress = 0;
static char g_fw_path[256]={0};
static char g_rfreg_path[256]={0};
static char g_swreg_path[256]={0};
static int g_update_firmware_rfreg = -1;
static int g_test_timeout = 30;
static int g_stress_total = -1;
static int g_part3_dump_as_intf_activated = 0;
static uint8_t g_forced_part3_param[7]={0};
static int g_part3_completed_count = 0;
static int g_factory_test_type = 0;
static uint8_t g_vendor_cmd[256]={0};
static uint8_t g_vendor_cmd_len = 0;

static int hdriver = 0;

static int open_driver();
static int close_driver();
static int set_nfc_mode(int mode);
static int cmd_get_bootloader();
static int wakeup_nfc();
static int cmd_fw_cfg(unsigned char cfg);
static int cmd_reset();
static int cmd_init();
static int download_fw(const char *fw_path, const char *rfreg_path, const char *swreg_path);
static int sync_read(uint8_t *rx_buff, int size, uint8_t bootmode);
static int sync_read_nci(uint8_t *rx_buff, int size);
static int sync_read_boot(uint8_t *rx_buff, int size);
static int sync_write(const uint8_t *p_data, uint16_t data_len);

typedef struct _chip_product_info{
    char *product_name;
    uint8_t version[4];
    char *fw_bin_name;
    char *hw_bin_reg_name;
    char *sw_bin_reg_name;
    int num_of_sectors;
}chip_product_info;

chip_product_info g_chip_products[]=
{
{
    "N74",
    {0x74,0x00,0x02,0x02},
    "sec_s3nrn74_firmware.bin",
    "sec_s3nrn74_hwreg.bin",
    "sec_s3nrn74_swreg.bin",
    37,
},
{
    "N81",
    {0x81,0x00,0x02,0x00},
    "sec_s3nrn81_firmware.bin",
    "sec_s3nrn81_rfreg.bin",
    NULL,
    29,
},
{
    "N82",
    {0x82,0x00,0x02,0x00},
    "sec_s3nrn82_firmware.bin",
    "sec_s3nrn82_rfreg.bin",
    NULL,
    37,
},
{
    "SN4V",
    {0x86,0x00,0x02,0x06},
    "sec_s3nsn4v_firmware.bin",
    "sec_s3nsn4v_hwreg.bin",
    "sec_s3nsn4v_swreg.bin",
    37,
},
{
    "RN4V",
    {0x85,0x00,0x02,0x06},
    "sec_s3nrn4v_firmware.bin",
    "sec_s3nrn4v_hwreg.bin",
    "sec_s3nrn4v_swreg.bin",
    37,
},
{
    "SEN6",
    {0x90,0x00,0x01,0x00},
    "sec_s3nsen6_firmware.bin",
    "sec_s3nsen6_hwreg.bin",
    "sec_s3nsen6_swreg.bin",
    60,
},
{
    "SEN6",
    {0x91,0x00,0x01,0x00},
    "sec_s3nsen6_firmware.bin",
    "sec_s3nsen6_hwreg.bin",
    "sec_s3nsen6_swreg.bin",
    60,
},
{
    "SN6V",
    {0x92,0x00,0x02,0x00},
    "sec_s3nsn6v_firmware.bin",
    "sec_s3nsn6v_hwreg.bin",
    "sec_s3nsn6v_swreg.bin",
    60,
},
{
    "SN6H",
    {0x93,0x00,0x01,0x00},
    "sec_s3nsn6h_firmware.bin",
    "sec_s3nsn6h_hwreg.bin",
    "sec_s3nsn6h_swreg.bin",
    60,
},
{
    "RN6H",
    {0x94,0x00,0x01,0x00},
    "sec_s3nrn6v_firmware.bin",
    "sec_s3nrn6h_hwreg.bin",
    "sec_s3nrn6h_swreg.bin",
    60,
},
};

const char *g_fw_bin_dirs[]={
    "/vendor/firmware",
    "/vendor/firmware/nfc",
    "/vendor/etc",
    "/vendor/etc/nfc",
    "/etc/firmware",
    "/etc/firmware/nfc",
    "/etc",
    "/etc/nfc",
    "."
};

#define FW_VERSION_LEN 3
#define RFREG_VERSION_SIZE 8
#define RFREG_SECTION_SIZE 252
#define CHIP_ID_SIZE 16
#define SECTOR_SIZE 4096
#define BASE_ADDRESS 0x2000

uint8_t g_chip_fw_ver[FW_VERSION_LEN]={0};
uint8_t g_chip_reg_ver[RFREG_VERSION_SIZE]={0};
uint8_t g_chip_swreg_ver[RFREG_VERSION_SIZE]={0};
uint8_t g_chip_id[CHIP_ID_SIZE]={0};
uint16_t g_chip_sector_size = SECTOR_SIZE;
uint16_t g_chip_base_address = BASE_ADDRESS;
uint16_t g_chip_total_sectors;
uint8_t g_chip_bl_version[4];

#define HAL_RF_TECH_A 0
#define HAL_RF_TECH_B 1
#define HAL_RF_TECH_F 2
/* 106kbps */
#define HAL_RF_BDRATE_DIVISOR_1  0
/* 212kbps */
#define HAL_RF_BDRATE_DIVISOR_2 1
/* 424kbps */
#define HAL_RF_BDRATE_DIVISOR_4 2 

int8_t g_test_tech = -1;
int8_t g_test_rate = -1;

uint8_t g_nci_version = 0x00;
uint8_t g_dual_registers = 0x00;


#define FROM_LITTLE_ARRY(data, arry, nn)              \
  do {                                                \
    int n = nn;                                       \
    data = 0;                                         \
    while (n--) data += ((*((arry) + n)) << (n * 8)); \
  } while (0)

#define TO_LITTLE_ARRY(arry, data, nn)      do { int n = nn; \
        while (n--) *((arry)+n) = (unsigned char)(((data) >> (n * 8))) & 0xFF; } while(0)

#define FW_HDR_SIZE 4
typedef enum {
    FW_MSG_CMD = 0,
    FW_MSG_RSP,
    FW_MSG_DATA
} eNFC_FW_BLTYPE;


#define HAL_EVT_SIZE 1
#define NCI_HDR_SIZE 3
#define NCI_MAX_PAYLOAD 0xFF
#define NCI_CTRL_SIZE (NCI_HDR_SIZE + NCI_MAX_PAYLOAD)

#define __osi_log(type, ...) (void)ALOG(type, "SecHAL", __VA_ARGS__)

#define TRACE_BUFFER_SIZE (NCI_CTRL_SIZE * 3 + 1)
void data_trace(const char *head, int len, const uint8_t *p_data) {
  int i = 0, header;
  char trace_buffer[TRACE_BUFFER_SIZE + 2];

  header = NCI_HDR_SIZE;
  while (len-- > 0 && i < NCI_CTRL_SIZE) {
    if (i < header)
      sprintf(trace_buffer + (i * 3), "%02x   ", p_data[i]);
    else
      sprintf(trace_buffer + (i * 3 + 2), "%02x ", p_data[i]);
    i++;
  }

  __osi_log(LOG_DEBUG, "data_trace:  %s(%3d) %s", head, i, trace_buffer);
  if (verbose) printf("data_trace:  %s(%3d) %s\n", head, i, trace_buffer);
}

void parse_clk(char *arg)
{
        if(arg != NULL){
            unsigned int tmp;
            sscanf(arg, "0x%x",&tmp);
            g_clock_set = (unsigned char)tmp;
        }
        log_dbg("g_clock_set=0x%02x\n",g_clock_set);
}

static void print_fw_version()
{
    unsigned char major_ver = (unsigned char)((g_chip_fw_ver[0]>>4)&0x0f);
    unsigned char minor_ver = (unsigned char)((g_chip_fw_ver[0])&0x0f);
    unsigned short build_info_high = (unsigned short)(g_chip_fw_ver[1]);
    unsigned short build_info_low = (unsigned short)(g_chip_fw_ver[2]);

    log_dbg("OK\nF/W version: %d.%d.%d\n",
            major_ver,
            minor_ver,
            ((build_info_high*0x100)+build_info_low));
}

int main(int argc, char* argv[])
{
    int ret = -1, i;

    feature_to_test = FEATURE_HAL_OPEN;
    char* arg;

    if (argc == 1)
    {
        fprintf(stderr, "usage: sec_nfc_test [FEATURE] [OPTIONS]\n\n"
            "FEATURES:\n"
            "  driver         test basic I2C HW\n"
            "  console        step by step driver controlling for GPIO debugging.\n"
            "  hal            test basic NFC initialization.\n"
            "  ese            test eSE SWP.\n"
            "  uicc           test UICC SWP.\n"
            "  tag            test NFC tag.\n"
            "  ceese          test CE by eSE.\n"
            "  ceuicc         test CE by UICC.\n"
            "  cehost         test CE by Host.\n"
            "  erase          erase chip flash.\n"
            "  prbs           start PRBS test.\n"
            "  sram           test SRAM.\n"
            "  coldreset      test COLDRESET.\n"
            "  factory        run facotry resonant frequency calibration.\n"
            "  vendor         send vendor command with data followed -d.\n"
            "\n"
            "OPTIONS:\n"
            "  -c <clock settings>  set clock settings:"
            "                       0x10:24Mhz PLL(19.Mhz for SN6V series);\n"
            "                       0x11:19.2Mhz PLL(24Mhz for SN6V series);\n"
            "                       0x12:26Mhz PLL;\n"
            "                       0x13:27.12Mhz crystal for RN4V or upper series;\n"
            "                       0x53:27.12Mhz crystal for N81,N82,RN74 series;\n"
            "                       0x14:38.4Mhz PLL\n"
            "  -s <slot>            set preferred UICC slot\n"
            "  -p                   get cplc when testing eSE SWP\n"
            "  -u <mode>            check firmware/refreg to update:\n"
            "                       mode 0: default, update if image version is different\n"
            "                       mode 1: update if image version is newer.\n"
            "                       mode 2: force update\n"
            "                       mode 4: update if image version is different and enable RF China options.\n"
            "  -f <firmware path>   set firmware path to update\n"
            "  -r <rfreg path>      set rfreg path to update\n"
            "  -S <swreg path>      set swreg path to update\n"
            "  -t <total count>     stress test, infinite if total count is not specified.\n"
            "  -e <mode>            if mode is not specified, erase flash in stress test.\n"
            "                       mode 2: Don't erase in SRAM test.\n"
            "  -T <type index>      prbs/discovery tech type: 0:typeA;1:TypeB; 2:typeF; -1:listen A&B; -2:poll&listen A&B&F\n"
            "  -R <bit rate index>  prbs bd Rate: 0:106kbps;1:212kbps;2:424kbps\n"
            "  -i <count>           test timeout in seconds for tag/ce/prbs test\n"
            "  -P                   Check dump part3 as interface activation for door reader.\n"
            "  -d <0400,edb90908,08>If test feature is cehost, set forced ISO14443-3 <ATQA,UID,SAK> parameters for door locker card emulation.\n"
            "  -d <2c0101>          If test feature is vendor, set vendor command data, first byte should be OID."
            "\n");
        return 0;
    }

    for (i = 1; i < argc; ++i)
    {
        if (argv[i][0] != '-')
        {
            if(strncmp(argv[i],"driver", 6) == 0){
                feature_to_test = FEATURE_DRIVER;
            }
            else if(strncmp(argv[i],"console", 7) == 0){
                feature_to_test = FEATURE_CONSOLE;
            }
            else if(strncmp(argv[i],"hal", 3) == 0){
                feature_to_test = FEATURE_HAL_OPEN;
            }
            else if(strncmp(argv[i],"ese", 3) == 0){
                feature_to_test = FEATURE_HAL_ESE_SWP;
            }
            else if(strncmp(argv[i],"uicc", 4) == 0){
                feature_to_test = FEATURE_HAL_UICC_SWP;
            }
            else if(strncmp(argv[i],"tag", 3) == 0){
                feature_to_test = FEATURE_HAL_TAG;
            }
            else if(strncmp(argv[i],"ceese", 5) == 0){
                feature_to_test = FEATURE_HAL_CE_ESE;
            }
            else if(strncmp(argv[i],"ceuicc", 6) == 0){
                feature_to_test = FEATURE_HAL_CE_UICC;
            }
            else if(strncmp(argv[i],"cehost", 6) == 0){
                feature_to_test = FEATURE_HAL_CE_HOST;
            }
            else if(strncmp(argv[i],"erase", 5) == 0){
                feature_to_test = FEATURE_FLASH_ERASE;
            }
            else if(strncmp(argv[i],"prbs", 4) == 0){
                feature_to_test = FEATURE_PRBS_TEST;
                if(g_test_tech < 0) g_test_tech =  HAL_RF_TECH_A;
                if(g_test_rate < 0) g_test_rate = HAL_RF_BDRATE_DIVISOR_1;
            }
            else if(strncmp(argv[i],"sram", 4) == 0){
                feature_to_test = FEATURE_SRAM_TEST;
            }
            else if(strncmp(argv[i],"coldreset", 9) == 0){
                feature_to_test = FEATURE_COLDRESET;
            }
            else if(strncmp(argv[i],"factory", 7) == 0){
                feature_to_test = FEATURE_HAL_FACTORY;
            }
            else if(strncmp(argv[i],"vendor", 6) == 0){
                feature_to_test = FEATURE_HAL_VENDOR;
            }
            continue;
        }

        // if there's a flag but no argument following, ignore it
        if (argc == i) continue;
        arg = argv[i+1];
        if (arg == NULL) arg = "";

        switch (argv[i][1])
        {
            case 'c': parse_clk(arg); break;
            case 's': g_uicc_slot = atoi(arg); break;
            case 'p': g_get_ese_cplc=1; --i; break;
            case 't': g_stress_test=1;
                     if(strlen(arg) >= 1 && arg[0] >= '1' && arg[0] <= '9'){
                        g_stress_total = atoi(arg);
                      }
                      else{
                        --i;
                      }
                      break;
            case 'u': g_update_firmware_rfreg=0;
                      if(strlen(arg) == 1 && arg[0] >= '0' && arg[0] <= '4'){
                        g_update_firmware_rfreg = atoi(arg);
                      }
                      else{
                        --i;
                      }
                      break;
            case 'f': strcpy(g_fw_path, arg); break;
            case 'r': strcpy(g_rfreg_path, arg); break;
            case 'e': g_erase_flash_in_stress=1;
                      if(strlen(arg) == 1 && arg[0] >= '2' && arg[0] <= '2'){
                         g_erase_flash_in_stress = atoi(arg);
                      }
                      else{
                        --i;
                      }
                      break;
            case 'T': g_test_tech = atoi(arg); break;
            case 'R': g_test_rate = atoi(arg); break;
            case 'S': strcpy(g_swreg_path, arg); break;
            case 'i': g_test_timeout = atoi(arg); break;
            case 'P':
                    if(strlen(arg) == 1 && arg[0] >= '1' && arg[0] <= '9'){
                         g_part3_dump_as_intf_activated = atoi(arg);
                    }
                    else{
                        g_part3_dump_as_intf_activated = 1;
                        --i;
                    }
                    break;
            case 'd': {
                int j;
                uint32_t value;
                if(strstr(arg,",") == 0 && arg[0] != '-' && strlen(arg) > 8){
                    int32_t slen = strlen(arg) / 2;
                    memset(g_vendor_cmd, 0, sizeof(g_vendor_cmd));
                    for(j=0;j<slen;j++){
                        sscanf(arg+j*2,"%02x", &value);
                        g_vendor_cmd[1+j] = value;
                    }
                    g_vendor_cmd_len =  1+slen;
                }
                else if(strlen(arg) == 7*2){
                    for(j=0;j<7;j++){
                        sscanf(arg+j*2,"%02x", &value);
                        g_forced_part3_param[j] = value;
                    }
                }
                else if(strlen(arg) == 7*2+2){
                    for(j=0;j<2;j++){
                        sscanf(arg+j*2,"%02x", &value);
                        g_forced_part3_param[j] = value;
                    }
                    for(j=0;j<4;j++){
                        sscanf(arg+2*2+1+j*2,"%02x", &value);
                        g_forced_part3_param[2+j] = value;
                    }
                    for(j=0;j<1;j++){
                        sscanf(arg+2*2+1+2*4+1+j*2,"%02x", &value);
                        g_forced_part3_param[2+4+j] = value;
                    }
                }
                else{
                    uint8_t tmp[7]={0x04,0x00,0xed,0xb9,0x09,0x08,0x08};
                    memcpy(g_forced_part3_param, tmp, sizeof(tmp));
                    --i;
                }

                break;

            }
        }
        ++i; // skip the argument
    }

    if(g_stress_test &&
        feature_to_test != FEATURE_FLASH_ERASE &&
        feature_to_test != FEATURE_CONSOLE)
    {
        loop_stress_test(g_erase_flash_in_stress);
    }
    else if(feature_to_test == FEATURE_DRIVER){
        ret = check_nfc();
        if(ret == 0){
            log_dbg("nfc driver I2C OK. But NFC is not working, no firmware?!\n");
        }
    }
    else if(feature_to_test == FEATURE_FLASH_ERASE){
        ret = download_fw(NULL, NULL, NULL);
    }
    else if(feature_to_test == FEATURE_CONSOLE){
        loop_console();
    }
    else{
        ret = check_nfc_hal();
    }

    log_dbg("result=%d\n",ret);

    return ret;
}

static uint16_t last_recv_data_len = 0;
static uint8_t last_rx_buff[256]={0};
static uint8_t nfcee_hci_reported = 0;
static uint8_t nfcee_hci_enabled = 0;
static uint8_t nfcee_hci_session_ok = 0;
static uint8_t nfcee_hci_pipes_cleared = 0;
static uint8_t nfcee_hci_admin_pipe_created = 0;
static uint8_t nfcee_hci_session_created = 0;
static uint8_t nfcee_hci_whitelist_set = 0;

static uint8_t nfcee_uicc_reported = 0;
static uint8_t nfcee_ese_reported = 0;
static uint8_t nfcee_enabled_ok = 0;
static uint8_t nfcee_hci_admin_all_pipe_cleared = 0;
static uint8_t nfcee_ntf_pipe_to_host_created = 0;
static uint8_t nfcee_ntf_pipe_to_host_opened = 0;
static uint8_t nfcee_ntf_pipe_to_host_pipeid = 0;

static uint8_t tag_detected = 0;
static uint8_t ce_interface_activated = 0;
static unsigned char ese_cplc_1_reported = 0;
static unsigned char ese_cplc_2_reported = 0;
static uint8_t waiting_select_disc = 0;

static uint8_t sram_test_ntf_reported = 0;
static uint8_t sram_test_result = 0;

static uint8_t cold_reset_ntf_received = 0;
static int cmd_ese_cold_reset();

pthread_mutex_t g_lock_read;

static void DebugPrintByteArray(uint16_t data_len, uint8_t* p_data)
{
    uint16_t i;
    char buf[1024]={0};
    for(i = 0; i < data_len; i++){
        sprintf(buf+3*i, "%02x ", p_data[i]);
    }
    log_dbg("%s\n",buf);
}

uint8_t is_file_exist(const char *fname)
{
    
    FILE *file;
    if ((file = fopen(fname, "r"))){
        fclose(file); 
        return 1;
    }
    return 0;
}

void get_bin_full_path(char *full_path, char * bin_name)
{
    int i, found = 0;
    for(i = 0; i < sizeof(g_fw_bin_dirs)/sizeof(const char *); i++){
        sprintf(full_path,"%s/%s",g_fw_bin_dirs[i], bin_name);
        if(is_file_exist(full_path)){
            found = 1;
            break;
        }
    }
    if(found == 0){
        log_dbg("%s was not found!\n",bin_name);
    }
}

void print_tag_info(uint16_t data_len, uint8_t* p_data)
{
    if(p_data[5] >= 0x01 && p_data[5] <= 0x04){
        log_dbg("Type %d Tag detected.\n",p_data[5]);
    }
    else if(p_data[4] == 0x01 && (p_data[5] == 0x80 || p_data[5] == 0x06)){
        log_dbg("Type 15693 Tag detected.\n");
    }
    else if(p_data[4] == 0x80 && (p_data[5] == 0x81 || p_data[5] == 0x80)){
        log_dbg("Type Mifare Classic Tag detected.\n");
    }
    else{
        log_dbg("Unknown Tag detected.\n");
    }

    switch(p_data[6]){
        case 0x00://poll A
            if(p_data[9] > 5){
                log_dbg("NFCID1:\n");
                DebugPrintByteArray(p_data[12], p_data+13);
                log_dbg("\n");
            }
            break;
        case 0x01://poll B
            if(p_data[9] > 5){
                log_dbg("NFCID0:\n");
                if(p_data[10] == 0x50){
                    DebugPrintByteArray(4, p_data+11);
                    log_dbg("\n");
                }
            }
            break;
        case 0x02://poll F
            if(p_data[9] > 10){
                log_dbg("NFCID2:\n");
                DebugPrintByteArray(8, p_data+12);
                log_dbg("\n");
            }
            break;
        default:;
    }
}

static uint8_t validate_part3_by_dump(uint16_t data_len, uint8_t* p_data)
{
    uint8_t dlen, offset, tlv_type,tlv_len;
    uint8_t recv_wupa = 0;
    uint8_t recv_anti = 0;
    dlen = p_data[2];
    offset = 4;

    if(p_data[0] == 0x6f && (p_data[1] == 0x00 || p_data[1] == 0x0d) && (p_data[3] == 0xd0 || p_data[3] == 0xd2)){
        log_dbg("validate_part3_by_dump.....\n");
        //uint8_t recv_select = 0;
        offset = 4;
        while(offset < 3 + dlen){
            tlv_type = p_data[offset]; 
            tlv_len = p_data[offset+1];
            if(p_data[3] == 0xd0){
                if(tlv_type == 0x02 && //ce_rx
                   tlv_len == 0x03 &&
                   (p_data[offset+2] & 0x80) && //Type A detected.
                   (!(p_data[offset+2] & 0x10)) && //No error
                   ((p_data[offset+2] & 0x0f) == 0x01) && // ACT1
                   (p_data[offset+4] == 0x26 || p_data[offset+4] == 0x52))
                {
                    log_dbg("validate_part3_by_dump recv_wupa.....\n");
                    recv_wupa = 1;
                }
                else if(tlv_type == 0x02 && //ce_rx
                   tlv_len == 0x04 &&
                   (p_data[offset+2] & 0x80) && //Type A detected.
                   (!(p_data[offset+2] & 0x10)) && //No error
                   ((p_data[offset+2] & 0x0f) == 0x01) && // ACT1
                   (p_data[offset+4] == 0x93 && p_data[offset+5] == 0x20))
                {
                    log_dbg("validate_part3_by_dump recv_anti.....\n");
                    if(recv_wupa == 1){
                        recv_anti = 1;
                        break;
                    }
                }
            }
            if(p_data[3] == 0xd2){
                if(tlv_type == 0x02 && //ce_rx
                   tlv_len == 0x04 &&
                   (p_data[offset+2] & 0x80) && //Type A detected.
                   (!(p_data[offset+2] & 0x10)) && //No error
                   ((p_data[offset+2] & 0x0f) == 0x01) && // ACT1
                   (p_data[offset+5] == 0x26 || p_data[offset+5] == 0x52))
                {
                    log_dbg("validate_part3_by_dump recv_wupa.....\n");
                    recv_wupa = 1;
                }
                else if(tlv_type == 0x02 && //ce_rx
                   tlv_len == 0x05 &&
                   (p_data[offset+2] & 0x80) && //Type A detected.
                   (!(p_data[offset+2] & 0x10)) && //No error
                   ((p_data[offset+2] & 0x0f) == 0x01) && // ACT1
                   (p_data[offset+5] == 0x93 && p_data[offset+6] == 0x20))
                {
                    log_dbg("validate_part3_by_dump recv_anti.....\n");
                    if(recv_wupa == 1){
                        recv_anti = 1;
                        break;
                    }
                }
            }
            offset += (2+tlv_len);
        }
    }
    else if(p_data[0] == 0x6f && p_data[1] == 0x06 && dlen > 18){
        log_dbg("validate_part3_by_dump full.....\n");
        //uint8_t recv_select = 0;
        offset = 3;
        while(offset < 3 + dlen){
            tlv_type = p_data[offset+1]; 
            tlv_len = p_data[offset+2];
            if(tlv_type == 0x01 && //ce_rx
               tlv_len == 0x06 && 
               (p_data[offset+3] == 0x26 || p_data[offset+3] == 0x52) && //REQA/WUPA
               !(p_data[offset+4]))  //No error
            {
                log_dbg("validate_part3_by_dump full recv_wupa.....\n");
                recv_wupa = 1;
            }
            else if(tlv_type == 0x01 && //ce_rx
               tlv_len == 0x07 && 
               (p_data[offset+3] == 0x93 && p_data[offset+4] == 0x20) &&
               !(p_data[offset+5])) //No error)
            {
                log_dbg("validate_part3_by_dump full recv_anti.....\n");
                if(recv_wupa == 1){
                    recv_anti = 1;
                }
            }
            offset += (3+tlv_len);
        }
    }

    if((recv_wupa && recv_anti)){
        g_part3_completed_count ++;
    }

    return (g_part3_completed_count >= g_part3_dump_as_intf_activated);
}

void hal_context_data_callback (uint16_t data_len, uint8_t* p_data)
{
    pthread_mutex_lock(&g_lock_read);
    //Only copy response for command to check later in main thread
    if(data_len > 3 && (p_data[0] >> 4) == 0x04){
        memcpy(last_rx_buff, p_data, data_len);
        last_recv_data_len = data_len;
    }

    if(feature_to_test == FEATURE_SRAM_TEST){
        if(data_len == 8 &&
            p_data[0] == 0x6f &&
            p_data[1] == 0x3f &&
            p_data[2] == 0x05)
        {
            log_dbg("SRAM test result reported.\n");
            sram_test_ntf_reported = 1;
            if(p_data[3] == 0xa0){
                sram_test_result = 1;
                log_dbg("SRAM test result ok.\n");
            }
            else{
                log_dbg("SRAM test result failed.\n");
                sram_test_result = 0;
            }
        }
    }
    else if(data_len == 12 &&
            p_data[0] == 0x60 &&
            p_data[1] == 0x00 &&
            p_data[2] == 0x09)
    {
       log_dbg("Reset notification received.\n");
       g_nci_version = p_data[5];
       if(g_nci_version == 0x20){
           memcpy(g_chip_fw_ver, p_data + 8, FW_VERSION_LEN);
           print_fw_version();
       }
    }
    else if(data_len >= 10 &&
            p_data[0] == 0x61 &&
            p_data[1] == 0x03 &&
            p_data[3] == 0x01 &&
            p_data[4] == 0x04 &&
            p_data[5] == 0x00)
    {
        log_dbg("Multiple RF discovery ID:%x protocol:%x, tech:%x\n", p_data[3] , p_data[4], p_data[5]);
        waiting_select_disc ++;
    }
    else if(data_len >= 10 &&
            p_data[0] == 0x61 &&
            p_data[1] == 0x05 &&
            ((p_data[4] == 0x02 && p_data[5] == 0x04) ||
             (p_data[4] == 0x01 && p_data[5] == 0x01) ||
             (p_data[4] == 0x01 && p_data[5] == 0x02) ||
             (p_data[4] == 0x01 && p_data[5] == 0x03) ||
             (p_data[4] == 0x01 && (p_data[5] == 0x80 || p_data[5] == 0x06)) ||
             (p_data[4] == 0x80 && (p_data[5] == 0x81 || p_data[5] == 0x80))) &&
            p_data[6] >= 0x00 &&
            p_data[6] <= 0x06)
    {
        print_tag_info(data_len, p_data);
        tag_detected = 1;
    }
    else if(data_len >= 10 &&
            p_data[0] == 0x61 &&
            p_data[1] == 0x05 &&
            p_data[4] == 0x02 &&
            p_data[5] == 0x04 &&
            p_data[6] >= 0x80 &&
            p_data[6] <= 0x86)
    {
        log_dbg("CE ISO_DEP interface activated.\n");
        ce_interface_activated = 1;
    }
    else if(((feature_to_test == FEATURE_HAL_CE_UICC && nfcee_uicc_reported) || (feature_to_test == FEATURE_HAL_CE_ESE && nfcee_ese_reported) || feature_to_test == FEATURE_HAL_CE_HOST) && g_part3_dump_as_intf_activated && 
            p_data[0] == 0x6f &&
            (((p_data[1] == 0x00 && p_data[3] == 0xd0) || (p_data[1] == 0x0d && p_data[3] == 0xd2) )|| p_data[1] == 0x06)){
            log_dbg("check Dump logs....\n");
        uint8_t is_valid_part3 = validate_part3_by_dump(data_len, p_data);
        if(is_valid_part3){
            log_dbg("Dump logs indicated part3 comunication completed, assume CE test success.\n");
            ce_interface_activated = 1;
        }
    }
    else if(nfcee_hci_reported == 0 &&
        data_len == 8 &&
        p_data[0] == 0x62 &&
        p_data[1] == 0x00 &&
        p_data[2] == 0x05 &&
        p_data[3] == 0x01)
    {
        log_dbg("Nfcee HCI reported\n");
        nfcee_hci_reported = 1;
        if(p_data[4] == 0x00){
            log_dbg("Nfcee HCI enabled\n");
            nfcee_hci_enabled = 1;
        }
    }
    else if(nfcee_hci_session_ok == 0 &&
        nfcee_hci_session_created == 0 &&
        data_len == 13 &&
        p_data[0] == 0x01 &&
        p_data[3] == 0x81 &&
        p_data[4] == 0x80)
    {
        log_dbg("Get HCI session OK!\n");
        nfcee_hci_session_ok = 1;
        nfcee_hci_admin_pipe_created = 1;
        nfcee_hci_session_created = 1;
    }
    else if(nfcee_hci_admin_pipe_created == 0xf0 &&
        data_len == 5 &&
        p_data[0] == 0x01 &&
        p_data[3] == 0x81 &&
        p_data[4] == 0x80)
    {
        log_dbg("HCI admin pipe opended\n");
        nfcee_hci_admin_pipe_created = 1;
    }
    else if(nfcee_hci_pipes_cleared == 0xf0 &&
        data_len == 5 &&
        p_data[0] == 0x01 &&
        p_data[3] == 0x81 &&
        p_data[4] == 0x80)
    {
        log_dbg("All HCI pipes cleared\n");
        nfcee_hci_pipes_cleared = 1;
    }
    else if(nfcee_hci_session_created == 0xf0 &&
        data_len == 5 &&
        p_data[0] == 0x01 &&
        p_data[3] == 0x81 &&
        p_data[4] == 0x80)
    {
        log_dbg("HCI session created\n");
        nfcee_hci_session_created = 1;
    }
    else if(nfcee_hci_whitelist_set == 0xf0 &&
        data_len == 5 &&
        p_data[0] == 0x01 &&
        p_data[3] == 0x81 &&
        p_data[4] == 0x80)
    {
        log_dbg("Whitelist set.\n");
        nfcee_hci_whitelist_set = 1;
    }
    else if(nfcee_uicc_reported == 0 &&
        (data_len == 11 || data_len == 12) &&
        p_data[0] == 0x62 &&
        p_data[1] == 0x00 &&
        (p_data[2] == 0x08 || p_data[2] == 0x09)&&
        (p_data[3] == 0x03 || p_data[3] == 0x83))
    {
        log_dbg("Nfcee UICC reported\n");
        nfcee_uicc_reported = 1;
    }
    else if(nfcee_ese_reported == 0 &&
        (data_len == 11 || data_len == 12) &&
        p_data[0] == 0x62 &&
        p_data[1] == 0x00 &&
        (p_data[2] == 0x08 || p_data[2] == 0x09)&&
        (p_data[3] == 0x02 || p_data[3] == 0x82))
    {
        log_dbg("Nfcee ESE reported\n");
        nfcee_ese_reported = 1;
    }

    else if(nfcee_enabled_ok == 0xf0 &&
        data_len == 4 &&
        p_data[0] == 0x62 &&
        p_data[1] == 0x01 &&
        p_data[2] == 0x01 &&
        p_data[3] == 0x00)
    {
        log_dbg("Nfcee enabled OK\n");
        nfcee_enabled_ok = 1;
    }
    else if(nfcee_hci_admin_all_pipe_cleared == 0 &&
        data_len == 6 &&
        p_data[0] == 0x01 &&
        p_data[3] == 0x81 &&
        p_data[4] == 0x15)
    {
        log_dbg("ADM_NOTIFY_ALL_PIPE_CLEARED\n");
        nfcee_hci_admin_all_pipe_cleared = 1;
    }
    else if(nfcee_ntf_pipe_to_host_created == 0 &&
        data_len == 10 &&
        p_data[0] == 0x01 &&
        p_data[3] == 0x81 &&
        p_data[4] == 0x12 &&
        p_data[7] == 0x01 &&
        (p_data[8] == 0xf0 || p_data[8] == 0x41))
    {
        if(p_data[8] == 0xf0 ){
            log_dbg("ADM_NOTIFY_PIPE_CREATED for APDU gate,pipe=0x%x\n",p_data[9]);
        }
        else if(p_data[8] == 0x41 ){
            log_dbg("ADM_NOTIFY_PIPE_CREATED for connectivity gate,pipe=0x%x\n",p_data[9]);
        }
        nfcee_ntf_pipe_to_host_created = 1;
    }
    else if(nfcee_ntf_pipe_to_host_created ==1 &&
        nfcee_ntf_pipe_to_host_opened == 0 &&
        data_len == 5 &&
        p_data[0] == 0x01 &&
        p_data[1] == 0x00 &&
        p_data[2] == 0x02 &&
        p_data[4] == 0x03)
    {
        log_dbg("ANY_OPEN_PIPE for pipe=0x%x.\n",p_data[3]&0x7F);
        nfcee_ntf_pipe_to_host_pipeid = p_data[3]&0x7F;
        nfcee_ntf_pipe_to_host_opened = 1;
    }

    else if(ese_cplc_1_reported == 0xf0 &&
        p_data[0] == 0x01 &&
        p_data[1] == 0x00)
    {
        if(p_data[3] == 0x15){
            log_dbg("ISD_cmd response continued fragment\n");
        }
        else if(p_data[3] == 0x95){
            log_dbg("ISD_cmd response final fragment\n");
            ese_cplc_1_reported = 1;
        }
    }
    else if(ese_cplc_2_reported == 0xf0 &&
        p_data[0] == 0x01 &&
        p_data[1] == 0x00)
    {
        if(p_data[3] == 0x15){
            log_dbg("read_CPLC_cmd response continued fragment\n");
        }
        else if(p_data[3] == 0x95){
            log_dbg("read_CPLC_cmd response final fragment\n");
            ese_cplc_2_reported = 1;
        }
    }
    else if(data_len == 4 &&
        p_data[0] == 0x6f &&
        p_data[1] == 0x3f &&
        p_data[2] == 0x01 &&
        p_data[3] == 0x15){
        log_dbg("AP coldreset notificaion received!\n");
        cold_reset_ntf_received = 1;
    }
    pthread_mutex_unlock(&g_lock_read);
}

static void reset_variables()
{
    memset(g_chip_fw_ver, 0, FW_VERSION_LEN);
    memset(g_chip_reg_ver, 0, RFREG_VERSION_SIZE);
    memset(g_chip_swreg_ver, 0, RFREG_VERSION_SIZE);
    g_chip_sector_size = SECTOR_SIZE;
    g_chip_base_address = BASE_ADDRESS;
    g_chip_total_sectors = 0;

    last_recv_data_len = 0;
    nfcee_hci_reported = 0;
    nfcee_hci_enabled = 0;
    nfcee_hci_session_ok = 0;
    nfcee_hci_pipes_cleared = 0;
    nfcee_hci_admin_pipe_created = 0;
    nfcee_hci_session_created = 0;
    nfcee_hci_whitelist_set = 0;

    nfcee_uicc_reported = 0;
    nfcee_ese_reported = 0;
    nfcee_enabled_ok = 0;
    nfcee_hci_admin_all_pipe_cleared = 0;
    nfcee_ntf_pipe_to_host_created = 0;
    nfcee_ntf_pipe_to_host_opened = 0;
    nfcee_ntf_pipe_to_host_pipeid = 0;

    tag_detected = 0;
    waiting_select_disc = 0;
    ce_interface_activated = 0;
    ese_cplc_1_reported = 0;
    ese_cplc_2_reported = 0;
    g_nci_version = 0x00;
    g_dual_registers = 0x00;

    sram_test_ntf_reported = 0;
    sram_test_result = 0;
}

static int g_read_thread_running = 0;
pthread_t g_read_thread;

static void* read_thread(void *p) {

  int ret;
  uint8_t buf[NCI_CTRL_SIZE];

  int max_fd;
  struct timeval tv;
  fd_set rfds;

  log_dbg("read_thread enter\n");
  g_read_thread_running = 1;

  max_fd = hdriver;

  while (g_read_thread_running) {

    FD_ZERO(&rfds);
    FD_SET(hdriver, &rfds);

    tv.tv_sec = 0;
    tv.tv_usec = 5000;

    ret = select(max_fd + 1, &rfds, NULL, NULL, &tv);
    //log_dbg("read_thread select=%d,errno=%d\n",ret,errno);

    if (ret == 0) // timeout
      continue;
    else if (ret < 0 && errno == EINTR)
      continue;
    else if (ret < 0) {
      log_dbg("Polling error!!\n");
      break;
    }

    memset(buf, 0, sizeof(buf));

    /* read 3 bytes (header)*/
    ret = read(hdriver, buf, NCI_HDR_SIZE);
    if (ret == 0)
      continue;
    else if (ret != NCI_HDR_SIZE) {
      log_dbg("Reading NCI header failed, ret=%d, errno=%d.\n",ret,errno);
      continue;
    }

    /* payload will read upper layer */
    ret = read(hdriver, buf + NCI_HDR_SIZE, buf[2]);
    if (ret != buf[2]) {
      log_dbg("Reading NCI payload failed ret=%d, errno=%d!!\n",ret,errno);
      continue;
    }

    data_trace("Recv", NCI_HDR_SIZE + buf[2], buf);

    hal_context_data_callback (NCI_HDR_SIZE + buf[2], buf);
  }
  log_dbg("read_thread exit\n");
  return NULL;

}

int nfc_hal_write(uint16_t data_len, const uint8_t *p_data)
{
    int ret;
    ret = write(hdriver, p_data, data_len);
    if(ret == data_len){
        data_trace("Send", data_len, p_data);
    }
    return ret;
}
int nfc_hal_open()
{
    int ret;

    log_dbg("nfc_hal_open...\n");
    ret = open_driver();
    if(ret){
        log_dbg ("open driver fail.\n");
        return -1;
    }
    pthread_mutex_init(&g_lock_read, NULL);

    log_dbg("Set NFC.mode on...\n");
    set_nfc_mode(NFC_DEV_MODE_ON);
    wakeup_nfc();

    pthread_create(&g_read_thread, NULL, read_thread, NULL);
    OSI_delay(50);

    return 0;

}
void nfc_hal_close()
{
    log_dbg("nfc_hal_close...\n");
    g_read_thread_running = 0;
    pthread_join(g_read_thread, NULL);

    pthread_mutex_destroy(&g_lock_read);

    set_nfc_mode(NFC_DEV_MODE_OFF);

    close_driver();
}

#define NCI_LMR_SCREEN_STATE_ON_UNLOCKED 0x00
#define NCI_LMR_SCREEN_STATE_OFF 0x01
int hal_cmd_set_power_substate(int state)
{
    int max_try;
    int ret= -1;
    uint8_t set_power_sub_state[4]={0x20,0x09,0x01,0x00};
    set_power_sub_state[3] = state;

    max_try = 0;
    last_recv_data_len = 0;
    memset(last_rx_buff, 0, sizeof(last_rx_buff));
    log_dbg("set_power_sub_state %s...\n", (state == NCI_LMR_SCREEN_STATE_OFF ? "screen off":"screen on"));
    nfc_hal_write(sizeof(set_power_sub_state), set_power_sub_state);
    while(last_recv_data_len == 0){
        OSI_delay(10);
        if(max_try ++ > 50){
            log_dbg("set_power_sub_state timeout\n");
            break;
        }
    }
    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 4 &&
        last_rx_buff[0] == 0x40 &&
        last_rx_buff[1] == 0x09 &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("set_power_sub_state failed.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);

    return ret;
}

int hal_cmd_set_discover_cfg(int on)
{
    int max_try;
    int ret= -1;
    uint8_t set_discover_cfg[]={0x20,0x02,0x04,0x01,0x02,0x01,0x00};

    set_discover_cfg[6] = on;

    max_try = 0;
    last_recv_data_len = 0;
    memset(last_rx_buff, 0, sizeof(last_rx_buff));
    log_dbg("set_discover_cfg %s...\n", (on?"on":"off"));
    nfc_hal_write(sizeof(set_discover_cfg), set_discover_cfg);
    while(last_recv_data_len == 0){
        OSI_delay(10);
        if(max_try ++ > 50){
            log_dbg("set_discover_cfg timeout\n");
            break;
        }
    }
    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 5 &&
        last_rx_buff[0] == 0x40 &&
        last_rx_buff[1] == 0x02 &&
        last_rx_buff[2] == 0x02 &&
        last_rx_buff[3] == 0x00 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("set_discover_cfg failed.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);

    return ret;
}

#define TRACE_UICC 0x01
#define TRACE_ESE 0x02
#define TRACE_FIELD_STRENGTH 0x04
#define TRACE_PART3_RX 0x08
#define TRACE_CLT 0x10
#define TRACE_MF_AUTH 020
#define TRACE_PART3_FULL 0x40

void reset_cmd_rsp_buffer()
{
    last_recv_data_len = 0;
    memset(last_rx_buff, 0, sizeof(last_rx_buff));
}
void wait_cmd_rsp_buffer(int timeout)
{
    int max_try = 0;
    while(last_recv_data_len == 0 && max_try++ < (timeout/10)){
        OSI_delay(10);
    }
    if(max_try ++ >= (timeout/10)){
        log_dbg("wait command response timeout\n");
    }
}


int hal_cmd_set_forced_part3(int enabled)
{
    uint8_t set_forced_part3_cmd[11]={0x2f,0x2c,0x08,0x01,0x04,0x00,0xed,0xb9,0x09,0x08,0x08};
    int ret = 0,cmd_len; //0 means success
    int cnt;
    int max_try = 0;

    if(enabled){
        memcpy(set_forced_part3_cmd+4,g_forced_part3_param,sizeof(g_forced_part3_param));
        cmd_len = sizeof(set_forced_part3_cmd);
    }
    else{
        set_forced_part3_cmd[2]=0x01;
        set_forced_part3_cmd[3]=0x00;
        cmd_len = 4;
    }


    log_dbg("Set forced part3 parameters...\n");

    max_try = 0;
    reset_cmd_rsp_buffer();
    nfc_hal_write(sizeof(set_forced_part3_cmd), set_forced_part3_cmd);


    wait_cmd_rsp_buffer(1000);
    pthread_mutex_lock(&g_lock_read);

    if(last_recv_data_len == 4 &&
        last_rx_buff[0] == 0x4f &&
        last_rx_buff[1] == 0x2c &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("failed to set part3 parameters.\n");
        if(last_recv_data_len > 0) DebugPrintByteArray(last_recv_data_len, last_rx_buff);
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);
    return ret;

}

int hal_cmd_enable_runtime_trace(uint8_t mask)
{
    int max_try;
    int ret= -1;
    uint8_t set_runtime_trace[4]={0x2f,0x17,0x01,0x00};
    set_runtime_trace[3] = mask;

    max_try = 0;
    reset_cmd_rsp_buffer();

    log_dbg("set_runtime_trace 0x%x...\n", mask);
    nfc_hal_write(sizeof(set_runtime_trace), set_runtime_trace);

    wait_cmd_rsp_buffer(1000);
    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 4 &&
        last_rx_buff[0] == 0x4f &&
        last_rx_buff[1] == 0x17 &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("set_runtime_trace failed.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);

    return ret;
}

int hal_cmd_set_door_locker_ce_param()
{
    return 0;
}

int hal_cmd_fwcfg()
{
    uint8_t fw_cfg_cmd[4]={0x2f,0x28,0x01,0x12};
    int ret = 0,cmd_len; //0 means success
    int cnt;

    int max_try = 0;
    if(g_clock_set == 0xff){
        log_dbg("Ignore fw clock setting 0x%x...\n",g_clock_set);
        return 0;
    }

    log_dbg("Set fw clock setting to 0x%x...\n",g_clock_set);

    fw_cfg_cmd[3] = g_clock_set;
    max_try = 0;
    reset_cmd_rsp_buffer();
    nfc_hal_write(sizeof(fw_cfg_cmd), fw_cfg_cmd);

    if(feature_to_test == FEATURE_SRAM_TEST){
        log_dbg("Don't wait for command response for SRAM test.\n");
        return 0;
    }

    wait_cmd_rsp_buffer(1000);
    pthread_mutex_lock(&g_lock_read);

    if(last_recv_data_len == 5 &&
        last_rx_buff[0] == 0x4f &&
        last_rx_buff[1] == 0x28 &&
        last_rx_buff[2] == 0x02 &&
        last_rx_buff[3] == 0x00 &&
        last_rx_buff[4] == g_clock_set)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("failed to set FW clock setting.\n");
        if(last_recv_data_len > 0) DebugPrintByteArray(last_recv_data_len, last_rx_buff);
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);
    return ret;
}

int hal_cmd_reset()
{

    int ret = 0,cmd_len; //0 means success
    int cnt;
    int max_try = 0;
    uint8_t nci_core_reset_cmd[4]={0x20,0x00,0x01,0x01};

    log_dbg("reset nfc device...\n");

    max_try = 0;
    reset_cmd_rsp_buffer();
    nfc_hal_write(sizeof(nci_core_reset_cmd), nci_core_reset_cmd);
    wait_cmd_rsp_buffer(1000);
    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len >=4 &&
        last_rx_buff[0] == 0x40 &&
        last_rx_buff[1] == 0x00 &&
        (last_rx_buff[2] == 0x03 || last_rx_buff[2] == 0x01 )&&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
        if(last_recv_data_len > 4){
            g_nci_version = last_rx_buff[5];
        }
    }
    else{
        log_dbg("failed to reset nfc device.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);

    max_try = 0;
    while(g_nci_version == 0x00 && max_try ++ < 500){
        OSI_delay(10);
    }
    if(g_nci_version == 0x00){
        log_dbg("Didn't reset successfully.\n");
        ret = -1;
    }
    return ret;
}

int hal_cmd_init()
{
    int ret = 0,cmd_len; //0 means success
    int cnt;
    int max_try = 0;
    uint8_t nci_initializ_cmd[5]={0x20,0x01,0x02,0x00,0x00};

    log_dbg("Initialize nfc device...\n");

    reset_cmd_rsp_buffer();
    cmd_len = 5;
    if(g_nci_version < 0x20){
        nci_initializ_cmd[2] = 0x00;
        cmd_len = 3;
    }
    nfc_hal_write(cmd_len, nci_initializ_cmd);
    wait_cmd_rsp_buffer(1000);
    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len >= 23 &&
        last_rx_buff[0] == 0x40 &&
        last_rx_buff[1] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("failed to initialze nfc device.\n");
        ret = -1;
    }

    if(ret == 0 && g_nci_version < 0x20){
        uint8_t major_ver = (uint8_t)((last_rx_buff[19]>>4)&0x0f);
        uint8_t minor_ver = (uint8_t)((last_rx_buff[19])&0x0f);
        uint16_t build_info_high = (uint16_t)(last_rx_buff[20]);
        uint16_t build_info_low = (uint16_t)(last_rx_buff[21]);

        log_dbg("F/W version: %d.%d.%d\n",
                major_ver,
                minor_ver,
                ((build_info_high*0x100)+build_info_low));
    }
    pthread_mutex_unlock(&g_lock_read);

    return ret;
}


int hal_update_china_rfreg(const char *rfreg_path)
{
#define CHINA_OPTIONS_LEN 512

    FILE *rfreg_file = NULL;
    uint8_t *img_data = NULL;
    int img_len,  ret, i, cmd_len;

    uint8_t cmd_start_china_rfreg_update[]={0x2f,0x13,0x00};
    uint8_t cmd_china_rfreg_set_param[260]={0x2f,0x14,0xfd};
    uint8_t cmd_stop_china_rfreg_update[5]={0x2f,0x15,0x02,0x00,0x00};
    int section;
    uint32_t check_sum = 0, *pcheck_sum;
    uint64_t check_sum64 = 0;

    if(g_dual_registers == 0){
        log_dbg("RF china options are only applicable to dual RF option binaries solutions.\n");
        return -1;
    }
    if ((rfreg_file = fopen(rfreg_path, "rb")) == NULL) {
        log_dbg("Failed to open rfreg file (file: %s)\n", rfreg_path);
        return -1;
    }

    img_len = CHINA_OPTIONS_LEN;
    img_data = (uint8_t *)malloc(img_len);
    if (img_data == NULL){
        fclose(rfreg_file);
        log_dbg("failed to allocate buffer for rfreg data\n");
        return -1;
    }
    fseek(rfreg_file, (-1L)*img_len, SEEK_END);
    fread(img_data, 1, img_len, rfreg_file);
    fclose(rfreg_file);
    rfreg_file = NULL;

    OSI_delay(80);

    log_dbg("Send start China RF update command...\n");

    reset_cmd_rsp_buffer();
    nfc_hal_write(sizeof(cmd_start_china_rfreg_update) , cmd_start_china_rfreg_update );

    wait_cmd_rsp_buffer(1000);
    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len != 4 ||
        last_rx_buff[0] != 0x4f ||
        last_rx_buff[1] != cmd_start_china_rfreg_update[1] ||
        last_rx_buff[2] != 0x01 ||
        last_rx_buff[3] != 0x00)
    {
        log_dbg("start China RF update command response error = %d\n", last_rx_buff[3]);
        free(img_data);
        pthread_mutex_unlock(&g_lock_read);
        return -1;
    }
    pthread_mutex_unlock(&g_lock_read);
    log_dbg("OK\n");

    for(section = 0; ; section++){
        int data_len = RFREG_SECTION_SIZE;
        if((section + 1)* RFREG_SECTION_SIZE > (img_len)){
            data_len = img_len - section * RFREG_SECTION_SIZE;
        }
        if(data_len <= 0){
            break;
        }

        cmd_china_rfreg_set_param[2] = (uint8_t)(1 + data_len);//Header length, 1 byte of section index
        cmd_china_rfreg_set_param[3] = (uint8_t)section;
        memcpy(cmd_china_rfreg_set_param + NCI_HDR_SIZE + 1, img_data + section * RFREG_SECTION_SIZE, data_len);
        cmd_len = NCI_HDR_SIZE + 1 + data_len;

        // checksum
        pcheck_sum = (uint32_t *)(cmd_china_rfreg_set_param + (cmd_len - data_len));
        for (i = 1; i <= data_len/sizeof(uint32_t); i++, pcheck_sum++) check_sum64 += *pcheck_sum;
        check_sum = (uint32_t)check_sum64;

        log_dbg("Set China RF param %d...\n",section);
        reset_cmd_rsp_buffer();
        nfc_hal_write(cmd_len , cmd_china_rfreg_set_param );

        wait_cmd_rsp_buffer(1000);
        pthread_mutex_lock(&g_lock_read);
        if(last_recv_data_len != 4 ||
            last_rx_buff[0] != 0x4f ||
            last_rx_buff[1] != cmd_china_rfreg_set_param[1] ||
            last_rx_buff[2] != 0x01 ||
            last_rx_buff[3] != 0x00)
        {
            log_dbg("cmd_china_rfreg_set_param response error = %d\n", last_rx_buff[3]);
            free(img_data);
            pthread_mutex_unlock(&g_lock_read);
            return -1;
        }
        pthread_mutex_unlock(&g_lock_read);
        log_dbg("OK\n");
    }

    log_dbg("Send stop China RF update command...\n");
    TO_LITTLE_ARRY(cmd_stop_china_rfreg_update + NCI_HDR_SIZE, check_sum & 0xFFFF, 2);
    reset_cmd_rsp_buffer();
    nfc_hal_write(sizeof(cmd_stop_china_rfreg_update) , cmd_stop_china_rfreg_update );

    wait_cmd_rsp_buffer(1000);
    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len != 4 ||
        last_rx_buff[0] != 0x4f ||
        last_rx_buff[1] != cmd_stop_china_rfreg_update[1] ||
        last_rx_buff[2] != 0x01 ||
        last_rx_buff[3] != 0x00)
    {
        log_dbg("stop cmd_stop_china_rfreg_update error = %d\n", last_rx_buff[3]);
        free(img_data);
        pthread_mutex_unlock(&g_lock_read);
        return -1;
    }
    pthread_mutex_unlock(&g_lock_read);
    log_dbg("OK\n");

    free(img_data);

    return 0;
}

int hal_cmd_factory_test()
{
    int ret = 0,cmd_len; //0 means success
    int cnt;
    int max_try = 0;

    uint8_t nci_vs_factory_cmd[4]={0x2f,0x37,0x01,0x01};

    log_dbg("Factory test (type %d)...\n", g_factory_test_type );
    max_try = 0;
    reset_cmd_rsp_buffer();
    nci_vs_factory_cmd[3] = (uint8_t)g_factory_test_type;
    nfc_hal_write(sizeof(nci_vs_factory_cmd), nci_vs_factory_cmd);
    wait_cmd_rsp_buffer(1000);
    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len >= 4 &&
        last_rx_buff[0] == 0x4f &&
        last_rx_buff[1] == 0x37 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("failed to run factory test.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);
    return ret;
}

int hal_cmd_vendor()
{
    int ret = 0;
    int cnt;
    int max_try = 0;

    log_dbg("Send vendor command...\n" );
    g_vendor_cmd[0] =0x2f;

    max_try = 0;
    reset_cmd_rsp_buffer();

    nfc_hal_write(g_vendor_cmd_len, g_vendor_cmd);
    wait_cmd_rsp_buffer(1000);
    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len >= 3 &&
        last_rx_buff[0] == 0x4f &&
        last_rx_buff[1] == g_vendor_cmd[1])
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("failed to send vendor command.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);
    return ret;
}
int hal_cmd_set_sim_slot()
{
    int ret = 0,cmd_len; //0 means success
    int cnt;
    int max_try = 0;
    uint8_t nci_vs_select_uicc_cmd[4]={0x2f,0x07,0x01,0x01};

    if(g_uicc_slot <= 0){
        log_dbg("UICC slot is not sepcified!\n");
        return ret;
    }

    log_dbg("Select UICC slot %d.\n", g_uicc_slot );
    max_try = 0;
    reset_cmd_rsp_buffer();
    nci_vs_select_uicc_cmd[3]  = (unsigned char)g_uicc_slot;
    nfc_hal_write(sizeof(nci_vs_select_uicc_cmd), nci_vs_select_uicc_cmd);
    wait_cmd_rsp_buffer(1000);
    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 4 &&
        last_rx_buff[0] == 0x4f &&
        last_rx_buff[1] == 0x07 &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("failed to select uicc slot.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);
    return ret;
}

int hal_cmd_enable_nfcee_discovery()
{
    int ret = 0,cmd_len; //0 means success
    int cnt;
    int max_try = 0;
    uint8_t nci_enable_nfcee_discover[]={0x22,0x00,0x01,0x01};

    log_dbg("Enabling NFCEE discovery...\n");
    max_try = 0;
    cmd_len = sizeof(nci_enable_nfcee_discover);
    if(g_nci_version >= 0x20){
        nci_enable_nfcee_discover[2] = 0x00;
        cmd_len = 3;
    }

    reset_cmd_rsp_buffer();
    nfc_hal_write(cmd_len, nci_enable_nfcee_discover);

    wait_cmd_rsp_buffer(1000);

    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 5 &&
        last_rx_buff[0] == 0x42 &&
        last_rx_buff[1] == 0x00 &&
        last_rx_buff[2] == 0x02 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("failed to enable NFCEE discovery .\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);
    
    return ret;
}

int hal_cmd_set_routing_table()
{
    int ret = 0,cmd_len; //0 means success
    int cnt;
    int max_try = 0;

    //uint8_t rf_set_cfg_for_ce[13] = {0x20,0x02,0x0a,0x03,0x32,0x01,0x60,0x38,0x01,0x01,0x50,0x01,0x00};

    uint8_t rf_set_routing_table_host[30] = {
    0x21,0x01,0x1b,0x00,0x05,
    0x00,0x03,0x00,0x01,0x00,
    0x00,0x03,0x00,0x01,0x02,
    0x01,0x03,0x00,0x01,0x03,
    0x01,0x03,0x00,0x01,0x04,
    0x01,0x03,0x00,0x01,0x05};

    uint8_t rf_set_routing_table_uicc[30] = {
    0x21,0x01,0x1b,0x00,0x05,
    0x00,0x03,0x03,0xff,0x00,
    0x00,0x03,0x03,0xff,0x02,
    0x01,0x03,0x03,0xff,0x03,
    0x01,0x03,0x03,0xff,0x04,
    0x01,0x03,0x03,0xff,0x05};

    uint8_t rf_set_routing_table_ese[30] = {
    0x21,0x01,0x1b,0x00,0x05,
    0x00,0x03,0x02,0xff,0x00,
    0x00,0x03,0x02,0xff,0x02,
    0x01,0x03,0x02,0xff,0x03,
    0x01,0x03,0x02,0xff,0x04,
    0x01,0x03,0x02,0xff,0x05};


    if(g_nci_version >= 0x20){
        //new ESE ID in 2.0 
        rf_set_routing_table_ese[7] = 0x82;
        rf_set_routing_table_ese[12] = 0x82;
        rf_set_routing_table_ese[17] = 0x82;
        rf_set_routing_table_ese[22] = 0x82;
        rf_set_routing_table_ese[27] = 0x82;

        //new UICC ID in 2.0 
        rf_set_routing_table_uicc[7] = 0x83;
        rf_set_routing_table_uicc[12] = 0x83;
        rf_set_routing_table_uicc[17] = 0x83;
        rf_set_routing_table_uicc[22] = 0x83;
        rf_set_routing_table_uicc[27] = 0x83;
    }

    max_try = 0;

    reset_cmd_rsp_buffer();

    if(feature_to_test == FEATURE_HAL_CE_UICC){
        log_dbg("set routing table UICC...\n");
        nfc_hal_write(sizeof(rf_set_routing_table_uicc), rf_set_routing_table_uicc);
    }
    else if(feature_to_test == FEATURE_HAL_CE_ESE){
        log_dbg("set routing table ESE...\n");
        nfc_hal_write(sizeof(rf_set_routing_table_ese), rf_set_routing_table_ese);
    }
    else{
        log_dbg("set routing table HOST...\n");
        nfc_hal_write(sizeof(rf_set_routing_table_host), rf_set_routing_table_host);
    }

    wait_cmd_rsp_buffer(1000);

    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 4 &&
        last_rx_buff[0] == 0x41 &&
        last_rx_buff[1] == 0x01 &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("set routing table failed.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);
    return ret;
}

int hal_cmd_set_configurations()
{
    int ret = 0,cmd_len; //0 means success
    int cnt;
    int max_try = 0;
    uint8_t nci_set_configuration_p2p_off[]={0x20,0x02,0x07,0x02,0x32,0x01,0x20,0x50,0x01,0x00};

    log_dbg("set configuration for ce %s...\n",(feature_to_test == FEATURE_HAL_CE_UICC || feature_to_test == FEATURE_HAL_CE_ESE)?"UICC/ESE":"HOST");

    max_try = 0;

    reset_cmd_rsp_buffer();

    nfc_hal_write(sizeof(nci_set_configuration_p2p_off), nci_set_configuration_p2p_off);

    wait_cmd_rsp_buffer(1000);

    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 5 &&
        last_rx_buff[0] == 0x40 &&
        last_rx_buff[1] == 0x02 &&
        last_rx_buff[2] == 0x02 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("set configuration failed.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);

    return ret;
}

int hal_cmd_set_discover_map()
{
    int ret = 0,cmd_len; //0 means success
    int cnt;

    uint8_t discover_map_cmd[10]={0x21,0x00,0x07,0x02,0x04,0x03,0x02,0x05,0x03,0x03};
    log_dbg("discover map...\n");

    reset_cmd_rsp_buffer();
    nfc_hal_write(sizeof(discover_map_cmd), discover_map_cmd);
    wait_cmd_rsp_buffer(1000);

    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 4 &&
        last_rx_buff[0] == 0x41 &&
        last_rx_buff[1] == 0x00 &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("discover map failed.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);

    return ret;
}

int hal_cmd_start_discover()
{
    int ret = 0,cmd_len; //0 means success
    int cnt;


    uint8_t rf_discover_cmd_all[30]={0x21,0x03,0x1b,0x0d,0x00,0x01,0x01,0x01,0x02,0x01,0x03,0x01,
                       0x05,0x01,0x80,0x01,0x81,0x01,0x82,0x01,0x83,0x01,0x85,0x01,
                       0x06,0x01,0x74,0x01,0x70,0x01};
    //uint8_t rf_discover_cmd_limit[8]={0x21,0x03,0x05,0x02,0x00,0x01,0x01,0x01};
    uint8_t nci_discovery_listen_only[]={0x21,0x03,0x05,0x02,0x80,0x01,0x81,0x01};
    uint8_t nci_discovery_poll_one_tech_only[]={0x21,0x03,0x03,0x01,0x00,0x01};
    uint8_t nci_discovery_listen_one_tech_only[]={0x21,0x03,0x03,0x01,0x80,0x01};
    log_dbg("rf discover...\n");

    reset_cmd_rsp_buffer();

    if(feature_to_test == FEATURE_HAL_CE_HOST || feature_to_test == FEATURE_HAL_CE_UICC || feature_to_test == FEATURE_HAL_CE_ESE){
        log_dbg("Discover tech:%d (0:listen A,1:listen B,2: listen F;-1: listen A&B; -2: poll&listen A&B&F)\n", g_test_tech);
        if(g_test_tech >= 0){
            nci_discovery_listen_one_tech_only[4] = 0x80 + g_test_tech;
            nfc_hal_write(sizeof(nci_discovery_listen_one_tech_only), nci_discovery_listen_one_tech_only);
        }
        else{
            if(g_test_tech < -1){
                nfc_hal_write(sizeof(rf_discover_cmd_all), rf_discover_cmd_all);
            }
            else{
                nfc_hal_write(sizeof(nci_discovery_listen_only), nci_discovery_listen_only);
            }
        }
    }
    else if(feature_to_test == FEATURE_PRBS_TEST){
         nfc_hal_write(sizeof(nci_discovery_poll_one_tech_only), nci_discovery_poll_one_tech_only);
    }
    else if(feature_to_test == FEATURE_HAL_TAG && g_test_tech >= 0){
         nci_discovery_poll_one_tech_only[4] = g_test_tech;
         log_dbg("Poll with only tech:%d (0:typeA,1:TypeB,2:typeF)\n", g_test_tech);
         nfc_hal_write(sizeof(nci_discovery_poll_one_tech_only), nci_discovery_poll_one_tech_only);
    }
    else{
        nfc_hal_write(sizeof(rf_discover_cmd_all), rf_discover_cmd_all);
    }

    wait_cmd_rsp_buffer(1000);

    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 4 &&
        last_rx_buff[0] == 0x41 &&
        last_rx_buff[1] == 0x03 &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("failed to set nfc to poll.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);
    return ret;
}

int hal_cmd_reset_discover()
{
    int ret = 0,cmd_len; 
    int cnt;

    uint8_t rf_deactivate_cmd[]={0x21,0x06,0x01,0x00};
    log_dbg("de-activate...%d\n",g_part3_completed_count);

    reset_cmd_rsp_buffer();
    nfc_hal_write(sizeof(rf_deactivate_cmd), rf_deactivate_cmd);

    wait_cmd_rsp_buffer(1000);

    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 4 &&
        last_rx_buff[0] == 0x41 &&
        last_rx_buff[1] == 0x06 &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("failed to de-activate.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);
    return ret;
}

int hal_cmd_start_prbs()
{
    int ret = 0,cmd_len; //0 means success
    int cnt, max_try;

    uint8_t nci_cmd_prbs_test[]={0x2f,0x32,0x02,0x00,0x00};
 
    log_dbg("start PRBS, tech:%d (0:typeA,1:TypeB,2:typeF); rate:%d (0:106kbps,1:212kbps,2:424kbps)\n", g_test_tech, g_test_rate);
    nci_cmd_prbs_test[3]=g_test_tech;
    nci_cmd_prbs_test[4]=g_test_rate;

    reset_cmd_rsp_buffer();
 
    nfc_hal_write(sizeof(nci_cmd_prbs_test), nci_cmd_prbs_test);

    wait_cmd_rsp_buffer(1000);

    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 4 && 
        last_rx_buff[0] == 0x4f &&
        last_rx_buff[1] == 0x32 &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("failed to start PRBS test.\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);

    if(ret == 0){
        log_dbg("PRBS test running...\n");
        max_try = 0;
        while(1){
            OSI_delay(100);
            if(max_try ++ > g_test_timeout*10){ //Wait max 30 seconds
                log_dbg("PRBS test timeout\n");
                break;
            }
        }
    }
    return ret;
}
 
int hal_hci_connect()
{
    int max_try = 0, ret = 0;

    uint8_t nci_set_mode_hci[]={0x22,0x01,0x02,0x01,0x01};
    uint8_t nci_create_conn[]={0x20,0x04,0x06,0x03,0x01,0x01,0x02,0x01,0x01};

    if(g_nci_version >= 0x20){
        //HCI connection is static from NCI 2.0
        return 0;
    }

    while(nfcee_hci_reported == 0 && max_try ++ < 500){
        OSI_delay(10);
    }
    if(nfcee_hci_reported == 0){
        log_dbg("Failed to wait HCI\n");
        return -1;
    }

    /*nfcee_hci_reported and nfcee_hci_enabled* might not be set at the same because of thread swithing
     so sleep to avoid that*/
    OSI_delay(50);
    if(nfcee_hci_enabled == 0){
        log_dbg("Enable HCI...\n");
        reset_cmd_rsp_buffer();

        nfc_hal_write(sizeof(nci_set_mode_hci), nci_set_mode_hci);

        wait_cmd_rsp_buffer(1000);

        pthread_mutex_lock(&g_lock_read);
        if(last_recv_data_len == 4 && 
            last_rx_buff[0] == 0x42 &&
            last_rx_buff[1] == 0x01 &&
            last_rx_buff[2] == 0x01 &&
            last_rx_buff[3] == 0x00)
        {
            log_dbg("OK\n");
            ret  = 0;
            nfcee_hci_enabled = 1;
        }
        else{
            log_dbg("Failed to enabled HCI\n");
            ret = -1;
        }
        pthread_mutex_unlock(&g_lock_read);
    }

    log_dbg("Connect HCI...\n");
    reset_cmd_rsp_buffer();

    nfc_hal_write(sizeof(nci_create_conn), nci_create_conn);

    wait_cmd_rsp_buffer(1000);

    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 7 && 
        last_rx_buff[0] == 0x40 &&
        last_rx_buff[1] == 0x04 &&
        last_rx_buff[2] == 0x04 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("Failed to connect HCI\n");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);

    return ret;
}

int hal_hci_init_session()
{
    int ret = 0; //0 means success
    int max_try = 0;
    const uint8_t cmd_hci_get_session_id[] ={0x01,0x00,0x03,0x81,0x02,0x01};
    const uint8_t cmd_clear_all_pipes[] ={0x01,0x00,0x04,0x81,0x14,0x02,0x01};
    const uint8_t cmd_hci_open_admin_pipe[]={0x01,0x00,0x02,0x81,0x03};
    const uint8_t cmd_hci_set_session_id[] ={0x01,0x00,0x0b,0x81,0x01,0x01,0x01,0x08,0x02,0x00,0xef,0x06,0x02,0x00};

    log_dbg("Get HCI session ID...\n");
    max_try = 0;
    nfc_hal_write(sizeof(cmd_hci_get_session_id), cmd_hci_get_session_id);
    while(nfcee_hci_session_ok == 0 && max_try ++ < 10){
        OSI_delay(10);
    }
    if(nfcee_hci_session_ok == 1){
        log_dbg("OK\n");
        return 0;
    }
    if(nfcee_hci_session_ok == 0){
        log_dbg("Failed to get session ID. Re-create...\n");

        log_dbg("Open HCI admin pipe...\n");
        max_try = 0;
        nfcee_hci_admin_pipe_created = 0xf0;
        nfc_hal_write(sizeof(cmd_hci_open_admin_pipe), cmd_hci_open_admin_pipe);
        while(nfcee_hci_admin_pipe_created == 0xf0 && max_try ++ < 500){
            OSI_delay(10);
        }
        if(nfcee_hci_admin_pipe_created == 0xf0){
            log_dbg("Failed to create HCI admin pipe\n");
            return -1;
        }

        log_dbg("Clear all HCI pipes...\n");
        max_try = 0;
        nfcee_hci_session_ok = 0;
        nfcee_hci_session_created = 0xf0;
        nfcee_hci_pipes_cleared = 0xf0;
        nfc_hal_write(sizeof(cmd_clear_all_pipes), cmd_clear_all_pipes);
        while(nfcee_hci_pipes_cleared == 0xf0 && max_try ++ < 500){
            OSI_delay(10);
        }
        if(nfcee_hci_pipes_cleared == 0xf0){
            log_dbg("Failed to Clear all HCI pipes\n");
            return -1;
        }

        log_dbg("Open HCI admin pipe again...\n");
        nfcee_hci_admin_pipe_created = 0xf0;
        max_try = 0;
        nfc_hal_write(sizeof(cmd_hci_open_admin_pipe), cmd_hci_open_admin_pipe);
        while(nfcee_hci_admin_pipe_created == 0xf0 && max_try ++ < 500){
            OSI_delay(10);
        }
        if(nfcee_hci_admin_pipe_created == 0xf0){
            log_dbg("Failed to create HCI admin pipe\n");
            return -1;
        }

        log_dbg("Set HCI session ID...\n");
        max_try = 0;
        nfc_hal_write(sizeof(cmd_hci_set_session_id), cmd_hci_set_session_id);
        while(nfcee_hci_session_created == 0xf0 && max_try ++ < 500){
            OSI_delay(10);
        }
        if(nfcee_hci_session_created == 0xf0){
            log_dbg("Failed to set HCI session ID!\n");
            return -1;
        }
    }

    return 0;
}

int hal_hci_enable_nfcees()
{
    int max_try = 0, ret = 0;
    uint8_t hci_set_whitelist[]={0x01,0x00,0x06,0x81,0x01,0x03,0x02,0x03,0x04};
    uint8_t nci_set_mode_uicc[]={0x22,0x01,0x02,0x03,0x01};
    uint8_t nci_set_mode_ese[]={0x22,0x01,0x02,0x02,0x01};

    log_dbg("Set HCI whitelist...\n");
    max_try = 0;
    nfcee_hci_whitelist_set = 0xf0;
    nfc_hal_write(sizeof(hci_set_whitelist), hci_set_whitelist);
    if(feature_to_test == FEATURE_HAL_UICC_SWP || feature_to_test == FEATURE_HAL_CE_UICC){
        while(nfcee_uicc_reported == 0 && max_try ++ < 500){
            OSI_delay(10);
        }
        if(nfcee_uicc_reported == 0){
            log_dbg("Failed to detect UICC!\n");
            return -1;
        }
    }
    else if(feature_to_test == FEATURE_HAL_ESE_SWP || feature_to_test == FEATURE_HAL_CE_ESE){
        while(nfcee_ese_reported == 0 && max_try ++ < 500){
            OSI_delay(10);
        }
        if(nfcee_ese_reported == 0){
            log_dbg("Failed to detect ESE!\n");
            return -1;
        }
    }

    if(g_nci_version >= 0x20){
        max_try = 0;
        while(nfcee_hci_whitelist_set == 0xf0 && max_try ++ < 500){
            OSI_delay(10);
        }
        if(nfcee_hci_whitelist_set == 0xf0){
            log_dbg("Whitelist set failed!\n");
        }
    }

    nfcee_enabled_ok = 0xf0;

    reset_cmd_rsp_buffer();

    if(feature_to_test == FEATURE_HAL_UICC_SWP ||feature_to_test == FEATURE_HAL_CE_UICC){
        log_dbg("Enable UICC...\n");
        if(g_nci_version >= 0x20){
            nci_set_mode_uicc[3] = 0x83;//new UICC ID in 2.0 
        }
        nfc_hal_write(sizeof(nci_set_mode_uicc), nci_set_mode_uicc);
    }
    else  if(feature_to_test == FEATURE_HAL_ESE_SWP || feature_to_test == FEATURE_HAL_CE_ESE){
        log_dbg("Enable ESE...\n");
        if(g_nci_version >= 0x20){
            nci_set_mode_ese[3] = 0x82;//new ESE ID in 2.0 
        }
        nfc_hal_write(sizeof(nci_set_mode_ese), nci_set_mode_ese);
    }
    else{
        OSI_delay(100);
        log_dbg("No need to enable NFCEE.\n");
        return ret;
    }

    wait_cmd_rsp_buffer(1000);

    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 4 && 
        last_rx_buff[0] == 0x42 &&
        last_rx_buff[1] == 0x01 &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        ret  = 0;
    }
    else{
        log_dbg("Failed to enable %s !\n", (feature_to_test == FEATURE_HAL_UICC_SWP ||feature_to_test == FEATURE_HAL_CE_UICC)?"UICC":"ESE");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);

    if(ret < 0){
        return ret;
    }

    if(g_nci_version >= 0x20){
        while(nfcee_enabled_ok != 1 && max_try ++ < 500){
            OSI_delay(10);
        }
        if(nfcee_enabled_ok != 1){
            log_dbg("Failed to enable %s !\n", (feature_to_test == FEATURE_HAL_UICC_SWP ||feature_to_test == FEATURE_HAL_CE_UICC)?"UICC":"ESE");
            ret = -1;
        }
    }
    else{
        nfcee_enabled_ok = 1;
    }

    if(ret >= 0){
        log_dbg("Nfcee enabled OK.\n");
    }

    return ret;
}

int hal_hci_wait_pipes_init()
{
    uint8_t cmd_hci_any_ok[]={0x01,0x00,0x02,0x81,0x80};
    int cnt;

    log_dbg("Check admin_all_pipe_cleared...\n");
    OSI_delay(100);
    if(nfcee_hci_admin_all_pipe_cleared){
        log_dbg("Send ANY_OK for ADM_NOTIFY_ALL_PIPE_CLEARED...\n");
        nfc_hal_write(sizeof(cmd_hci_any_ok), cmd_hci_any_ok);
        OSI_delay(100);
    }

    for(cnt = 0; cnt<2; cnt++){
        OSI_delay(200);
        log_dbg("Check apdu gate and connectivity gate...%d\n",cnt);
        if(nfcee_ntf_pipe_to_host_created){
            log_dbg("Send ANY_OK for ADM_NOTIFY_PIPE_CREATED...\n");
            cmd_hci_any_ok[3] = 0x81;//default pipe for admin
            nfc_hal_write(sizeof(cmd_hci_any_ok), cmd_hci_any_ok);
            OSI_delay(100);
        }

        OSI_delay(200);
        if(nfcee_ntf_pipe_to_host_opened){

            cmd_hci_any_ok[3] = (0x80 | nfcee_ntf_pipe_to_host_pipeid);

            nfcee_ntf_pipe_to_host_opened = 0;
            nfcee_ntf_pipe_to_host_created = 0;
            nfcee_ntf_pipe_to_host_pipeid = 0;

            log_dbg("Send ANY_OK for ANY_OPEN_PIPE...\n");
            nfc_hal_write(sizeof(cmd_hci_any_ok), cmd_hci_any_ok);
            OSI_delay(100);
        }
        else{
            nfcee_ntf_pipe_to_host_opened = 0;
            nfcee_ntf_pipe_to_host_created = 0;
            nfcee_ntf_pipe_to_host_pipeid = 0;
            OSI_delay(100);
        }
    }
    return 0;
}

int hal_hci_deinit_nfcees()
{
    int max_try = 0;
    int ret = 0;

    uint8_t nci_set_mode_uicc[]={0x22,0x01,0x02,0x03,0x00};
    uint8_t nci_set_mode_ese[]={0x22,0x01,0x02,0x02,0x00};
    const uint8_t nci_close_conn[]={0x20,0x05,0x01,0x01};

    reset_cmd_rsp_buffer();

    if(feature_to_test == FEATURE_HAL_UICC_SWP ||feature_to_test == FEATURE_HAL_CE_UICC){
        log_dbg("Diable UICC...\n");
        if(g_nci_version >= 0x20){
            nci_set_mode_uicc[3] = 0x83;//new UICC ID in 2.0 
        }
        nfc_hal_write(sizeof(nci_set_mode_uicc), nci_set_mode_uicc);
    }
    else  if(feature_to_test == FEATURE_HAL_ESE_SWP || feature_to_test == FEATURE_HAL_CE_ESE){
        log_dbg("Disable ESE...\n");
        if(g_nci_version >= 0x20){
            nci_set_mode_ese[3] = 0x82;//eSE in 2.0 
        }
        nfc_hal_write(sizeof(nci_set_mode_ese), nci_set_mode_ese);
    }

    wait_cmd_rsp_buffer(1000);

    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 4 && 
        last_rx_buff[0] == 0x42 &&
        last_rx_buff[1] == 0x01 &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("Failed to disable %s !\n", (feature_to_test == FEATURE_HAL_UICC_SWP ||feature_to_test == FEATURE_HAL_CE_UICC)?"UICC":"ESE");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);

    if(g_nci_version < 0x20){
        log_dbg("Close connection...\n");

        reset_cmd_rsp_buffer();

        nfc_hal_write(sizeof(nci_close_conn), nci_close_conn);

        wait_cmd_rsp_buffer(1000);

        pthread_mutex_lock(&g_lock_read);
        if(last_recv_data_len == 4 && 
            last_rx_buff[0] == 0x40 &&
            last_rx_buff[1] == 0x05 &&
            last_rx_buff[2] == 0x01 &&
            last_rx_buff[3] == 0x00)
        {
            log_dbg("OK\n");
            ret  = 0;
        }
        else{
            log_dbg("Failed to close connection!\n");
            ret = -1;
        }
        pthread_mutex_unlock(&g_lock_read);
    }

    return ret;
}

int hal_test_swp()
{
    int max_try = 0, ret = 0;
    uint8_t nci_se_test_cmd[4]={0x2f,0x31,0x01,0x00};

    log_dbg("Test %s SWP...\n", (feature_to_test == FEATURE_HAL_UICC_SWP)?"UICC":"ESE");

    if(feature_to_test == FEATURE_HAL_ESE_SWP){
        nci_se_test_cmd[3]=0x01;
    }

    reset_cmd_rsp_buffer();

    nfc_hal_write(sizeof(nci_se_test_cmd), nci_se_test_cmd);

    wait_cmd_rsp_buffer(1000);

    pthread_mutex_lock(&g_lock_read);
    if(last_recv_data_len == 4 && 
        last_rx_buff[0] == 0x4f &&
        last_rx_buff[1] == 0x31 &&
        last_rx_buff[2] == 0x01 &&
        last_rx_buff[3] == 0x00)
    {
        log_dbg("OK\n");
        ret  = 0;
    }
    else{
        log_dbg("Failed to test %s SWP!\n", (feature_to_test == FEATURE_HAL_UICC_SWP)?"UICC":"ESE");
        ret = -1;
    }
    pthread_mutex_unlock(&g_lock_read);

    if(ret < 0){
        return -1;
    }

    OSI_delay(100);

    if(feature_to_test == FEATURE_HAL_ESE_SWP && g_get_ese_cplc){
       //Get Thales CPLC by APDU applicate gate
       uint8_t nci_send_ISD_cmd[]={0x01,0x00,0x07,0x95,0x50,0x00,0xA4,0x04,0x00,0x00};
       uint8_t nci_send_read_CPLC_cmd[]={0x01,0x00,0x07,0x95,0x50,0x80,0xCA,0x9F,0x7F,0x00};

        log_dbg("eSE ISD_cmd...\n");

        ese_cplc_1_reported = 0xf0;
        max_try = 0;
        nfc_hal_write(sizeof(nci_send_ISD_cmd), nci_send_ISD_cmd);
        while(ese_cplc_1_reported == 0xf0 && max_try ++ < 500){
            OSI_delay(10);
        }
        if(ese_cplc_1_reported == 0xf0){
            log_dbg("ISD_cmd no response??\n");;
        }
        OSI_delay(10);
        log_dbg("eSE read_CPLC_cmd...\n");
        ese_cplc_2_reported = 0xf0;
        max_try = 0;
        nfc_hal_write(sizeof(nci_send_read_CPLC_cmd), nci_send_read_CPLC_cmd);
        while(ese_cplc_2_reported == 0xf0 && max_try ++ < 500){
            OSI_delay(10);
        }
        if(ese_cplc_2_reported == 0xf0){
            log_dbg("nci_send_read_CPLC_cmd no response??\n");
            return -1;
        }
    }
    return 0;
}

int check_nfc_hal()
{
    int ret = 0,cmd_len; //0 means success
    int cnt;

    int max_try = 0;


    reset_variables();

    if(g_update_firmware_rfreg >= 0){
        log_dbg("FW update mode: %d \n", g_update_firmware_rfreg);
        if(g_update_firmware_rfreg < 2 || g_update_firmware_rfreg == 4){
            log_dbg("Check existing chip FW version...\n");
            ret = check_nfc();
            if(ret < 0){
                log_dbg("Driver I2C is not working.");
                return ret;
            }
        }

        ret = download_fw((const char*)g_fw_path, (const char*)g_rfreg_path, (const char*)g_swreg_path);
        if(ret){
            log_dbg("failed to update firmware.\n");
            return ret;
        }
    }

    ret = nfc_hal_open();
    if(ret){
        return -1;
    }
    ret = hal_cmd_fwcfg();
    if(ret){
        nfc_hal_close();
        return -1;
    }

    if(feature_to_test == FEATURE_SRAM_TEST){
        if(g_test_timeout > 5){
            g_test_timeout = 5;
        }
        max_try = 0;
        while(sram_test_ntf_reported == 0){
            OSI_delay(100);
            if(max_try ++ > g_test_timeout*10){
                log_dbg("waiting SRAM test timeout\n");
                break;
            }
        }

        nfc_hal_close();

        if(g_erase_flash_in_stress != 2){
            log_dbg("Erase SRAM test firmware...\n");
            download_fw(NULL, NULL, NULL);
        }

        if(sram_test_result == 1){
            return 0;
        }
        else{
            return -1;
        }
    }

    OSI_delay(10);
    ret = hal_cmd_reset();
    if(ret){
        nfc_hal_close();
        return -1;
    }


    //Initialize device
    ret = hal_cmd_init();
    if(ret){
        nfc_hal_close();
        return -1;
    }

    if(g_update_firmware_rfreg == 4){
        log_dbg("Enable China RF options...\n");
        hal_update_china_rfreg(g_rfreg_path);
    }

    if(feature_to_test == FEATURE_HAL_OPEN){
        log_dbg("Close device\n");
        nfc_hal_close();
        return 0;
    }
    if(feature_to_test == FEATURE_HAL_FACTORY){
        ret = hal_cmd_factory_test();
        log_dbg("Close device\n");
        nfc_hal_close();
        return ret;
    }
    if(feature_to_test == FEATURE_HAL_VENDOR){
        ret = hal_cmd_vendor();
        log_dbg("Close device\n");
        nfc_hal_close();
        return ret;
    }
    if(feature_to_test == FEATURE_COLDRESET){
        ret = cmd_ese_cold_reset();
        log_dbg("Close device\n");
        nfc_hal_close();
        return ret;
    }

    if(feature_to_test == FEATURE_HAL_UICC_SWP || feature_to_test == FEATURE_HAL_ESE_SWP ||
        feature_to_test == FEATURE_HAL_CE_HOST || feature_to_test == FEATURE_HAL_CE_UICC || feature_to_test == FEATURE_HAL_CE_ESE){

        ret = hal_cmd_set_sim_slot();
        if(ret){
            nfc_hal_close();
            return -1;
        }

        ret = hal_cmd_enable_nfcee_discovery();
        if(ret){
            nfc_hal_close();
            return -1;
        }
        ret = hal_hci_connect();
        if(ret){
            nfc_hal_close();
            return -1;
        }


        ret = hal_hci_init_session();
        if(ret){
            nfc_hal_close();
            return -1;
        }

        ret = hal_hci_enable_nfcees();
        if(ret){
            nfc_hal_close();
            return -1;
        }

        hal_hci_wait_pipes_init();


        if(feature_to_test == FEATURE_HAL_UICC_SWP || feature_to_test == FEATURE_HAL_ESE_SWP)
        {

            ret = hal_test_swp();
            if(ret){
                nfc_hal_close();
                return -1;
            }

            hal_hci_deinit_nfcees();

            OSI_delay(100);

            log_dbg("Close device...\n");
            nfc_hal_close();
            return 0;
        }


    }

    //Set routing table
    if(feature_to_test == FEATURE_HAL_CE_HOST || feature_to_test == FEATURE_HAL_CE_UICC || feature_to_test == FEATURE_HAL_CE_ESE)
    {

        ret = hal_cmd_set_routing_table();
        if(ret){
            nfc_hal_close();
            return -1;
        }
        ret = hal_cmd_set_configurations();
        if(ret){
            nfc_hal_close();
            return -1;
        }
 
    }

    if(g_part3_dump_as_intf_activated){
       /*hal_cmd_set_door_locker_ce_param();*/
       hal_cmd_enable_runtime_trace(TRACE_PART3_FULL);
    }

    //Set nfc device to poll
    ret = hal_cmd_set_discover_map();
    if(ret){
        nfc_hal_close();
        return -1;
    }
    ret = hal_cmd_start_discover();
    if(ret){
        nfc_hal_close();
        return -1;
    }


    if(feature_to_test == FEATURE_PRBS_TEST && ret == 0){

        ret = hal_cmd_start_prbs();

        return ret;
    }

    //Ready to detect the tags from antenna
    if(feature_to_test == FEATURE_HAL_TAG && ret == 0){

        if(g_nci_version >= 0x20){
            hal_cmd_set_discover_cfg(0);
            hal_cmd_set_power_substate(NCI_LMR_SCREEN_STATE_OFF);
            OSI_delay(100);
            hal_cmd_set_power_substate(NCI_LMR_SCREEN_STATE_ON_UNLOCKED);
            hal_cmd_set_discover_cfg(1);
        }

        log_dbg("nfc is polling, please put a tag to the antenna...\n");
        max_try = 0;

        while(tag_detected == 0){
            OSI_delay(100);
            pthread_mutex_lock(&g_lock_read);
            if(waiting_select_disc > 0){
                uint8_t nci_cmd_disc_select[]={0x21,0x04,0x03,0x01,0x04,0x02};
                waiting_select_disc = 0;
                last_recv_data_len = 0;
                memset(last_rx_buff, 0, sizeof(last_rx_buff));
                nfc_hal_write(sizeof(nci_cmd_disc_select), nci_cmd_disc_select);
            }
            pthread_mutex_unlock(&g_lock_read);
            if(max_try ++ > g_test_timeout*10){ //Wait max 30 seconds
                log_dbg("waiting tag timeout\n");
                break;
            }
        }
        pthread_mutex_lock(&g_lock_read);
        if(tag_detected){
            uint8_t nci_cmd_apdu_select_file[] = {0x00,0x00,0x0D,0x00,0xA4,0x04,0x00,0x07,0xD2,0x76,0x00,0x00,0x85,0x01,0x01,0x00};
            ret  = 0;
            log_dbg("Send select file APDU...\n");
            nfc_hal_write(sizeof(nci_cmd_apdu_select_file), nci_cmd_apdu_select_file);
            pthread_mutex_unlock(&g_lock_read);
            OSI_delay(200);
        }
        else{
            pthread_mutex_unlock(&g_lock_read);
            log_dbg("failed to detect known tags.\n");
            ret = -1;
        }
    }
        //Ready to detect the tags from antenna
    if((feature_to_test == FEATURE_HAL_CE_HOST || feature_to_test == FEATURE_HAL_CE_UICC || feature_to_test == FEATURE_HAL_CE_ESE) && ret == 0){

        if(g_forced_part3_param[6] != 0x00){
          hal_cmd_set_forced_part3(1);
        }

        if(g_nci_version >= 0x20){
            hal_cmd_set_discover_cfg(0);
            hal_cmd_set_power_substate(NCI_LMR_SCREEN_STATE_OFF);
            OSI_delay(100);
            hal_cmd_set_power_substate(NCI_LMR_SCREEN_STATE_ON_UNLOCKED);
            hal_cmd_set_discover_cfg(1);
        }

        log_dbg("nfc is listening, please put a reader to the antenna...\n");

        max_try = 0;
        while(ce_interface_activated == 0){
            OSI_delay(100);
            if(max_try ++ > g_test_timeout*10){ //Wait max 30 seconds
                log_dbg("waiting reader timeout\n");
                break;
            }
            if(g_part3_dump_as_intf_activated && (max_try%3 == 0)){
                hal_cmd_reset_discover();
                g_part3_completed_count = 0;
                hal_cmd_start_discover();
            }
        }
        pthread_mutex_lock(&g_lock_read);
        if(ce_interface_activated){
            ret  = 0;
        }
        else{
            log_dbg("failed to wait for reader reading CE.\n");
            ret = -1;
        }
        pthread_mutex_unlock(&g_lock_read);

        OSI_delay(200);
        if(g_nci_version >= 0x20) {
           uint8_t cmd_hci_save_flash[]={0x2f,0x30,0x01,0x01};
           OSI_delay(50);
           log_dbg("force save HCI params...\n");
           nfc_hal_write(sizeof(cmd_hci_save_flash), cmd_hci_save_flash);
        }

        OSI_delay(100);
    }


    log_dbg("Close device\n");
    nfc_hal_close();
    return ret;
}


static int open_driver()
{
    if(hdriver > 0){
        log_dbg("Driver is already opened!\n");
        return 0;
    }
    hdriver = open(POWER_DRIVER, O_RDWR | O_NOCTTY);
    if (hdriver <= 0)
    {
        log_dbg("Failed to open device driver: %s\n", POWER_DRIVER);
        log_dbg("Is NFC turned off from Settings?\n");
        hdriver = 0;
        return -1;
    }
    log_dbg("open device drive \"%s\" succeeded!\n", POWER_DRIVER);
    return 0;
}
static int close_driver()
{
    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }
    if(close(hdriver)){
        log_dbg("close driver error, no=%d!\n", errno);
        return -1;
    }
    hdriver = 0;
    log_dbg("close driver succeeded!\n");
    return 0;
}
static int set_nfc_mode(int mode)
{
    int ret;
    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }
    ret = ioctl(hdriver, SEC_NFC_SET_MODE, mode);
    if (ret)
    {
        log_dbg("Mode set error ret=%d, no=%d\n", ret,errno);
        return -1;
    }
    OSI_delay(50);
    log_dbg("NFC mode %s set succeeded!\n",(mode == NFC_DEV_MODE_OFF) ? "off" : (mode == NFC_DEV_MODE_ON ? "on" : "bootloader"));
    return 0;
}

static int sleep_nfc()
{
    int ret;
    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }
    ret = ioctl(hdriver, SEC_NFC_SLEEP, 0);
    if(ret){
        log_dbg("NFC mode sleep error=%d,no+%d!\n",ret,errno);
    }
    else{
        log_dbg("NFC mode sleep succeeded!\n");
    }
    return ret;
}
static int wakeup_nfc()
{
    int ret;
    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }
    ret = ioctl(hdriver, SEC_NFC_WAKEUP, 0);
    if(ret){
        log_dbg("wakeup error=%d,no+%d!\n",ret,errno);
    }
    else{
        OSI_delay(80);
        log_dbg("wakeup succeeded!\n");
    }
    return ret;
}

static int sync_write(const uint8_t *p_data, uint16_t data_len)
{
    int ret;
    ret = write(hdriver, p_data, data_len);
    if(ret == data_len){
        data_trace("Send", data_len, p_data);
    }
    return ret;
}

static int wait_for_data_ready()
{
    int ret, max_fd;
    struct timeval tv;
    fd_set rfds;

    max_fd = hdriver;
    FD_ZERO(&rfds);
    FD_SET(hdriver, &rfds);
    tv.tv_sec = 3;
    tv.tv_usec = 0;

    ret = select(max_fd + 1, &rfds, NULL, NULL, &tv);

    if (ret <= 0){
      log_dbg("Wait for interrupt error:%d, is GPIO1 set correctly?\n", ret);
      return -1;
    }

    return 0;
}

static int sync_read(unsigned char* rx_buff, int buf_size, unsigned char bootmode)
{
    int ret;
    int head_len = NCI_HDR_SIZE, payload_len;
    if(bootmode){
        head_len = FW_HDR_SIZE;
    }
    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }
    wait_for_data_ready();
    ret = read(hdriver, rx_buff, head_len);
    if (ret != head_len){
        log_dbg("Read nci header error ret = %d, errno = %d\n", ret, errno);
        return -1;
    }
    if(bootmode){
        payload_len = (rx_buff[3] << 8) + rx_buff[2];
    }else{
        payload_len = (int)rx_buff[2];
    }
    if(payload_len > 256){
        log_dbg("Invalid payload len = %d, errno = %d\n", payload_len, errno);
        return -1;
    }
    ret = read(hdriver, rx_buff+head_len, payload_len);
    if (ret != payload_len){
        log_dbg("Read nci payload error ret = %d, errno = %d\n", ret, errno);
        return -1;
    }
    data_trace("Recv", ret+head_len, rx_buff);

    return ret+head_len;
}
static int sync_read_nci(unsigned char *rx_buff, int size)
{
    return sync_read(rx_buff, size, 0);
}
static int sync_read_boot(unsigned char *rx_buff, int size)
{
    return sync_read(rx_buff, size, 1);
}

chip_product_info *map_product(uint8_t *version)
{
    uint8_t i;
    for(i=0; i< sizeof(g_chip_products)/sizeof(chip_product_info); i++){
        if(g_chip_products[i].version[0] == version[0]){
            return &g_chip_products[i];
        }
    }
    return NULL;
}

uint8_t get_ext_bin_full_path(char *full_path, char * bin_name, uint8_t bl_version)
{
    int i, found = 0;

    char *ext_pos = strrchr(bin_name,'.');
    if(ext_pos){
        char key_file_name[50]={0};
        char key_str[8]={0};
        sprintf(key_str,"_k%02x", bl_version);
        strncpy(key_file_name, bin_name, ext_pos - bin_name);
        strcat(key_file_name, key_str);
        strcat(key_file_name, ext_pos);
        log_dbg("exact key file name:%s", key_file_name);

        for(i = 0; i < sizeof(g_fw_bin_dirs)/sizeof(const char *); i++){
            sprintf(full_path,"%s/%s",g_fw_bin_dirs[i], key_file_name);
            if(is_file_exist(full_path)){
                found = 1;
                break;
            }
        }
    }
    return found;
}

static int cmd_get_bootloader()
{
    int ret;
    unsigned char get_boot_info_cmd[11]={0x00,0x01,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    unsigned char rx_buff[23]={0};
    int len;

    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }

    ret = sync_write(get_boot_info_cmd , sizeof(get_boot_info_cmd) );
    if (ret != sizeof(get_boot_info_cmd)){
        log_dbg("failed to send get bootloader command.\n");
        return -1;
    }
    log_dbg("OK\nValidate bootloader...\n");

    ret = sync_read_boot(rx_buff, sizeof(rx_buff));
    if (ret < FW_HDR_SIZE){
        log_dbg("Read bootloader info error ret = %d, errno = %d\n", ret, errno);
        return -1;
    }
    if((rx_buff[0] & 0x7f) != 0x01 ||
        rx_buff[1] != 0x00)
    {
        log_dbg("bootloader info validation error = %d, errno = %d\n", ret, errno);
        return -1;
    }

    chip_product_info * product = map_product(rx_buff+4);
    if(product == NULL){
        log_dbg("Unknown product!!!\n");
        return -1;
    }
    log_dbg("OK\nBootloader version=%02x %02x %02x %02x\n", rx_buff[4], rx_buff[5], rx_buff[6], rx_buff[7]);
    memcpy(g_chip_bl_version, rx_buff+4, 4);
    FROM_LITTLE_ARRY(g_chip_sector_size, rx_buff + FW_HDR_SIZE + 4, 2);
    FROM_LITTLE_ARRY(g_chip_base_address, rx_buff + FW_HDR_SIZE + 12, 2);
    log_dbg("Chip %s\n",product->product_name);
    g_chip_total_sectors = product->num_of_sectors;
    if(strlen(g_fw_path) == 0){
        if (get_ext_bin_full_path(g_fw_path, product->fw_bin_name, rx_buff[6]) == 0) {
            get_bin_full_path(g_fw_path, product->fw_bin_name);
        }
    }
    if(strlen(g_rfreg_path) == 0){
        get_bin_full_path(g_rfreg_path, product->hw_bin_reg_name);
    }
    if(strlen(g_swreg_path) == 0 && product->sw_bin_reg_name != NULL){
        get_bin_full_path(g_swreg_path, product->sw_bin_reg_name);
    }
    if(strlen(g_swreg_path) != 0){
        g_dual_registers = 0x01;
    }
    log_dbg("Sector size=%d,baseaddr=0x%04x\n", g_chip_sector_size, g_chip_base_address);

    return 0;
}

static int cmd_fw_cfg(unsigned char cfg)
{
    int ret;
    unsigned char fw_cfg_cmd[4]={0x2f,0x28,0x01,0x12};
    unsigned char rx_buff[8]={0};
    log_dbg("cmd_fw_cfg, cfg=0x%x!\n",cfg);
    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }

    fw_cfg_cmd[3] = cfg;
    ret = sync_write(fw_cfg_cmd , 4 );
    if (ret != 4){
        log_dbg("failed to send fw cfg command.ret=%d, errno=%d\n",ret,errno);
        return -1;
    }


    ret = sync_read_nci(rx_buff, 5);
    if (ret != 5){
        log_dbg("Read fw cfg error ret = %d, errno = %d\n", ret, errno);
        return -1;
    }
    if(rx_buff[0] != 0x4f ||
        rx_buff[1] != 0x28 ||
        rx_buff[2] != 0x02 ||
        rx_buff[3] != 0x00 ||
        rx_buff[4] != cfg)
    {
        log_dbg("fw cfg validation error = %d, errno = %d\n", ret, errno);
        return -1;
    }
    log_dbg("OK\n");
    return 0;
}


static int cmd_reset()
{
    int ret, len;
    unsigned char nci_core_reset_cmd[4]={0x20,0x00,0x01,0x01};

    unsigned char rx_buff[23]={0};
    log_dbg("cmd_reset...\n");
    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }

    ret = sync_write(nci_core_reset_cmd , 4 );
    if (ret != 4){
        log_dbg("failed to send reset command.ret=%d, errno = %d\n", ret, errno);
        return -1;
    }


    memset(rx_buff, 0, sizeof(rx_buff));
    ret = sync_read_nci(rx_buff, sizeof(rx_buff));

    if(ret <= 0 ||
        rx_buff[0] != 0x40 ||
        rx_buff[1] != 0x00 ||
        (rx_buff[2] != 0x03 && rx_buff[2] != 0x01)||
        rx_buff[3] != 0x00)
    {
        log_dbg("reset command validation error = %d, errno = %d\n", ret, errno);
        return -1;
    }
    log_dbg("OK\n");
    if(ret == 4){
       log_dbg("Get reset notification...\n");
       ret = sync_read_nci(rx_buff, sizeof(rx_buff));
       if(ret < 12 || rx_buff[0] != 0x60 || rx_buff[1] != 0x00 ){
          log_dbg("Failed to receive reset notification\n");
          return -1;
       }
       g_nci_version = rx_buff[5];
       if(g_nci_version == 0x20){
           memcpy(g_chip_fw_ver, rx_buff + 8, FW_VERSION_LEN);
           print_fw_version();
       }
    }
    else{
       g_nci_version = rx_buff[4];
    }
    log_dbg("NCI version: %d.%d\n",((g_nci_version & 0xf0) >> 4), (g_nci_version & 0x0f));

    return 0;
}

static int cmd_init()
{
    int ret;

    unsigned char nci_initializ_cmd[5]={0x20,0x01,0x02,0x00,0x00};
    unsigned char rx_buff[32]={0};
    log_dbg("cmd_init...\n");
    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }

    if(g_nci_version < 0x20){
       nci_initializ_cmd[2] = 0x00;
    }
    ret = sync_write(nci_initializ_cmd, 3 + nci_initializ_cmd[2]);
    if (ret != 3 + nci_initializ_cmd[2])
    {
        log_dbg("failed to send initialize command.ret=%d, errno = %d\n", ret, errno);
        return -1;
    }


    memset(rx_buff, 0, sizeof(rx_buff));
    ret = sync_read_nci(rx_buff, sizeof(rx_buff));

    if(ret <= 0 ||
        rx_buff[0] != 0x40 ||
        rx_buff[1] != 0x01 ||
        rx_buff[3] != 0x00)
    {
        log_dbg("Initialze command validation error = %d, errno = %d\n", ret, errno);
        return -1;
    }
    if(g_nci_version < 0x20){
        memcpy(g_chip_fw_ver, rx_buff + 19, FW_VERSION_LEN);
        print_fw_version();
    }

    return 0;
}

static int cmd_get_reg_ver()
{
    int ret, cmd_len;
    unsigned char get_reg_cmd[4]={0x2f,0x29,0x00};
    unsigned char rx_buff[40]={0};
    unsigned char reg_ver_num[4];

    log_dbg("cmd_get_reg_ver...\n");
    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }
    cmd_len = 3;
    if(g_dual_registers == 0x01){
        get_reg_cmd[1] = 0x2a;
        get_reg_cmd[2] = 0x01;
        get_reg_cmd[3] = 0x00;
        cmd_len = 4;
    }

    ret = sync_write(get_reg_cmd, cmd_len);
    if (ret != cmd_len)
    {
        log_dbg("failed to send get rfreg ,ret=%d, errno = %d\n", ret, errno);
        return -1;
    }


    memset(rx_buff, 0, sizeof(rx_buff));
    ret = sync_read_nci(rx_buff, sizeof(rx_buff));

    if(rx_buff[0] != 0x4f ||
        rx_buff[1] != get_reg_cmd[1])
    {
        log_dbg("get reg rfreg version ret = %d, errno = %d\n", ret, errno);
        return -1;
    }

    memcpy(g_chip_reg_ver, rx_buff + NCI_HDR_SIZE + 5, 5);
    memcpy(g_chip_reg_ver + 5, rx_buff + NCI_HDR_SIZE + 12, 3);
    log_dbg("Chip RF version: %d/%d/%d/%d.%d.%d\n",
            (g_chip_reg_ver[0] >> 4) + 2014,
            g_chip_reg_ver[0] & 0xF,
            g_chip_reg_ver[1],
            g_chip_reg_ver[2],
            g_chip_reg_ver[3],
            g_chip_reg_ver[4]);

    reg_ver_num[0] = rx_buff[NCI_HDR_SIZE] >> 4;  // main version
    reg_ver_num[1] = rx_buff[NCI_HDR_SIZE] & 0x0f;  // sub version
    reg_ver_num[2] = rx_buff[NCI_HDR_SIZE + 1];  // patch version
    reg_ver_num[3] = rx_buff[NCI_HDR_SIZE + 11];  // minor version (number version)

    log_dbg("Chip RF Number Version: %d.%d.%d.%02x\n",
             reg_ver_num[0], reg_ver_num[1],
             reg_ver_num[2], reg_ver_num[3]);

    if(g_dual_registers){
        unsigned char swreg_ver_num[4];
        memcpy(g_chip_swreg_ver, rx_buff + NCI_HDR_SIZE + 15 + 5, 5);
        memcpy(g_chip_swreg_ver + 5, rx_buff + NCI_HDR_SIZE + 15 + 12, 3);

        log_dbg("Chip SW version: %d/%d/%d/%d.%d.%d\n",
            (g_chip_swreg_ver[0] >> 4) + 2014,
            g_chip_swreg_ver[0] & 0xF,
            g_chip_swreg_ver[1],
            g_chip_swreg_ver[2],
            g_chip_swreg_ver[3],
            g_chip_swreg_ver[4]);

        swreg_ver_num[0] = rx_buff[NCI_HDR_SIZE + 15] >> 4;  // main version
        swreg_ver_num[1] = rx_buff[NCI_HDR_SIZE + 15] & 0x0f;  // sub version
        swreg_ver_num[2] = rx_buff[NCI_HDR_SIZE + 16];  // patch version
        swreg_ver_num[3] = rx_buff[NCI_HDR_SIZE + 15 + 11];  // minor version (number version)

        log_dbg("Chip SW Number Version: %d.%d.%d.%02x\n",
                 swreg_ver_num[0], swreg_ver_num[1],
                 swreg_ver_num[2], swreg_ver_num[3]);
    }

    return 0;
}

static int cmd_get_chip_id()
{
    int ret;
    unsigned char get_chip_id[4]={0x2f,0x37,0x01,0x01};
    unsigned char rx_buff[20]={0};
    log_dbg("cmd_get_chip_id...!\n");
    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }

    ret = sync_write(get_chip_id, 4);
    if (ret != 4){
        log_dbg("failed to send get chip id, ret=%d, errno = %d\n", ret, errno);
        return -1;
    }


    memset(rx_buff, 0, sizeof(rx_buff));
    ret = sync_read_nci(rx_buff, sizeof(rx_buff));
    if (ret != sizeof(rx_buff)){
        log_dbg("get chip ID ret = %d, errno = %d\n", ret, errno);
        log_dbg("This F/W maybe not support GET_CHIP_ID\n");
        return 0;
    }

    if(rx_buff[0] != 0x4f ||
       rx_buff[1] != 0x37 ||
       rx_buff[2] != 0x11)
    {
        log_dbg("get chip ID ret = %d, errno = %d", ret, errno);
        return 0;
    }
    memcpy(g_chip_id, rx_buff + NCI_HDR_SIZE + 1, 16);
    log_dbg("[Chip ID]LOT number   : %02x %02x %02x %02x %02x %02x %02x %02x\n",
            g_chip_id[0], g_chip_id[1], g_chip_id[2], g_chip_id[3],
            g_chip_id[4], g_chip_id[5], g_chip_id[6], g_chip_id[7]);
    log_dbg("[Chip ID]Wafer number : %02x\n",
            g_chip_id[8]);
    log_dbg("[Chip ID]Die Position : %02x %02x %02x\n",
            g_chip_id[9], g_chip_id[10], g_chip_id[11]);
    log_dbg("[Chip ID]IC Fabricator: %02x %02x\n",
            g_chip_id[12], g_chip_id[13]);
    log_dbg("[Chip ID]IC Fabricator Date: %02x %02x\n",
            g_chip_id[14], g_chip_id[15]);
    return 0;
}

static int cmd_ese_cold_reset()
{
    int ret;
    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        return -1;
    }
    cold_reset_ntf_received = 0;
    ret = ioctl(hdriver, SEC_NFC_COLD_RESET, 1);
    if(ret){
        log_dbg("ese cold reset error=%d,no+%d!\n",ret,errno);
    }
    else{
        log_dbg("ese cold reset command succeeded!\n");
        OSI_delay(100);
    }
    if(cold_reset_ntf_received){
        ret = 0;
    }
    else{
        log_dbg("ese cold reset notification not received within 100ms!\n");
        ret = -1;
    }
    return ret;
}

void print_console_usage()
{
    log_dbg("Console mode:\nopen\nclose\noffmode\nbootmode\nonmode\nsleep\nwakeup\ngetboot\nfwcfg\nreset\ninit\ngetreg\ngetchipid\nesecoldreset\nquit\n");
}
void loop_console()
{
    char cmd[32]={0};
    print_console_usage();
    while(1){
        if(scanf("%s", cmd) != 1 || strlen(cmd) == 0){
        log_dbg("input error!");
        print_console_usage();
        continue;
        }
        if(!strcmp(cmd,"open")){
        open_driver();
        }
        else if(!strcmp(cmd,"close")){
        close_driver();
        }
        else if(!strcmp(cmd,"offmode")){
        set_nfc_mode(NFC_DEV_MODE_OFF);
        }
        else if(!strcmp(cmd,"bootmode")){
        set_nfc_mode(NFC_DEV_MODE_BOOTLOADER);
        }
        else if(!strcmp(cmd,"onmode")){
        set_nfc_mode(NFC_DEV_MODE_ON);
        }
        else if(!strcmp(cmd,"sleep")){
        sleep_nfc();
        }
        else if(!strcmp(cmd,"wakeup")){
        wakeup_nfc();
        }
        else if(!strcmp(cmd,"getboot")){
        cmd_get_bootloader();
        }
        else if(!strcmp(cmd,"fwcfg")){
        cmd_fw_cfg(g_clock_set);
        }
        else if(!strcmp(cmd,"reset")){
        cmd_reset();
        }
        else if(!strcmp(cmd,"init")){
        cmd_init();
        }
        else if(!strcmp(cmd,"getreg")){
        cmd_get_reg_ver();
        }
        else if(!strcmp(cmd,"getchipid")){
        cmd_get_chip_id();
        }
        else if(!strcmp(cmd,"esecoldreset")){
        cmd_ese_cold_reset();
        }
        else if(!strcmp(cmd,"quit")){
        break;
        }
        else{
        print_console_usage();
        }
        memset(cmd,0,32);
    };
}

void OSI_delay (uint32_t timeout)
{
    struct timespec delay;
    int err;

    delay.tv_sec = timeout / 1000;
    delay.tv_nsec = 1000 * 1000 * (timeout%1000);

    do
    {
        err = nanosleep(&delay, &delay);
    } while (err < 0 && errno == EINTR);
}


int check_nfc(void)
{
    int ret;

    reset_variables();

    ret = open_driver();
    if(ret) goto close_exit;

    //Bootloader checking
    log_dbg("setting device to bootloader mode...\n");
    ret = set_nfc_mode(NFC_DEV_MODE_BOOTLOADER);
    if(ret) goto close_exit;

    log_dbg("OK\nGet bootloader info...\n");

    ret = cmd_get_bootloader();
    if(ret) goto close_exit;

    log_dbg("I2C working normally!\n\n");

    log_dbg("NCI checking requires the NFC chip to have a working firmware.\n");
    log_dbg("Original NFC chips from factory do not have firmware, firmware will be downloaded\n");
    log_dbg("when the first time Android NFC is turned on or NFC HAL is initialized.\n");
    log_dbg("If the NFC in Android is never turned on or HAL is never initialized, following NCI checking might fail.\n\n");

    log_dbg("Setting device to full power nci mode...\n");
    ret = set_nfc_mode(NFC_DEV_MODE_ON);
    if(ret) { ret = 0; goto close_exit;}

    //sleep_nfc();
    //OSI_delay(1);
    log_dbg("Wakeup chip...\n");
    ret = wakeup_nfc();
    if(ret) { ret = 0; goto close_exit;}

    OSI_delay(80);

    log_dbg("Set fw config...\n");
    ret = cmd_fw_cfg(g_clock_set);
    if(ret) { ret = 0; goto close_exit;}

    //Reset command checking
    log_dbg("Try reset command...\n");
    ret = cmd_reset();
    if(ret) { ret = 0; goto close_exit;}

    //Initialize command checking
    log_dbg("Try initialize command...\n");
    //OSI_delay(10);
    ret = cmd_init();
    if(ret) { ret = 0; goto close_exit;}

    log_dbg("Get register version...\n");
    //OSI_delay(10);
    ret = cmd_get_reg_ver();
    if(ret) { ret = 0; goto close_exit;}

    log_dbg("done! close device.\n");

    ret = 1;

close_exit:
    log_dbg("Set mode off...\n");
    set_nfc_mode(NFC_DEV_MODE_OFF);

    close_driver();

    return ret;
}
int do_coldreset()
{
    int ret;

    ret = open_driver();
    if(ret) goto coldreset_exit;

    ret = cmd_ese_cold_reset();

coldreset_exit:
    close_driver();

    return ret;
}

static int _download_fw(const char *fw_path)
{
    int ret;
    unsigned char                 seq_no = 0x80;
    unsigned char enter_update_mode_cmd[8]={0x80,0x02,0x04,0x00,0x20,0x00,0x80,0x00};
    unsigned char hash_check_cmd[36]={0x82,0x00,0x20,0x00,0xbc,0x69,0x7d,0x07,0xe3,0x52,0xaf,0xff,0xbd,0x76,0xb2,0x4d,0x8d,0x08,0x69,0xdb,0x31,0x0b,0x6f,0xfd,0xaa,0x6d,0x6b,0x28,0xf7,0x06,0x69,0x78,0x23,0xba,0x15,0x81};
    unsigned char signature_check_cmd[132]={0x02,0x00,0x80,0x00,0x64,0x1f,0x97,
        0xdd,0x1c,0x27,0x86,0x19,0x6a,0xcc,0x80,0x41,0x82,0x3a,0x59,0x17,0xbb,0xe0,0x86,
        0x0b,0xca,0x15,0xce,0x85,0x83,0x49,0x76,0x70,0x68,0xb3,0xa4,0x96,0x97,0xab,0x03,0xd6,0xf5,0x6d,
        0x19,0x21,0x44,0x7f,0xac,0x9f,0xc1,0xe7,0xfe,0x7a,0xbd,0xbf,0x83,0x0c,0x7b,0x54,0xb2,0xb2,0x9d,0xb1,0x1c,
        0x15,0x50,0xb3,0x5b,0x22,0xbc,0xd9,0xd9,0xc9,0xf4,0xf5,0xe9,0x65,0x4f,0xd2,0x4b,0xa9,0x15,0x61,0x95,
        0x4b,0xca,0x3f,0x21,0x21,0xfe,0x36,0x38,0x3b,0x06,0x86,0xe8,0x5d,0x00,0x17,0x02,0xc8,0xa6,0xd4,0x8b,
        0x35,0x7a,0xe3,0xb0,0xed,0x80,0xf4,0xfe,0x73,0x11,0xea,0x6e,0x18,0x0e,0xff,0x10,0xe0,0x29,0x02,0xec,0xd2,
        0x76,0xae,0xd1,0x4f,0x60,0x02,0x82,0xf5};
    unsigned char update_sector_cmd[8]={0x00,0x04,0x04,0x00,0x00,0x30,0x00,0x00};

    int current_sector = 0, cnt;

    unsigned char send_sector_data[260]={02,0x00,0x00,0x01};
    unsigned char complete_update_mode[4]={0x80,0x05,0x00,0x00};
    unsigned char rx_buff[32]={0};

    FILE *fw_file = NULL;
    uint8_t *img_data = NULL, *fw_data = NULL, *signature;
    int img_len, fw_data_len = 0, signature_len;

    log_dbg("download firmware: %s\n", fw_path);

    if (fw_path != NULL && (fw_file = fopen(fw_path, "rb")) == NULL) {
        log_dbg("Failed to open F/W file (file: %s)\n", fw_path);
        return -1;
    }
    if(fw_file != NULL){
        fseek(fw_file, 0, SEEK_END);

        img_len = ftell(fw_file);

        if (img_len <= 0){
            fclose(fw_file);
            log_dbg("invalid length of F/W file\n");
            return -1;
        }
        img_data = (uint8_t *)malloc(img_len);
        if (img_data == NULL){
            fclose(fw_file);
            log_dbg("failed to allocate buffer for fw data\n");
            return -1;
        }
        fseek(fw_file, 0, SEEK_SET);
        fread(img_data, 1, img_len, fw_file);
        fclose(fw_file);
        fw_file = NULL;
    }

    if(img_data != NULL){
        uint8_t img_fw_ver[FW_VERSION_LEN];
        uint8_t major_ver, minor_ver;
        uint16_t build_info_high, build_info_low;

        typedef struct {
            uint8_t release_ver[12];
            uint8_t fw_ver[8];
            uint32_t sig_addr;
            uint32_t sig_size;
            uint32_t img_addr;
            uint32_t sector_num;
            uint32_t custom_sig_addr;
            uint32_t custom_sig_size;
        } img_header;

        img_header *p_header = (img_header *)img_data;

        memcpy(img_fw_ver , p_header->fw_ver + 4, FW_VERSION_LEN);

        major_ver = (unsigned char)((img_fw_ver[0]>>4)&0x0f);
        minor_ver = (unsigned char)((img_fw_ver[0])&0x0f);
        build_info_high = (unsigned short)(img_fw_ver[1]);
        build_info_low = (unsigned short)(img_fw_ver[2]);
        log_dbg("Image FW version: %d.%d.%d\n", major_ver, minor_ver, ((build_info_high*0x100)+build_info_low));

        if(g_update_firmware_rfreg == 2 || 
                (g_update_firmware_rfreg == 0 && memcmp(g_chip_fw_ver, img_fw_ver, FW_VERSION_LEN) != 0) ||
                (g_update_firmware_rfreg == 1 && memcmp(g_chip_fw_ver, img_fw_ver, FW_VERSION_LEN) < 0)){
            SHA256_CTX sha_ctx;
            log_dbg("FW update is required...\n");

            if(g_chip_bl_version[2] != 0){
                log_dbg("Use customer signature...\n");
                signature = img_data + p_header->custom_sig_addr;
                signature_len = p_header->custom_sig_size;
            }
            else{
                log_dbg("Use dev signature...\n");
                signature = img_data + p_header->sig_addr;
                signature_len = p_header->sig_size;
            }
            memcpy(signature_check_cmd + FW_HDR_SIZE, signature, signature_len);

            fw_data_len = p_header->sector_num * g_chip_sector_size;
            fw_data = img_data + p_header->img_addr;
            if(fw_data_len > img_len - sizeof(img_header)){
                log_dbg("invalid sector number=%d, ector_size=%d.\n",p_header->sector_num, g_chip_sector_size);
                free(img_data);
                img_data = NULL;
                return -1;
            }

            SHA256_Init(&sha_ctx);
            SHA256_Update(&sha_ctx, fw_data, fw_data_len);
            SHA256_Final(hash_check_cmd + FW_HDR_SIZE, &sha_ctx);

            log_dbg("signature_len=%d, fw_data_len=%d.\n",signature_len, fw_data_len);
        }
        else{
            log_dbg("FW update is NOT required.\n");
            free(img_data);
            img_data = NULL;
            return 0;
        }
    }
    if(fw_data_len == 0){
        fw_data_len = g_chip_total_sectors * g_chip_sector_size;
    }

    if(hdriver <= 0){
        log_dbg("Driver not opened yet!\n");
        if(img_data != NULL) free(img_data);
        return -1;
    }

    log_dbg("send enter_update_mode_cmd...\n");

    enter_update_mode_cmd[0] = FW_MSG_CMD|seq_no;
    seq_no ^=0x80;

    ret = sync_write(enter_update_mode_cmd , sizeof(enter_update_mode_cmd) );
    if (ret != sizeof(enter_update_mode_cmd)){
        log_dbg("failed to send enter_update_mode_cmd.\n");
        if(img_data != NULL) free(img_data);
        return -1;
    }
    log_dbg("OK\nValidate enter_update_mode_cmd...\n");


    ret = sync_read_boot(rx_buff, 4);
    if (ret != 4){
        log_dbg("enter_update_mode_cmd error ret = %d, errno = %d\n", ret, errno);
        if(img_data != NULL) free(img_data);
        return -1;
    }
    if((rx_buff[0] & 0x7f) != 0x01 ||
        rx_buff[1] != 0x00 ||
        rx_buff[2] != 0x00 ||
        rx_buff[3] != 0x00 )
    {
        log_dbg("enter_update_mode_cmd rsp validation error = %d, errno = %d,%02x %02x %02x %02x\n",
         ret, errno,
         rx_buff[0],rx_buff[1],rx_buff[2],rx_buff[3]);
         if(img_data != NULL) free(img_data);
        return -1;
    }
    log_dbg("OK\n");

    log_dbg("send hash_check_cmd...\n");
    hash_check_cmd[0] = FW_MSG_DATA|seq_no;
    seq_no ^=0x80;
    ret = sync_write(hash_check_cmd , sizeof(hash_check_cmd) );
    if (ret != sizeof(hash_check_cmd))
    {
        log_dbg("failed to send hash_check_cmd.\n");
        if(img_data != NULL) free(img_data);
        return -1;
    }
    log_dbg("OK\nValidate hash_check_cmd...\n");


    ret = sync_read_boot(rx_buff, 4);
    if (ret != 4){
            log_dbg("hash_check_cmd error ret = %d, errno = %d\n", ret, errno);
            if(img_data != NULL) free(img_data);
            return -1;
    }
    if((rx_buff[0] & 0x7f) != 0x01 ||
        rx_buff[1] != 0x00 ||
        rx_buff[2] != 0x00 ||
        rx_buff[3] != 0x00 )
    {
        log_dbg("hash_check_cmd rsp validation error = %d, errno = %d,%02x %02x %02x %02x\n",
         ret, errno,
         rx_buff[0],rx_buff[1],rx_buff[2],rx_buff[3]);
         if(img_data != NULL) free(img_data);
         return -1;
    }
    log_dbg("OK\n");

    log_dbg("Send signature_check_cmd...\n");
    signature_check_cmd[0] = FW_MSG_DATA|seq_no;
    seq_no ^=0x80;
    ret = sync_write(signature_check_cmd , sizeof(signature_check_cmd) );
    if (ret != sizeof(signature_check_cmd))
    {
        log_dbg("failed to send signature_check_cmd.\n");
        if(img_data != NULL) free(img_data);
        return -1;
    }
    log_dbg("OK\nValidate signature_check_cmd...\n");


    ret = sync_read_boot(rx_buff, 4);
    if (ret != 4){
        log_dbg("signature_check_cmd error ret = %d, errno = %d\n", ret, errno);
        if(img_data != NULL) free(img_data);
        return -1;
    }
    if((rx_buff[0] & 0x7f) != 0x01 ||
        rx_buff[1] != 0x00 ||
        rx_buff[2] != 0x00 ||
        rx_buff[3] != 0x00 )
    {
        log_dbg("signature_check_cmd rsp validation error = %d, errno = %d,%02x %02x %02x %02x\n",
         ret, errno,
         rx_buff[0],rx_buff[1],rx_buff[2],rx_buff[3]);
         if(img_data != NULL) free(img_data);
        return -1;
    }
    log_dbg("OK\n");

    if(fw_data == NULL){
        log_dbg("Flash is erased.\n");
        return 0;
    }

    for(cnt=0; cnt<256; cnt++) {
        send_sector_data[4+cnt]=0xff;
    }
    for(current_sector = 0; ; current_sector++){

        log_dbg("Send update_sector_cmd, current_sector=%d\n",current_sector);
        TO_LITTLE_ARRY(update_sector_cmd + FW_HDR_SIZE, g_chip_base_address+current_sector*g_chip_sector_size,4);

        update_sector_cmd[0] = FW_MSG_CMD|seq_no;
        seq_no ^=0x80;

        ret = sync_write(update_sector_cmd , sizeof(update_sector_cmd) );
        if (ret != sizeof(update_sector_cmd))
        {
            log_dbg("failed to send update_sector_cmd.\n");
            if(img_data != NULL) free(img_data);
            return -1;
        }
        log_dbg("OK\nValidate update_sector_cmd...\n");


        ret = sync_read_boot(rx_buff, 4);
        if (ret != 4)
        {
            log_dbg("update_sector_cmd error ret = %d, errno = %d\n", ret, errno);
            if(img_data != NULL) free(img_data);
            return -1;
        }
        if( (rx_buff[0] & 0x7f) != 0x01 ||
            rx_buff[1] != 0x00 ||
            rx_buff[2] != 0x00 ||
            rx_buff[3] != 0x00 )
        {
            log_dbg("update_sector_cmd rsp validation error = %d, errno = %d,%02x %02x %02x %02x\n",
             ret, errno,
             rx_buff[0],rx_buff[1],rx_buff[2],rx_buff[3]);
             if(img_data != NULL) free(img_data);
            return -1;
        }
        log_dbg("OK\n");

        for(cnt=0; cnt < g_chip_sector_size/256; cnt++){
            log_dbg("Send send_sector_data (cnt=%d)...\n",cnt);
            send_sector_data[0] = FW_MSG_DATA|seq_no;
            seq_no ^=0x80;

            if(fw_data != NULL){
                memcpy(send_sector_data + FW_HDR_SIZE, fw_data + current_sector*g_chip_sector_size + 256*cnt, 256);
            }

            ret = sync_write(send_sector_data , sizeof(send_sector_data)  );
            if (ret != sizeof(send_sector_data)){
                log_dbg("failed to send send_sector_data.\n");
                if(img_data != NULL) free(img_data);
                return -1;
            }
            log_dbg("OK\nValidate send_sector_data...\n");


            ret = sync_read_boot(rx_buff, 4);
            if (ret != 4){
                log_dbg("send_sector_data rsp error ret = %d, errno = %d\n", ret, errno);
                if(img_data != NULL) free(img_data);
                return -1;
            }
            if( (rx_buff[0] & 0x7f) != 0x01 ||
                rx_buff[1] != 0x00 ||
                rx_buff[2] != 0x00 ||
                rx_buff[3] != 0x00 )
            {
                log_dbg("send_sector_data rsp validation error = %d, errno = %d,%02x %02x %02x %02x\n",
                 ret, errno,
                 rx_buff[0],rx_buff[1],rx_buff[2],rx_buff[3]);
                 if(img_data != NULL) free(img_data);
                return -1;
            }
            log_dbg("OK\n");

         }
         if((current_sector+1)*g_chip_sector_size >= fw_data_len){
            log_dbg("Flashed %d sectors\n", current_sector+1);
            break;
         }
    }
    if(img_data != NULL) free(img_data);

    complete_update_mode[0] = FW_MSG_CMD|seq_no;
    seq_no ^=0x80;
    log_dbg("Send complete_update_mode...\n");
    ret = sync_write(complete_update_mode , sizeof(complete_update_mode) );
    if (ret != sizeof(complete_update_mode))
    {
        log_dbg("failed to send complete_update_mode.\n");
        return -1;
    }
    log_dbg("OK\nValidate complete_update_mode...\n");

    ret = sync_read_boot(rx_buff, 4);
    if (ret != 4){
        log_dbg("complete_update_mode error ret = %d, errno = %d\n", ret, errno);
        return -1;
    }
    if(rx_buff[1] != 0x00){
        log_dbg("Update FW failed! Status:0x%02x\n", rx_buff[1]);
        return -1;
    }

    log_dbg("Update FW successfully.\n");
    return 0;
}

int _update_rfreg(const char *rfreg_path, const char *swreg_path)
{
    FILE *rfreg_file = NULL, *swreg_file = NULL;
    uint8_t *img_data = NULL;
    int img_len, sw_img_len, ret, i, cmd_len;
    uint8_t value[16];
    int position = 16;  // Default position of version information
    uint8_t img_reg_ver[RFREG_VERSION_SIZE], sw_img_reg_ver[RFREG_VERSION_SIZE];
    uint8_t cmd_start_rfreg_update[4]={0x2f,0x26,0x00};
    uint8_t cmd_rfreg_set_param[260]={0x2f,0x22,0xfd};
    uint8_t cmd_rfreg_set_version[11]={0x2f,0x25,0x08};
    uint8_t cmd_stop_rfreg_update[6]={0x2f,0x27,0x02,0x23,0x1a};
    uint8_t rx_buff[128];
    int section;
    uint32_t check_sum = 0, *pcheck_sum;
    uint64_t check_sum64 = 0;

    ret = wakeup_nfc();
    if(ret) {return -1;}

    ret = cmd_fw_cfg(g_clock_set);
    if(ret) {return -1;}

    ret = cmd_reset();
    if(ret) {return -1;}
    ret = cmd_init();
    if(ret) {return -1;}
    cmd_get_reg_ver();
    log_dbg("update rfreg: %s\n", rfreg_path);
    if (rfreg_path == NULL){
        return -1;
    }

    if ((rfreg_file = fopen(rfreg_path, "rb")) == NULL) {
        log_dbg("Failed to open rfreg file (file: %s)\n", rfreg_path);
        return -1;
    }

    fseek(rfreg_file, 0, SEEK_END);

    img_len = ftell(rfreg_file);
    if (img_len <= 0){
        fclose(rfreg_file);
        log_dbg("invalid length of rfreg file\n");
        return -1;
    }

    log_dbg("rfreg file size=%d\n",img_len);

    sw_img_len = 0;
    if(g_dual_registers && swreg_path != NULL){
        log_dbg("update swreg_path: %s\n", swreg_path);
        if ((swreg_file = fopen(swreg_path, "rb")) == NULL) {
            log_dbg("Failed to open swreg file (file: %s)\n", swreg_path);
        }
        else{
            fseek(swreg_file, 0, SEEK_END);
            sw_img_len = ftell(swreg_file);
            log_dbg("swreg file size=%d\n",sw_img_len);
            if (sw_img_len < 0){
                sw_img_len = 0;
                log_dbg("invalid length of swreg file\n");
            }
        }
    }

    img_data = (uint8_t *)malloc(img_len + sw_img_len);
    if (img_data == NULL){
        fclose(rfreg_file);
        if(swreg_file) fclose(swreg_file);
        log_dbg("failed to allocate buffer for rfreg data\n");
        return -1;
    }
    fseek(rfreg_file, 0, SEEK_SET);
    fread(img_data, 1, img_len, rfreg_file);
    fclose(rfreg_file);
    rfreg_file = NULL;

    memcpy(value, &img_data[img_len - position], 16);

    memcpy(img_reg_ver, value + 5, 5);       // Version information (time)
    memcpy(img_reg_ver + 5, value + 12, 3);  // ID (csc code)

    log_dbg("Image RF version: %d/%d/%d/%d.%d.%d\n",
            (img_reg_ver[0] >> 4) + 2014,
            img_reg_ver[0] & 0xF,
            img_reg_ver[1],
            img_reg_ver[2],
            img_reg_ver[3],
            img_reg_ver[4]);

    if(swreg_file != NULL){
        fseek(swreg_file, 0, SEEK_SET);
        fread(img_data + img_len, 1, sw_img_len, swreg_file);
        fclose(swreg_file);
        swreg_file = NULL;

        memcpy(value, &img_data[img_len + sw_img_len - position], 16);

        memcpy(sw_img_reg_ver, value + 5, 5);       // Version information (time)
        memcpy(sw_img_reg_ver + 5, value + 12, 3);  // ID (csc code)

        log_dbg("Image SW version: %d/%d/%d/%d.%d.%d\n",
                (sw_img_reg_ver[0] >> 4) + 2014,
                sw_img_reg_ver[0] & 0xF,
                sw_img_reg_ver[1],
                sw_img_reg_ver[2],
                sw_img_reg_ver[3],
                sw_img_reg_ver[4]);
    }

    if(memcmp(g_chip_reg_ver, img_reg_ver, RFREG_VERSION_SIZE) == 0 && 
       (g_dual_registers == 0x00 || memcmp(g_chip_swreg_ver, sw_img_reg_ver, RFREG_VERSION_SIZE) == 0)){
        log_dbg("Image RF & SW reg version is same as chip RF & SW version.\n");
        free(img_data);
        return 0;
    }

    OSI_delay(80);

    log_dbg("Send start RF update command...\n");
    cmd_len = 3;
    if(g_dual_registers == 0x01){
        cmd_start_rfreg_update[1] = 0x2a;
        cmd_start_rfreg_update[2] = 0x01;
        cmd_start_rfreg_update[3] = 0x01;
        cmd_len = 4;
    }
    ret = sync_write(cmd_start_rfreg_update , cmd_len );
    if (ret != cmd_len){
        log_dbg("failed to send start RF update command.ret=%d, errno=%d\n",ret,errno);
        free(img_data);
        return -1;
    }


    memset(rx_buff, 0, sizeof(rx_buff));
    ret = sync_read_nci(rx_buff, 4);
    if (ret != 4){
        log_dbg("Read response for RF start update command error, ret= %d, errno = %d\n", ret, errno);
        free(img_data);
        return -1;
    }
    if(rx_buff[0] != 0x4f ||
        rx_buff[1] != cmd_start_rfreg_update[1] ||
        rx_buff[2] != 0x01 ||
        rx_buff[3] != 0x00)
    {
        log_dbg("RF start update command response error = %d\n", rx_buff[3]);
        free(img_data);
        return -1;
    }
    log_dbg("OK\n");

    for(section = 0; ; section++){
        int data_len = RFREG_SECTION_SIZE;
        if((section + 1)* RFREG_SECTION_SIZE > (img_len + sw_img_len)){
            data_len = img_len + sw_img_len - section * RFREG_SECTION_SIZE;
        }
        if(data_len <= 0){
            break;
        }

        if(g_dual_registers == 0x01){
            cmd_rfreg_set_param[1] = 0x2a; //Header OID for dual uption
            cmd_rfreg_set_param[2] = (uint8_t)(2 + data_len);//Header length
            cmd_rfreg_set_param[3] = 0x02; //Set data cmd
            cmd_rfreg_set_param[4] = (uint8_t)section;
            memcpy(cmd_rfreg_set_param + NCI_HDR_SIZE + 2, img_data + section * RFREG_SECTION_SIZE, data_len);
            cmd_len = NCI_HDR_SIZE + 2 + data_len;
        }
        else{
            cmd_rfreg_set_param[NCI_HDR_SIZE-1] = (uint8_t)(1 + data_len);
            cmd_rfreg_set_param[NCI_HDR_SIZE] = (uint8_t)section;
            memcpy(cmd_rfreg_set_param + NCI_HDR_SIZE + 1, img_data + section * RFREG_SECTION_SIZE, data_len);
            cmd_len = NCI_HDR_SIZE + 1 + data_len;
        }

        // checksum
        pcheck_sum = (uint32_t *)(cmd_rfreg_set_param + (cmd_len - data_len));
        for (i = 1; i <= data_len/sizeof(uint32_t); i++, pcheck_sum++) check_sum64 += *pcheck_sum;
        check_sum = (uint32_t)check_sum64;

        log_dbg("Set RF param %d...\n",section);
        ret = sync_write(cmd_rfreg_set_param , cmd_len);
        if (ret != cmd_len){
            log_dbg("failed to send set RF param command.ret=%d, errno=%d\n",ret,errno);
            free(img_data);
            return -1;
        }


        memset(rx_buff, 0, sizeof(rx_buff));
        ret = sync_read_nci(rx_buff, 4);
        if (ret != 4){
            log_dbg("Read response for set RF param command error= %d, errno = %d\n", ret, errno);
            free(img_data);
            return -1;
        }
        if(rx_buff[0] != 0x4f ||
            rx_buff[1] != cmd_rfreg_set_param[1] ||
            rx_buff[2] != 0x01 ||
            rx_buff[3] != 0x00)
        {
            log_dbg("RF set param response error = %d\n", rx_buff[3]);
            free(img_data);
            return -1;
        }
        log_dbg("OK\n");
    }

    if(g_dual_registers == 0x00){
        log_dbg("Send set RF version command...\n");
        memcpy(cmd_rfreg_set_version + NCI_HDR_SIZE, img_reg_ver, RFREG_VERSION_SIZE);
        ret = sync_write(cmd_rfreg_set_version , sizeof(cmd_rfreg_set_version) );
        if (ret != sizeof(cmd_rfreg_set_version)){
            log_dbg("failed to send start RF version command.ret=%d, errno=%d\n",ret,errno);
            free(img_data);
            return -1;
        }


        memset(rx_buff, 0, sizeof(rx_buff));
        ret = sync_read_nci(rx_buff, 4);
        if (ret != 4){
            log_dbg("Read response for set RF version command error= %d, errno = %d\n", ret, errno);
            free(img_data);
            return -1;
        }
        if(rx_buff[0] != 0x4f ||
            rx_buff[1] != 0x25 ||
            rx_buff[2] != 0x01 ||
            rx_buff[3] != 0x00)
        {
            log_dbg("RF set rf version response error = %d\n", rx_buff[3]);
            free(img_data);
            return -1;
        }
        log_dbg("OK\n");
    }

    log_dbg("Send stop RF update command...\n");
    if(g_dual_registers == 0x01){
        cmd_stop_rfreg_update[1] = 0x2a;//dual option OID
        cmd_stop_rfreg_update[2] = 3; //length
        cmd_stop_rfreg_update[3] = 0x03;//Stop update cmd
        TO_LITTLE_ARRY(cmd_stop_rfreg_update + NCI_HDR_SIZE + 1, check_sum & 0xFFFF, 2);
        cmd_len = 6;
    }
    else{
        TO_LITTLE_ARRY(cmd_stop_rfreg_update + NCI_HDR_SIZE, check_sum & 0xFFFF, 2);
        cmd_len = 5;
    }
    ret = sync_write(cmd_stop_rfreg_update , cmd_len );
    if (ret != cmd_len){
        log_dbg("failed to send stop RF update command.ret=%d, errno=%d\n",ret,errno);
        free(img_data);
        return -1;
    }


    memset(rx_buff, 0, sizeof(rx_buff));
    ret = sync_read_nci(rx_buff, 4);
    if (ret != 4){
        log_dbg("Read response for stop RF version command error= %d, errno = %d\n", ret, errno);
        free(img_data);
        return -1;
    }
    if(rx_buff[0] != 0x4f ||
        rx_buff[1] != cmd_stop_rfreg_update[1] ||
        rx_buff[2] != 0x01 ||
        rx_buff[3] != 0x00)
    {
        log_dbg("stop RF update response error = %d\n", rx_buff[3]);
        free(img_data);
        return -1;
    }
    log_dbg("OK\n");

    free(img_data);

    return 0;
}

int download_fw(const char *fw_path, const char *rfreg_path, const char *swreg_path)
{
    int ret;
    ret = open_driver();
    if(ret) goto close_exit;
    log_dbg("setting device to bootloader mode...\n");
    ret = set_nfc_mode(NFC_DEV_MODE_BOOTLOADER);
    if(ret) goto close_exit;

    log_dbg("OK\nGet bootloader info...\n");

    ret = cmd_get_bootloader();
    if(ret) goto close_exit;

    log_dbg("OK\nDownload FW flash...\n");
    OSI_delay(100);
    ret = _download_fw(fw_path);
    if(ret) goto close_exit;

    if(feature_to_test == FEATURE_SRAM_TEST && fw_path != NULL){
        log_dbg("Don't close driver for SRAM test.\n");
        return ret;
    }

    if(rfreg_path == NULL || strlen(rfreg_path) == 0 ) goto close_exit;

    log_dbg("Set device to ON mode...\n");
    ret = set_nfc_mode(NFC_DEV_MODE_ON);
    if(ret) goto close_exit;

    log_dbg("Update Rf reg...\n");
    ret = _update_rfreg(rfreg_path, swreg_path);

close_exit:
    log_dbg("Set mode off...\n");
    set_nfc_mode(NFC_DEV_MODE_OFF);

    close_driver();

    return ret;
}


void loop_stress_test(int erase)
{
    int count = 0;
    int ret = 0;
    int failed_count = 0;
    struct timeval start, end;

    long total_micros_used = 0,secs_used, micros_used;

    while(g_stress_total < 0 || count < g_stress_total){
        count++;
        gettimeofday(&start, NULL);
        if(feature_to_test == FEATURE_DRIVER){
            ret = check_nfc();
        }
        else{
            ret = check_nfc_hal();
        }
        if(ret < 0){
            log_dbg("Test failed at %d attempt.\n", count);
            failed_count++;
        }
        else{
            gettimeofday(&end, NULL);
            secs_used=(end.tv_sec - start.tv_sec);
            micros_used= ((secs_used*1000000) + end.tv_usec) - (start.tv_usec);
            total_micros_used += micros_used;
            log_dbg("Test succeeded at %d attempt, took:%.03f seconds\n", count, (double)micros_used/1000);
        }

        log_dbg("Stress test statistics, failed/total: %d/%d, success average time:%.03f seconds\r\n\r\n\r\n", failed_count, count, (double)total_micros_used/(count - failed_count)/1000);

        OSI_delay(1000);
        if(erase == 1){
            download_fw(NULL, NULL, NULL);
            log_dbg("Erase done.\n");
        }

    }
}


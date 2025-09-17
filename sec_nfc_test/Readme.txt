!Make sure to turn off NFC from Android UI before running this tool if Android is running!
(This tool access the NFC driver directly, make sure Android NfcService is not occupying the "/dev/sec-nfc" before running this tool.
In case Nfc cannot be turned off from UI, rename "/system/lib64/libnfc-sec.so" to disable NfcService temperately.)

On Android 8.0 or later, please run this test program from /vendor/bin/


1. usage: sec_nfc_test [FEATURE] [OPTIONS]
FEATURES:
  driver         test basic I2C HW
  console        step by step driver controlling for GPIO debugging.
  hal            test basic NFC initialization.
  ese            test eSE SWP.
  uicc           test UICC SWP.
  tag            test NFC tag.
  ceese          test CE by eSE.
  ceuicc         test CE by UICC.
  cehost         test CE by Host.
  erase          erase chip flash.
  prbs           start PRBS test.

OPTIONS:
  -c <clock settings>  set clock settings: 0x10:24Mhz PLL; 0x11:19.2Mhz PLL; 0x12:26Mhz PLL; 0x13: 27.12Mhz crystal; 0x14:38.4Mhz PLL.
  -s <slot>            set preferred UICC slot
  -p                   get cplc when testing eSE SWP
  -u <mode>            check firmware/refreg to update:
                       mode 0: default, update if different
                       mode 1: update if different
                       mode 2: force update
  -f <firmware path>   set firmware path to update
  -r <rfreg path>      set rfreg path to update
  -S <swreg path>      set swreg path to update
  -t <total count>     stress test, infinite if total count is not specified.
  -e                   erase flash in stress test
  -T <type index>      prbs tech type: 0:typeA;1:TypeB; 2:typeF
  -R <bit rate index>  prbs bd Rate: 0:106kbps;1:212kbps;2:424kbps
  -i <count>           test timeout in seconds for tag/ce/prbs test



2. Console mode:

   console commands:

   open: Open driver.
   close: Close driver.
   offmode: Set chip power off.
   quit: Quit console.

   bootmode: Set chip to BootLoader mode.
   getboot: Get BootLoader information; expected only in BootLoader mode.

   onmode: Set chip to Firmware/PowerOn mode (Below commands are expected only in Firmware mode)
   sleep: Put chip to sleep.
   wakeup: Wakeup chip.
   fwcfg: Set chip to use clock set by -c <clock settings>
   reset: Reset the chip by NCI command.
   init: Initialize the chip by NCI command.
   getreg: Get register option file information.
   
3. To integrate the tool into automated testing system.
   Run the tool with corresponding command and check the return value.
   Or integrate the codes directly.
   
4. Driver debugging process

        4.1)Check PVDD
                Phone is on: 1.8v
                Phone is off(even when in charging): 0v
                
        4.2) Check NFCPD, GPIO0/FIRM
        4.2.1)sec_nfc_test console -> open -> offmode:
                NFCPD: 1.8v
                GPIO0/FIRM: 0v
        4.2.2)sec_nfc_test console -> open -> offmode -> bootmode
                NFCPD: 0v
                GPIO0/FIRM: 1.8v
        4.2.3)sec_nfc_test console -> open -> offmode -> onmode
                NFCPD: 0v
                GPIO0/FIRM: 0v

        4.3) Check I2C
                sec_nfc_test driver
        

        4.4)Check IRQ/GPIO1, CLK_REQ/GPIO2, CLK/XI
                19.2Mhz PLL:
                sec_nfc_test hal -u -c 0x11
                26 Mhz PLL:
                sec_nfc_test hal -u -c 0x12
                27.12 Mhz Crystal:
                sec_nfc_test hal -u -c 0x5f

                4.4.1)Measure IRQ/GPIO1, CLK_REQ/GPIO2, CLK/XI:
                        > IRQ/GPIO1 should become high during program runing.
                        > CLK_REQ/GPIO2 should become high during program runing.
                        > CLK/XI: Corresponding clock signals should be observed.
                4.4.2)Check driver logs: 
                        > Interrupt routine of IRQ/GPIO1 should be called when IRQ/GPIO1 becomes high.
                        > Interrupt routine of CLK_REQ/GPIO2 should be called when CLK_REQ/GPIO2 becomes high.

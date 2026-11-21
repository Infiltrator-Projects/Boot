/*
 * Legacy dispatch takes a parsed BDS option. UpdateBbsTable derives device
 * priorities from its description and device type before the platform
 * LegacyBoot call receives the selected BBS path and load options.
 */

#ifdef __MAKEWITH_TIANO
#include "../include/tiano_includes.h"
#else
#include "gnuefi-helper.h"
#include "GenericBdsLib.h"
#endif

#ifndef _BDS_HELPER_H_
#define _BDS_HELPER_H_

EFI_STATUS
BdsLibDoLegacyBoot (
  IN  BDS_COMMON_OPTION           *Option
  );

EFI_STATUS BdsConnectDevicePath  (  IN EFI_DEVICE_PATH_PROTOCOL *    DevicePath,
                                    OUT EFI_HANDLE *     Handle,
                                    OUT EFI_DEVICE_PATH_PROTOCOL **     RemainingDevicePath
);

#endif  

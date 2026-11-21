/*
 * Firmware-reported BOOT version label. The native release build overrides
 * BOOT_RELEASE_VERSION; this header's development fallback is not evidence
 * of which release was installed or successfully started.
 */
 











#ifndef __VERSION_H
#define __VERSION_H

 
#ifndef BOOT_RELEASE_VERSION
#define BOOT_RELEASE_VERSION L"0.1.0-dev"
#endif
#define BOOT_VERSION BOOT_RELEASE_VERSION

#endif

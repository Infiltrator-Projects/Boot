/*
 * BOOT_VERSION is a CHAR16 literal for firmware formatting. Release builds
 * inject BOOT_RELEASE_VERSION while compiling. A plain source build falls
 * back to the development string defined here.
 */
 











#ifndef __VERSION_H
#define __VERSION_H

 
#ifndef BOOT_RELEASE_VERSION
#define BOOT_RELEASE_VERSION L"0.1.0-dev"
#endif
#define BOOT_VERSION BOOT_RELEASE_VERSION

#endif

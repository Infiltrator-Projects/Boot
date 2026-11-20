/*
 * include/version.h
 * Version number header file
 *
 * Copyright (c) 2017-2024 Roderick W. Smith
 *
 * Distributed under the terms of the GNU General Public License (GPL)
 * version 3 (GPLv3), a copy of which must be distributed with this
 * source code or binaries made from it.
 *
 */

#ifndef __VERSION_H
#define __VERSION_H

/* Release builds provide their exact version via the compiler. */
#ifndef BOOT_RELEASE_VERSION
#define BOOT_RELEASE_VERSION L"0.1.0-dev"
#endif
#define BOOT_VERSION BOOT_RELEASE_VERSION

#endif

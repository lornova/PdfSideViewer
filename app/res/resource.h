#pragma once

// Included by both rc.exe and C++ sources: plain #defines only.

#define IDI_APP 1

// Explorer context-menu verb icons, referenced from the registry as
// "exe,-<id>" (ShellIntegration.cpp). IDI_APP must keep the LOWEST id: the
// shell shows an exe's first icon in resource-id order as its file icon.
#define IDI_VERB_LEFT 2
#define IDI_VERB_CENTER 3
#define IDI_VERB_RIGHT 4

#define PSV_VERSION_MAJOR 0
#define PSV_VERSION_MINOR 9
#define PSV_VERSION_PATCH 2
#define PSV_VERSION_BUILD 0
#define PSV_VERSION_STR "0.9.2.0"
#define PSV_VERSION_WSTR L"0.9.2.0"

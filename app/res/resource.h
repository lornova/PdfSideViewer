#pragma once

// Included by both rc.exe and C++ sources: plain #defines only.

#define IDI_APP 1

// Explorer context-menu verb icons, referenced from the registry as
// "exe,-<id>" (ShellIntegration.cpp). IDI_APP must keep the LOWEST id: the
// shell shows an exe's first icon in resource-id order as its file icon.
#define IDI_VERB_LEFT 2
#define IDI_VERB_CENTER 3
#define IDI_VERB_RIGHT 4

// app\app.manifest carries a SECOND copy of this number (the assembly identity)
// and nothing checks the two against each other: bump both together, or the
// manifest silently drifts, as it did between 0.9.2 and 0.9.3.
#define PSV_VERSION_MAJOR 0
#define PSV_VERSION_MINOR 9
#define PSV_VERSION_PATCH 4
#define PSV_VERSION_BUILD 0
#define PSV_VERSION_STR "0.9.4.0"
#define PSV_VERSION_WSTR L"0.9.4.0"

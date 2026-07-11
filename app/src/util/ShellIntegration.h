#pragma once

#include "framework.h"

// Explorer context-menu verbs for .pdf files ("Open left/right in
// PdfSideViewer"). They live under HKCU\Software\Classes\
// SystemFileAssociations\.pdf\shell: per-user (no admin) and, by documented
// design, NEVER part of default-handler resolution, so the app cannot become
// the default PDF viewer through this. Labels are written in the UI language
// active at registration time (re-registering refreshes them).
namespace ShellIntegration {

bool Register();     // writes/updates both verbs for the CURRENT exe path
bool Unregister();   // removes both verbs (missing keys count as success)
bool IsRegistered(); // both verbs exist AND their commands reference this exe

} // namespace ShellIntegration

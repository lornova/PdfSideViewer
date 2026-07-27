#pragma once

#include "framework.h"

// Explorer context-menu verbs for .pdf files ("Open left/right/centre in
// PdfSideViewer"). They live under HKCU\Software\Classes\
// SystemFileAssociations\.pdf\shell: per-user (no admin) and, by documented
// design, NEVER part of default-handler resolution, so the app cannot become
// the default PDF viewer through this. Labels are written in the UI language
// active at registration time (re-registering refreshes them).
namespace ShellIntegration {

// Applies a DESIRED state (the Options checkbox), probing and mutating inside
// one lock acquisition: registered = all three verbs point at this exe,
// unregistered = none of the verbs this exe owns is left. Idempotent, and the
// only entry point that may be driven from a checkbox - an unlocked probe
// followed by a separate mutation can silently drop the user's request.
bool Apply(bool desired);
bool Register();        // writes/updates all three verbs for the CURRENT exe path
bool Unregister();      // removes all three verbs regardless of owner (headless
                        // -unregister-shell; missing keys count as success)
bool UnregisterOwned(); // removes only the verbs whose command launches THIS
                        // exe: the uninstaller's scope, safe under mixed ownership
bool IsRegistered();    // every verb exists AND its command launches this exe;
                        // unlocked DISPLAY state (the Options checkbox), never
                        // the basis for a mutation decision
bool OwnsAnyVerb();     // at least one verb launches this exe: a PARTIAL set
                        // (an upgrade over the two-verb release, a failed
                        // write) reads as unregistered above, yet is visible
                        // in Explorer and must stay removable

} // namespace ShellIntegration

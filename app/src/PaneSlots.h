#pragma once

#include <array>

// The frame owns a FIXED set of pane slots, listed in visual (left to right)
// order. Only a subset is ACTIVE: the two-pane default uses {Left, Right} and
// the optional three-pane mode activates Center in between. The indices never
// move when the mode changes, which is the whole point: settings sections, the
// per-slot session/fallback state and the sync-point tuples keep their meaning
// across a mode switch, so nothing has to be remapped or migrated.
//
// Two-pane mode is therefore just the active set {kSlotLeft, kSlotRight}: an
// ordered, strictly increasing subset exactly like the three-pane one, which is
// what lets every generalized routine degenerate to the pre-slot behavior.
enum PaneSlot : int {
    kSlotLeft = 0,
    kSlotCenter = 1,
    kSlotRight = 2,
};

// The COUNT, deliberately not an enumerator: it is arithmetic, and as a
// PaneSlot it would not deduce as int in templates like std::clamp.
inline constexpr int kPaneSlots = 3;

// Per-slot value, indexed by PaneSlot.
template <typename T> using PerPane = std::array<T, kPaneSlots>;

// Whether a slot participates, given the pane count (2 or 3). Iterating
// 0..kPaneSlots and skipping the inactive ones walks the panes in visual
// order, which is what every layout, status and sync routine wants.
inline constexpr bool SlotActive(int slot, int paneCount) {
    return slot != kSlotCenter || paneCount >= 3;
}

// INI section (and MRU/sync-point key prefix) per slot. Stable forever: these
// names are written into settings.ini.
inline constexpr const wchar_t* kSlotKeys[kPaneSlots] = {L"left", L"center", L"right"};

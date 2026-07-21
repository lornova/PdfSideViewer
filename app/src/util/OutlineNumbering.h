#pragma once

#include "engine/Document.h"

#include <optional>
#include <utility>
#include <vector>

// Hierarchical numeric key parsed from a bookmark title ("1.2 Title",
// "Chapter 3", "Sezione 2.1: x", "§ 4"). nullopt = the title does not
// participate in bookmark-based sync-point generation. A lone leading letter
// ("A Foo") is a WORD by default and rejected; allowLoneLetter admits it as a
// single letter key ([-1] = A), for appendix sub-items labelled "A"/"B"/"C"
// with no intro word - the caller vouches for the context (see
// MatchOutlineNumberings).
std::optional<std::vector<int>> ParseOutlineNumbering(const std::wstring& title,
                                                      bool allowLoneLetter = false);

// "1.2.3" display form of a parsed key (used as the sync-point label).
std::wstring FormatOutlineNumbering(const std::vector<int>& key);

// Pairs of outline indices (left, right) that appear in BOTH outlines, either
// by numeric key ("1.2 Title") or by canonical title (unnumbered sections,
// with common front/back matter names matched across Italian, English, German,
// French and Hungarian; title pairs additionally require the SAME outline
// depth, so a top-level section never anchors to a nested homonym). First
// occurrence per key and per side wins; the result follows the left outline's
// document order.
std::vector<std::pair<int, int>> MatchOutlineNumberings(
    const std::vector<Document::OutlineItem>& left,
    const std::vector<Document::OutlineItem>& right);

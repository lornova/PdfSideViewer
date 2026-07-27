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

// Rows of outline indices, one column per input outline, for every key that
// appears in ALL of them - either a numeric key ("1.2 Title") or a canonical
// title (unnumbered sections, with common front/back matter names matched
// across the fourteen UI languages; title matches additionally require the
// SAME outline depth, so a top-level section never anchors to a nested
// homonym). First occurrence per key and per outline wins; the result follows
// the FIRST outline's document order.
//
// This is a JOIN on a document-independent key, so more outlines simply means
// a smaller intersection, not a different algorithm; with two outlines the
// result is identical to the pairwise version this replaces. Requiring every
// outline to carry the key is what keeps the tuples usable as hard anchors: a
// row with a missing coordinate could satisfy neither the strictly-increasing
// invariant nor the alignment-gap arithmetic.
std::vector<std::vector<int>> MatchOutlineNumberings(
    const std::vector<const std::vector<Document::OutlineItem>*>& outlines);

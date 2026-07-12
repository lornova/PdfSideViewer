#pragma once

#include "engine/Document.h"

#include <optional>
#include <utility>
#include <vector>

// Hierarchical numeric key parsed from a bookmark title ("1.2 Title",
// "Chapter 3", "Sezione 2.1: x", "§ 4"). nullopt = the title does not
// participate in bookmark-based sync-point generation.
std::optional<std::vector<int>> ParseOutlineNumbering(const std::wstring& title);

// "1.2.3" display form of a parsed key (used as the sync-point label).
std::wstring FormatOutlineNumbering(const std::vector<int>& key);

// Pairs of outline indices (left, right) whose numeric keys appear in BOTH
// outlines. First occurrence per key and per side wins; the result follows
// the left outline's document order.
std::vector<std::pair<int, int>> MatchOutlineNumberings(
    const std::vector<Document::OutlineItem>& left,
    const std::vector<Document::OutlineItem>& right);

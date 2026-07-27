#pragma once

#include "framework.h"

#include <optional>
#include <vector>

// XML-over-WM_COPYDATA protocol between PdfSideViewer instances: the Explorer
// verbs and -forward-search spawn a short-lived second instance that hands the
// request to the running one. One dwData magic covers every command; the
// payload is a small UTF-16 XML document built and parsed with XmlLite (in the
// OS since Vista, no COM init required, DTD processing prohibited):
//
//   <psv v="1"><open slot="center" path="C:\doc\b.pdf"/></psv>
//   <psv v="1"><forward tex="C:\doc\a.tex" line="123" pdf="C:\doc\a.pdf"/></psv>
//
// The slot travels as a WORD (kSlotKeys), never a PaneSlot integer: a word
// cannot be reinterpreted if the enum ever renumbers. v versions the whole
// protocol: a receiver REJECTS unknown versions and commands, WM_COPYDATA
// comes back unhandled, and the sender falls back to a cold start - a
// mixed-version handoff degrades to a new window instead of mis-slotting.
// The payload arrives from arbitrary processes: Parse enforces shape and
// caps, and the grammar is CLOSED except in exactly one direction - elements,
// namespaces (none allowed) and character content are strict, while unknown
// ATTRIBUTES are ignored, which is v1's designated extension point (optional
// fields can be added without a version bump). The SEMANTIC checks (rooted
// paths, line range, active-set slot handling) stay with the receiver.
// Delivery is AT-LEAST-ONCE: on a SendMessageTimeout timeout the sender
// cold-starts even though the hung receiver may still complete the original
// request later, so a request can be served twice (a duplicate window, a
// repeated flash). Accepted: deduplication machinery is not worth the two
// benign outcomes.
namespace IpcXml {

constexpr ULONG_PTR kCopyDataId = 0x50535658; // 'PSVX'

struct OpenCommand {
    int slot = 0; // PaneSlot, decoded from the slot word (always in range)
    std::wstring path;
};

struct ForwardCommand {
    std::wstring tex;
    int line = 0; // digits-only on the wire; the receiver range-checks
    std::wstring pdf;
};

// Exactly one member is engaged.
struct Command {
    std::optional<OpenCommand> open;
    std::optional<ForwardCommand> forward;
};

// Serialized payloads ready for COPYDATASTRUCT; empty on failure (the caller
// then skips the handoff and cold-starts, like an unhandled send).
std::vector<BYTE> BuildOpen(int slot, const std::wstring& path);
std::vector<BYTE> BuildForward(const std::wstring& tex, int line, const std::wstring& pdf);

// Structural parse of a received payload. Everything is copied out before
// returning: WM_COPYDATA buffers die when the handler returns.
std::optional<Command> Parse(const void* data, DWORD size);

} // namespace IpcXml

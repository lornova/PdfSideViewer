#include "util/IpcXml.h"

#include "PaneSlots.h"

#include <shlwapi.h> // SHCreateMemStream
#include <xmllite.h>

#include <utility>

namespace {

constexpr PCWSTR kRoot = L"psv";
constexpr PCWSTR kVersionValue = L"1";
// Caps for a hostile sender: same per-string limit as the retired binary
// blobs, and a total far above any real command (two max-length paths fit).
constexpr size_t kMaxAttrChars = 0x8000;
constexpr ULONGLONG kMaxPayloadBytes = 256 * 1024;

// The wire slot words ARE kSlotKeys ("left"/"center"/"right"): both name the
// same stable slots and both are frozen (the keys live in settings.ini, the
// words in the 1.0 wire format), so one table serves.

// One attribute value, copied out; nullopt when absent OR over the cap (a
// required attribute over the cap therefore rejects the whole command).
std::optional<std::wstring> Attr(IXmlReader* reader, PCWSTR name) {
    if (reader->MoveToAttributeByName(name, nullptr) != S_OK)
        return std::nullopt;
    PCWSTR value = nullptr;
    UINT len = 0;
    const HRESULT hr = reader->GetValue(&value, &len);
    std::optional<std::wstring> out;
    if (SUCCEEDED(hr) && value && len <= kMaxAttrChars)
        out.emplace(value, len);
    reader->MoveToElement(); // back to the element for the next lookup
    return out;
}

std::vector<BYTE> Build(PCWSTR command,
                        std::initializer_list<std::pair<PCWSTR, PCWSTR>> attrs) {
    std::vector<BYTE> payload;
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(nullptr, 0)); // Attach: comes refcounted
    if (!stream)
        return payload;
    ComPtr<IXmlWriter> writer;
    if (FAILED(CreateXmlWriter(IID_PPV_ARGS(&writer), nullptr)))
        return payload;
    // UTF-16 output: the strings are wchar_t on both ends, no transcoding.
    ComPtr<IXmlWriterOutput> output;
    if (FAILED(CreateXmlWriterOutputWithEncodingName(stream.Get(), nullptr, L"utf-16",
                                                     &output)) ||
        FAILED(writer->SetOutput(output.Get())))
        return payload;
    HRESULT hr = writer->WriteStartDocument(XmlStandalone_Omit);
    if (SUCCEEDED(hr))
        hr = writer->WriteStartElement(nullptr, kRoot, nullptr);
    if (SUCCEEDED(hr))
        hr = writer->WriteAttributeString(nullptr, L"v", nullptr, kVersionValue);
    if (SUCCEEDED(hr))
        hr = writer->WriteStartElement(nullptr, command, nullptr);
    for (const auto& [name, value] : attrs)
        if (SUCCEEDED(hr))
            hr = writer->WriteAttributeString(nullptr, name, nullptr, value);
    if (SUCCEEDED(hr))
        hr = writer->WriteEndDocument(); // closes both elements
    if (SUCCEEDED(hr))
        hr = writer->Flush();
    if (FAILED(hr))
        return payload;
    STATSTG st{};
    if (FAILED(stream->Stat(&st, STATFLAG_NONAME)) || st.cbSize.QuadPart == 0 ||
        st.cbSize.QuadPart > kMaxPayloadBytes)
        return payload;
    payload.resize(static_cast<size_t>(st.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    ULONG read = 0;
    if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)) ||
        FAILED(stream->Read(payload.data(), static_cast<ULONG>(payload.size()), &read)) ||
        read != payload.size())
        payload.clear();
    return payload;
}

} // namespace

std::vector<BYTE> IpcXml::BuildOpen(int slot, const std::wstring& path) {
    if (slot < 0 || slot >= kPaneSlots || path.empty() || path.size() > kMaxAttrChars)
        return {};
    return Build(L"open", {{L"slot", kSlotKeys[slot]}, {L"path", path.c_str()}});
}

namespace {

std::vector<BYTE> BuildForwardAs(PCWSTR element, const std::wstring& tex, int line,
                                 const std::wstring& pdf) {
    if (tex.empty() || tex.size() > kMaxAttrChars || pdf.empty() ||
        pdf.size() > kMaxAttrChars || line < 1)
        return {};
    const std::wstring lineText = std::to_wstring(line);
    return Build(element,
                 {{L"tex", tex.c_str()}, {L"line", lineText.c_str()}, {L"pdf", pdf.c_str()}});
}

} // namespace

std::vector<BYTE> IpcXml::BuildForward(const std::wstring& tex, int line,
                                       const std::wstring& pdf) {
    return BuildForwardAs(L"forward", tex, line, pdf);
}

std::vector<BYTE> IpcXml::BuildForwardProbe(const std::wstring& tex, int line,
                                            const std::wstring& pdf) {
    return BuildForwardAs(L"forwardprobe", tex, line, pdf);
}

std::optional<IpcXml::Command> IpcXml::Parse(const void* data, DWORD size) {
    if (!data || size < 4 || size > kMaxPayloadBytes)
        return std::nullopt;
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(static_cast<const BYTE*>(data), size));
    if (!stream)
        return std::nullopt;
    ComPtr<IXmlReader> reader;
    if (FAILED(CreateXmlReader(IID_PPV_ARGS(&reader), nullptr)))
        return std::nullopt;
    // Hostile-input posture: DTDs prohibited (the XmlLite default, spelled out
    // because it is load-bearing: no XXE, no entity expansion) and a depth cap
    // that fits nothing but the two-level document above. FAIL CLOSED: if a
    // protection cannot be installed, the payload is not parsed at all.
    if (FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) ||
        FAILED(reader->SetProperty(XmlReaderProperty_MaxElementDepth, 4)) ||
        FAILED(reader->SetInput(stream.Get())))
        return std::nullopt;

    Command out;
    bool sawRoot = false;
    bool haveCommand = false;
    XmlNodeType type = XmlNodeType_None;
    HRESULT hr = S_OK;
    while ((hr = reader->Read(&type)) == S_OK) {
        if (type == XmlNodeType_Text || type == XmlNodeType_CDATA)
            return std::nullopt; // no character content anywhere in the grammar
        if (type != XmlNodeType_Element)
            continue; // decl, whitespace, comments, PIs, end tags
        PCWSTR name = nullptr;
        UINT nameLen = 0;
        if (FAILED(reader->GetLocalName(&name, &nameLen)) || !name)
            return std::nullopt;
        // The vocabulary lives in NO namespace: a protocol-looking element
        // smuggled in under some namespace is not the protocol.
        PCWSTR ns = nullptr;
        UINT nsLen = 0;
        if (FAILED(reader->GetNamespaceUri(&ns, &nsLen)) || nsLen != 0)
            return std::nullopt;
        if (!sawRoot) {
            // Exact root + version match; an unknown version is REJECTED so
            // the sender's timeout/handled check falls back to a cold start.
            const std::optional<std::wstring> v = Attr(reader.Get(), L"v");
            if (wcscmp(name, kRoot) != 0 || !v || *v != kVersionValue)
                return std::nullopt;
            sawRoot = true;
            continue;
        }
        if (haveCommand)
            return std::nullopt; // exactly one command element
        if (wcscmp(name, L"open") == 0) {
            const std::optional<std::wstring> slot = Attr(reader.Get(), L"slot");
            std::optional<std::wstring> path = Attr(reader.Get(), L"path");
            if (!slot || !path || path->empty())
                return std::nullopt;
            int decoded = -1;
            for (int s = 0; s < kPaneSlots; ++s)
                if (*slot == kSlotKeys[s])
                    decoded = s;
            if (decoded < 0)
                return std::nullopt; // closed vocabulary, nothing to reinterpret
            out.open.emplace(OpenCommand{decoded, std::move(*path)});
            haveCommand = true;
        } else if (wcscmp(name, L"forward") == 0 || wcscmp(name, L"forwardprobe") == 0) {
            const bool probe = wcscmp(name, L"forwardprobe") == 0;
            std::optional<std::wstring> tex = Attr(reader.Get(), L"tex");
            const std::optional<std::wstring> line = Attr(reader.Get(), L"line");
            std::optional<std::wstring> pdf = Attr(reader.Get(), L"pdf");
            if (!tex || tex->empty() || !line || line->empty() || !pdf || pdf->empty())
                return std::nullopt;
            int value = 0;
            for (const wchar_t c : *line) { // digits only, no overflow
                if (c < L'0' || c > L'9' || value > 100'000'000)
                    return std::nullopt;
                value = value * 10 + (c - L'0');
            }
            if (value < 1)
                return std::nullopt;
            out.forward.emplace(ForwardCommand{std::move(*tex), value, std::move(*pdf), probe});
            haveCommand = true;
        } else {
            return std::nullopt; // unknown command: unhandled, sender cold-starts
        }
    }
    if (hr != S_FALSE || !haveCommand)
        return std::nullopt; // malformed tail (S_FALSE is the clean EOF)
    return out;
}

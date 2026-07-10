# Generates small hand-built test PDFs with correct xref offsets.
param([string]$OutDir = "D:\Progetti\PdfSideViewer\testdata")

New-Item -ItemType Directory -Force $OutDir | Out-Null

function New-TestPdf {
    param([string]$Path, [array]$Pages, [switch]$WithLinks) # Pages: @{W=..; H=..; Label=..}

    $n = $Pages.Count
    $objs = @()
    # obj 1: catalog, obj 2: pages tree
    $kids = (0..($n-1) | ForEach-Object { "$(3 + 2*$_) 0 R" }) -join ' '
    $fontObj = 3 + 2*$n
    $outlinesRef = if ($WithLinks) { " /Outlines $($fontObj + 3) 0 R" } else { '' }
    $objs += "1 0 obj`n<< /Type /Catalog /Pages 2 0 R$outlinesRef >>`nendobj`n"
    $objs += "2 0 obj`n<< /Type /Pages /Kids [$kids] /Count $n >>`nendobj`n"
    for ($i = 0; $i -lt $n; $i++) {
        $p = $Pages[$i]
        $pageNum = 3 + 2*$i
        $contNum = $pageNum + 1
        $cx = 40; $cy = $p.H - 100
        $border = "1 w 0.2 0.2 0.8 RG 20 20 $($p.W - 40) $($p.H - 40) re S"
        $text = "BT /F1 48 Tf $cx $cy Td ($($p.Label)) Tj ET"
        $mid = "BT /F1 18 Tf 40 $([int]($p.H/2)) Td ($($p.W) x $($p.H) pt) Tj ET"
        $stream = "$border`n$text`n$mid"
        $annots = ''
        if ($WithLinks -and $i -eq 0) {
            # first page: internal link to page 3 (obj 7) + external link
            $annots = " /Annots [$($fontObj + 1) 0 R $($fontObj + 2) 0 R]"
            $stream += "`nBT /F1 14 Tf 40 $($p.H - 250) Td (Vai a pagina 3) Tj ET"
            $stream += "`nBT /F1 14 Tf 40 $($p.H - 350) Td (https://example.com/) Tj ET"
        }
        $len = [System.Text.Encoding]::ASCII.GetByteCount($stream)
        $objs += "$pageNum 0 obj`n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 $($p.W) $($p.H)] /Contents $contNum 0 R /Resources << /Font << /F1 $fontObj 0 R >> >>$annots >>`nendobj`n"
        $objs += "$contNum 0 obj`n<< /Length $len >>`nstream`n$stream`nendstream`nendobj`n"
    }
    $objs += "$fontObj 0 obj`n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>`nendobj`n"
    if ($WithLinks) {
        $h = $Pages[0].H
        # rects are bottom-up: y in [H-260, H-230] wraps the internal-link text
        $objs += "$($fontObj + 1) 0 obj`n<< /Type /Annot /Subtype /Link /Rect [40 $($h - 260) 300 $($h - 230)] /Border [0 0 0] /A << /S /GoTo /D [$(3 + 2*2) 0 R /Fit] >> >>`nendobj`n"
        $objs += "$($fontObj + 2) 0 obj`n<< /Type /Annot /Subtype /Link /Rect [40 $($h - 360) 300 $($h - 330)] /Border [0 0 0] /A << /S /URI /URI (https://example.com/) >> >>`nendobj`n"
        # outline: two top-level chapters, the second with one child
        $o = $fontObj + 3
        $objs += "$o 0 obj`n<< /Type /Outlines /First $($o + 1) 0 R /Last $($o + 2) 0 R /Count 3 >>`nendobj`n"
        $objs += "$($o + 1) 0 obj`n<< /Title (Capitolo 1) /Parent $o 0 R /Next $($o + 2) 0 R /Dest [3 0 R /Fit] >>`nendobj`n"
        $objs += "$($o + 2) 0 obj`n<< /Title (Capitolo 2) /Parent $o 0 R /Prev $($o + 1) 0 R /First $($o + 3) 0 R /Last $($o + 3) 0 R /Count 1 /Dest [5 0 R /Fit] >>`nendobj`n"
        $objs += "$($o + 3) 0 obj`n<< /Title (Sezione 2.1 - pagina 3) /Parent $($o + 2) 0 R /Dest [7 0 R /Fit] >>`nendobj`n"
    }

    $body = "%PDF-1.4`n"
    $offsets = @()
    foreach ($o in $objs) {
        $offsets += [System.Text.Encoding]::ASCII.GetByteCount($body)
        $body += $o
    }
    $xrefPos = [System.Text.Encoding]::ASCII.GetByteCount($body)
    $count = $objs.Count + 1
    $body += "xref`n0 $count`n0000000000 65535 f `n"
    foreach ($off in $offsets) { $body += ("{0:0000000000} 00000 n `n" -f $off) }
    $body += "trailer`n<< /Size $count /Root 1 0 R >>`nstartxref`n$xrefPos`n%%EOF`n"

    [System.IO.File]::WriteAllBytes($Path, [System.Text.Encoding]::ASCII.GetBytes($body))
    Write-Host "written $Path ($([System.Text.Encoding]::ASCII.GetByteCount($body)) bytes)"
}

New-TestPdf -Path (Join-Path $OutDir 'test-a.pdf') -WithLinks -Pages @(
    @{W=595; H=842; Label='A - Pagina 1 (A4)'},
    @{W=842; H=595; Label='A - Pagina 2 (A4 orizz.)'},
    @{W=420; H=595; Label='A - Pagina 3 (A5)'},
    @{W=595; H=842; Label='A - Pagina 4 (A4)'}
)
New-TestPdf -Path (Join-Path $OutDir 'test-b.pdf') -Pages @(
    @{W=612; H=792; Label='B - Pagina 1 (Letter)'},
    @{W=612; H=792; Label='B - Pagina 2 (Letter)'},
    @{W=612; H=792; Label='B - Pagina 3 (Letter)'}
)

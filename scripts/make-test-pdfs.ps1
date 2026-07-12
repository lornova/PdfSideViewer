# Generates small hand-built test PDFs with correct xref offsets.
param([string]$OutDir = "D:\Progetti\PdfSideViewer\testdata")

New-Item -ItemType Directory -Force $OutDir | Out-Null

function New-TestPdf {
    param([string]$Path, [array]$Pages, [switch]$WithLinks, [switch]$WithPageLabels,
          [array]$Outline)
    # Pages: @{W=..; H=..; Label=..}
    # Outline: @{Title=..; Page=<0-based>} - a FLAT bookmark chain with /Fit
    # destinations (page-top targets, deterministic for the sync-point tests).
    # Not combinable with -WithLinks, which hardcodes its own outline.

    $n = $Pages.Count
    $objs = @()
    # obj 1: catalog, obj 2: pages tree
    $kids = (0..($n-1) | ForEach-Object { "$(3 + 2*$_) 0 R" }) -join ' '
    $fontObj = 3 + 2*$n
    $outlinesRef = if ($WithLinks) { " /Outlines $($fontObj + 3) 0 R" }
                   elseif ($Outline) { " /Outlines $($fontObj + 1) 0 R" }
                   else { '' }
    # The labels object is appended LAST: object numbers must stay sequential
    # for the xref builder below (objs[i] is object i+1).
    $labelsNum = if ($WithLinks) { $fontObj + 7 }
                 elseif ($Outline) { $fontObj + 2 + $Outline.Count }
                 else { $fontObj + 1 }
    $labelsRef = if ($WithPageLabels) { " /PageLabels $labelsNum 0 R" } else { '' }
    $objs += "1 0 obj`n<< /Type /Catalog /Pages 2 0 R$outlinesRef$labelsRef >>`nendobj`n"
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
    if ($Outline -and -not $WithLinks) {
        $o = $fontObj + 1
        $cnt = $Outline.Count
        $objs += "$o 0 obj`n<< /Type /Outlines /First $($o + 1) 0 R /Last $($o + $cnt) 0 R /Count $cnt >>`nendobj`n"
        for ($k = 0; $k -lt $cnt; $k++) {
            $item = $Outline[$k]
            $pageObj = 3 + 2 * $item.Page
            $prev = if ($k -gt 0) { " /Prev $($o + $k) 0 R" } else { '' }
            $next = if ($k -lt $cnt - 1) { " /Next $($o + $k + 2) 0 R" } else { '' }
            $objs += "$($o + 1 + $k) 0 obj`n<< /Title ($($item.Title)) /Parent $o 0 R$prev$next /Dest [$pageObj 0 R /Fit] >>`nendobj`n"
        }
    }
    if ($WithPageLabels) {
        # Front matter in lowercase roman (i, ii), then decimal restarting at
        # 1: pages 3..4 get labels "1", "2" that differ from their ordinals.
        $objs += "$labelsNum 0 obj`n<< /Nums [0 << /S /r >> 2 << /S /D >>] >>`nendobj`n"
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

New-TestPdf -Path (Join-Path $OutDir 'test-a.pdf') -WithLinks -WithPageLabels -Pages @(
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

# Numbered-bookmark pair for the sync-points feature (scripts\test-sync-points.ps1).
# Expected generated map for a=left, b=right (0-based pages): [1]->(0,0),
# [1,1]->(1,1), [2]->(4,2). "1.2" exists only in a (skipped); b's "3 Appendice"
# points BACKWARD to page 1 and is dropped by the monotonicity filter.
New-TestPdf -Path (Join-Path $OutDir 'sync-a.pdf') -Pages @(
    @{W=595; H=842; Label='SA - Pagina 1'},
    @{W=595; H=842; Label='SA - Pagina 2'},
    @{W=595; H=842; Label='SA - Pagina 3'},
    @{W=595; H=842; Label='SA - Pagina 4'},
    @{W=595; H=842; Label='SA - Pagina 5'},
    @{W=595; H=842; Label='SA - Pagina 6'}
) -Outline @(
    @{Title='1 Introduzione'; Page=0},
    @{Title='1.1 Prima'; Page=1},
    @{Title='1.2 Seconda'; Page=2},
    @{Title='2 Conclusioni'; Page=4},
    @{Title='3 Appendice'; Page=5}
)
New-TestPdf -Path (Join-Path $OutDir 'sync-b.pdf') -Pages @(
    @{W=612; H=792; Label='SB - Pagina 1'},
    @{W=612; H=792; Label='SB - Pagina 2'},
    @{W=612; H=792; Label='SB - Pagina 3'},
    @{W=612; H=792; Label='SB - Pagina 4'},
    @{W=612; H=792; Label='SB - Pagina 5'},
    @{W=612; H=792; Label='SB - Pagina 6'}
) -Outline @(
    @{Title='Capitolo 1'; Page=0},
    @{Title='Sezione 1.1'; Page=1},
    @{Title='2. Fine'; Page=2},
    @{Title='3 Appendice'; Page=1}
)
# sync-a variant with section "2 Conclusioni" REMOVED: pairing it with sync-b
# yields 2 points instead of 3. test-sync-points.ps1 copies it over its live
# sync-a copy to prove the auto-reload really re-derives the map.
New-TestPdf -Path (Join-Path $OutDir 'sync-a2.pdf') -Pages @(
    @{W=595; H=842; Label='SA2 - Pagina 1'},
    @{W=595; H=842; Label='SA2 - Pagina 2'},
    @{W=595; H=842; Label='SA2 - Pagina 3'},
    @{W=595; H=842; Label='SA2 - Pagina 4'},
    @{W=595; H=842; Label='SA2 - Pagina 5'},
    @{W=595; H=842; Label='SA2 - Pagina 6'}
) -Outline @(
    @{Title='1 Introduzione'; Page=0},
    @{Title='1.1 Prima'; Page=1},
    @{Title='1.2 Seconda'; Page=2},
    @{Title='3 Appendice'; Page=5}
)
# No numbered bookmarks at all: the zero-match partner.
New-TestPdf -Path (Join-Path $OutDir 'sync-c.pdf') -Pages @(
    @{W=595; H=842; Label='SC - Pagina 1'},
    @{W=595; H=842; Label='SC - Pagina 2'}
) -Outline @(
    @{Title='Preambolo'; Page=0},
    @{Title='Indice'; Page=1}
)

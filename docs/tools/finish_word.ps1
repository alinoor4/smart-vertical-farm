# Documentation tooling (the product is C++ only). Opens the report in Word, builds the table of
# contents with its page numbers, and saves the file again. build_report.py runs it when Word is
# installed; without Word, open the report and press F9 on the contents page.
param([Parameter(Mandatory = $true)][string]$Path)
$ErrorActionPreference = 'Stop'
$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0
try {
    $doc = $word.Documents.Open((Resolve-Path -LiteralPath $Path).Path)
    $doc.Repaginate()
    foreach ($toc in @($doc.TablesOfContents)) { $toc.Update() | Out-Null }
    $doc.Repaginate()
    foreach ($toc in @($doc.TablesOfContents)) { $toc.UpdatePageNumbers() | Out-Null }
    "pages: " + $doc.ComputeStatistics(2)
    $doc.Save()
    $doc.Close(0)
}
finally {
    $word.Quit()
}

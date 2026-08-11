param(
    [string]$ReadyFile
)

$ErrorActionPreference = 'Stop'
$Host.UI.RawUI.WindowTitle = 'ESP32 V1.2 USB CDC + MSC Verification'

function ConvertTo-HexString([byte[]]$Bytes) {
    if ($null -eq $Bytes -or $Bytes.Length -eq 0) {
        return '(none)'
    }
    return [System.BitConverter]::ToString($Bytes).Replace('-', ' ')
}

function Test-CdcEcho([string]$Name, [byte[]]$Payload) {
    $port = [System.IO.Ports.SerialPort]::new('COM13', 115200, 'None', 8, 'One')
    $port.ReadTimeout = 1500
    $port.WriteTimeout = 1500
    $port.DtrEnable = $true
    $port.RtsEnable = $false

    try {
        $port.Open()
        Start-Sleep -Milliseconds 500
        $port.DiscardInBuffer()
        $port.Write($Payload, 0, $Payload.Length)
        Start-Sleep -Milliseconds 1000

        $count = $port.BytesToRead
        $received = [byte[]]::new($count)
        if ($count -gt 0) {
            [void]$port.Read($received, 0, $count)
        }

        $txHex = ConvertTo-HexString $Payload
        $rxHex = ConvertTo-HexString $received
        $passed = ($Payload.Length -eq $received.Length) -and ($txHex -eq $rxHex)

        [pscustomobject]@{
            Name = $Name
            TX = $txHex
            RX = $rxHex
            Passed = $passed
        }
    }
    finally {
        if ($port.IsOpen) {
            $port.Close()
        }
        $port.Dispose()
    }
}

Clear-Host
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host ' ESP32 V1.2 USB CDC + MSC HARDWARE VERIFICATION' -ForegroundColor Cyan
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host ('Time       : ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))

$cdc = Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -match 'VID_303A&PID_4002&MI_00'
} | Select-Object -First 1
$msc = Get-PnpDevice -PresentOnly | Where-Object {
    $_.FriendlyName -match 'ESP Recorder MSC'
} | Select-Object -First 1
$volume = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='F:'"

Write-Host ''
Write-Host '[1] Windows enumeration' -ForegroundColor Yellow
Write-Host ('CDC device : ' + $cdc.FriendlyName)
Write-Host ('CDC status : ' + $cdc.Status)
Write-Host ('MSC device : ' + $msc.FriendlyName)
Write-Host ('MSC status : ' + $msc.Status)
Write-Host ('MSC volume : ' + $volume.DeviceID + '  ' + $volume.FileSystem + '  ' + [math]::Round($volume.Size / 1GB, 2) + ' GB')

Write-Host ''
Write-Host '[2] USB CDC raw echo tests (COM13, DTR=ON)' -ForegroundColor Yellow
$textResult = Test-CdcEcho 'Text hello' ([System.Text.Encoding]::ASCII.GetBytes('hello'))
Write-Host ('Text TX    : ' + $textResult.TX)
Write-Host ('Text RX    : ' + $textResult.RX)
Write-Host ('Text result: ' + $(if ($textResult.Passed) { 'PASS' } else { 'FAIL' })) -ForegroundColor $(if ($textResult.Passed) { 'Green' } else { 'Red' })

$binaryResult = Test-CdcEcho 'Binary' ([byte[]](0x00, 0x0D, 0x0A))
Write-Host ('Binary TX  : ' + $binaryResult.TX)
Write-Host ('Binary RX  : ' + $binaryResult.RX)
Write-Host ('Binary result: ' + $(if ($binaryResult.Passed) { 'PASS' } else { 'FAIL' })) -ForegroundColor $(if ($binaryResult.Passed) { 'Green' } else { 'Red' })

Write-Host ''
Write-Host '[3] MSC read/write test (F:)' -ForegroundColor Yellow
$testName = 'CODEX_MSC_SCREENSHOT_TEST_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '.txt'
$testPath = Join-Path 'F:\' $testName
$expected = 'ESP32 CDC+MSC screenshot verification'
[System.IO.File]::WriteAllText($testPath, $expected, [System.Text.Encoding]::ASCII)
$actual = [System.IO.File]::ReadAllText($testPath, [System.Text.Encoding]::ASCII)
$readBackPassed = ($actual -ceq $expected)
$testSize = (Get-Item -LiteralPath $testPath).Length
Remove-Item -LiteralPath $testPath -Force
$deletePassed = -not (Test-Path -LiteralPath $testPath)

Write-Host ('Test file   : ' + $testName)
Write-Host ('File bytes  : ' + $testSize)
Write-Host ('Read-back   : ' + $(if ($readBackPassed) { 'PASS' } else { 'FAIL' })) -ForegroundColor $(if ($readBackPassed) { 'Green' } else { 'Red' })
Write-Host ('Cleanup     : ' + $(if ($deletePassed) { 'PASS' } else { 'FAIL' })) -ForegroundColor $(if ($deletePassed) { 'Green' } else { 'Red' })

$allPassed = $cdc.Status -eq 'OK' -and $msc.Status -eq 'OK' -and
    $textResult.Passed -and $binaryResult.Passed -and
    $readBackPassed -and $deletePassed

Write-Host ''
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host (' OVERALL RESULT: ' + $(if ($allPassed) { 'PASS' } else { 'FAIL' })) -ForegroundColor $(if ($allPassed) { 'Green' } else { 'Red' })
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host ''
if ($ReadyFile) {
    [System.IO.File]::WriteAllText($ReadyFile, $(if ($allPassed) { 'PASS' } else { 'FAIL' }))
}
Write-Host 'Press Enter to close this evidence window.' -ForegroundColor DarkGray
[void](Read-Host)

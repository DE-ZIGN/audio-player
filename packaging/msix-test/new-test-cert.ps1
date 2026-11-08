# Creates a self-signed code-signing certificate for the S2 test package in
# the current user's personal store and exports its public part (.cer).
# Run as a normal user. Trusting the certificate is a separate, manual step
# that needs an administrator (see README.md).

param(
    [string]$Subject = "CN=AudioPlayer Test",
    [string]$OutDir = (Join-Path $PSScriptRoot "..\..\build\msix-test")
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $OutDir | Out-Null

$existing = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $Subject -and $_.NotAfter -gt (Get-Date) }
if ($existing) {
    $cert = $existing | Select-Object -First 1
    Write-Host "既存の証明書を使います: $($cert.Thumbprint)"
} else {
    $cert = New-SelfSignedCertificate -Type Custom -Subject $Subject `
        -KeyUsage DigitalSignature -FriendlyName "AudioPlayer S2 test signing" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}") `
        -NotAfter (Get-Date).AddMonths(6)
    Write-Host "証明書を作りました: $($cert.Thumbprint)"
}

$cerPath = Join-Path $OutDir "ap-test.cer"
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null
Write-Host "公開鍵を書き出しました: $((Resolve-Path $cerPath).Path)"
Write-Output $cert.Thumbprint

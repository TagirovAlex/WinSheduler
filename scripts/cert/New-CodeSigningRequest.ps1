# Генерация PKCS#10 запроса на сертификат подписи кода для AD CS.
# Запускать под учётной записью, которая будет подписывать MSI/exe:
# закрытый ключ создаётся в профиле текущего пользователя и не экспортируется.
[CmdletBinding()]
param(
    [string]$Subject = 'CN=WinSheduler Code Signing, OU=IT, O=WinSheduler, C=RU',
    [string]$Template = 'CodeSigning',
    [int]$KeyLength = 3072,
    [string]$OutputDir = './temp/cert'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out = if ([System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $root $OutputDir }
New-Item -ItemType Directory -Path $out -Force | Out-Null

$inf = Join-Path $out 'CodeSigningRequest.inf'
$req = Join-Path $out 'WinShedulerCodeSigning.req'
$b64 = Join-Path $out 'WinShedulerCodeSigning.b64'

$infText = @"
[Version]
Signature="`$Windows NT`$"

[NewRequest]
Subject = "$Subject"
KeySpec = 2
KeyLength = $KeyLength
Exportable = FALSE
MachineKeySet = FALSE
PrivateKeyArchive = FALSE
UseExistingKeySet = FALSE
ProviderName = "Microsoft Enhanced RSA and AES Cryptographic Provider"
ProviderType = 24
RequestType = PKCS10
KeyUsage = 0x80
SMIME = FALSE
HashAlgorithm = SHA256

[EnhancedKeyUsageExtension]
OID = 1.3.6.1.5.5.7.3.3

[RequestAttributes]
CertificateTemplate = $Template
"@

# INF для certreq — ASCII.
[System.IO.File]::WriteAllText($inf, $infText, [System.Text.Encoding]::ASCII)

if (Test-Path -LiteralPath $req) { Remove-Item -LiteralPath $req -Force }
& certreq -new $inf $req
if ($LASTEXITCODE -ne 0) { throw "certreq -new завершился с кодом $LASTEXITCODE" }

# certreq -new уже пишет PKCS#10 в base64 (PEM). Для веб-заявки
# оставляем только тело между BEGIN/END, без повторного кодирования.
$pem = Get-Content -LiteralPath $req
$body = $pem | Where-Object { $_ -notmatch '^-----' }
[System.IO.File]::WriteAllLines($b64, [string[]]$body, [System.Text.Encoding]::ASCII)

Write-Host ''
Write-Host 'Запрос создан:' -ForegroundColor Green
Write-Host "  INF:   $inf"
Write-Host "  REQ:   $req"
Write-Host "  B64:   $b64"
Write-Host ''
Write-Host 'Дальше:'
Write-Host '  1. Отправить .req/.b64 в AD CS (веб-заявка или certreq -submit).'
Write-Host '  2. Шаблон сертификата: Code Signing, EKU 1.3.6.1.5.5.7.3.3.'
Write-Host '  3. После выпуска установить .cer: certreq -accept -user <файл.cer>'
Write-Host '  4. Проверить: certutil -user -store My'

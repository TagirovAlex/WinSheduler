<#
.SYNOPSIS
    Генерирует WiX-фрагмент с компонентами для каталога публикации UI.

.DESCRIPTION
    Перечисляет файлы self-contained публикации (включая подкаталоги runtimes
    и культур), создаёт по компоненту на файл и детерминированные GUID
    (MD5 от относительного пути). Это заменяет heat и не требует сети.

    Результат подключается к пакету как дополнительный .wxs и не коммитится.

.PARAMETER SourceDirectory
    Каталог публикации (например, ./artifacts/ui).

.PARAMETER DirectoryId
    Id корневого каталога в MSI (например, UIDIR).

.PARAMETER ComponentGroupId
    Id группы компонентов (например, UiComponents).

.PARAMETER OutputPath
    Путь к сгенерированному .wxs (внутри ./temp/).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][string]$DirectoryId,
    [Parameter(Mandatory = $true)][string]$ComponentGroupId,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'

$source = (Resolve-Path -LiteralPath $SourceDirectory).Path
$files = @(Get-ChildItem -LiteralPath $source -Recurse -File | Sort-Object FullName)

if ($files.Count -eq 0) {
    throw "Каталог публикации пуст: $SourceDirectory"
}

function Get-ShortHash {
    param([string]$Text)

    $sha = [System.Security.Cryptography.SHA1]::Create()
    try {
        $bytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Text.ToLowerInvariant()))
    }
    finally {
        $sha.Dispose()
    }

    return ([System.BitConverter]::ToString($bytes) -replace '-', '').Substring(0, 8)
}

function Get-ComponentGuid {
    param([string]$Text)

    $md5 = [System.Security.Cryptography.MD5]::Create()
    try {
        $hash = $md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Text.ToLowerInvariant()))
    }
    finally {
        $md5.Dispose()
    }

    return ([guid]::new($hash)).ToString('B').ToUpperInvariant()
}

function Get-SafeId {
    param(
        [string]$Text,
        [string]$Prefix
    )

    $safe = $Text -replace '[^A-Za-z0-9_.]', '_'
    if ($safe.Length -gt 40) {
        $safe = $safe.Substring(0, 40)
    }

    return '{0}_{1}_{2}' -f $Prefix, $safe, (Get-ShortHash $Text)
}

function ConvertTo-XmlText {
    param([string]$Text)

    return [System.Security.SecurityElement]::Escape($Text)
}

# Относительные пути с прямыми слэшами.
$entries = foreach ($file in $files) {
    $relative = $file.FullName.Substring($source.Length + 1) -replace '\\', '/'
    $relativeDirectory = [System.IO.Path]::GetDirectoryName($relative) -replace '\\', '/'

    [pscustomobject]@{
        FullName          = $file.FullName -replace '\\', '/'
        Relative          = $relative
        RelativeDirectory = if ($relativeDirectory) { $relativeDirectory } else { '' }
    }
}

# Набор каталогов, включая всех предков.
$directories = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $entries) {
    $current = $entry.RelativeDirectory
    while ($current) {
        if (-not $directories.Add($current)) {
            break
        }
        $parent = [System.IO.Path]::GetDirectoryName($current) -replace '\\', '/'
        $current = if ($parent) { $parent } else { '' }
    }
}

function Get-DirectoryId {
    param([string]$Relative)

    if (-not $Relative) {
        return $DirectoryId
    }

    return Get-SafeId -Text $Relative -Prefix $DirectoryId
}

function Write-DirectoryTree {
    param(
        [string]$ParentRelative,
        [int]$Indent
    )

    $children = $directories |
        Where-Object {
            $parent = [System.IO.Path]::GetDirectoryName($_) -replace '\\', '/'
            if (-not $parent) { $parent = '' }
            $parent -eq $ParentRelative
        } |
        Sort-Object

    foreach ($child in $children) {
        $name = [System.IO.Path]::GetFileName($child)
        $padding = ' ' * $Indent
        $script:lines.Add("$padding<Directory Id=""$(Get-DirectoryId $child)"" Name=""$(ConvertTo-XmlText $name)"">")
        Write-DirectoryTree -ParentRelative $child -Indent ($Indent + 2)
        $script:lines.Add("$padding</Directory>")
    }
}

$script:lines = [System.Collections.Generic.List[string]]::new()
$script:lines.Add('<?xml version="1.0" encoding="utf-8"?>')
$script:lines.Add('<!-- Сгенерировано ./scripts/installer/gen-components.ps1; не редактировать вручную. -->')
$script:lines.Add('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">')
$script:lines.Add('  <Fragment>')

if ($directories.Count -gt 0) {
    $script:lines.Add("    <DirectoryRef Id=""$DirectoryId"">")
    Write-DirectoryTree -ParentRelative '' -Indent 6
    $script:lines.Add('    </DirectoryRef>')
}

$script:lines.Add("    <ComponentGroup Id=""$ComponentGroupId"">")

foreach ($entry in $entries) {
    $componentId = Get-SafeId -Text $entry.Relative -Prefix 'cmp'
    $fileId = Get-SafeId -Text $entry.Relative -Prefix 'fil'
    $guid = Get-ComponentGuid -Text $entry.Relative
    $directory = Get-DirectoryId $entry.RelativeDirectory

    $script:lines.Add("      <Component Id=""$componentId"" Directory=""$directory"" Guid=""$guid"" Win64=""yes"">")
    $script:lines.Add("        <File Id=""$fileId"" Source=""$(ConvertTo-XmlText $entry.FullName)"" KeyPath=""yes"" />")
    $script:lines.Add('      </Component>')
}

$script:lines.Add('    </ComponentGroup>')
$script:lines.Add('  </Fragment>')
$script:lines.Add('</Wix>')

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

[System.IO.File]::WriteAllLines($OutputPath, $script:lines, [System.Text.UTF8Encoding]::new($false))
Write-Host "    Компонентов: $($entries.Count), каталогов: $($directories.Count) -> $OutputPath"

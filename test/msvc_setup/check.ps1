# Runs each MSVC setup script with Visual Studio found at a path containing !
# Usage: powershell -File test/msvc_setup/check.ps1

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
$work = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid())
$realPf86 = ${env:ProgramFiles(x86)}
$realVs = & "$realPf86\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath

# A Program Files (x86) whose vswhere reports the install through a junction named vs!x
$pf86 = "$work\pf86"
$vs = "$work\vs!x"
New-Item -ItemType Directory -Force "$pf86\Microsoft Visual Studio\Installer" | Out-Null
New-Item -ItemType Junction -Path $vs -Target $realVs | Out-Null
Get-ChildItem $realPf86 -Directory | Where-Object Name -ne "Microsoft Visual Studio" |
   ForEach-Object { New-Item -ItemType Junction -Path "$pf86\$($_.Name)" -Target $_.FullName | Out-Null }
Get-ChildItem "$realPf86\Microsoft Visual Studio" -Directory | Where-Object Name -ne "Installer" |
   ForEach-Object { New-Item -ItemType Junction -Path "$pf86\Microsoft Visual Studio\$($_.Name)" -Target $_.FullName | Out-Null }
Set-Content -Encoding ascii "$work\vswhere.cs" "class P { static void Main() { System.Console.WriteLine(@`"$vs`"); } }"
& "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /out:"$pf86\Microsoft Visual Studio\Installer\vswhere.exe" "$work\vswhere.cs"

${env:ProgramFiles(x86)} = $pf86
$env:msvc_host_arch = "x64"
$failures = 0
# VsDevCmd writes to stderr, which Stop would turn into an error
$ErrorActionPreference = "Continue"
foreach ($script in "msvc-setup", "msvc64-setup", "msvc-arm64-setup", "msvc-winrt-setup", "msvc-winrt64-setup", "msvc-winrt-arm64-setup") {
   $out = cmd /c "$root\toolchain\$script.bat" 2>&1 | Out-String
   if ($out -match "(?m)^HXCPP_VARS" -and $out -match [regex]::Escape("VSINSTALLDIR=$vs\")) {
      "ok   $script"
   } else {
      "FAIL $script"
      $failures++
   }
}
${env:ProgramFiles(x86)} = $realPf86

# On an ARM64 machine msvc_host_arch lists the compilers' hosts best first, and
# the ARM64 scripts call VsDevCmd for the first with a compiler for arm64
# installed: the native one when there is one
$arm64Hosts = "arm64", "x64", "x86"
$env:msvc_host_arch = $arm64Hosts -join " "
$vcTools = (Get-Content "$realVs\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt").Trim()
$expect = $arm64Hosts | Where-Object { Test-Path "$realVs\VC\Tools\MSVC\$vcTools\bin\Host$_\arm64\cl.exe" } | Select-Object -First 1
if ($null -eq $expect) {
   "skip the ARM64 scripts on an ARM64 machine: this Visual Studio has no compiler for arm64"
}
else {
   foreach ($script in "msvc-arm64-setup", "msvc-winrt-arm64-setup") {
      $out = cmd /c "$root\toolchain\$script.bat" 2>&1 | Out-String
      if ($out -match "(?m)^HXCPP_VARS" -and $out -match "(?m)^VSCMD_ARG_HOST_ARCH=$expect\s*$") {
         "ok   $script on an ARM64 machine uses the $expect-hosted compiler"
      } else {
         "FAIL $script on an ARM64 machine: expected the $expect-hosted compiler"
         $out -split "`r?`n" | Where-Object { $_ -match "^(Error|VSCMD_ARG_HOST_ARCH|HXCPP_VARS)" }
         $failures++
      }
   }
}

# HXCPP_MSVC_VER names an install by its Common7\Tools folder, and the ARM64
# script runs that install's vcvars script for the host. A stand-in install
# whose vcvars scripts only say which one ran, named as Visual Studio names them,
# with a compiler for arm64 on each host until the cases below take them away
$fake = "$work\fake vs"
New-Item -ItemType Directory -Force "$fake\Common7\Tools", "$fake\VC\Auxiliary\Build" | Out-Null
foreach ($name in "x86_amd64", "x86_arm64", "amd64_x86", "amd64_arm64", "arm64") {
   Set-Content -Encoding ascii "$fake\VC\Auxiliary\Build\vcvars$name.bat" "@set VCVARS_RAN=$name"
}
Set-Content -Encoding ascii "$fake\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt" "14.99.0"
foreach ($h in $arm64Hosts) {
   New-Item -ItemType Directory -Force "$fake\VC\Tools\MSVC\14.99.0\bin\Host$h\arm64" | Out-Null
   Set-Content -Encoding ascii "$fake\VC\Tools\MSVC\14.99.0\bin\Host$h\arm64\cl.exe" ""
}
$env:HXCPP_MSVC = "$fake\Common7\Tools\"
foreach ($case in @(
      @{ host = "x64"; expect = "amd64_arm64"; on = "an x64 host" },
      @{ host = "x86"; expect = "x86_arm64"; on = "an x86 host" },
      @{ host = $arm64Hosts -join " "; expect = "arm64"; on = "an ARM64 host" },
      @{ host = $arm64Hosts -join " "; without = "arm64"; expect = "amd64_arm64"; on = "an ARM64 host without the ARM64-hosted compiler" },
      @{ host = $arm64Hosts -join " "; without = "x64"; expect = "x86_arm64"; on = "an ARM64 host with only the x86-hosted compiler" })) {
   if ($case.ContainsKey("without")) { Remove-Item "$fake\VC\Tools\MSVC\14.99.0\bin\Host$($case.without)\arm64\cl.exe" }
   $env:msvc_host_arch = $case.host
   $out = cmd /c "$root\toolchain\msvc-arm64-setup.bat" 2>&1 | Out-String
   if ($out -match "(?m)^HXCPP_VARS" -and $out -match "(?m)^VCVARS_RAN=$($case.expect)\s*$") {
      "ok   msvc-arm64-setup with HXCPP_MSVC on $($case.on)"
   } else {
      "FAIL msvc-arm64-setup with HXCPP_MSVC on $($case.on): expected vcvars$($case.expect).bat"
      $out -split "`r?`n" | Where-Object { $_ -match "^(Error|VCVARS_RAN|HXCPP_VARS)" }
      $failures++
   }
}
Remove-Item Env:HXCPP_MSVC

Remove-Item -Recurse -Force $work
exit $failures

setlocal
@set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
@set InstallDir=
@if exist "%vswhere%" for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.arm64 -property installationPath`) do @set "InstallDir=%%i"
@rem The first host in msvc_host_arch with a compiler for arm64 installed, as msvc-arm64-setup.bat picks it
@set "vctools="
@if exist "%InstallDir%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt" set /p vctools=<"%InstallDir%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
@set "host="
@for %%h in (%msvc_host_arch%) do @if not defined host if exist "%InstallDir%\VC\Tools\MSVC\%vctools%\bin\Host%%h\arm64\cl.exe" set "host=%%h"
@if not defined host for /f "tokens=1" %%h in ("%msvc_host_arch%") do @set "host=%%h"
@if exist "%InstallDir%\Common7\Tools\VsDevCmd.bat" (
	@call "%InstallDir%\Common7\Tools\VsDevCmd.bat" -host_arch=%host% -arch=arm64 -app_platform=UWP -no_logo
	@echo HXCPP_VARS
	@set
) else (
	echo Error: Could not find the Visual Studio ARM64 tools for WinRT
)

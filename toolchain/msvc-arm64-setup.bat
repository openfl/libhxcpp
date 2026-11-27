setlocal
@set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
@set InstallDir=
@if exist "%vswhere%" for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.arm64 -property installationPath`) do @set "InstallDir=%%i"
@if exist "%HXCPP_MSVC%\..\..\VC\" set "InstallDir=%HXCPP_MSVC%\..\.."
@rem msvc_host_arch lists the compilers' hosts best first, and the first with a
@rem compiler for arm64 installed is used: on an ARM64 machine the native one,
@rem then the x64-hosted one, which Windows runs emulated with a 64-bit address
@rem space, and last the x86-hosted one, emulated in 32 bits, which can run out
@rem of memory on a precompiled header. With none found, the first is used
@set "vctools="
@if exist "%InstallDir%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt" set /p vctools=<"%InstallDir%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
@set "host="
@for %%h in (%msvc_host_arch%) do @if not defined host if exist "%InstallDir%\VC\Tools\MSVC\%vctools%\bin\Host%%h\arm64\cl.exe" set "host=%%h"
@if not defined host for /f "tokens=1" %%h in ("%msvc_host_arch%") do @set "host=%%h"
@rem Visual Studio names the x64 host amd64 in its vcvars scripts, and the ARM64 host's own script vcvarsarm64
@set "vcvars=vcvars%host%_arm64.bat"
@if /i "%host%"=="x64" set "vcvars=vcvarsamd64_arm64.bat"
@if /i "%host%"=="arm64" set "vcvars=vcvarsarm64.bat"
@if exist "%HXCPP_MSVC%\..\..\VC\" (
	@if not exist "%hxcpp_msvc%\..\..\VC\Auxiliary\Build\%vcvars%" (
		@echo Error: the specified MSVC version does not have %vcvars% setup script
	) else (
		@echo "%HXCPP_MSVC%"
		@call "%HXCPP_MSVC%\..\..\VC\Auxiliary\Build\%vcvars%"
		@echo HXCPP_VARS
		@set
	)
) else if exist "%vswhere%" (
	@if exist "%InstallDir%\Common7\Tools\VsDevCmd.bat" (
		@call "%InstallDir%\Common7\Tools\VsDevCmd.bat" -host_arch=%host% -arch=arm64 -app_platform=Desktop -no_logo
		@echo HXCPP_VARS
		@set
	) else (
		echo Warning: Could not find Visual Studio VsDevCmd
	)
) else (
	echo Error: arm64 is not automatically supported for this version of VC.
)

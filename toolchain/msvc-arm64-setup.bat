setlocal
@set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
@set InstallDir=
@if exist "%vswhere%" for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.arm64 -property installationPath`) do @set "InstallDir=%%i"
@rem Visual Studio names the x64 host amd64 in its vcvars scripts
@set "vcvars_host=%msvc_host_arch%"
@if /i "%vcvars_host%"=="x64" set "vcvars_host=amd64"
@if exist "%HXCPP_MSVC%\..\..\VC\" (
	@if not exist "%hxcpp_msvc%\..\..\VC\Auxiliary\Build\vcvars%vcvars_host%_arm64.bat" (
		@echo Error: the specified MSVC version does not have vcvars%vcvars_host%_arm64.bat setup script
	) else (
		@echo "%HXCPP_MSVC%"
		@call "%HXCPP_MSVC%\..\..\VC\Auxiliary\Build\vcvars%vcvars_host%_arm64.bat"
		@echo HXCPP_VARS
		@set
	)
) else if exist "%vswhere%" (
	@if exist "%InstallDir%\Common7\Tools\VsDevCmd.bat" (
		@call "%InstallDir%\Common7\Tools\VsDevCmd.bat" -host_arch=%msvc_host_arch% -arch=arm64 -app_platform=Desktop -no_logo
		@echo HXCPP_VARS
		@set
	) else (
		echo Warning: Could not find Visual Studio VsDevCmd
	)
) else (
	echo Error: arm64 is not automatically supported for this version of VC.
)

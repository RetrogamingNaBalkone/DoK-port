param([switch]$Headless, [switch]$Fresh, [string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$taskZig = Join-Path $env:LOCALAPPDATA 'Programs/Zig/0.13.0/zig.exe'
$taskSdk = Join-Path $env:LOCALAPPDATA 'Programs/SDL2/SDL2-2.32.10/x86_64-w64-mingw32'
$taskCmake = (& py -3.14 -c 'import cmake; print(cmake.CMAKE_BIN_DIR)').Trim()
$taskNinja = (& py -3.14 -c 'import ninja; print(ninja.BIN_DIR)').Trim()
if (!(Test-Path -LiteralPath $taskZig)) { throw 'Shared Zig 0.13.0 installation missing' }
$taskBuild = Join-Path $taskRoot 'build/sdl2/windows'
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $taskRoot 'build/sdl2/zig-cache'
New-Item -ItemType Directory -Force -Path $taskBuild | Out-Null
$taskToolchain = Join-Path $taskBuild 'zig-toolchain.cmake'
$taskZig = $taskZig.Replace('\','/')
$taskRules = (Join-Path $taskRoot 'ports/sdl2/cmake/zig-rules.cmake').Replace('\','/')
$taskToolchainText = @"
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_CXX_COMPILER "$taskZig")
set(CMAKE_CXX_COMPILER_ARG1 "c++")
set(CMAKE_CXX_FLAGS_INIT "-target x86_64-windows-gnu")
set(CMAKE_AR "$taskZig")
set(CMAKE_RANLIB "$taskZig")
set(CMAKE_USER_MAKE_RULES_OVERRIDE "$taskRules")
"@
[IO.File]::WriteAllText($taskToolchain, $taskToolchainText, [Text.UTF8Encoding]::new($false))
$taskApp = if ($Headless) {'OFF'} else {'ON'}
[string[]]$taskFreshArgs = @()
if ($Fresh) { $taskFreshArgs = @('--fresh') }
& "$taskCmake/cmake.exe" @taskFreshArgs -S "$taskRoot/ports/sdl2" -B $taskBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$taskNinja/ninja.exe" "-DCMAKE_TOOLCHAIN_FILE=$taskToolchain" "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_PREFIX_PATH=$taskSdk" "-DDOK_BUILD_APP=$taskApp"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& "$taskCmake/cmake.exe" --build $taskBuild --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& "$taskCmake/ctest.exe" --test-dir $taskBuild --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest failed' }

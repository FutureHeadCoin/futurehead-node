$ErrorActionPreference = "Continue"

if (${env:artifact} -eq 1) {
    if ( ${env:BETA} -eq 1 ) {
        $env:NETWORK_CFG = "beta"
        $env:BUILD_TYPE = "RelWithDebInfo"
    }
    else {
        $env:NETWORK_CFG = "live"
        $env:BUILD_TYPE = "Release"
    }
    $env:FUTUREHEAD_SHARED_BOOST = "ON"
    $env:ROCKS_LIB = '-DROCKSDB_LIBRARIES="c:\vcpkg\installed\x64-windows-static\lib\rocksdb.lib"'
    $env:FUTUREHEAD_TEST = "-DFUTUREHEAD_TEST=OFF"
    $env:TRAVIS_TAG = ${env:TAG}
    
    $env:CI = "-DCI_BUILD=ON"
    $env:RUN = "artifact"
}
else {
    if ( ${env:RELEASE} -eq 1 ) {
        $env:BUILD_TYPE = "RelWithDebInfo"
        $env:ROCKS_LIB = '-DROCKSDB_LIBRARIES="c:\vcpkg\installed\x64-windows-static\lib\rocksdb.lib"'
    }
    else { 
        $env:BUILD_TYPE = "Debug"
        $env:ROCKS_LIB = '-DROCKSDB_LIBRARIES="c:\vcpkg\installed\x64-windows-static\debug\lib\rocksdbd.lib"'
    }
    $env:FUTUREHEAD_SHARED_BOOST = "OFF"
    $env:NETWORK_CFG = "test"
    $env:FUTUREHEAD_TEST = "-DFUTUREHEAD_TEST=ON"
    $env:CI = '-DCI_TEST="1"'
    $env:RUN = "test"
}

mkdir build
Push-Location build
$env:BOOST_ROOT = ${env:BOOST_ROOT_1_69_0}

#accessibility of Boost dlls for generating config samples
$ENV:PATH = "$ENV:PATH;$ENV:BOOST_ROOT\lib"

& ..\ci\actions\windows\configure.bat
if (${LastExitCode} -ne 0) {
    throw "Failed to configure"
}

if (${env:RUN} -eq "artifact") {
    $p = Get-Location
    Invoke-WebRequest -Uri https://aka.ms/vs/16/release/vc_redist.x64.exe -OutFile "$p\vc_redist.x64.exe"
}

& ..\ci\actions\windows\build.bat
if (${LastExitCode} -ne 0) {
    throw "Failed to build ${env:RUN}"
}
$env:cmake_path = Split-Path -Path(get-command cmake.exe).Path
. "$PSScriptRoot\signing.ps1"

& ..\ci\actions\windows\run.bat
if (${LastExitCode} -ne 0) {
    throw "Failed to Pass Test ${env:RUN}"
}

Pop-Location
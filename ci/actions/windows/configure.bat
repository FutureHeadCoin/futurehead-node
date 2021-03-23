@echo off
set exit_code=0

echo "BUILD TYPE %BUILD_TYPE%"
echo "RUN %RUN%"

cmake .. ^
  -Ax64 ^
  %FUTUREHEAD_TEST% ^
  %CI% ^
  -DFUTUREHEAD_ROCKSDB=ON ^
  %ROCKS_LIB% ^
  -DROCKSDB_INCLUDE_DIRS="c:\vcpkg\installed\x64-windows-static\include" ^
  -DZLIB_LIBRARY_RELEASE="c:\vcpkg\installed\x64-windows-static\lib\zlib.lib" ^
  -DZLIB_LIBRARY_DEBUG="c:\vcpkg\installed\x64-windows-static\debug\lib\zlibd.lib" ^
  -DZLIB_INCLUDE_DIR="c:\vcpkg\installed\x64-windows-static\include" ^
  -DQt5_DIR="c:\qt\5.13.1\msvc2017_64\lib\cmake\Qt5" ^
  -DFUTUREHEAD_GUI=ON ^
  -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
  -DACTIVE_NETWORK=futurehead_%NETWORK_CFG%_network ^
  -DFUTUREHEAD_SIMD_OPTIMIZATIONS=TRUE ^
  -Dgtest_force_shared_crt=on ^
  -DBoost_NO_SYSTEM_PATHS=TRUE ^
  -DBoost_NO_BOOST_CMAKE=TRUE ^
  -DFUTUREHEAD_SHARED_BOOST=%FUTUREHEAD_SHARED_BOOST%

set exit_code=%errorlevel%
if %exit_code% neq 0 goto exit

:exit
exit /B %exit_code%
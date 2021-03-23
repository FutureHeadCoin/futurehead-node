#!/bin/bash

qt_dir=${1}
src_dir=${2}

set -o errexit
set -o nounset
set -o xtrace
OS=`uname`

mkdir build
pushd build

if [[ "${BETA:-0}" -eq 1 ]]; then
    NETWORK_CFG="beta"
    CONFIGURATION="RelWithDebInfo"
else
    NETWORK_CFG="live"
    CONFIGURATION="Release"
fi

cmake \
    -G'Unix Makefiles' \
    -DACTIVE_NETWORK=futurehead_${NETWORK_CFG}_network \
    -DFUTUREHEAD_POW_SERVER=ON \
    -DFUTUREHEAD_GUI=ON \
    -DFUTUREHEAD_ROCKSDB=ON \
    -DROCKSDB_LIBRARIES=/tmp/rocksdb/lib/librocksdb.a \
    -DROCKSDB_INCLUDE_DIRS=/tmp/rocksdb/include \
    -DCMAKE_BUILD_TYPE=${CONFIGURATION} \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DBOOST_ROOT=/tmp/boost/ \
    -DFUTUREHEAD_SHARED_BOOST=ON \
    -DQt5_DIR=${qt_dir} \
    -DCI_BUILD=true \
    ..

if [[ "$OS" == 'Linux' ]]; then
    cmake --build ${PWD} --target package --config ${CONFIGURATION} -- -j$(nproc)
else
    sudo cmake --build ${PWD} --target package --config ${CONFIGURATION} -- -j2
fi

popd

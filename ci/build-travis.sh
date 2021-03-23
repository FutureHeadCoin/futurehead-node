#!/bin/bash

qt_dir=${1}
src_dir=${2}

set -o errexit
set -o nounset
set -o xtrace
OS=`uname`

# This is to prevent out of scope access in async_write from asio which is not picked up by static analysers
if [[ $(grep -rl --exclude="*asio.hpp" "asio::async_write" ./futurehead) ]]; then
    echo "Using boost::asio::async_write directly is not permitted (except in futurehead/lib/asio.hpp). Use futurehead::async_write instead"
    exit 1
fi

# prevent unsolicited use of std::lock_guard & std::unique_lock outside of allowed areas
if [[ $(grep -rl --exclude={"*random_pool.cpp","*random_pool.hpp","*random_pool_shuffle.hpp","*locks.hpp","*locks.cpp"} "std::unique_lock\|std::lock_guard\|std::condition_variable" ./futurehead) ]]; then
    echo "Using std::unique_lock, std::lock_guard or std::condition_variable is not permitted (except in futurehead/lib/locks.hpp and non-futurehead dependent libraries). Use the futurehead::* versions instead"
    exit 1
fi

if [[ $(grep -rlP "^\s*assert \(" ./futurehead) ]]; then
    echo "Using assert is not permitted. Use debug_assert instead."
    exit 1
fi

# prevent unsolicited use of std::lock_guard & std::unique_lock outside of allowed areas
mkdir build
pushd build

if [[ ${RELEASE-0} -eq 1 ]]; then
    BUILD_TYPE="RelWithDebInfo"
else
    BUILD_TYPE="Debug"
fi

if [[ ${ASAN_INT-0} -eq 1 ]]; then
    SANITIZERS="-DFUTUREHEAD_ASAN_INT=ON"
elif [[ ${ASAN-0} -eq 1 ]]; then
    SANITIZERS="-DFUTUREHEAD_ASAN=ON"
elif [[ ${TSAN-0} -eq 1 ]]; then
    SANITIZERS="-DFUTUREHEAD_TSAN=ON"
else
    SANITIZERS=""
fi

ulimit -S -n 8192

if [[ "$OS" == 'Linux' ]]; then
    ROCKSDB="-DROCKSDB_LIBRARIES=/tmp/rocksdb/lib/librocksdb.a \
    -DROCKSDB_INCLUDE_DIRS=/tmp/rocksdb/include"
    if clang --version; then
        BACKTRACE="-DFUTUREHEAD_STACKTRACE_BACKTRACE=ON \
        -DBACKTRACE_INCLUDE=</tmp/backtrace.h>"
    else
        BACKTRACE="-DFUTUREHEAD_STACKTRACE_BACKTRACE=ON"
    fi
else
    ROCKSDB=""
    BACKTRACE=""
fi

cmake \
    -G'Unix Makefiles' \
    -DACTIVE_NETWORK=futurehead_test_network \
    -DFUTUREHEAD_TEST=ON \
    -DFUTUREHEAD_GUI=ON \
    -DFUTUREHEAD_ROCKSDB=ON \
    ${ROCKSDB} \
    -DFUTUREHEAD_WARN_TO_ERR=ON \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DBOOST_ROOT=/tmp/boost/ \
    -DFUTUREHEAD_SHARED_BOOST=ON \
    -DQt5_DIR=${qt_dir} \
    -DCI_TEST="1" \
    ${BACKTRACE} \
    ${SANITIZERS} \
    ..

if [[ "$OS" == 'Linux' ]]; then
    cmake --build ${PWD} -- -j2
else
    sudo cmake --build ${PWD} -- -j2
fi

popd

./ci/test.sh ./build

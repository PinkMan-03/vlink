#!/usr/bin/env bash

set -e

# if grep -q "Ubuntu" /etc/issue; then
#     apt-get update && apt-get install -yq --no-install-recommends \
#         libboost-log1.74-dev \
#         libboost-system1.74-dev \
#         libboost-thread1.74-dev \
#         libboost-filesystem1.74-dev
# fi

BUILD_DIR=${BUILD_DIR:-/opt/thirdparty}
mkdir -p $BUILD_DIR && chown $USER_UID:$USER_UID $BUILD_DIR
cd $BUILD_DIR

# download
git clone -b v3.21.12 --depth=1 https://github.com/protocolbuffers/protobuf.git
git clone -b v0.7-4 --depth=1 https://github.com/foonathan/memory.git
git clone -b v1.1.1 --depth=1 https://github.com/eProsima/Fast-CDR.git
git clone -b v2.10.7 --depth=1 https://github.com/eProsima/Fast-DDS.git
git clone -b 3.4.10 --depth=1 https://github.com/COVESA/vsomeip.git
git clone -b v2.0.6 --depth=1 https://github.com/eclipse-iceoryx/iceoryx.git
git clone -b 0.10.5 --depth=1 https://github.com/eclipse-cyclonedds/cyclonedds.git
git clone -b v5.4.0 --depth=1 https://gitee.com/jeremyczhen/fdbus.git
git clone -b v2.5.3 --depth=1 https://github.com/eProsima/Fast-DDS-Gen.git

# build
# cd $BUILD_DIR/protobuf && cmake -B build -DBUILD_SHARED_LIBS=ON -Dprotobuf_BUILD_TESTS=OFF && cmake --build build --target install -- -j$(nproc)
cd $BUILD_DIR/memory && cmake -B build -DFOONATHAN_MEMORY_BUILD_EXAMPLES=OFF -DFOONATHAN_MEMORY_BUILD_TESTS=OFF -DFOONATHAN_MEMORY_BUILD_TOOLS=OFF && cmake --build build --target install -- -j$(nproc)
cd $BUILD_DIR/Fast-CDR && cmake -B build -DBUILD_SHARED_LIBS=ON && cmake --build build --target install -- -j$(nproc)
cd $BUILD_DIR/Fast-DDS && cmake -B build -DBUILD_SHARED_LIBS=ON && cmake --build build --target install -- -j$(nproc)
# cd $BUILD_DIR/vsomeip && cmake -B build && cmake --build build --target install -- -j$(nproc)
cd $BUILD_DIR/iceoryx/iceoryx_meta && cmake -B ../build -DBUILD_SHARED_LIBS=ON && cmake --build ../build --target install -- -j$(nproc)
cd $BUILD_DIR/cyclonedds && cmake -B build -DBUILD_SHARED_LIBS=ON && cmake --build build --target install -- -j$(nproc)
# cd $BUILD_DIR/fdbus/cmake && cmake -B ../build && cmake --build ../build --target install -- -j$(nproc)
# cd $BUILD_DIR/Fast-DDS-Gen && ./gradlew assemble
# sed -i "s#dir\=\"\`dirname \\\\\"\$0\\\\\"\`\"#dir\=\$(dirname \$(readlink -f \"\$0\"))#g" $BUILD_DIR/Fast-DDS-Gen/scripts/fastddsgen
# ln -sf $BUILD_DIR/Fast-DDS-Gen/scripts/fastddsgen /usr/local/bin/fastddsgen
# cp -r /usr/local/usr/* /usr/local/ && rm -r /usr/local/usr/

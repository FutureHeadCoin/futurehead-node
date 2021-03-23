FROM futureheadcurrency/futurehead-env:gcc

ARG NETWORK=live
ARG TRAVIS_TAG=DEV_BUILD
ARG CI_BUILD=OFF
ADD ./ /tmp/src

RUN mkdir /tmp/build && \
    cd /tmp/build && \
    cmake /tmp/src -DCI_BUILD=${CI_BUILD} -DBOOST_ROOT=${BOOST_ROOT} -DACTIVE_NETWORK=futurehead_${NETWORK}_network \
    -DFUTUREHEAD_ROCKSDB=ON -DFUTUREHEAD_POW_SERVER=ON -DROCKSDB_LIBRARIES=/tmp/rocksdb/lib/librocksdb.a \
    -DROCKSDB_INCLUDE_DIRS=/tmp/rocksdb/include -DFUTUREHEAD_SHARED_BOOST=ON && \
    make futurehead_node -j $(nproc) && \
    make futurehead_rpc -j $(nproc) && \
    make futurehead_pow_server -j $(nproc) && \
    cd .. && \
    echo ${NETWORK} > /etc/futurehead-network

FROM ubuntu:16.04

RUN groupadd --gid 1000 futureheadcurrency && \
    useradd --uid 1000 --gid futureheadcurrency --shell /bin/bash --create-home futureheadcurrency

COPY --from=0 /tmp/build/futurehead_node /usr/bin
COPY --from=0 /tmp/build/futurehead_rpc /usr/bin
COPY --from=0 /tmp/build/futurehead_pow_server /usr/bin
COPY --from=0 /tmp/src/api/ /usr/bin/api/
COPY --from=0 /etc/futurehead-network /etc
COPY docker/node/entry.sh /usr/bin/entry.sh
COPY docker/node/config /usr/share/futurehead/config
COPY --from=0 /tmp/boost/lib/* /usr/local/lib/
RUN chmod +x /usr/bin/entry.sh
RUN ln -s /usr/bin/futurehead_node /usr/bin/rai_node
RUN ldconfig

WORKDIR /root
USER root

ENV PATH="${PATH}:/usr/bin"
ENTRYPOINT ["/bin/bash",  "/usr/bin/entry.sh"]
CMD ["futurehead_node daemon -l"]

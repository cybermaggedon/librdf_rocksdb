
FROM docker.io/cybermaggedon/sparql-service

RUN dnf install -y rocksdb && dnf clean all

COPY librdf_storage_rocksdb.so /usr/lib64/redland/

RUN mkdir /data/db

CMD /usr/local/bin/sparql 8089 rocksdb /data/db


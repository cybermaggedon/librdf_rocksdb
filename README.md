
= librdf_rocksdb =

== Overview ==

This is a plugin for Redland / librdf (see https://librdf.org/), which
supports using a RocksDB key-value instance as storage.

== RocksDB ==

RocksDB is an open-source key/value store maintained by Facebook, built on
earlier work by LevelDB.  It is highly performant for fast writes but can
only run on a single CPU node.

RocksDB is what is sometimes called an embedded database, which means
that the compute and input/output which manages the store exists
within the program using librdf, so there are no separate servers to run,
and no network communication.  RocksDB writes to a directory on the local
system.

This makes for a very simple system, you can just install the RocksDB
plugin and then use all the Redland / librdf tools.  RocksDB only works
with a single writer to the database, so no concurrent access is possible.
If you want concurrent access you need to look at a larger-scale store.

== This plugin ==

This plugin stores RDF triples in RocksDB by mapping the triples to
key-values are storing them in RocksDB.  For each triple, three key-value
pairs are stored in RocksDB using a specific encoding which only has
meaning to this plugin.

== Installation ==

This is written in C and C++.  C is librdf's native language, and the C
plugin interface wraps a C++ library.

To compile:
```
make
```

To use, the plugin object should be installed in the appropriate
library directory.  This may work for you:
```
make install
```

== SPARQL service on RocksDB ==

This repository also builds a container which supports a SPARQL service, by
installing this plugin in the SPARQL service container at:

  https://hub.docker.com/r/cybermaggedon/sparql-service

and publishes the container at:

  https://hub.docker.com/r/cybermaggedon/sparql-service-rocksdb

== `sparql-service-rocksdb` ==

The resultant container is able to provide a SPARQL service backed by a
RocksDB store.  The container also contains the Redland `rdfproc` utility
which allows the container to build a RocksDB store from a file containing
triples e.g. a Turtle format file.  To use this, you need to know how
to invoke containers so that a volume is mounted.  This example
mounts the current directory, and converts the Turtle file data.ttl to
a new RocksDB store in directory rocks-db:

```
  docker run -v $(pwd):/files \
      docker.io/cybermaggedon/sparql-service-rocksdb \
      rdfproc -n -s rocksdb /files/rocks-db parse /files/data.ttl turtle
```

The SPARQL service can then be launched e.g. 
```
  docker run -v $(pwd):/files -p 8089:8089 \
      docker.io/cybermaggedon/sparql-service-rocksdb \
      sparql 8089 rocksdb /files/rocks-db
```

The SPARQL service supports much of 1.1 and most of 1.0 and is read-only.
Write operations will cause an error.


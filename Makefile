
CFLAGS=-I/usr/include/raptor2 -I/usr/include/rasqal -fPIC -I. -Iredland -g
CXXFLAGS=-I/usr/include/raptor2 -I/usr/include/rasqal -fPIC -I. -Iredland -g
LIBS=-lrasqal -lrdf

SQLITE_FLAGS=-DSTORE=\"sqlite\" -DSTORE_NAME=\"STORE.db\"

ROCKSDB_FLAGS=-DSTORE=\"rocksdb\" -DSTORE_NAME=\"ROCKS-DB\"

#LIB_OBJS= 
#rocksdb.o \
#	rocksdb_comms.o

all: test-sqlite test-rocksdb librdf_storage_rocksdb.so

#librocksdb.a: ${LIB_OBJS}
#	${AR} cr $@ ${LIB_OBJS}
#	ranlib $@

test-sqlite: test-sqlite.o
	${CXX} ${CXXFLAGS} test-sqlite.o -o $@ ${LIBS}

test-rocksdb: test-rocksdb.o
	${CXX} ${CXXFLAGS} test-rocksdb.o -o $@ ${LIBS}

bulk_load: bulk_load.o
	${CXX} ${CXXFLAGS} bulk_load.o -o $@ ${LIBS}

test-sqlite.o: test.C
	${CXX} ${CXXFLAGS} -c $< -o $@  ${SQLITE_FLAGS}

test-rocksdb.o: test.C
	${CXX} ${CXXFLAGS} -c $< -o $@ ${ROCKSDB_FLAGS}

ROCKSDB_OBJECTS=rocksdb.o impl.o

librdf_storage_rocksdb.so: ${ROCKSDB_OBJECTS}
	${CXX} ${CXXFLAGS} -shared -o $@ ${ROCKSDB_OBJECTS} -lrocksdb

rocksdb.o: CFLAGS += -DHAVE_CONFIG_H -DLIBRDF_INTERNAL=1
rocksdb.o: CFLAGS += -Icpp/include

install: all
	sudo cp librdf_storage_rocksdb.so /usr/lib64/redland

depend:
	makedepend -Y -I. *.c *.C


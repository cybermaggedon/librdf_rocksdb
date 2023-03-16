
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"

extern "C" {
#include "store.h"
}

using ROCKSDB_NAMESPACE::DB;
using ROCKSDB_NAMESPACE::DBOptions;
using ROCKSDB_NAMESPACE::Options;
using ROCKSDB_NAMESPACE::PinnableSlice;
using ROCKSDB_NAMESPACE::ReadOptions;
using ROCKSDB_NAMESPACE::Status;
using ROCKSDB_NAMESPACE::WriteBatch;
using ROCKSDB_NAMESPACE::WriteOptions;
using ROCKSDB_NAMESPACE::ColumnFamilyOptions;
using ROCKSDB_NAMESPACE::ColumnFamilyDescriptor;
using ROCKSDB_NAMESPACE::ColumnFamilyHandle;
using ROCKSDB_NAMESPACE::Slice;
using ROCKSDB_NAMESPACE::Iterator;

typedef std::vector<char> bytes;

class rocksdb_store {
public:

    static const unsigned int SPO = 0;
    static const unsigned int POS = 1;
    static const unsigned int OSP = 2;

    // FIXME: Ignored
    int sync;

    int is_new;

    DB* db;
    std::string name;
    std::vector<ColumnFamilyHandle*> handles;

    static void close(struct implementation_t* impl);
    void close();

    static void free(struct implementation_t* impl);
    void free();

    static int open(struct implementation_t* impl);
    int _open();

    static int size(struct implementation_t* impl);
    int size();


    static bytes encode_key(const char* a, const char* b, const char* c);
    static std::vector<bytes> decode_key(Slice* sl);

    static bytes encode_start(const char* a = 0, const char* b = 0,
					  const char* c = 0);
    static bytes encode_limit(const char* a = 0, const char* b = 0,
			      const char* c = 0);

    static int add(struct implementation_t* impl,
		   char* s, char* p, char* o, char* c);
    int add(char* s, char* p, char* o, char* c);

    static int remove(struct implementation_t* impl,
		      char* s, char* p, char* o, char* c);
    int remove(char* s, char* p, char* o, char* c);

    static int contains(struct implementation_t* impl,
			char* s, char* p, char* o, char* c);
    int contains(char* s, char* p, char* o, char* c);

    static struct implementation_stream_t*
    new_stream(struct implementation_t *impl, char*, char*, char*, char*);
    struct implementation_stream_t* new_stream(char* s, char* p,
					       char* o, char* c);

    implementation* impl;

};

class rocksdb_stream {
public:

    bytes limit;
    Iterator* iter;
    std::vector<bytes> triple;
    unsigned int index;

    static const unsigned int S = 0;
    static const unsigned int P = 1;
    static const unsigned int O = 2;

    void fetch();

    static void free(struct implementation_stream_t* impl);
    void free();

    static int get_s(struct implementation_stream_t* impl,
		     const char**, size_t*);
    int get_s(const char**, size_t*);

    static int get_p(struct implementation_stream_t* impl,
		     const char**, size_t*);
    int get_p(const char**, size_t*);

    static int get_o(struct implementation_stream_t* impl,
		     const char**, size_t*);
    int get_o(const char**, size_t*);

    static int at_end(struct implementation_stream_t* impl);
    int at_end();

    static int next(struct implementation_stream_t* impl);
    int next();

};

implementation* implementation_new(char* name, int sync, int is_new) {

    rocksdb_store* store = new rocksdb_store();
    store->name = name;
    store->sync = sync;
    store->is_new = is_new;

    implementation* impl = new implementation();

    store->impl = impl;

    impl->store = (void *) store;
    impl->close = &rocksdb_store::close;
    impl->free = &rocksdb_store::free;
    impl->open = &rocksdb_store::open;
    impl->size = &rocksdb_store::size;
    impl->add = &rocksdb_store::add;
    impl->remove = &rocksdb_store::remove;
    impl->contains = &rocksdb_store::contains;
    impl->new_stream = &rocksdb_store::new_stream;

    return impl;

}

void rocksdb_store::close(struct implementation_t* impl) {
    rocksdb_store* store = ((rocksdb_store*) impl->store);
    store->close();
}

void rocksdb_store::close() {

    for (auto handle : handles) {
	Status s = db->DestroyColumnFamilyHandle(handle);
    }

    db->Close();
}

void rocksdb_store::free(struct implementation_t* impl) {
    rocksdb_store* store = ((rocksdb_store*) impl->store);
    store->free();
}

void rocksdb_store::free() {
    delete db;
    db = 0;
}

int rocksdb_store::open(struct implementation_t* impl) {
    rocksdb_store* store = ((rocksdb_store*) impl->store);
    return store->_open();
}

int rocksdb_store::_open() {

    //////////////////////////////////////////////////////////////////////
    Status status;

    ColumnFamilyOptions cfo;
    Options options;
    options.create_if_missing = true;

    //////////////////////////////////////////////////////////////////////

    std::vector<ColumnFamilyDescriptor> colf;
    colf.push_back(
	ColumnFamilyDescriptor(
	    ROCKSDB_NAMESPACE::kDefaultColumnFamilyName, cfo
	    )
	);
    colf.push_back(ColumnFamilyDescriptor("spo", cfo));
    colf.push_back(ColumnFamilyDescriptor("pos", cfo));
    colf.push_back(ColumnFamilyDescriptor("osp", cfo));

    //////////////////////////////////////////////////////////////////////

    if (is_new) {
	DestroyDB(name, options);
    }

    //////////////////////////////////////////////////////////////////////

    options.create_missing_column_families = true;

    status = DB::Open(options, name, colf, &handles, &db);
    if (!status.ok()) {
	std::cerr << "Failed to open database" << std::endl;
	std::cerr << status.ToString() << std::endl;
	return -1;
    }

    return 0;

}

int rocksdb_store::size(struct implementation_t* impl) {
    rocksdb_store* store = ((rocksdb_store*) impl->store);
    return store->size();
}

int rocksdb_store::size() {

    std::string count;
    if (!db->GetProperty(handles[0],
			 DB::Properties::kEstimateNumKeys, &count))
	return -1;

    std::istringstream buf(count);
    int num;
    buf >> num;
    return num;

}

bytes rocksdb_store::encode_key(
    const char* a, const char* b, const char* c)
{
    bytes enc;
    for(int i = 0; a[i] != 0; i++) enc.push_back(a[i]);
    enc.push_back(0);
    for(int i = 0; b[i] != 0; i++) enc.push_back(b[i]);
    enc.push_back(0);
    for(int i = 0; c[i] != 0; i++) enc.push_back(c[i]);
    return enc;
}

static std::vector<bytes> decode_key(Slice sl)
{

    const char* k = sl.data();
    int len = sl.size();

    std::vector<bytes> ret;
    int ptr = 0;

    {
	std::vector<char> part;
	while ((ptr < len) && (k[ptr] != 0)) {
	    part.push_back(k[ptr]);
	    ptr++;
	}
	if (ptr == len) return std::vector<bytes>();
	ret.push_back(part);
	ptr++;
    }
    
    {
	std::vector<char> part;
	while ((ptr < len) && (k[ptr] != 0)) {
	    part.push_back(k[ptr]);
	    ptr++;
	}
	if (ptr == len) return std::vector<bytes>();
	ret.push_back(part);
	ptr++;
    }
    
    {
	std::vector<char> part;
	while (ptr < len) {
	    part.push_back(k[ptr]);
	    ptr++;
	}
	ret.push_back(part);
	ptr++;
    }

    return ret;

}

bytes rocksdb_store::encode_start(
    const char* a, const char* b, const char* c)
{

    std::vector<char> ret;

    if (a) {

	for(int i = 0; a[i] != 0; i++) ret.push_back(a[i]);
	ret.push_back(0);

	if (b) {

	    for(int i = 0; b[i] != 0; i++) ret.push_back(b[i]);
	    ret.push_back(0);

	    if (c) {

		for(int i = 0; c[i] != 0; i++) ret.push_back(c[i]);

	    }

	}

    }

    return ret;

}

bytes rocksdb_store::encode_limit(
    const char* a, const char* b, const char* c)
{

    std::vector<char> ret;

    if (a) {

	for(int i = 0; a[i] != 0; i++) ret.push_back(a[i]);
	ret.push_back(0);

	if (b) {

	    for(int i = 0; b[i] != 0; i++) ret.push_back(b[i]);
	    ret.push_back(0);

	    if (c) {

		for(int i = 0; c[i] != 0; i++) ret.push_back(c[i]);

	    }

	}

    }

    ret.push_back(127);

    return ret;

}

// FIXME: Contexts not used

int rocksdb_store::add(struct implementation_t* impl,
		       char* s, char* p, char* o, char* c)
{

    rocksdb_store* store = ((rocksdb_store*) impl->store);
    return store->add(s, p, o, c);

}

int rocksdb_store::add(char* s, char* p, char* o, char* c)
{
    

    bytes spo = encode_key(s, p, o);
    bytes pos = encode_key(p, o, s);
    bytes osp = encode_key(o, s, p);

    db->Put(WriteOptions(), handles[SPO],
	    Slice(spo.data(), spo.size()), Slice());

    db->Put(WriteOptions(), handles[POS],
	    Slice(pos.data(), pos.size()), Slice());

    db->Put(WriteOptions(), handles[OSP],
	    Slice(osp.data(), osp.size()), Slice());

    return 0;

}
    
int rocksdb_store::remove(struct implementation_t* impl,
			  char* s, char* p, char* o, char* c)
{
    rocksdb_store* store = ((rocksdb_store*) impl->store);
    return store->remove(s, p, o, c);
}

int rocksdb_store::remove(char* s, char* p, char* o, char* c) 
{
    
    bytes spo = encode_key(s, p, o);
    bytes pos = encode_key(p, o, s);
    bytes osp = encode_key(o, s, p);

    db->Delete(WriteOptions(), handles[SPO],
		      Slice(spo.data(), spo.size()));

    db->Delete(WriteOptions(), handles[POS],
		      Slice(pos.data(), pos.size()));

    db->Delete(WriteOptions(), handles[OSP],
		      Slice(osp.data(), osp.size()));

    return 0;
}

int rocksdb_store::contains(struct implementation_t* impl,
			    char* s, char* p, char* o, char* c)
{
    rocksdb_store* store = ((rocksdb_store*) impl->store);
    return store->contains(s, p, o, c);
}

int rocksdb_store::contains(char* s, char* p, char* o, char* c) 
{

    PinnableSlice sl;

    bytes spo = encode_key(s, p, o);

    Status st = db->Get(ReadOptions(), handles[SPO],
			Slice(spo.data(), spo.size()), &sl);
    if (!st.ok()) return -1;

    return 0;

}

struct implementation_stream_t* rocksdb_store::new_stream(
    struct implementation_t *impl,
    char* s, char* p, char* o, char* c)
{
    rocksdb_store* store = ((rocksdb_store*) impl->store);
    return store->new_stream(s, p, o, c);
}

struct implementation_stream_t* rocksdb_store::new_stream(
    char* s, char* p, char* o, char* c) 
{

    rocksdb_stream* stream = new rocksdb_stream();

    unsigned int index;
    bytes start;
    bytes limit;

    if (s) {
	if (p) {
	    if (o) {
		// SPO
		start = encode_start(s, p, o);
		limit = encode_limit(s, p, o);
		index = SPO;
	    } else {
		// SP?
		start = encode_start(s, p);
		limit = encode_limit(s, p);
		index = SPO;
	    }
	} else {
	    if (o) {
		// S?O
		start = encode_start(o, s);
		limit = encode_limit(o, s);
		index = OSP;
	    } else {
		// S??
		start = encode_start(s);
		limit = encode_limit(s);
		index = SPO;
	    }
	}
    } else {
	if (p) {
	    if (o) {
		// ?PO
		start = encode_start(p, o);
		limit = encode_limit(p, o);
		index = POS;
	    } else {
		// ?P?
		start = encode_start(p);
		limit = encode_limit(p);
		index = POS;
	    }
	} else {
	    if (o) {
		// ??O
		start = encode_start(o);
		limit = encode_limit(o);
		index = OSP;
	    } else {
		// ???
		start = encode_start();
		limit = encode_limit();
		index = SPO;
	    }
	}
    }

    stream->limit = limit;
    stream->iter = db->NewIterator(ReadOptions(), handles[index]);
    stream->index = index;

    if (start.size() == 0)
	stream->iter->SeekToFirst();
    else
	stream->iter->Seek(Slice(start.data(), start.size()));

    if (stream->iter->Valid()) {
	stream->fetch();
    }

    implementation_stream* is = new implementation_stream();
    is->impl = impl;
    is->free = rocksdb_stream::free;
    is->get_s = rocksdb_stream::get_s;
    is->get_p = rocksdb_stream::get_p;
    is->get_o = rocksdb_stream::get_o;
    is->at_end = rocksdb_stream::at_end;
    is->next = rocksdb_stream::next;
    is->stream = stream;

    return is;

}

void rocksdb_stream::fetch()
{
    triple = decode_key(iter->key());
}

void rocksdb_stream::free(struct implementation_stream_t* impl)
{
    rocksdb_stream* stream = ((rocksdb_stream*) impl->stream);
    stream->free();
}



void rocksdb_stream::free() 
{
    delete iter;
    // FIXME: Is this freed?
}

// Given the index we fetched, this helps work out which part of the
// key to return
unsigned int mapping[3][3] = {
    { 0, 1, 2 },    // SPO
    { 2, 0, 1 },    // POS
    { 1, 2, 0 },    // OSP
};

int rocksdb_stream::get_s(struct implementation_stream_t* impl,
			  const char**data, size_t* len)
{
    rocksdb_stream* stream = ((rocksdb_stream*) impl->stream);
    return stream->get_s(data, len);
}

int rocksdb_stream::get_s(const char**data, size_t* len) 
{

    if (!iter->Valid()) return -1;

    int part = mapping[index][S];
    *data = triple[part].data();
    *len = triple[part].size();
    return 0;
}

int rocksdb_stream::get_p(struct implementation_stream_t* impl,
			  const char** data, size_t* len)
{
    rocksdb_stream* stream = ((rocksdb_stream*) impl->stream);
    return stream->get_p(data, len);
}

int rocksdb_stream::get_p(const char** data, size_t* len) 
{

    if (!iter->Valid()) return -1;

    int part = mapping[index][P];
    *data = triple[part].data();
    *len = triple[part].size();
    return 0;
}

int rocksdb_stream::get_o(struct implementation_stream_t* impl,
			  const char** data, size_t* len)
{
    rocksdb_stream* stream = ((rocksdb_stream*) impl->stream);
    return stream->get_o(data, len);
}

int rocksdb_stream::get_o(const char** data, size_t* len) 
{

    if (!iter->Valid()) return -1;

    int part = mapping[index][O];
    *data = triple[part].data();
    *len = triple[part].size();
    return 0;
}

int rocksdb_stream::at_end(struct implementation_stream_t* impl)
{
    rocksdb_stream* stream = ((rocksdb_stream*) impl->stream);
    return stream->at_end();
}

int rocksdb_stream::at_end() {

    if (!iter->Valid()) {
	return 1;
    }

    Slice k = iter->key();

    bytes b = bytes(k.data(), k.data() + k.size());

    if (b < limit) {
	return 0;
    }

    return 1;

}

int rocksdb_stream::next(struct implementation_stream_t* impl)
{
    rocksdb_stream* stream = ((rocksdb_stream*) impl->stream);
    return stream->next();
}

int rocksdb_stream::next() 
{

    iter->Next();

    if (iter->Valid())
	fetch();

    return 0;

}


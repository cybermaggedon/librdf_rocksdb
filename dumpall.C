
#include <vector>
#include <iostream>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"

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

int main()
{

    const std::string name = "ROCKS-DB";

    std::vector<ColumnFamilyHandle*> handles;
    ColumnFamilyOptions cfo;

    Options options;
    options.create_if_missing = true;

    std::vector<ColumnFamilyDescriptor> colf;
    colf.push_back(
	ColumnFamilyDescriptor(
	    ROCKSDB_NAMESPACE::kDefaultColumnFamilyName, cfo
	    )
	);

    colf.push_back(ColumnFamilyDescriptor("spo", cfo));
    colf.push_back(ColumnFamilyDescriptor("pos", cfo));
    colf.push_back(ColumnFamilyDescriptor("osp", cfo));

    DB* db;

    Status status = DB::Open(
	options, name, colf, &handles, &db
	);

    Iterator* it = db->NewIterator(rocksdb::ReadOptions());
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
	std::cout << it->key().ToString() << ": " <<
	    it->value().ToString() << std::endl;
    }

    delete it;
  
    for (auto handle : handles) {
	Status s = db->DestroyColumnFamilyHandle(handle);
    }

    db->Close();
    delete db;
    
}


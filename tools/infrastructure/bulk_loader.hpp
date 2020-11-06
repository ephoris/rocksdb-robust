#ifndef BULK_LOADER_H_ 
#define BULK_LOADER_H_ 

#include <iostream>

#include "spdlog/spdlog.h"
#include "rocksdb/db.h"

#include "tmpdb/fluid_lsm_compactor.hpp"

#include "data_generator.hpp"

#define BATCH_SIZE 10000

class FluidLSMBulkLoader : public tmpdb::FluidCompactor
{
public:
    FluidLSMBulkLoader(DataGenerator & data_gen, const tmpdb::FluidOptions fluid_opt, const rocksdb::Options rocksdb_opt)
        : FluidCompactor(fluid_opt, rocksdb_opt), data_gen(&data_gen) {};

    rocksdb::Status bulk_load_entries(rocksdb::DB * db, size_t num_entries);
    // rocksdb::Status bulk_load_levels(rocksdb::DB * db, size_t levels);

    // Override both compaction events to prevent any compactions during bulk loading
    // void OnFlushCompleted(rocksdb::DB * /* db */, const ROCKSDB_NAMESPACE::FlushJobInfo & /* info */) override {};
    void ScheduleCompaction(tmpdb::CompactionTask * /* task */) override {};
    tmpdb::CompactionTask * PickCompaction(rocksdb::DB * /* db */, const std::string & /* cf_name */, const size_t /* level */) override {return nullptr;};


private:
    DataGenerator * data_gen;

    rocksdb::Status bulk_load(rocksdb::DB * db, std::vector<size_t> entries_per_level, size_t num_levels);

    rocksdb::Status bulk_load_single_level(rocksdb::DB * db, size_t level_idx, size_t num_entries, size_t num_runs);

    rocksdb::Status bulk_load_single_run(rocksdb::DB * db, size_t level_idx, size_t num_entries);
};

#endif /*  BULK_LOADER_H_ */

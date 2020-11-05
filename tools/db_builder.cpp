#include <iostream>

#include "clipp.h"
#include "spdlog/spdlog.h"

#include "rocksdb/db.h"
#include "tmpdb/fluid_lsm_compactor.hpp"
#include "infrastructure/bulk_loader.hpp"
#include "infrastructure/data_generator.hpp"

typedef enum { BUILD, EXECUTE } cmd_mode;

typedef enum { ENTRIES, LEVELS } build_mode;

typedef struct
{
    std::string db_path;
    build_mode build_fill;

    // Build mode
    double T = 2;
    double K = 1;
    double Z = 1;
    size_t B = 1048576;
    size_t E = 8192;
    double bits_per_element = 5.0;
    size_t N = 1e6;
    size_t L = 1;

    int verbose = 0;
    bool destroy_db = false;

    int max_rocksdb_levels = 100;

} environment;


environment parse_args(int argc, char * argv[])
{
    using namespace clipp;
    using std::to_string;

    size_t minimum_entry_size = 32;

    environment env;
    bool help = false;

    auto general_opt = "general options" % (
        (option("-v", "--verbose") & integer("level", env.verbose))
            % ("Logging levels (DEFAULT: INFO, 1: DEBUG, 2: TRACE)"),
        (option("-h", "--help").set(help, true)) % "prints this message"
    );

    auto build_opt = (
        "build options:" % (
            (value("db_path", env.db_path)) % "path to the db",
            (option("-T", "--size_ratio") & number("ratio", env.T))
                % ("size ratio, [default: " + to_string(env.T) + "]"),
            (option("-K", "--lower_level_size_ratio") & number("ratio", env.K))
                % ("size ratio, [default: " + to_string(env.K) + "]"),
            (option("-Z", "--largest_level_size_ratio") & number("ratio", env.Z))
                % ("size ratio, [default: " + to_string(env.Z) + "]"),
            (option("-B", "--buffer_size") & integer("size", env.B))
                % ("buffer size (in bytes), [default: " + to_string(env.B) + "]"),
            (option("-E", "--entry_size") & integer("size", env.E))
                % ("entry size (bytes) [default: " + to_string(env.E) + ", min: 32]"),
            (option("-b", "--bpe") & number("bits", env.bits_per_element))
                % ("bits per entry per bloom filter across levels [default: " + to_string(env.bits_per_element) + "]"),
            (option("-d", "--destroy").set(env.destroy_db)) % "destroy the DB if it exists at the path"
        ),
        "db fill options (pick one):" % (
            one_of(
                (option("-N", "--entries").set(env.build_fill, build_mode::ENTRIES) & integer("num", env.N))
                    % ("total entries, default pick [default: " + to_string(env.N) + "]"),
                (option("-L", "--levels").set(env.build_fill, build_mode::LEVELS) & integer("num", env.L)) 
                    % ("total filled levels [default: " + to_string(env.L) + "]")
            )
        ),
        "minor options:" % (
            (option("--max_rocksdb_level") & integer("num", env.max_rocksdb_levels))
                % ("limits the maximum levels rocksdb has [default :" + to_string(env.max_rocksdb_levels) + "]"),
            (option(""))
        )
    );

    auto cli = (
        general_opt, 
        build_opt 
    );

    if (!parse(argc, argv, cli))
        help = true;

    if (env.E < minimum_entry_size)
    {
        help = true;
        spdlog::error("Entry size is less than {} bytes", minimum_entry_size);
    }

    if (help)
    {
        auto fmt = doc_formatting{}.doc_column(42);
        std::cout << make_man_page(cli, "db_builder", fmt);
        exit(EXIT_FAILURE);
    }

    return env;
}


void fill_fluid_opt(environment env, tmpdb::FluidOptions & fluid_opt)
{
    fluid_opt.size_ratio = env.T;
    fluid_opt.largest_level_run_max = env.Z;
    fluid_opt.lower_level_run_max = env.K;
    fluid_opt.buffer_size = env.B;
    fluid_opt.entry_size = env.E;
    fluid_opt.bits_per_element = env.bits_per_element;
}


void build_db(environment & env)
{
    spdlog::info("Building DB: {}", env.db_path);
    rocksdb::Options rocksdb_opt;
    tmpdb::FluidOptions fluid_opt;

    rocksdb_opt.create_if_missing = true;
    rocksdb_opt.compaction_style = rocksdb::kCompactionStyleNone;
    rocksdb_opt.compression = rocksdb::kNoCompression;
    rocksdb_opt.IncreaseParallelism(1);

    rocksdb_opt.PrepareForBulkLoad();
    rocksdb_opt.num_levels = env.max_rocksdb_levels;

    fill_fluid_opt(env, fluid_opt);
    RandomGenerator gen = RandomGenerator();
    FluidLSMBulkLoader * fluid_compactor = new FluidLSMBulkLoader(gen, fluid_opt, rocksdb_opt);
    rocksdb_opt.listeners.emplace_back(fluid_compactor);

    rocksdb::DB * db = nullptr;
    rocksdb::Status status = rocksdb::DB::Open(rocksdb_opt, env.db_path, &db);
    if (!status.ok())
    {
        spdlog::error("Problems openiing DB {}", status.ToString());
        delete db;
        exit(EXIT_FAILURE);
    }

    fluid_compactor->init_open_db(db);
    status = fluid_compactor->bulk_load_entries(db, env.N);
    spdlog::info("Finished building");

    if (!status.ok())
    {
        spdlog::error("Problems bulk loading: {}", status.ToString());
        delete db;
        exit(EXIT_FAILURE);
    }

    db->Close();
    delete db;
}


int main(int argc, char * argv[])
{
    spdlog::set_pattern("[%T.%e] %^[%l]%$ %v");
    environment env = parse_args(argc, argv);

    spdlog::info("Welcome to db_builder!");
    if(env.verbose == 1)
    {
        spdlog::set_level(spdlog::level::debug);
    }
    else if(env.verbose == 2)
    {
        spdlog::set_level(spdlog::level::trace);
    }
    else
    {
        spdlog::set_level(spdlog::level::info);
    }

    if (env.destroy_db)
    {
        spdlog::info("Destroying DB: {}", env.db_path);
        rocksdb::DestroyDB(env.db_path, rocksdb::Options());
    }

    build_db(env);

    return EXIT_SUCCESS;
}
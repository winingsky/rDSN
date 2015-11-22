#include "../../tools/common/simple_logger.h"
#include <gtest/gtest.h>

using namespace dsn;
using namespace dsn::tools;

static const int simple_logger_gc_gap = 20;

static void get_log_file_index(std::vector<int> &log_index)
{
    std::vector<std::string> sub_list;
    std::string path = "./";
    if ( !utils::filesystem::get_subfiles(path, sub_list, false) ) {
        ASSERT_TRUE(false);
    }

    for (auto &ptr: sub_list) {
        auto &&name = utils::filesystem::get_file_name(ptr);
        if ( name.length()<=8 || name.substr(0, 4)!="log.") continue;
        int index;
        if (1 != sscanf(name.c_str(), "log.%d.txt", &index)) continue;
        log_index.push_back(index);
    }
}

static void clear_files(std::vector<int> &log_index)
{
    char file[256];
    memset(file, 0, sizeof(file));
    for (auto i: log_index) {
        snprintf(file, 256, "log.%d.txt", i);
        dsn::utils::filesystem::remove_path( std::string(file) );
    }
}

static void prepare_test_dir() {
    const char* dir = "./test";
    mkdir(dir, 0755);
    chdir(dir);
}

static void finish_test_dir() {
    const char* dir = "./test";
    chdir("..");
    rmdir(dir);
}

void log_print(logging_provider* logger, const char* fmt, ...) {
    va_list vl;
    va_start(vl, fmt);
    logger->dsn_logv(__FILE__, __FUNCTION__, __LINE__, LOG_LEVEL_INFORMATION, "test", fmt, vl);
    va_end(vl);
}


TEST(tools_common, simple_logger)
{
    //cases for print_header
    screen_logger* logger = new screen_logger();
    log_print(logger, "%s", "test_print");
    std::thread t([](screen_logger* lg){
        tls_dsn.magic = 0xdeadbeef;
        log_print(lg, "%s", "test_print");
    }, logger);
    t.join();

    logger->flush();
    delete logger;

    prepare_test_dir();
    //create multiple files
    for (unsigned int i=0; i<simple_logger_gc_gap + 10; ++i) {
        simple_logger* logger = new simple_logger();
        //in this case stdout is useless
        for (unsigned int i=0; i!=1000; ++i)
            log_print(logger, "%s", "test_print");
        logger->flush();

        delete logger;
    }

    std::vector<int> index;
    get_log_file_index(index);
    ASSERT_TRUE(!index.empty());
    sort(index.begin(), index.end());
    ASSERT_TRUE(index.size() == simple_logger_gc_gap+1);
    for (unsigned int i=0; i<=simple_logger_gc_gap; ++i)
        ASSERT_TRUE(index[i]==i+10);
    clear_files(index);
    finish_test_dir();
}

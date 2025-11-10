
#ifndef DIARKIS_STORAGE_H
#define DIARKIS_STORAGE_H

#include "stdint.h"
#include "stdlib.h"
#include <string>
#include <vector>
#include <mutex>
#include <set>

namespace diarkis {

class Storage {
public:
    explicit Storage(std::string base_path);
    int init();

    int create_file(const std::string& path);
    int create_directory(const std::string& path);

    size_t read_file(const std::string& path, uint8_t* buffer);
    int write_file(const std::string& path, uint8_t* buffer, size_t size);
    int append_file(const std::string& path, uint8_t* buffer, size_t size);

    int rename_file(const std::string& old_path, const std::string& new_path);

    int delete_file(const std::string& path);
    int delete_directory(const std::string& path);

    std::vector<std::string> list_directory(const std::string& path);

    const std::string& get_base_path() const { return base_path_; }

private:
    std::string get_full_path(const std::string& relative_path) const;
    bool path_exists(const std::string& full_path) const;
    bool is_directory(const std::string& full_path) const;

    std::string base_path_;

    std::set<std::string> writes_in_progress_;
    std::mutex writes_in_progress_mtx_;
};

}

#endif

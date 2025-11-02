
#ifndef DIARKIS_FS_REPLICATOR_H
#define DIARKIS_FS_REPLICATOR_H

#include <string>
#include "diarkis/events.h"
#include "diarkis/fs_watcher.h"

namespace fs {

    class Replicator {
    public:
        explicit Replicator(const std::string& root_dir, fs::Watcher* watcher_);
        bool applyEvent(const events::Event& event);

    private:
        bool createFile(const std::string& path, const std::string& contents);
        bool createDirectory(const std::string& path);
        bool deleteFile(const std::string& path);
        bool deleteDirectory(const std::string& path);
        bool modifyFile(const std::string& path, const std::string& contents);
        bool moveFile(const std::string& old_path, const std::string& new_path);
        bool moveDirectory(const std::string& old_path, const std::string& new_path);
        
        void ensureParentDirectory(const std::string& path);

        std::string root_dir;
        fs::Watcher* watcher;
    };

};

#endif /* DIARKIS_FS_REPLICATOR_H */

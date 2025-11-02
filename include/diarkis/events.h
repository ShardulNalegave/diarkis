
#ifndef DIARKIS_EVENTS_H
#define DIARKIS_EVENTS_H

#include <string>
#include <functional>

namespace events {
    enum class EventType {
        INVALID,
        CREATED,
        MODIFIED,
        DELETED,
        MOVED,
    };

    struct Event {
        EventType type;
        std::string path;
        std::string relative_path;
        bool is_dir;

        std::string old_path; // old path, used only for MOVED event type
        std::string contents; // file contents, used only for CREATED/MODIFIED event types

        size_t getSerializedSize() const;
        const char* serialize() const;
        static Event deserialize(const char* bytes);
    };

    using EventHandler = std::function<void(const Event&)>;
};

#endif

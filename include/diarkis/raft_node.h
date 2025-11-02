
#ifndef DIARKIS_RAFT_NODE_H
#define DIARKIS_RAFT_NODE_H

#include <string>
#include <atomic>
#include <braft/raft.h>
#include <braft/util.h>

#include "diarkis/events.h"

#define DIARKIS_RAFT_GROUP_ID "diarkis-raft"

namespace raft {

    class Node {
    public:
        Node(int node_id_, const std::string& listen_addr_, const std::string& data_dir_);
        ~Node();

        bool init(const std::string& peers);
        void shutdown();

        bool proposeEvent(events::Event& event);

        bool isLeader() const;
        std::string getLeaderAddr() const;

        void setApplyCallback(events::EventHandler callback);

    private:
        int node_id;
        std::string listen_addr;
        std::string data_dir;

        braft::Node* node;
        braft::StateMachine* state_machine;

        events::EventHandler apply_callback;
        std::atomic<bool> is_leader;
    };

};

#endif /* DIARKIS_RAFT_NODE_H */

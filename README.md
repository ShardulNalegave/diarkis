# Diarkis
A distributed replicated filesystem built with Raft consensus in C++.

Diarkis is a filesystem replication system that keeps directories synchronized across multiple nodes using the Raft consensus algorithm. When files are created, modified, deleted, or moved on one node, those changes are automatically replicated to all other nodes in the cluster.

This project was built as a learning exercise to understand distributed consensus and filesystem monitoring.

## Features
- **Real-time filesystem monitoring** using Linux inotify
- **Raft consensus** for reliable replication across nodes
- **Automatic leader election** and failover
- **Support for basic file operations**: create, modify, delete, and move
- **Binary file support** with content replication
- **Recursive directory watching** and replication

## Architecture
The system consists of three main components:

1. **Filesystem Watcher**: Monitors a directory for changes using `inotify`
2. **Raft Node**: Manages consensus and log replication using the `braft` library
3. **Filesystem Replicator**: Applies committed changes to the local filesystem

When a file change is detected on the leader node, it's serialized and proposed to the Raft cluster. Once a majority of nodes acknowledge the change, it's committed and applied to all follower nodes' filesystems.

## Dependencies
- **braft**: Raft consensus implementation
- **brpc**: RPC framework (required by braft)
- **spdlog**: Logging library
- **CLI11**: Command-line argument parsing

## Building
Diarkis uses the CMake build system and is fairly easy to build.

```bash
git clone https://github.com/ShardulNalegave/diarkis.git
git submodule update --init
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage
Start a 3-node cluster:

```bash
# Node 1
./diarkis --id 1 --address 127.0.0.1:8001 \
  --peers 127.0.0.1:8001,127.0.0.1:8002,127.0.0.1:8003 \
  --watch /path/to/dir1 --data /path/to/data1

# Node 2
./diarkis --id 2 --address 127.0.0.1:8002 \
  --peers 127.0.0.1:8001,127.0.0.1:8002,127.0.0.1:8003 \
  --watch /path/to/dir2 --data /path/to/data2

# Node 3
./diarkis --id 3 --address 127.0.0.1:8003 \
  --peers 127.0.0.1:8001,127.0.0.1:8002,127.0.0.1:8003 \
  --watch /path/to/dir3 --data /path/to/data3
```

The cluster will automatically elect a leader. Any changes made to the watched directory on the leader node will be replicated to all follower nodes.

## Command-line Options
- `-i, --id`: Unique node ID (integer)
- `-a, --address`: Address for Raft communication (host:port)
- `-p, --peers`: Comma-separated list of all peer addresses
- `-w, --watch`: Directory to monitor and replicate
- `-d, --data`: Directory to store Raft metadata and logs
- `-v, --version`: Show version information

## Limitations
- Linux-only (uses inotify)
- Files larger than 10MB are not replicated
- Only the leader accepts new filesystem changes
- No snapshot compression (large filesystems may have large Raft logs)
- No authentication or encryption between nodes

## How It Works
1. The filesystem watcher detects local changes using inotify
2. If the node is the leader, it proposes the change to the Raft cluster
3. Raft replicates the change to a majority of nodes
4. Once committed, all nodes apply the change to their local filesystem
5. An ignore mechanism prevents replication loops

## Project Structure

```
diarkis/
├── events.h/cpp          # Event serialization and types
├── fs_watcher.h/cpp      # Filesystem monitoring with inotify
├── fs_replicator.h/cpp   # Filesystem change application
├── raft_node.h/cpp       # Raft node wrapper
├── state_machine.h/cpp   # Raft state machine implementation
└── main.cpp              # Application entry point
```

## License
This project is licensed under the MIT License.
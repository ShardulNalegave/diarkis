# Diarkis Distributed Filesystem
A replicated filesystem library built on Raft consensus using `braft`. This library provides strong consistency guarantees for file operations across a distributed cluster.

## Features
- **Leader-based writes**: All write operations (create, write, append, delete) go through the elected leader and are replicated via Raft consensus
- **Local reads**: Read operations can be performed on any node using local replicas
- **Strong consistency**: Reads see all writes committed before them (linearizable reads)
- **Automatic failover**: If the leader fails, a new leader is automatically elected
- **Path-based API**: Simple, intuitive API similar to POSIX filesystem operations

### How it works

1. **Write operations** (create, write, delete, etc.):
   - Must be submitted to the leader node
   - Leader proposes the operation through Raft
   - Once a majority commits, the operation is applied
   - Operation is then replicated to all followers

2. **Read operations** (read, list, stat):
   - Can be performed on any node
   - Served from local storage (no network overhead)

## Building

### Dependencies

- C++17 compiler
- CMake 3.15+
- bRaft
- Apache brpc
- spdlog
- gflags
- protobuf
- leveldb

### Build instructions

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage

### Starting the demo cluster

To run a 3-node cluster on localhost:

**Terminal 1 (Node 1):**
```bash
./fs_example \
  --peer_id=127.0.0.1:8100:0 \
  --conf=127.0.0.1:8100:0,127.0.0.1:8101:0,127.0.0.1:8102:0 \
  --data_path=./data1 \
  --raft_path=./raft1
```

**Terminal 2 (Node 2):**
```bash
./fs_example \
  --peer_id=127.0.0.1:8101:0 \
  --conf=127.0.0.1:8100:0,127.0.0.1:8101:0,127.0.0.1:8102:0 \
  --data_path=./data2 \
  --raft_path=./raft2
```

**Terminal 3 (Node 3):**
```bash
./fs_example \
  --peer_id=127.0.0.1:8102:0 \
  --conf=127.0.0.1:8100:0,127.0.0.1:8101:0,127.0.0.1:8102:0 \
  --data_path=./data3 \
  --raft_path=./raft3
```

### API Example

```cpp
#include "diarkis/fs_client.h"

using namespace diarkis;

int main() {
    Client::Config config;
    config.data_path = "./data";
    config.raft_path = "./raft";
    config.group_id = "my_fs";
    config.peer_id = "127.0.0.1:8100:0";
    config.initial_conf = "127.0.0.1:8100:0,127.0.0.1:8101:0,127.0.0.1:8102:0";
    
    Client client(config);
    auto result = client.init();
    if (!result.ok()) {
        return 1;
    }
    
    // Write operations (must be leader)
    if (client.is_leader()) {
        client.create_directory("docs");
        client.write_file("docs/hello.txt", "Hello, World!");
        client.append_file("docs/hello.txt", "\nGoodbye!");
    }
    
    // Read operations (any node)
    auto content = client.read_file_string("docs/hello.txt");
    if (content.ok()) {
        std::cout << content.value << std::endl;
    }
    
    auto entries = client.list_directory("docs");
    if (entries.ok()) {
        for (const auto& entry : entries.value) {
            std::cout << entry << std::endl;
        }
    }
    
    client.shutdown();
    return 0;
}
```

## API Reference

### Write Operations

These operations require the node to be the leader. They return `Result<void>` with status:

- `create_file(path)` - Create a new file (idempotent)
- `write_file(path, data)` - Write data to file, overwriting existing content
- `append_file(path, data)` - Append data to file
- `delete_file(path)` - Delete a file (idempotent)
- `create_directory(path)` - Create a directory (idempotent)
- `delete_directory(path)` - Delete an empty directory
- `rename(old_path, new_path)` - Rename/move a file or directory

### Read Operations

These operations can be performed on any node:

- `read_file(path)` → `Result<vector<uint8_t>>` - Read file contents as bytes
- `read_file_string(path)` → `Result<string>` - Read file contents as string
- `list_directory(path)` → `Result<vector<string>>` - List directory entries
- `stat(path)` → `Result<FileInfo>` - Get file/directory metadata
- `exists(path)` → `Result<bool>` - Check if path exists

### Status Operations

- `is_leader()` → `bool` - Check if this node is the leader
- `get_leader()` → `string` - Get the current leader's peer ID
- `get_commit_index()` → `int64_t` - Get current commit index

### Result Type

All operations return a `Result<T>` type:

```cpp
Result<T> result = client.some_operation();

if (result.ok()) {
    // Success - use result.value
    auto value = result.value;
} else {
    // Error - check result.status and result.error_message
    std::cerr << "Error: " << result.error_message << std::endl;
}
```

Status codes: `OK`, `NOT_FOUND`, `ALREADY_EXISTS`, `NOT_LEADER`, `NO_LEADER`, `IO_ERROR`, `INVALID_PATH`, `NOT_DIRECTORY`, `DIRECTORY_NOT_EMPTY`, `RAFT_ERROR`

### Read consistency

Followers serve reads from local storage but check their commit index. This provides:
- **Low latency**: No network round-trip for reads
- **Strong consistency**: Reads see committed writes
- **High availability**: Reads work even if leader is unavailable

## License
This project is licensed under the MIT License.
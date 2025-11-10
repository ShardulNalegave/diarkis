# Diarkis Replicated Filesystem
A distributed, replicated filesystem built with Raft consensus in C++. Diarkis provides strong consistency guarantees for file operations across multiple nodes, ensuring data durability and high availability.

## Features
- **Raft Consensus**: Built on bRaft for leader election and log replication
- **Strong Consistency**: All write operations go through consensus
- **TCP-based RPC**: MessagePack protocol for efficient client-server communication
- **File Operations**: Create, read, write, append, delete files and directories

## Architecture
Diarkis consists of two main components:

- **Server**: Handles Raft consensus, state machine operations, and RPC requests
- **Client Library**: Provides a simple C++ API for filesystem operations

## Prerequisites
- C++17 compatible compiler
- CMake 3.10+
- bRaft
- brpc
- gflags
- spdlog
- yaml-cpp
- msgpack-c

## Building

### Server

```bash
mkdir build && cd build
cmake ..
make
```

### Client Library Integration
Add to your `CMakeLists.txt`:

```cmake
add_subdirectory(client/cpp)
target_link_libraries(your_target diarkis_client)
```

## Running the Server
Create a configuration file `config.yaml`:

```yaml
storage:
  base_path: "./data"

raft:
  path: "./raft"
  group_id: "diarkis_fs"
  peer_addr: "127.0.0.1:8100"
  initial_conf: "127.0.0.1:8100"
  election_timeout_ms: 5000
  snapshot_interval: 3600

rpc:
  addr: "0.0.0.0"
  port: 9100
```

Start the server:

```bash
./diarkis_server --config=config.yaml
```

## Client Usage Example

```cpp
#include "diarkis_client/client.h"
#include <iostream>

int main() {
    // Connect to server
    diarkis_client::Client client("127.0.0.1", 9100);
    
    // Create a file
    std::string path = "test.txt";
    if (client.create_file(path) == 0) {
        std::cout << "File created successfully\n";
    }
    
    // Write data
    std::string data = "Hello, Diarkis!";
    if (client.write_file(path, reinterpret_cast<uint8_t*>(data.data()), data.size()) == 0) {
        std::cout << "Data written successfully\n";
    }
    
    // Read data
    uint8_t buffer[1024];
    size_t bytes_read = client.read_file(path, buffer);
    std::cout << "Read " << bytes_read << " bytes\n";
    
    // List directory
    std::string dir_path = "/";
    auto entries = client.list_directory(dir_path);
    for (const auto& entry : entries) {
        std::cout << entry << "\n";
    }
    
    return 0;
}
```

## Configuration Options

### Command Line Flags

All configuration options can be overridden via command line:

```bash
./diarkis_server \
  --base_path=./data \
  --raft_path=./raft \
  --peer_addr=127.0.0.1:8100 \
  --rpc_port=9100 \
  --log_level=debug
```

## Protocol
Diarkis uses a simple length-prefixed MessagePack protocol:
```
[4 bytes length (network order)][msgpack data]
```

All commands are serialized using MessagePack and sent over TCP connections.

## License
This project is licensed under the MIT License.
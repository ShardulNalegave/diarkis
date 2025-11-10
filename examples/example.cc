
#include <iostream>
#include <cstring>
#include "diarkis_client/client.h"
#include "spdlog/spdlog.h"

void print_separator(const std::string& title) {
    std::cout << "\n========== " << title << " ==========\n" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <server_address> <server_port>" << std::endl;
        return 1;
    }
    
    std::string address = argv[1];
    uint16_t port = std::atoi(argv[2]);
    
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Connecting to Diarkis server at {}:{}", address, port);
    
    diarkis_client::Client client(address, port);
    
    // Test 1: Create directories
    print_separator("Test 1: Create Directories");
    {
        std::string dir1 = "test_dir";
        std::string dir2 = "test_dir/subdir";
        
        if (client.create_directory(dir1) == 0) {
            std::cout << "✓ Created directory: " << dir1 << std::endl;
        } else {
            std::cout << "✗ Failed to create directory: " << dir1 << std::endl;
        }
        
        if (client.create_directory(dir2) == 0) {
            std::cout << "✓ Created directory: " << dir2 << std::endl;
        } else {
            std::cout << "✗ Failed to create directory: " << dir2 << std::endl;
        }
    }
    
    // Test 2: Create files
    print_separator("Test 2: Create Files");
    {
        std::string file1 = "test_file.txt";
        std::string file2 = "test_dir/nested_file.txt";
        
        if (client.create_file(file1) == 0) {
            std::cout << "✓ Created file: " << file1 << std::endl;
        } else {
            std::cout << "✗ Failed to create file: " << file1 << std::endl;
        }
        
        if (client.create_file(file2) == 0) {
            std::cout << "✓ Created file: " << file2 << std::endl;
        } else {
            std::cout << "✗ Failed to create file: " << file2 << std::endl;
        }
    }
    
    // Test 3: Write to file
    print_separator("Test 3: Write to File");
    {
        std::string path = "test_file.txt";
        std::string content = "Hello, Diarkis! This is a test file.\n";
        
        if (client.write_file(path, reinterpret_cast<uint8_t*>(const_cast<char*>(content.c_str())), content.size()) == 0) {
            std::cout << "✓ Wrote " << content.size() << " bytes to: " << path << std::endl;
        } else {
            std::cout << "✗ Failed to write to file: " << path << std::endl;
        }
    }
    
    // Test 4: Append to file
    print_separator("Test 4: Append to File");
    {
        std::string path = "test_file.txt";
        std::string content = "This line was appended.\n";
        
        if (client.append_file(path, reinterpret_cast<uint8_t*>(const_cast<char*>(content.c_str())), content.size()) == 0) {
            std::cout << "✓ Appended " << content.size() << " bytes to: " << path << std::endl;
        } else {
            std::cout << "✗ Failed to append to file: " << path << std::endl;
        }
    }
    
    // Test 5: Read from file
    print_separator("Test 5: Read from File");
    {
        std::string path = "test_file.txt";
        uint8_t buffer[1024];
        std::memset(buffer, 0, sizeof(buffer));
        
        size_t bytes_read = client.read_file(path, buffer);
        if (bytes_read > 0) {
            std::cout << "✓ Read " << bytes_read << " bytes from: " << path << std::endl;
            std::cout << "Content:\n" << std::string(reinterpret_cast<char*>(buffer), bytes_read) << std::endl;
        } else {
            std::cout << "✗ Failed to read file: " << path << std::endl;
        }
    }
    
    // Test 6: Write to nested file
    print_separator("Test 6: Write to Nested File");
    {
        std::string path = "test_dir/nested_file.txt";
        std::string content = "This is a nested file in a subdirectory.\n";
        
        if (client.write_file(path, reinterpret_cast<uint8_t*>(const_cast<char*>(content.c_str())), content.size()) == 0) {
            std::cout << "✓ Wrote " << content.size() << " bytes to: " << path << std::endl;
        } else {
            std::cout << "✗ Failed to write to file: " << path << std::endl;
        }
    }
    
    // Test 7: List directory contents
    print_separator("Test 7: List Directory Contents");
    {
        std::string root_path = "";
        auto root_entries = client.list_directory(root_path);
        std::cout << "Root directory (/): " << root_entries.size() << " entries" << std::endl;
        for (const auto& entry : root_entries) {
            std::cout << "  - " << entry << std::endl;
        }
        
        std::string dir_path = "test_dir";
        auto dir_entries = client.list_directory(dir_path);
        std::cout << "\nDirectory (test_dir/): " << dir_entries.size() << " entries" << std::endl;
        for (const auto& entry : dir_entries) {
            std::cout << "  - " << entry << std::endl;
        }
    }
    
    // Test 8: Rename file
    print_separator("Test 8: Rename File");
    {
        std::string old_path = "test_file.txt";
        std::string new_path = "renamed_file.txt";
        
        if (client.rename_file(old_path, new_path) == 0) {
            std::cout << "✓ Renamed: " << old_path << " -> " << new_path << std::endl;
        } else {
            std::cout << "✗ Failed to rename file" << std::endl;
        }
        
        // Verify rename by reading
        uint8_t buffer[1024];
        std::memset(buffer, 0, sizeof(buffer));
        size_t bytes_read = client.read_file(new_path, buffer);
        if (bytes_read > 0) {
            std::cout << "✓ Verified renamed file exists and is readable" << std::endl;
        }
    }
    
    // Test 9: Multiple append operations
    print_separator("Test 9: Multiple Append Operations");
    {
        std::string path = "test_dir/append_test.txt";
        
        // Create file
        if (client.create_file(path) == 0) {
            std::cout << "✓ Created file: " << path << std::endl;
        }
        
        // Append multiple times
        for (int i = 1; i <= 5; i++) {
            std::string line = "Line " + std::to_string(i) + "\n";
            if (client.append_file(path, reinterpret_cast<uint8_t*>(const_cast<char*>(line.c_str())), line.size()) == 0) {
                std::cout << "✓ Appended line " << i << std::endl;
            }
        }
        
        // Read and display
        uint8_t buffer[1024];
        std::memset(buffer, 0, sizeof(buffer));
        size_t bytes_read = client.read_file(path, buffer);
        if (bytes_read > 0) {
            std::cout << "\nFinal content:\n" << std::string(reinterpret_cast<char*>(buffer), bytes_read) << std::endl;
        }
    }
    
    // Test 10: Delete files
    print_separator("Test 10: Delete Files");
    {
        std::string file1 = "renamed_file.txt";
        std::string file2 = "test_dir/nested_file.txt";
        std::string file3 = "test_dir/append_test.txt";
        
        if (client.delete_file(file1) == 0) {
            std::cout << "✓ Deleted file: " << file1 << std::endl;
        } else {
            std::cout << "✗ Failed to delete file: " << file1 << std::endl;
        }
        
        if (client.delete_file(file2) == 0) {
            std::cout << "✓ Deleted file: " << file2 << std::endl;
        } else {
            std::cout << "✗ Failed to delete file: " << file2 << std::endl;
        }
        
        if (client.delete_file(file3) == 0) {
            std::cout << "✓ Deleted file: " << file3 << std::endl;
        } else {
            std::cout << "✗ Failed to delete file: " << file3 << std::endl;
        }
    }
    
    // Test 11: Delete directories
    print_separator("Test 11: Delete Directories");
    {
        std::string dir1 = "test_dir/subdir";
        std::string dir2 = "test_dir";
        
        if (client.delete_directory(dir1) == 0) {
            std::cout << "✓ Deleted directory: " << dir1 << std::endl;
        } else {
            std::cout << "✗ Failed to delete directory: " << dir1 << std::endl;
        }
        
        if (client.delete_directory(dir2) == 0) {
            std::cout << "✓ Deleted directory: " << dir2 << std::endl;
        } else {
            std::cout << "✗ Failed to delete directory: " << dir2 << std::endl;
        }
    }
    
    // Test 12: List root directory after cleanup
    print_separator("Test 12: List Root After Cleanup");
    {
        std::string root_path = "";
        auto entries = client.list_directory(root_path);
        std::cout << "Root directory (/): " << entries.size() << " entries" << std::endl;
        for (const auto& entry : entries) {
            std::cout << "  - " << entry << std::endl;
        }
    }
    
    print_separator("All Tests Completed");
    std::cout << "Example application finished successfully!" << std::endl;
    
    return 0;
}

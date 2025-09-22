#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>

// Function to print the buffer in a classic hex dump format
void print_hex_dump(const std::vector<char>& buffer) {
    for (size_t i = 0; i < buffer.size(); ++i) {
        // Print address at the beginning of each line
        if (i % 16 == 0) {
            std::cout << std::setw(8) << std::setfill('0') << std::hex << i << ": ";
        }

        // Print hex value of the character
        std::cout << std::setw(2) << std::setfill('0') << std::hex << (0xFF & static_cast<int>(buffer[i])) << " ";

        // Print ASCII representation at the end of the line
        if ((i + 1) % 16 == 0 || i == buffer.size() - 1) {
            // Pad the last line if it's not a full 16 bytes
            if (i == buffer.size() - 1 && (i + 1) % 16 != 0) {
                size_t remainder = 16 - ((i + 1) % 16);
                for (size_t j = 0; j < remainder; ++j) {
                    std::cout << "   ";
                }
            }
            
            std::cout << " ";
            // Print ASCII characters for the current line
            for (size_t j = i - (i % 16); j <= i; ++j) {
                char c = buffer[j];
                if (c >= 32 && c <= 126) {
                    std::cout << c;
                } else {
                    std::cout << ".";
                }
            }
            std::cout << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Could not open file: " << filename << std::endl;
        return 1;
    }

    // Read the entire file into a vector of chars
    std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    
    std::cout << "Hex dump for " << filename << ":" << std::endl;
    print_hex_dump(buffer);

    return 0;
}

#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <cstdint>

void dump_vgm_commands(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return;
    }

    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    uint32_t data_offset = 0x40;
    if (fileSize >= 0x38) {
        uint32_t vgm_data_offset_val = *reinterpret_cast<uint32_t*>(&buffer[0x34]);
        if (vgm_data_offset_val != 0) {
            data_offset = 0x34 + vgm_data_offset_val;
        }
    }

    std::cout << "Starting command dump from offset 0x" << std::hex << data_offset << std::dec << std::endl;

    uint32_t i = data_offset;
    while (i < fileSize) {
        uint8_t cmd = buffer[i];
        std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0') << i << ": ";
        if (cmd == 0xbc) {
            if (i + 2 < fileSize) {
                uint8_t port = buffer[i + 1];
                uint8_t val = buffer[i + 2];
                std::cout << "0xbc (WS Write) - Port: 0x" << std::hex << (int)port << ", Val: 0x" << (int)val << std::dec << std::endl;
                i += 3;
            } else {
                std::cout << "Incomplete 0xbc command" << std::endl;
                break;
            }
        } else if (cmd == 0xc6) {
             if (i + 3 < fileSize) {
                uint16_t addr = (buffer[i + 2] << 8) | buffer[i + 1];
                uint8_t val = buffer[i + 3];
                std::cout << "0xc6 (RAM Write) - Addr: 0x" << std::hex << (int)addr << ", Val: 0x" << (int)val << std::dec << std::endl;
                i += 4;
            } else {
                std::cout << "Incomplete 0xc6 command" << std::endl;
                break;
            }
        }
        else if (cmd == 0x61) {
            i += 3;
        } else if (cmd == 0x62 || cmd == 0x63) {
            i += 1;
        } else if (cmd >= 0x70 && cmd <= 0x7f) {
            i += 1;
        } else if (cmd == 0x66) {
            std::cout << "0x66 (End of Data)" << std::endl;
            break;
        }
        else {
            // Skip other commands for brevity
            if (cmd >= 0x51 && cmd <= 0x5f) i += 3;
            else if (cmd == 0x4f || cmd == 0x50) i += 2;
            else if (cmd == 0x67) {
                if (i + 6 < fileSize) {
                    i += 6 + (*reinterpret_cast<uint32_t*>(&buffer[i + 2]));
                } else {
                    break;
                }
            }
            else {
                i++;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.vgm>" << std::endl;
        return 1;
    }
    dump_vgm_commands(argv[1]);
    return 0;
}

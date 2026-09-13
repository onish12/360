// SPDX-License-Identifier: MIT
#include "firmware_image.h"
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    using namespace phaser360::sof;
    if (argc != 2) {
        std::cerr << "Usage: phaser360_sof_inspect <firmware.ri>\n"
                     "Offline structural inspection only; never installs or loads firmware.\n";
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file) { std::cerr << "Cannot open firmware file.\n"; return 2; }
    const std::streamoff length = file.tellg();
    if (length <= 0 || length > static_cast<std::streamoff>(kMaxImageBytes)) {
        std::cerr << "Invalid firmware file length.\n";
        return 2;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) {
        std::cerr << "Cannot read complete firmware file.\n";
        return 2;
    }
    FirmwareImage image{};
    const ParseStatus status = ParseFirmwareImage(bytes.data(), bytes.size(), &image);
    if (status != ParseStatus::Ok) {
        std::cerr << "STRUCTURE=FAIL REASON=" << ParseStatusName(status) << '\n';
        return 1;
    }
    std::cout << "{\n"
              << "  \"structure\": \"PASS\",\n"
              << "  \"profile\": \"APL_GLK_CSE_V1_8\",\n"
              << "  \"signature_verification\": \"NOT_PERFORMED\",\n"
              << "  \"hardware_access\": \"NONE\",\n"
              << "  \"installable\": false,\n"
              << "  \"payload_offset\": " << image.payload_offset << ",\n"
              << "  \"payload_bytes\": " << image.payload_bytes << ",\n"
              << "  \"adsp_header_offset\": " << image.adsp_header_offset << ",\n"
              << "  \"module_table_offset\": " << image.module_table_offset << ",\n"
              << "  \"module_count\": " << image.module_count << ",\n"
              << "  \"file_backed_segments\": " << image.file_backed_segments << ",\n"
              << "  \"extended_element_count\": " << image.extended_element_count << ",\n"
              << "  \"preload_pages\": " << image.preload_pages << ",\n"
              << "  \"version\": \"" << image.major << '.' << image.minor << '.'
              << image.hotfix << '.' << image.build << "\"\n}\n";
    return 0;
}

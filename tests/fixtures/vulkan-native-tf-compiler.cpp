// SPDX-License-Identifier: GPL-3.0-or-later
#include "compiler.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc != 4 && argc != 5) throw std::runtime_error("usage: compiler input.spv output.metal metadata.json [16|32]");
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        auto size = file.tellg();
        if (!file || size <= 0 || size % 4 || size > (1 << 20)) throw std::runtime_error("invalid SPIR-V file");
        std::vector<uint32_t> code(size_t(size) / 4);
        file.seekg(0);file.read(reinterpret_cast<char*>(code.data()), size);
        trames::tf::Compiler compiler(code);
        auto options = compiler.get_msl_options();
        options.msl_version = spirv_cross::CompilerMSL::Options::make_msl_version(2, 3);
        options.enable_point_size_builtin = false;
        if (argc == 5) {
            if (std::string(argv[4]) == "16") options.vertex_index_type = spirv_cross::CompilerMSL::Options::IndexType::UInt16;
            else if (std::string(argv[4]) == "32") options.vertex_index_type = spirv_cross::CompilerMSL::Options::IndexType::UInt32;
            else throw std::runtime_error("invalid index type");
        }
        compiler.set_msl_options(options);
        const auto program = compiler.compile_feedback();
        if (!program.capture_stride || program.capture_stride > 2048) throw std::runtime_error("invalid Metal capture stride");
        std::ofstream output(argv[2]); output << program.source;
        std::ofstream metadata(argv[3]);
        metadata << "{\"entry\":\"" << program.capture_entry << "\",\"buffers\":" << program.buffers
                 << ",\"capture_stride\":" << program.capture_stride << ",\"scalars\":" << program.scalars << ",\"strides\":[";
        for (unsigned i = 0; i < 4; ++i) metadata << (i ? "," : "") << program.strides[i];
        metadata << "]}\n";
        if (!output || !metadata) throw std::runtime_error("could not write generated shader");
        std::cout << "original-spirv-cross-tf-ok: " << program.scalars << " scalars; buffers=" << program.buffers << '\n';
    } catch (const std::exception& e) {
        std::cerr << "TF compiler rejected: " << e.what() << '\n';return 1;
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "spirv_msl.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace trames { namespace tf {
// Compiles GPU shaders only. The original SPIRV-Cross vertex-as-compute
// lowering, resource accesses and vertex fetch remain unchanged. No CPU JIT.
struct Program {
    std::string source;
    std::string capture_entry;
    std::array<uint32_t, 4> strides{};
    uint32_t buffers = 0;
    uint32_t scalars = 0;
};

class Compiler : public spirv_cross::CompilerMSL {
public:
    using CompilerMSL::CompilerMSL;
    // Configure resources, stage inputs, index type and specialization through
    // the original CompilerMSL API before calling this exactly once.
    Program compile_feedback();
private:
    struct Semantic {
        uint32_t location, component, builtin;
        bool operator<(const Semantic& b) const;
    };
    struct Scalar { Semantic semantic; uint32_t buffer, offset; };
    std::vector<Scalar> scalars_;
    Program program_;
    void reflect_outputs();
    uint32_t reflect_value(const spirv_cross::SPIRType& type, Semantic semantic,
                           uint32_t buffer, uint32_t offset);
    void reflect_stride(uint32_t buffer, uint32_t stride);
    std::map<Semantic, std::string> stage_values(const std::string& prefix);
    void stage_value(std::map<Semantic, std::string>& values, const spirv_cross::SPIRType& type,
                     Semantic semantic, const std::string& expression);
    void append_helpers(Compiler& raster);
};
}}

// SPDX-License-Identifier: GPL-3.0-or-later
#include <metal_stdlib>
using namespace metal;

// Private GPU ABI, not guest memory. Bound buffers are already authenticated by
// the Vulkan backend. Sizes/offsets are bytes, relative to the bound range.
struct TFParams {
    uint vertices, instances, topology, restart;
    uint indexType, firstIndex, buffers, mapCapacity;
    uint4 sizes;
    uint4 strides;
};
struct TFState {
    uint4 cursors;
    ulong written, needed;
};
// draw[0] = TF vertices; draw[1] = raster vertices; draw[4..7] = starting bytes.
// indirect[0..3] is a Metal DrawPrimitivesIndirectArguments on the GPU.
kernel void trames_tf_assemble(constant TFParams& p [[buffer(0)]],
    const device uint* indices [[buffer(1)]], device uint* map [[buffer(2)]],
    device uint* draw [[buffer(3)]], device TFState& state [[buffer(4)]],
    device uint* indirect [[buffer(5)]], uint tid [[thread_position_in_grid]]) {
    if (tid) return;
    uint primitiveSize = p.topology == 0 ? 1u : (p.topology <= 2 ? 2u : 3u);
    uint capacity = 0xffffffffu;
    for (uint b = 0; b < 4; ++b) {
        draw[4+b] = state.cursors[b];
        if (p.buffers & (1u << b)) {
            uint available = state.cursors[b] <= p.sizes[b] ? p.sizes[b] - state.cursors[b] : 0u;
            capacity = min(capacity, available / p.strides[b]);
        }
    }
    uint written = 0, needed = 0;
    bool full = false;
    if (!p.restart) {
        // Common draws use arithmetic counts and a parallel map dispatch below.
        // Never serialize every vertex through this control thread.
        uint primitives = p.topology == 0 ? p.vertices : p.topology == 1 ? p.vertices/2 :
            p.topology == 2 ? (p.vertices ? p.vertices-1 : 0) : p.topology == 3 ? p.vertices/3 :
            (p.vertices >= 2 ? p.vertices-2 : 0);
        needed = primitives * p.instances * primitiveSize;
        written = min(needed, capacity/primitiveSize*primitiveSize);
    }
    for (uint instance = 0; p.restart && instance < p.instances; ++instance) {
        uint pending[3] = {0, 0, 0};
        uint count = 0, parity = 0;
        for (uint vtx = 0; vtx < p.vertices; ++vtx) {
            uint source = instance * p.vertices + vtx;
            uint index = vtx;
            if (p.indexType == 16) index = reinterpret_cast<const device ushort*>(indices)[p.firstIndex+vtx];
            else if (p.indexType == 32) index = indices[p.firstIndex+vtx];
            uint restart = p.indexType == 16 ? 0xffffu : 0xffffffffu;
            if (p.restart && p.indexType && index == restart) { count = 0; parity = 0; continue; }
            pending[count++] = source;
            if (count < primitiveSize) continue;
            // All assembled primitives feed rasterization, even after TF is
            // full. Overflow never disables rendering or partially writes one.
            if (needed <= p.mapCapacity && primitiveSize <= p.mapCapacity-needed) {
                for (uint v = 0; v < primitiveSize; ++v) {
                    // Vulkan's default first provoking vertex is preserved by
                    // these rotations, in addition to preserving winding.
                    uint n = p.topology == 4 && parity && v > 0 ? 3u-v : p.topology == 5 ? (v+1u)%3u : v;
                    map[needed+v] = pending[n];
                }
            }
            needed += primitiveSize;
            if (!full && primitiveSize <= capacity-written) written += primitiveSize;
            else full = true;
            if (p.topology == 2) { pending[0] = pending[1]; count = 1; }
            else if (p.topology == 4) { pending[0] = pending[1]; pending[1] = pending[2]; count = 2; parity ^= 1; }
            else if (p.topology == 5) { pending[1] = pending[2]; count = 2; }
            else count = 0;
        }
    }
    // The host must reserve the checked worst case (3 * vertices * instances).
    // On a violated internal contract, prevent scatter/replay from using OOB
    // data. draw[2] is an internal validation failure, not guest success.
    draw[2] = needed > p.mapCapacity;
    draw[0] = needed <= p.mapCapacity ? written : 0;
    draw[1] = needed <= p.mapCapacity ? needed : 0;
    draw[3] = 0;
    if (needed <= p.mapCapacity) {
        state.written += written / primitiveSize;
        state.needed += needed / primitiveSize;
        for (uint b = 0; b < 4; ++b)
            if (p.buffers & (1u << b)) state.cursors[b] += written * p.strides[b];
    }
    indirect[0] = draw[1]; indirect[1] = 1; indirect[2] = 0; indirect[3] = 0;
}

kernel void trames_tf_map(constant TFParams& p [[buffer(0)]], const device uint* draw [[buffer(1)]],
    device uint* map [[buffer(2)]], uint i [[thread_position_in_grid]]) {
    if (p.restart || i >= draw[1]) return;
    uint size = p.topology == 0 ? 1u : p.topology <= 2 ? 2u : 3u;
    uint primitives = p.topology == 0 ? p.vertices : p.topology == 1 ? p.vertices/2 :
        p.topology == 2 ? (p.vertices ? p.vertices-1 : 0) : p.topology == 3 ? p.vertices/3 :
        (p.vertices >= 2 ? p.vertices-2 : 0);
    uint perInstance = primitives*size;
    uint instance = i/perInstance, local = i%perInstance, primitive = local/size, vtx = local%size;
    uint source = local;
    if (p.topology == 2) source = primitive+vtx;
    else if (p.topology == 4) source = primitive+((primitive&1u) && vtx>0 ? 3u-vtx : vtx);
    else if (p.topology == 5) source = vtx < 2 ? primitive+vtx+1 : 0;
    map[i] = instance*p.vertices+source;
}

// Optional begin/end counters remain GPU buffers, including pause/resume.
kernel void trames_tf_begin(device TFState& state [[buffer(0)]], constant uint& mask [[buffer(1)]],
    const device uint* c0 [[buffer(2)]], const device uint* c1 [[buffer(3)]],
    const device uint* c2 [[buffer(4)]], const device uint* c3 [[buffer(5)]], uint i [[thread_position_in_grid]]) {
    if (i) return;
    state.cursors = uint4(mask&1 ? *c0 : 0, mask&2 ? *c1 : 0, mask&4 ? *c2 : 0, mask&8 ? *c3 : 0);
    state.written = 0; state.needed = 0;
}
kernel void trames_tf_end(const device TFState& state [[buffer(0)]], constant uint& mask [[buffer(1)]],
    device uint* c0 [[buffer(2)]], device uint* c1 [[buffer(3)]],
    device uint* c2 [[buffer(4)]], device uint* c3 [[buffer(5)]], uint i [[thread_position_in_grid]]) {
    if (i) return;
    if (mask&1) *c0=state.cursors[0]; if (mask&2) *c1=state.cursors[1];
    if (mask&4) *c2=state.cursors[2]; if (mask&8) *c3=state.cursors[3];
}
kernel void trames_tf_byte_count(const device uint* counter [[buffer(0)]],
    constant uint4& p [[buffer(1)]], device uint* indirect [[buffer(2)]], uint i [[thread_position_in_grid]]) {
    if (i) return;
    uint bytes = *counter;
    indirect[0] = bytes >= p.x && p.y ? (bytes-p.x)/p.y : 0;
    indirect[1] = p.z; indirect[2] = 0; indirect[3] = p.w;
}

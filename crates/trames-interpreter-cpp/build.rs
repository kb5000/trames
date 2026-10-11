// SPDX-License-Identifier: GPL-3.0-or-later
fn main() {
    println!("cargo:rerun-if-changed=../../native/interpreter-cpp/engine.cpp");
    println!("cargo:rerun-if-changed=../../native/interpreter-cpp/trames_cpu.h");
    cc::Build::new()
        .cpp(true)
        .std("c++17")
        .cpp_link_stdlib(None)
        .flag_if_supported("-fno-exceptions")
        .flag_if_supported("-fno-rtti")
        .file("../../native/interpreter-cpp/engine.cpp")
        .compile("trames_interpreter_cpp");
}

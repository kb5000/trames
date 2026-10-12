// SPDX-License-Identifier: GPL-3.0-or-later
//! Assemble the original pinned Arm routines, with symbol/object-format adaptation.
use std::{collections::BTreeMap, env, fs, path::Path};
fn main() {
    if env::var("CARGO_CFG_TARGET_ARCH").as_deref() != Ok("aarch64") {
        return;
    }
    let apple = env::var("CARGO_CFG_TARGET_VENDOR").as_deref() == Ok("apple");
    for name in ["memcpy", "memset"] {
        let path = format!("upstream/musl-1.2.5/aarch64/{name}.S");
        println!("cargo:rerun-if-changed={path}");
        let source = fs::read_to_string(path).expect("vendored musl source");
        let mut aliases = BTreeMap::new();
        let symbol = format!("{}trames_native_{name}", if apple { "_" } else { "" });
        aliases.insert(name.to_owned(), symbol);
        let mut asm = String::new();
        for line in source.lines() {
            let trimmed = line.trim();
            if let Some(define) = trimmed.strip_prefix("#define ") {
                let words: Vec<_> = define.split_whitespace().collect();
                assert_eq!(words.len(), 2, "only register aliases are accepted");
                aliases.insert(words[0].to_owned(), words[1].to_owned());
                continue;
            }
            // This upstream conditional is unset; retain its DCZID runtime check.
            if trimmed == "#ifndef SKIP_ZVA_CHECK" || trimmed == "#endif" {
                continue;
            }
            assert!(
                !trimmed.starts_with('#'),
                "unexpected upstream preprocessing"
            );
            if apple && (trimmed.starts_with(".type ") || trimmed.starts_with(".size ")) {
                continue;
            }
            let mut word = String::new();
            for ch in line.chars().chain(std::iter::once('\n')) {
                if ch.is_ascii_alphanumeric() || ch == '_' {
                    word.push(ch);
                } else {
                    let mut replacement = word.as_str();
                    while let Some(alias) = aliases.get(replacement) {
                        replacement = alias;
                    }
                    asm.push_str(replacement);
                    word.clear();
                    asm.push(ch);
                }
            }
        }
        if apple {
            // Mach-O's assembler-local prefix is L, not ELF's .L. Keeping
            // .L makes ThinLTO treat conditional targets as external symbols.
            asm = asm.replace(".L", &format!("Ltrames_native_{name}_"));
        }
        fs::write(
            Path::new(&env::var_os("OUT_DIR").unwrap()).join(format!("{name}.s")),
            asm,
        )
        .expect("write static assembly");
    }
}

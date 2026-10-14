// SPDX-License-Identifier: GPL-3.0-or-later
use std::{env, path::Path};
fn main() {
    println!("cargo:rustc-check-cfg=cfg(trames_moltenvk)");
    println!("cargo:rerun-if-env-changed=TRAMES_MOLTENVK_STATIC_DIR");
    if let Some(directory) = env::var_os("TRAMES_MOLTENVK_STATIC_DIR") {
        let target = env::var("TARGET").unwrap();
        assert!(
            target.contains("apple"),
            "MoltenVK static linking is Apple-only"
        );
        assert!(
            Path::new(&directory).join("libMoltenVK.a").is_file(),
            "missing pinned native MoltenVK archive"
        );
        println!("cargo:rustc-cfg=trames_moltenvk");
        println!(
            "cargo:rustc-link-search=native={}",
            Path::new(&directory).display()
        );
        println!("cargo:rustc-link-lib=static=MoltenVK");
        println!("cargo:rustc-link-lib=c++");
        for framework in [
            "Metal",
            "Foundation",
            "QuartzCore",
            "IOSurface",
            "CoreGraphics",
            "CoreVideo",
            "MetalPerformanceShaders",
        ] {
            println!("cargo:rustc-link-lib=framework={framework}");
        }
        println!(
            "cargo:rustc-link-lib=framework={}",
            if target.contains("ios") {
                "UIKit"
            } else {
                "AppKit"
            }
        );
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
use std::{
    env,
    path::{Path, PathBuf},
    process::Command,
};
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
        let archive = Path::new(&directory).join("libMoltenVK.a");
        println!("cargo:rerun-if-changed={}", archive.display());
        // Apple's simulator XCFramework contains a fat archive; rustc's native
        // static bundler needs an ordinary archive for the selected CPU. Keep
        // the verified upstream bytes unchanged and thin only into OUT_DIR.
        let bytes = std::fs::read(&archive).expect("read pinned MoltenVK archive");
        let directory = if bytes.starts_with(b"!<arch>\n") {
            PathBuf::from(directory)
        } else {
            let arch = match env::var("CARGO_CFG_TARGET_ARCH").unwrap().as_str() {
                "aarch64" => "arm64",
                "x86_64" => "x86_64",
                _ => panic!("unsupported MoltenVK target architecture"),
            };
            let out = PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("moltenvk");
            std::fs::create_dir_all(&out).expect("create thin archive directory");
            let status = Command::new("xcrun")
                .arg("lipo")
                .arg(&archive)
                .args(["-thin", arch, "-output"])
                .arg(out.join("libMoltenVK.a"))
                .status()
                .expect("xcrun lipo is required for universal MoltenVK archives");
            assert!(status.success(), "failed to thin MoltenVK for {arch}");
            out
        };
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

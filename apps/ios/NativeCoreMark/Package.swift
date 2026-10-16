// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "NativeCoreMark",
    products: [.library(name: "NativeCoreMark", targets: ["CCoreMark"])],
    targets: [.target(name: "CCoreMark", cSettings: [
        .define("main", to: "trames_coremark_main"),
        // Match the guest's default make PORT_DIR=linux optimization level.
        .unsafeFlags(["-O2", "-fno-lto"])
    ])]
)

// swift-tools-version:6.0
import PackageDescription
import Foundation
let libraryDirectory = ProcessInfo.processInfo.environment["TRAMES_RUST_LIB_DIR"]
let nativeLinkerSettings: [LinkerSetting] = [.linkedLibrary("trames_ffi")] + (libraryDirectory.map { [.unsafeFlags(["-L", $0])] } ?? [])
let package = Package(
    name: "TramesKit", platforms: [.iOS(.v17), .macOS(.v15)],
    products: [.library(name: "TramesKit", targets: ["TramesKit"])],
    dependencies: [
        .package(path: "../../../vendor/citadel"),
        .package(path: "../../../vendor/swift-nio-ssh"),
        .package(url: "https://github.com/apple/swift-nio.git", exact: "2.81.0"),
        .package(url: "https://github.com/apple/swift-crypto.git", exact: "3.12.3")],
    targets: [
        .systemLibrary(name: "CTrames"),
        .target(name: "TramesKit", dependencies: ["CTrames", .product(name: "Citadel", package: "citadel"),
            .product(name: "NIOSSH", package: "swift-nio-ssh"),
            .product(name: "NIO", package: "swift-nio"),
            .product(name: "Crypto", package: "swift-crypto")],
            linkerSettings: nativeLinkerSettings),
        .testTarget(name: "TramesKitTests", dependencies: ["TramesKit", .product(name: "NIOEmbedded", package: "swift-nio")]),
        .testTarget(name: "NIOSSHRegressionTests", dependencies: [
            .product(name: "NIOSSH", package: "swift-nio-ssh"),
            .product(name: "NIOCore", package: "swift-nio"),
            .product(name: "NIOEmbedded", package: "swift-nio"),
            .product(name: "NIOFoundationCompat", package: "swift-nio")])],
    swiftLanguageModes: [.v5])

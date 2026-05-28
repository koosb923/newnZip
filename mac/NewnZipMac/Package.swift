// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "NewnZipMac",
    platforms: [
        .macOS(.v14)
    ],
    products: [
        .executable(name: "NewnZipMac", targets: ["NewnZipMacApp"])
    ],
    targets: [
        .executableTarget(
            name: "NewnZipMacApp",
            path: "Sources/NewnZipMacApp",
            resources: [
                .copy("Resources")
            ]
        )
    ]
)

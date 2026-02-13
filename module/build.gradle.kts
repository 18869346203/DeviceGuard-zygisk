import com.android.build.gradle.internal.dsl.BaseAppModuleExtension
import java.io.FileInputStream
import java.util.Properties

plugins {
    id("com.android.library")
}

val prop = Properties().apply {
    load(FileInputStream(File(rootDir, "module.prop")))
}

android {
    namespace = "com.example.zygisk.module"
    compileSdk = 34

    defaultConfig {
        minSdk = 26
        targetSdk = 34
        externalNativeBuild {
            cmake {
                arguments(
                    "-DANDROID_STL=c++_shared",    // ✅ 把 none 改成 c++_shared
                    "-DMODULE_NAME=${prop["id"]}"
                )
                abiFilters("arm64-v8a")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
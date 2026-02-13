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
                    "-DANDROID_STL=c++_shared",    // ✅ 你修改的部分，保留
                    "-DMODULE_NAME=${prop["id"]}"   // 从 module.prop 读取模块名
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

// ========== 以下是必须保留的打包任务 ==========
tasks.register("zipRelease") {
    dependsOn("assembleRelease")
    doLast {
        val buildDir = layout.buildDirectory.asFile.get()
        val moduleId = prop["id"] ?: "module"
        val versionName = prop["version"] ?: "1.0"
        val zipFile = File(buildDir, "${moduleId}-${versionName}.zip")
        val zipDir = File(buildDir, "outputs/module")
        zipDir.mkdirs()
        zipDir.listFiles()?.forEach { it.delete() }
        copy {
            from(File(buildDir, "outputs/aar"))
            into(zipDir)
            include("*.aar")
            rename(".*\\.aar", "module.aar")
        }
        copy {
            from(projectDir)
            into(zipDir)
            include("module.prop")
            include("customize.sh")
            include("action.sh")
            include("uninstall.sh")
            include("config.sh")
            include("zygisk/")
        }
        task("zip") {
            doLast {
                zipDir.zip(zipFile)
            }
        }.also { it.actions.forEach { action -> action.execute(it) } }
    }
}
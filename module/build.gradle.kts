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
                    "-DANDROID_STL=c++_shared",    // ✅ 关键：使用标准库
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

// ========== 必须保留的任务：打包模块 ==========
tasks.register("zipRelease") {
    dependsOn("assembleRelease")
    doLast {
        val buildDir = layout.buildDirectory.asFile.get()
        val moduleId = prop["id"] ?: "module"
        val versionName = prop["version"] ?: "1.0"
        val zipFile = File(buildDir, "outputs/module/${moduleId}-${versionName}.zip")
        val zipDir = File(buildDir, "outputs/module")
        zipDir.mkdirs()
        zipDir.listFiles()?.forEach { it.delete() }

        // 复制 AAR（模块编译产物）
        copy {
            from(File(buildDir, "outputs/aar"))
            into(zipDir)
            include("*.aar")
            rename(".*\\.aar", "module.aar")
        }

        // 复制模块必需的文件
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

        // 打包成 ZIP
        tasks.register<Zip>("zip") {
            archiveFileName.set(zipFile.name)
            destinationDirectory.set(zipFile.parentFile)
            from(zipDir)
        }.also { zipTask ->
            zipTask.get().actions.forEach { it.execute(zipTask.get()) }
        }
    }
}
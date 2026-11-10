plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Sample games shipped inside the editor so a fresh install has something to open and run.
val templatesDir = layout.buildDirectory.dir("generated/templates")
val copyTemplates = tasks.register<Copy>("copyTemplates") {
    from("../../games/bullet_heaven") { into("templates/bullet_heaven"); exclude("tests/**") }
    from("../../games/starter") { into("templates/starter") }
    into(templatesDir)
}

android {
    namespace = "com.imbluubyte.aether.editor"
    compileSdk = 34
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "com.imbluubyte.aether.editor"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
        externalNativeBuild { cmake { arguments += listOf("-DANDROID_STL=c++_static") } }
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    sourceSets["main"].assets.srcDir(templatesDir)
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    buildTypes {
        release { isMinifyEnabled = false }
    }
}

tasks.matching { it.name.startsWith("merge") && it.name.endsWith("Assets") }.configureEach { dependsOn(copyTemplates) }

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
}

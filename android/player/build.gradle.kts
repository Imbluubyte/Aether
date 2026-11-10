// Standalone player: a native-only APK (no Kotlin/Java code). The game in -PgameDir (default: the
// bullet heaven demo) is copied into assets/game/ and run by the C++ engine + Lua runtime.
plugins {
    id("com.android.application")
}

val gameDir = (project.findProperty("gameDir") as String?) ?: "../../games/bullet_heaven"
val gameId = (project.findProperty("gameId") as String?) ?: "com.imbluubyte.aether.game"
val gameName = (project.findProperty("gameName") as String?) ?: "Aether Game"
val gameAssets = layout.buildDirectory.dir("generated/game_assets")

val copyGame = tasks.register<Copy>("copyGame") {
    from(gameDir) {
        into("game")
        exclude("tests/**", "**/*.md", ".git/**")
    }
    into(gameAssets)
}

android {
    namespace = "com.imbluubyte.aether.player"
    compileSdk = 34
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = gameId
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
        externalNativeBuild { cmake { arguments += listOf("-DANDROID_STL=c++_static") } }
        resValue("string", "app_name", gameName)
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    sourceSets["main"].assets.srcDir(gameAssets)
    buildTypes {
        release { isMinifyEnabled = false }
    }
}

tasks.matching { it.name.startsWith("merge") && it.name.endsWith("Assets") }.configureEach { dependsOn(copyGame) }

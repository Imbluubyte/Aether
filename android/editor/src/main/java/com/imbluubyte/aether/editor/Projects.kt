package com.imbluubyte.aether.editor

import android.content.Context
import java.io.File

/** Projects live in the app's private storage: files/projects/<name>/ (game.lua, main.lua, scripts/, assets/ ...). */
object Projects {
    fun root(ctx: Context): File = File(ctx.filesDir, "projects").also { it.mkdirs() }
    fun saves(ctx: Context, name: String): File = File(ctx.filesDir, "saves/$name").also { it.mkdirs() }

    fun list(ctx: Context): List<String> =
        root(ctx).listFiles { f -> f.isDirectory }?.map { it.name }?.sorted() ?: emptyList()

    fun validName(name: String) = name.matches(Regex("[A-Za-z0-9_\\- ]{1,40}"))

    /** Copies a bundled template (assets/templates/<template>) into a new project. Returns null on success, else an error. */
    fun create(ctx: Context, name: String, template: String): String? {
        if (!validName(name)) return "Use letters, digits, space, - or _ (max 40 characters)"
        val dest = File(root(ctx), name)
        if (dest.exists()) return "A project named '$name' already exists"
        return try {
            copyAssetDir(ctx, "templates/$template", dest)
            null
        } catch (e: Exception) {
            dest.deleteRecursively()
            "Could not create project: ${e.message}"
        }
    }

    private fun copyAssetDir(ctx: Context, assetPath: String, dest: File) {
        val children = ctx.assets.list(assetPath) ?: emptyArray()
        if (children.isEmpty()) {  // a file
            dest.parentFile?.mkdirs()
            ctx.assets.open(assetPath).use { input -> dest.outputStream().use { input.copyTo(it) } }
        } else {
            dest.mkdirs()
            for (c in children) copyAssetDir(ctx, "$assetPath/$c", File(dest, c))
        }
    }

    /** All editable files in the project (relative paths), skipping binary assets. */
    fun files(project: File): List<String> =
        project.walkTopDown().filter { it.isFile }
            .map { it.relativeTo(project).path.replace('\\', '/') }
            .filter { it.endsWith(".lua") || it.endsWith(".txt") || it.endsWith(".json") }
            .sorted().toList()

    /** Resolves a project-relative path, refusing anything that escapes the project directory. */
    fun resolve(project: File, rel: String): File? {
        val f = File(project, rel).canonicalFile
        return if (f.path.startsWith(project.canonicalPath + File.separator)) f else null
    }
}

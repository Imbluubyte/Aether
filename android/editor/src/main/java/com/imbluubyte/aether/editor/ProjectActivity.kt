package com.imbluubyte.aether.editor

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.text.InputType
import android.view.View
import android.view.WindowManager
import android.widget.*
import java.io.File

/** File list + plain-text code editor for one project, with Play and Run-tests buttons. */
class ProjectActivity : Activity() {
    private lateinit var project: File
    private lateinit var name: String
    private lateinit var fileList: LinearLayout
    private lateinit var editor: EditText
    private lateinit var title: TextView
    private var current: String? = null
    private var dirty = false
    private var loading = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        name = intent.getStringExtra("project") ?: return finish()
        project = File(Projects.root(this), name)
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)

        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val bar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(dp(8), dp(4), dp(8), dp(4)) }
        title = TextView(this).apply { textSize = 16f; typeface = Typeface.DEFAULT_BOLD }
        bar.addView(title, LinearLayout.LayoutParams(0, -2, 1f))
        bar.addView(button("Save") { save() })
        bar.addView(button("New file") { newFile() })
        bar.addView(button("▶ Play") { play(null) })
        bar.addView(button("Run tests") { runTests() })
        root.addView(bar)

        val body = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        fileList = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        body.addView(ScrollView(this).apply { addView(fileList); setBackgroundColor(0xFF0B0E13.toInt()) }, LinearLayout.LayoutParams(dp(200), -1))
        editor = EditText(this).apply {
            typeface = Typeface.MONOSPACE; textSize = 13f; gravity = android.view.Gravity.TOP or android.view.Gravity.START
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD
            setHorizontallyScrolling(true); background = null; setPadding(dp(12), dp(8), dp(12), dp(8))
            addTextChangedListener(object : android.text.TextWatcher {
                override fun afterTextChanged(s: android.text.Editable?) { if (!loading) markDirty() }
                override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
                override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
            })
        }
        body.addView(ScrollView(this).apply { isFillViewport = true; addView(editor) }, LinearLayout.LayoutParams(0, -1, 1f))
        root.addView(body, LinearLayout.LayoutParams(-1, 0, 1f))
        setContentView(root)

        refreshFiles()
        val first = Projects.files(project).firstOrNull { it == "main.lua" } ?: Projects.files(project).firstOrNull()
        if (first != null) open(first) else updateTitle()
    }

    private fun button(text: String, onClick: () -> Unit) = Button(this).apply { this.text = text; isAllCaps = false; setOnClickListener { onClick() } }

    private fun refreshFiles() {
        fileList.removeAllViews()
        for (f in Projects.files(project)) {
            fileList.addView(TextView(this).apply {
                text = f; setPadding(dp(12), dp(8), dp(8), dp(8)); textSize = 13f
                setTextColor(if (f == current) Color.WHITE else Color.LTGRAY)
                if (f == current) setBackgroundColor(0xFF243049.toInt())
                setOnClickListener { switchTo(f) }
            })
        }
    }

    private fun switchTo(rel: String) {
        if (rel == current) return
        if (dirty) save()
        open(rel)
    }

    private fun open(rel: String) {
        val f = Projects.resolve(project, rel) ?: return
        loading = true
        editor.setText(try { f.readText() } catch (e: Exception) { "" })
        loading = false
        current = rel
        dirty = false
        refreshFiles()
        updateTitle()
    }

    private fun markDirty() { if (!dirty) { dirty = true; updateTitle() } }
    private fun updateTitle() { title.text = "$name  ·  ${current ?: "(no file)"}${if (dirty) " •" else ""}" }

    private fun save(): Boolean {
        val rel = current ?: return true
        val f = Projects.resolve(project, rel) ?: return false
        return try {
            f.parentFile?.mkdirs()
            f.writeText(editor.text.toString())
            dirty = false
            updateTitle()
            true
        } catch (e: Exception) {
            Toast.makeText(this, "Save failed: ${e.message}", Toast.LENGTH_LONG).show()
            false
        }
    }

    private fun newFile() {
        val input = EditText(this).apply { hint = "scripts/enemy.lua"; setSingleLine() }
        AlertDialog.Builder(this).setTitle("New file").setView(input).setPositiveButton("Create") { _, _ ->
            var rel = input.text.toString().trim().replace('\\', '/')
            if (rel.isEmpty()) return@setPositiveButton
            if (!rel.contains('.')) rel += ".lua"
            val f = Projects.resolve(project, rel)
            if (f == null) { Toast.makeText(this, "Invalid path", Toast.LENGTH_SHORT).show(); return@setPositiveButton }
            if (dirty) save()
            if (!f.exists()) { f.parentFile?.mkdirs(); f.writeText(if (rel.endsWith(".lua")) "local Script = {}\n\nfunction Script:ready()\nend\n\nreturn Script\n" else "") }
            open(rel)
        }.setNegativeButton("Cancel", null).show()
    }

    private fun play(test: String?) {
        if (dirty && !save()) return
        startActivity(Intent(this, PlayActivity::class.java).putExtra("project", name).apply { if (test != null) putExtra("test", test) })
    }

    private fun runTests() {
        val tests = project.walkTopDown().filter { it.isFile && (it.name == "smoke.lua" || it.name == "test.lua") }.map { it.relativeTo(project).path }.toList()
        when {
            tests.isEmpty() -> Toast.makeText(this, "No tests found (add tests/smoke.lua – see the Lua guide)", Toast.LENGTH_LONG).show()
            tests.size == 1 -> play(tests[0])
            else -> AlertDialog.Builder(this).setTitle("Run which test?").setItems(tests.toTypedArray()) { _, i -> play(tests[i]) }.show()
        }
    }

    override fun onPause() { super.onPause(); if (dirty) save() }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
}

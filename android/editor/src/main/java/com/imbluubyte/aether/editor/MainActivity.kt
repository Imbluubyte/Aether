package com.imbluubyte.aether.editor

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.*

/** Project list: open, create from a template, delete. */
class MainActivity : Activity() {
    private lateinit var list: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(24), dp(24), dp(24), dp(24)) }
        root.addView(TextView(this).apply { text = "Aether"; textSize = 32f; typeface = Typeface.DEFAULT_BOLD })
        root.addView(TextView(this).apply { text = "2D game engine · Lua scripting"; setTextColor(Color.GRAY) })
        val bar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, dp(16), 0, dp(8)) }
        bar.addView(Button(this).apply { text = "New project"; setOnClickListener { newProject() } })
        root.addView(bar)
        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        root.addView(ScrollView(this).apply { addView(list) }, LinearLayout.LayoutParams(-1, 0, 1f))
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        list.removeAllViews()
        val names = Projects.list(this)
        if (names.isEmpty()) list.addView(TextView(this).apply { text = "No projects yet. Tap “New project”."; setTextColor(Color.GRAY) })
        for (n in names) {
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
            row.addView(Button(this).apply {
                text = n; isAllCaps = false; gravity = Gravity.START or Gravity.CENTER_VERTICAL
                setOnClickListener { startActivity(Intent(this@MainActivity, ProjectActivity::class.java).putExtra("project", n)) }
            }, LinearLayout.LayoutParams(0, -2, 1f))
            row.addView(Button(this).apply { text = "Delete"; setOnClickListener { confirmDelete(n) } })
            list.addView(row)
        }
    }

    private fun newProject() {
        val name = EditText(this).apply { hint = "Project name"; setSingleLine() }
        val templates = arrayOf("starter", "bullet_heaven")
        val spinner = Spinner(this).apply { adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, listOf("Empty starter", "Bullet heaven demo")) }
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(20), dp(8), dp(20), 0); addView(name); addView(spinner) }
        AlertDialog.Builder(this).setTitle("New project").setView(box)
            .setPositiveButton("Create") { _, _ ->
                val err = Projects.create(this, name.text.toString().trim(), templates[spinner.selectedItemPosition])
                if (err != null) Toast.makeText(this, err, Toast.LENGTH_LONG).show()
                refresh()
            }.setNegativeButton("Cancel", null).show()
    }

    private fun confirmDelete(name: String) {
        AlertDialog.Builder(this).setTitle("Delete “$name”?").setMessage("This removes the project's files and its save data.")
            .setPositiveButton("Delete") { _, _ ->
                java.io.File(Projects.root(this), name).deleteRecursively()
                Projects.saves(this, name).deleteRecursively()
                refresh()
            }.setNegativeButton("Cancel", null).show()
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
}

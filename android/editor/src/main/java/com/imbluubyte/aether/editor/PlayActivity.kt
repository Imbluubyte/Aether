package com.imbluubyte.aether.editor

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.*
import android.widget.*
import java.io.File
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/** Runs the project in the real engine inside the editor (same C++ core the exported game uses). */
class PlayActivity : Activity() {
    private lateinit var surface: GLSurfaceView
    private lateinit var console: TextView
    private lateinit var consoleScroll: ScrollView
    private lateinit var stats: TextView
    private var handle = 0L
    @Volatile private var stopped = false
    private val ui = Handler(Looper.getMainLooper())
    private var colliders = false
    private var statsOverlay = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val name = intent.getStringExtra("project") ?: return finish()
        val test = intent.getStringExtra("test") ?: ""
        val projectDir = File(Projects.root(this), name)
        handle = AetherNative.nativeCreate(projectDir.path, Projects.saves(this, name).path, test)

        surface = object : GLSurfaceView(this) {
            override fun onGenericMotionEvent(e: MotionEvent): Boolean {
                if (e.source and InputDevice.SOURCE_JOYSTICK != 0 && e.action == MotionEvent.ACTION_MOVE) {
                    for (a in intArrayOf(MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ,
                        MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y))
                        AetherNative.nativeAxis(handle, a, e.getAxisValue(a))
                    return true
                }
                return super.onGenericMotionEvent(e)
            }
        }.apply {
            setEGLContextClientVersion(3)
            setEGLConfigChooser(8, 8, 8, 0, 0, 0)
            preserveEGLContextOnPause = false
            isFocusable = true
            isFocusableInTouchMode = true
            setRenderer(object : GLSurfaceView.Renderer {
                override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) = AetherNative.nativeSurfaceCreated(handle)
                override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) = AetherNative.nativeSurfaceChanged(handle, w, h)
                override fun onDrawFrame(gl: GL10?) {
                    if (stopped) return
                    val r = AetherNative.nativeDrawFrame(handle)
                    if (r != 0) { stopped = true; ui.post { onGameStopped(r - 1) } }
                }
            })
            setOnTouchListener { _, e -> forwardTouch(e); true }
            setOnKeyListener { _, code, ev ->
                if (code == KeyEvent.KEYCODE_BACK && ev.source and InputDevice.SOURCE_GAMEPAD == 0) false
                else if (ev.action == KeyEvent.ACTION_DOWN || ev.action == KeyEvent.ACTION_UP) {
                    AetherNative.nativeKey(handle, ev.action == KeyEvent.ACTION_DOWN, code); true
                } else false
            }
        }

        val root = FrameLayout(this)
        root.addView(surface, FrameLayout.LayoutParams(-1, -1))

        stats = TextView(this).apply { setTextColor(Color.rgb(140, 255, 160)); textSize = 11f; typeface = Typeface.MONOSPACE; setBackgroundColor(0x88000000.toInt()); setPadding(10, 4, 10, 4) }
        root.addView(stats, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.END))

        console = TextView(this).apply { setTextColor(Color.WHITE); textSize = 11f; typeface = Typeface.MONOSPACE; setPadding(12, 8, 12, 8) }
        consoleScroll = ScrollView(this).apply { addView(console); setBackgroundColor(0xCC000000.toInt()); visibility = View.GONE }
        root.addView(consoleScroll, FrameLayout.LayoutParams(-1, (resources.displayMetrics.heightPixels * 0.4).toInt(), Gravity.BOTTOM))

        val tools = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setBackgroundColor(0x66000000) }
        fun tool(text: String, onClick: (Button) -> Unit) = Button(this).apply { this.text = text; isAllCaps = false; textSize = 11f; setOnClickListener { onClick(this) } }.also { tools.addView(it) }
        tool("■ Stop") { finish() }
        tool("↻ Restart") { surface.queueEvent { stopped = false; AetherNative.nativeRestart(handle) } }
        tool("Console") { consoleScroll.visibility = if (consoleScroll.visibility == View.VISIBLE) View.GONE else View.VISIBLE }
        tool("Colliders") { colliders = !colliders; AetherNative.nativeSetDebug(handle, colliders, statsOverlay) }
        tool("Stats") { statsOverlay = !statsOverlay; AetherNative.nativeSetDebug(handle, colliders, statsOverlay) }
        root.addView(tools, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.START))
        setContentView(root)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        pollLog()
    }

    private fun forwardTouch(e: MotionEvent) {
        val a = e.actionMasked
        val idx = e.actionIndex
        fun send(phase: Int, i: Int) = AetherNative.nativeTouch(handle, phase, e.getPointerId(i), e.getX(i), e.getY(i))
        when (a) {
            MotionEvent.ACTION_DOWN -> send(0, 0)
            MotionEvent.ACTION_POINTER_DOWN -> send(0, idx)
            MotionEvent.ACTION_MOVE -> for (i in 0 until e.pointerCount) send(1, i)
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> for (i in 0 until e.pointerCount) send(2, i)
            MotionEvent.ACTION_POINTER_UP -> send(2, idx)
        }
    }

    private fun onGameStopped(exitCode: Int) {
        console.append(if (exitCode == 0) "\n— game finished —\n" else "\n— game stopped (exit code $exitCode) —\n")
        consoleScroll.visibility = View.VISIBLE
    }

    private fun pollLog() {
        if (isFinishing) return
        val text = AetherNative.nativeDrainLog()
        if (text.isNotEmpty()) {
            console.append(text)
            if (console.text.length > 30000) console.text = console.text.takeLast(20000)
            consoleScroll.post { consoleScroll.fullScroll(View.FOCUS_DOWN) }
        }
        stats.text = if (statsOverlay) AetherNative.nativeStats(handle) else ""
        stats.visibility = if (statsOverlay) View.VISIBLE else View.GONE
        ui.postDelayed({ pollLog() }, 250)
    }

    override fun onPause() {
        super.onPause()
        surface.onPause()
        AetherNative.nativePause(handle, true)
    }

    override fun onResume() {
        super.onResume()
        surface.onResume()
        AetherNative.nativePause(handle, false)
    }

    override fun onDestroy() {
        super.onDestroy()
        ui.removeCallbacksAndMessages(null)
        val h = handle
        handle = 0
        stopped = true
        // Release GL resources on the GL thread while the context still exists, then free the session.
        surface.queueEvent { AetherNative.nativeDestroy(h) }
    }
}

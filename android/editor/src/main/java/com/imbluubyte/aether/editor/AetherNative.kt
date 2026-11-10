package com.imbluubyte.aether.editor

/** JNI entry points into the C++ engine (libaether_jni.so). All handle-taking calls except
 *  [nativeCreate] / [nativeDrainLog] must be made with a handle from [nativeCreate]. */
object AetherNative {
    init { System.loadLibrary("aether_jni") }

    external fun nativeCreate(projectDir: String, saveDir: String, testScript: String): Long
    external fun nativeSurfaceCreated(handle: Long)
    external fun nativeSurfaceChanged(handle: Long, width: Int, height: Int)
    /** 0 while running, otherwise 1 + exit code. GL thread only. */
    external fun nativeDrawFrame(handle: Long): Int
    external fun nativeTouch(handle: Long, action: Int, id: Int, x: Float, y: Float)
    external fun nativeKey(handle: Long, down: Boolean, keycode: Int)
    external fun nativeAxis(handle: Long, axis: Int, value: Float)
    external fun nativePause(handle: Long, paused: Boolean)
    external fun nativeSetDebug(handle: Long, colliders: Boolean, stats: Boolean)
    /** GL thread only. */
    external fun nativeRestart(handle: Long)
    external fun nativeDrainLog(): String
    external fun nativeStats(handle: Long): String
    /** GL thread only (or after the context is gone). */
    external fun nativeDestroy(handle: Long)
}

package space.vrplayer.immersive

import android.app.Activity
import android.graphics.SurfaceTexture
import android.view.Surface

/**
 * JNI surface area for the C++ OpenXR renderer.
 *
 * Lifecycle:
 *  - [create] once per Activity
 *  - [resume] / [pause] mirror Activity lifecycle
 *  - [destroy] before the Activity is gone
 *
 * Java owns the SurfaceTexture (because creating it needs a Looper-friendly
 * frame-available listener) and gives it to native via [bindVideoSurfaceTexture].
 * Native, on its GL thread, runs `attachToGLContext` against the texture ID it
 * created earlier and then calls `updateTexImage` whenever [postFrameAvailable]
 * has been signalled.
 */
object NativeBridge {

    init {
        System.loadLibrary("vrplayer")
    }

    /** Stereo layout of the source video. */
    enum class StereoMode(val raw: Int) {
        Mono(0),
        SbsLeftRight(1),
    }

    /** Called by C++ when the user interacts with the in-VR control panel. */
    interface Callback {
        fun onPlayPause()
        fun onSeekRelative(deltaMs: Long)
        fun onSeekFraction(fraction: Float)
        fun onExit()
    }

    external fun create(activity: Activity)
    external fun destroy()

    external fun resume()
    external fun pause()

    /** Returns the OES texture name, or 0 if the renderer failed before GL became ready. */
    external fun acquireVideoTextureId(): Int

    /** Pass a Java-side SurfaceTexture so native can attach it to its GL context. */
    external fun bindVideoSurfaceTexture(surfaceTexture: SurfaceTexture)

    /** Mark a new frame ready; the render thread will updateTexImage on its next pass. */
    external fun postFrameAvailable()

    /** Tell native code about the current playback state for the panel UI. */
    external fun setPlaybackState(isPlaying: Boolean, isLoading: Boolean, currentMs: Long, durationMs: Long)

    /** Set stereo mode. See [StereoMode]. */
    external fun setStereoMode(mode: Int)

    /** Wire up the playback callback. Pass null to clear. */
    external fun setCallback(callback: Callback?)
}

package space.vrplayer.immersive

import android.graphics.SurfaceTexture
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.view.Surface
import androidx.activity.ComponentActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import space.vrplayer.MainActivity
import space.vrplayer.player.PlayerSession

/**
 * Immersive (OpenXR) host activity.
 *
 * The native renderer owns the OpenXR session, GL context and the VR180 mesh /
 * SBS shader. Java owns ExoPlayer and the SurfaceTexture; the two sides meet in
 * the GL thread where native attaches the SurfaceTexture to the OES texture it
 * created.
 */
class ImmersiveActivity : ComponentActivity(), NativeBridge.Callback {

    private var player: PlayerSession? = null
    private var surfaceTexture: SurfaceTexture? = null
    private var videoSurface: Surface? = null
    private var listenerThread: HandlerThread? = null
    private var listenerHandler: Handler? = null
    private var nativeReady = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "onCreate")

        val uriString = intent.getStringExtra(EXTRA_VIDEO_URI)
        if (uriString.isNullOrEmpty()) {
            Log.w(TAG, "missing $EXTRA_VIDEO_URI; finishing")
            startActivity(Intent(this, MainActivity::class.java))
            finish()
            return
        }

        NativeBridge.create(this)
        NativeBridge.setCallback(this)
        nativeReady = true

        // acquireVideoTextureId blocks until the native render thread finishes
        // GL init; run it on a background thread so we don't ANR the main thread.
        lifecycleScope.launch(Dispatchers.Default) {
            val texId = NativeBridge.acquireVideoTextureId()
            Log.i(TAG, "OES texId=$texId")
            if (texId == 0) {
                Log.e(TAG, "native renderer failed to create the video texture")
                withContext(Dispatchers.Main) {
                    if (!isFinishing && !isDestroyed) finish()
                }
                return@launch
            }

            withContext(Dispatchers.Main) {
                if (isFinishing || isDestroyed) return@withContext
                // SurfaceTexture is created without a GL texture name; native attaches it
                // on its own GL thread.
                val st = SurfaceTexture(false)
                surfaceTexture = st

                val ht = HandlerThread("vrp-frame-cb").also { it.start() }
                listenerThread = ht
                val h = Handler(ht.looper)
                listenerHandler = h
                st.setOnFrameAvailableListener({ _ ->
                    NativeBridge.postFrameAvailable()
                }, h)

                NativeBridge.bindVideoSurfaceTexture(st)
                NativeBridge.setStereoMode(NativeBridge.StereoMode.SbsLeftRight.raw)

                val surface = Surface(st)
                videoSurface = surface

                val player = PlayerSession(this@ImmersiveActivity).also { player = it }
                player.setSurface(surface)
                player.load(Uri.parse(uriString))
                player.play()

                // Bridge ExoPlayer state into the native panel.
                lifecycleScope.launch {
                    combine(player.isPlaying, player.isLoading, player.currentMs, player.durationMs) { isPlaying, isLoading, current, duration ->
                        State(isPlaying, isLoading, current, duration)
                    }.collect { s ->
                        NativeBridge.setPlaybackState(s.isPlaying, s.isLoading, s.currentMs, s.durationMs)
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        Log.i(TAG, "onResume")
        if (nativeReady) NativeBridge.resume()
    }

    override fun onPause() {
        Log.i(TAG, "onPause")
        if (nativeReady) NativeBridge.pause()
        player?.pause()
        super.onPause()
    }

    override fun onDestroy() {
        Log.i(TAG, "onDestroy")
        runCatching { player?.setSurface(null) }
        runCatching { player?.release() }
        player = null
        runCatching { videoSurface?.release() }
        videoSurface = null
        runCatching { surfaceTexture?.setOnFrameAvailableListener(null) }
        runCatching { surfaceTexture?.release() }
        surfaceTexture = null
        runCatching { listenerThread?.quitSafely() }
        listenerThread = null
        listenerHandler = null
        if (nativeReady) {
            NativeBridge.setCallback(null)
            NativeBridge.destroy()
            nativeReady = false
        }
        super.onDestroy()
    }

    // -------------------------------------------------------------------------
    // NativeBridge.Callback (called from the native render thread)

    override fun onPlayPause() {
        runOnUiThread { player?.togglePlay() }
    }

    override fun onSeekRelative(deltaMs: Long) {
        runOnUiThread { player?.seekRelative(deltaMs) }
    }

    override fun onSeekFraction(fraction: Float) {
        runOnUiThread {
            val player = player ?: return@runOnUiThread
            val duration = player.durationMs.value
            if (duration > 0) {
                val target = (fraction.coerceIn(0f, 1f) * duration).toLong()
                player.seekTo(target)
            }
        }
    }

    override fun onExit() {
        runOnUiThread { finish() }
    }

    private data class State(
        val isPlaying: Boolean,
        val isLoading: Boolean,
        val currentMs: Long,
        val durationMs: Long,
    )

    companion object {
        private const val TAG = "ImmersiveActivity"
        const val EXTRA_VIDEO_URI = "video_uri"
        const val EXTRA_TITLE = "video_title"
    }
}

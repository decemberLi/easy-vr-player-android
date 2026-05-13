package space.vrplayer.player

import android.content.Context
import android.net.Uri
import android.util.Log
import android.view.Surface
import androidx.media3.common.MediaItem
import androidx.media3.common.MimeTypes
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory
import androidx.media3.datasource.DefaultDataSource
import androidx.media3.datasource.okhttp.OkHttpDataSource
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import space.vrplayer.net.HttpClient

/**
 * ExoPlayer wrapper used by the immersive activity. Outputs into an OES-backed
 * Surface created by the native renderer.
 *
 * Mirrors iOS [VideoPlayer]: exposes play/pause/seek + currentTime/duration,
 * but without HLS multi-track UI surfacing for v0.
 */
class PlayerSession(context: Context) {

    private val appContext = context.applicationContext

    private val mediaSourceFactory = DefaultMediaSourceFactory(
        DefaultDataSource.Factory(
            appContext,
            OkHttpDataSource.Factory(HttpClient.client),
        )
    )

    private val player: ExoPlayer = ExoPlayer.Builder(appContext)
        .setMediaSourceFactory(mediaSourceFactory)
        .build()
        .apply {
            playWhenReady = false
            addListener(InternalListener())
        }

    private val scope = CoroutineScope(Dispatchers.Main.immediate + SupervisorJob())

    private val _isPlaying = MutableStateFlow(false)
    val isPlaying: StateFlow<Boolean> = _isPlaying.asStateFlow()

    private val _isLoading = MutableStateFlow(true)
    val isLoading: StateFlow<Boolean> = _isLoading.asStateFlow()

    private val _currentMs = MutableStateFlow(0L)
    val currentMs: StateFlow<Long> = _currentMs.asStateFlow()

    private val _durationMs = MutableStateFlow(0L)
    val durationMs: StateFlow<Long> = _durationMs.asStateFlow()

    private val _hasReachedEnd = MutableStateFlow(false)
    val hasReachedEnd: StateFlow<Boolean> = _hasReachedEnd.asStateFlow()

    init {
        scope.launch {
            while (isActive) {
                delay(100)
                if (player.playbackState == Player.STATE_READY) {
                    _currentMs.value = player.currentPosition
                    val d = player.duration
                    _durationMs.value = if (d > 0) d else 0L
                }
            }
        }
    }

    fun load(uri: Uri) {
        Log.i(TAG, "load uri=$uri")
        _hasReachedEnd.value = false
        val urlString = uri.toString()
        val mimeType = if (urlString.contains("m3u8", ignoreCase = true)) {
            MimeTypes.APPLICATION_M3U8
        } else {
            null
        }
        val item = if (mimeType != null) {
            MediaItem.Builder().setUri(uri).setMimeType(mimeType).build()
        } else {
            MediaItem.fromUri(uri)
        }
        player.setMediaItem(item)
        player.prepare()
    }

    fun play() {
        Log.i(TAG, "play")
        if (_hasReachedEnd.value) {
            player.seekTo(0)
            _hasReachedEnd.value = false
        }
        player.playWhenReady = true
    }

    fun pause() {
        Log.i(TAG, "pause")
        player.playWhenReady = false
    }

    fun togglePlay() {
        if (player.isPlaying) pause() else play()
    }

    fun seekTo(ms: Long) {
        val target = ms.coerceIn(0L, (player.duration.takeIf { it > 0 } ?: ms))
        Log.i(TAG, "seekTo $target")
        player.seekTo(target)
        _currentMs.value = target
        _hasReachedEnd.value = false
    }

    fun seekRelative(deltaMs: Long) {
        seekTo(player.currentPosition + deltaMs)
    }

    fun setSurface(surface: Surface?) {
        Log.i(TAG, "setSurface ${surface != null}")
        player.setVideoSurface(surface)
    }

    fun release() {
        Log.i(TAG, "release")
        player.setVideoSurface(null)
        player.release()
        scope.cancel()
    }

    private inner class InternalListener : Player.Listener {
        override fun onIsPlayingChanged(playing: Boolean) {
            _isPlaying.value = playing
        }

        override fun onPlaybackStateChanged(state: Int) {
            _isLoading.value = state == Player.STATE_BUFFERING || state == Player.STATE_IDLE
            if (state == Player.STATE_ENDED) {
                _hasReachedEnd.value = true
                _isPlaying.value = false
            }
            if (state == Player.STATE_READY) {
                val d = player.duration
                if (d > 0) _durationMs.value = d
            }
        }

        override fun onPlayerError(error: PlaybackException) {
            Log.w(TAG, "player error", error)
        }
    }

    companion object {
        private const val TAG = "PlayerSession"
    }
}

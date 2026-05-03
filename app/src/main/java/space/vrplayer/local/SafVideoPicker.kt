package space.vrplayer.local

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext

/**
 * SAF picker for video files. Persists read permission so the URI keeps working
 * after the immersive activity is launched.
 */
object SafVideoPicker {
    private val MIME_TYPES = arrayOf("video/*")

    /**
     * Returns a launcher function that opens the system picker. The picked URI
     * is delivered to [onResult]; null means the user cancelled.
     */
    @Composable
    fun rememberLauncher(onResult: (Uri?) -> Unit): () -> Unit {
        val ctx = LocalContext.current
        val launcher = rememberLauncherForActivityResult(
            ActivityResultContracts.OpenDocument()
        ) { uri ->
            if (uri != null) takePermission(ctx, uri)
            onResult(uri)
        }
        val mimes = remember { MIME_TYPES }
        return { launcher.launch(mimes) }
    }

    private fun takePermission(ctx: Context, uri: Uri) {
        runCatching {
            ctx.contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION,
            )
        }
    }
}

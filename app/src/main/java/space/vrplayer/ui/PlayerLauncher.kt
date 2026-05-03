package space.vrplayer.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.util.Log
import space.vrplayer.immersive.ImmersiveActivity

object PlayerLauncher {
    /** Start the immersive (OpenXR) activity with a video URI to play. */
    fun launch(context: Context, uri: Uri, title: String? = null) {
        Log.i(TAG, "launch uri=$uri title=$title")
        val intent = Intent(context, ImmersiveActivity::class.java).apply {
            action = Intent.ACTION_MAIN
            addCategory("com.oculus.intent.category.VR")
            putExtra(ImmersiveActivity.EXTRA_VIDEO_URI, uri.toString())
            if (!title.isNullOrEmpty()) putExtra(ImmersiveActivity.EXTRA_TITLE, title)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        context.startActivity(intent)
    }

    private const val TAG = "PlayerLauncher"
}

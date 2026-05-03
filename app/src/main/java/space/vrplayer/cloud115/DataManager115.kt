package space.vrplayer.cloud115

import android.net.Uri
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MultipartBody
import okhttp3.Request
import org.json.JSONObject
import space.vrplayer.cloud115.models.FileItem115
import space.vrplayer.cloud115.models.FileListResponse
import space.vrplayer.cloud115.models.VideoUrl115
import space.vrplayer.cloud115.models.toJsonObjectList
import space.vrplayer.net.HttpClient

/**
 * Wraps the 115 open APIs used by the player.
 *
 * Mirrors iOS DataManager115:
 * - GET  /open/ufile/files     (list a directory)
 * - GET  /open/video/play      (multi-resolution streaming URLs)
 * - POST /open/ufile/downurl   (raw download URL for "原画")
 */
object DataManager115 {
    private const val TAG = "DataManager115"

    private const val FILES_URL = "https://proapi.115.com/open/ufile/files"
    private const val VIDEO_PLAY_URL = "https://proapi.115.com/open/video/play"
    private const val DOWNLOAD_URL = "https://proapi.115.com/open/ufile/downurl"

    /** List children of `cid`. Pass null/empty for root. */
    suspend fun getFileList(cid: String?, limit: Int = 100, offset: Int = 0): FileListResponse =
        withContext(Dispatchers.IO) {
            ensureFreshToken()

            val u = Uri.parse(FILES_URL).buildUpon()
                .apply { if (!cid.isNullOrEmpty()) appendQueryParameter("cid", cid) }
                .appendQueryParameter("limit", limit.toString())
                .appendQueryParameter("offset", offset.toString())
                .appendQueryParameter("cur", "1")
                .appendQueryParameter("show_dir", "1")
                .build()

            val req = Request.Builder()
                .url(u.toString())
                .header("Authorization", "Bearer ${TokenManager115.accessToken.orEmpty()}")
                .get()
                .build()

            HttpClient.client.newCall(req).execute().use { resp ->
                val body = resp.body?.string().orEmpty()
                require(resp.isSuccessful) { "files failed http=${resp.code} body=$body" }
                val json = JSONObject(body)
                val arr = json.optJSONArray("data")
                val items = arr?.toJsonObjectList()?.map(FileItem115::fromJson).orEmpty()
                val total = if (json.has("count") && !json.isNull("count")) json.optInt("count") else null
                FileListResponse(items, total)
            }
        }

    /** Returns the multi-quality stream list (m3u8 / mp4) for a video. */
    suspend fun getVideoPlayUrls(pickCode: String): List<VideoUrl115> = withContext(Dispatchers.IO) {
        ensureFreshToken()

        val u = Uri.parse(VIDEO_PLAY_URL).buildUpon()
            .appendQueryParameter("pick_code", pickCode)
            .build()
        val req = Request.Builder()
            .url(u.toString())
            .header("Authorization", "Bearer ${TokenManager115.accessToken.orEmpty()}")
            .get()
            .build()

        HttpClient.client.newCall(req).execute().use { resp ->
            val body = resp.body?.string().orEmpty()
            require(resp.isSuccessful) { "play failed http=${resp.code} body=$body" }
            val json = JSONObject(body)
            val data = json.optJSONObject("data") ?: return@withContext emptyList()
            val arr = data.optJSONArray("video_url") ?: return@withContext emptyList()
            arr.toJsonObjectList().map(VideoUrl115::fromJson)
        }
    }

    /**
     * Returns the raw download URL for `pickCode`, or null if unavailable.
     *
     * The 115 response shape is awkward: `data` is a dictionary whose single value
     * is `{ "url": { "url": "<actual>" }, ... }`.
     */
    suspend fun getFileDownloadUrl(pickCode: String): String? = withContext(Dispatchers.IO) {
        ensureFreshToken()

        val u = Uri.parse(DOWNLOAD_URL).buildUpon()
            .appendQueryParameter("pick_code", pickCode)
            .build()
        val form = MultipartBody.Builder().setType(MultipartBody.FORM)
            .addFormDataPart("pick_code", pickCode)
            .build()
        val req = Request.Builder()
            .url(u.toString())
            .header("Authorization", "Bearer ${TokenManager115.accessToken.orEmpty()}")
            .post(form)
            .build()

        HttpClient.client.newCall(req).execute().use { resp ->
            val body = resp.body?.string().orEmpty()
            if (!resp.isSuccessful) {
                Log.w(TAG, "downurl failed http=${resp.code} body=$body")
                return@withContext null
            }
            val json = JSONObject(body)
            val data = json.optJSONObject("data") ?: return@withContext null
            val keys = data.keys()
            if (!keys.hasNext()) return@withContext null
            val firstKey = keys.next()
            val item = data.optJSONObject(firstKey) ?: return@withContext null
            val urlObj = item.optJSONObject("url") ?: return@withContext null
            urlObj.optString("url", "").ifEmpty { null }
        }
    }

    private suspend fun ensureFreshToken() {
        if (TokenManager115.isExpired) TokenManager115.refresh()
    }
}

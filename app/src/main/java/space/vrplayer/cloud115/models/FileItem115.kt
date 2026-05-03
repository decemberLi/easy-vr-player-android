package space.vrplayer.cloud115.models

import org.json.JSONArray
import org.json.JSONObject

/** A 115 file or folder. Mirrors iOS [FileItem]. */
data class FileItem115(
    val fid: String?,
    val pid: String?,
    /** "0" = folder, otherwise file */
    val fc: String?,
    /** File name */
    val fn: String?,
    /** Pick code, used for video play / download */
    val pc: String?,
    /** Lower-case extension hint, e.g. "mp4" */
    val ico: String?,
    val fileSize: Long?,
    val playLong: Int?,
    val thumb: String?,
) {
    val isFolder: Boolean get() = fc == "0"

    val isVideoFile: Boolean
        get() {
            val ext = ico?.lowercase() ?: return false
            return ext in VIDEO_EXTENSIONS
        }

    companion object {
        private val VIDEO_EXTENSIONS = setOf("mp4", "mov", "avi", "mkv", "wmv", "flv", "webm", "ts", "m2ts", "m4v")

        fun fromJson(o: JSONObject): FileItem115 = FileItem115(
            fid = o.optStringOrNull("fid"),
            pid = o.optStringFlexible("pid"),
            fc = o.optStringOrNull("fc"),
            fn = o.optStringOrNull("fn"),
            pc = o.optStringOrNull("pc"),
            ico = o.optStringOrNull("ico"),
            fileSize = o.optLongOrNull("fs"),
            playLong = o.optIntOrNull("play_long"),
            thumb = o.optStringOrNull("thumb"),
        )
    }
}

/** Result of `/open/ufile/files`. */
data class FileListResponse(
    val items: List<FileItem115>,
    val totalCount: Int?,
)

internal fun JSONObject.optStringOrNull(key: String): String? =
    if (!has(key) || isNull(key)) null else optString(key, null)

internal fun JSONObject.optStringFlexible(key: String): String? {
    if (!has(key) || isNull(key)) return null
    return when (val v = get(key)) {
        is String -> v
        is Number -> v.toString()
        else -> null
    }
}

internal fun JSONObject.optIntOrNull(key: String): Int? =
    if (!has(key) || isNull(key)) null else optInt(key)

internal fun JSONObject.optLongOrNull(key: String): Long? =
    if (!has(key) || isNull(key)) null else optLong(key)

internal fun JSONArray.toJsonObjectList(): List<JSONObject> {
    val out = ArrayList<JSONObject>(length())
    for (i in 0 until length()) {
        val item = optJSONObject(i)
        if (item != null) out.add(item)
    }
    return out
}

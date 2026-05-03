package space.vrplayer.cloud115.models

import org.json.JSONObject

/** Single video stream variant returned by `/open/video/play`. */
data class VideoUrl115(
    val title: String,
    val url: String,
    val width: Int,
    val height: Int,
    /** Lower number = lower quality, 0 placeholder for "原画" sourced from /downurl */
    val definitionN: Int,
) {
    companion object {
        fun fromJson(o: JSONObject): VideoUrl115 = VideoUrl115(
            title = o.optString("title", ""),
            url = o.optString("url", ""),
            width = o.optInt("width", 0),
            height = o.optInt("height", 0),
            definitionN = o.optInt("definition_n", 0),
        )

        /** Synthesize an entry for the raw download URL (= "原画"). */
        fun original(downloadUrl: String): VideoUrl115 = VideoUrl115(
            title = "原画",
            url = downloadUrl,
            width = 0,
            height = 0,
            definitionN = 0,
        )
    }
}

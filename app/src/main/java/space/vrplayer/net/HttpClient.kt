package space.vrplayer.net

import okhttp3.OkHttpClient
import okhttp3.logging.HttpLoggingInterceptor
import space.vrplayer.BuildConfig
import java.util.concurrent.TimeUnit

/** Shared OkHttp client used by 115 API, the local proxy, and any ad-hoc fetches. */
object HttpClient {
    val client: OkHttpClient = OkHttpClient.Builder().apply {
        connectTimeout(15, TimeUnit.SECONDS)
        readTimeout(60, TimeUnit.SECONDS)
        callTimeout(120, TimeUnit.SECONDS)
        retryOnConnectionFailure(true)
        if (BuildConfig.DEBUG) {
            addInterceptor(HttpLoggingInterceptor().apply {
                level = HttpLoggingInterceptor.Level.HEADERS
            })
        }
    }.build()
}

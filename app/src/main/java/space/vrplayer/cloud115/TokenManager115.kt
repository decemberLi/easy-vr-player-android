package space.vrplayer.cloud115

import android.content.Context
import android.content.SharedPreferences
import android.net.Uri
import android.util.Log
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MultipartBody
import okhttp3.Request
import org.json.JSONObject
import space.vrplayer.net.HttpClient
import java.security.MessageDigest
import java.util.UUID

object TokenManager115 {
    private const val TAG = "TokenManager115"

    private const val CLIENT_ID = "100197637"
    private const val REDIRECT_URI = "https://vrplayer.space"
    private const val AUTHORIZE_URL = "https://passportapi.115.com/open/authorize"
    private const val REFRESH_URL = "https://passportapi.115.com/open/refreshToken"
    private const val EXCHANGE_BASE = "https://vocalremover.us/api/115/authCodeToToken"

    private const val PREFS = "vrp.115.token"
    private const val K_ACCESS = "access_token"
    private const val K_REFRESH = "refresh_token"
    private const val K_EXPIRES_IN = "expires_in"
    private const val K_LAST_UPDATE = "last_update_ms"

    private lateinit var prefs: SharedPreferences

    @Volatile var accessToken: String? = null
        private set
    @Volatile private var refreshToken: String? = null
    @Volatile private var expiresInSec: Int = 0
    @Volatile private var lastUpdateMs: Long = 0
    @Volatile var lastIssuedState: String? = null
        private set

    fun init(context: Context) {
        prefs = createPrefs(context)
        accessToken = prefs.getString(K_ACCESS, null)
        refreshToken = prefs.getString(K_REFRESH, null)
        expiresInSec = prefs.getInt(K_EXPIRES_IN, 0)
        lastUpdateMs = prefs.getLong(K_LAST_UPDATE, 0L)
        Log.i(TAG, "init loaded=${accessToken != null} expiresIn=$expiresInSec lastUpdateMs=$lastUpdateMs")
    }

    val isLoggedIn: Boolean get() = !accessToken.isNullOrEmpty()

    val isExpired: Boolean
        get() {
            if (lastUpdateMs == 0L || expiresInSec == 0) return true
            val elapsed = (System.currentTimeMillis() - lastUpdateMs) / 1000L
            return elapsed >= expiresInSec
        }

    /** Build the authorize URL. Generates a fresh state each call. */
    fun buildAuthorizeUrl(): String {
        val state = md5(UUID.randomUUID().toString())
        lastIssuedState = state
        val u = Uri.parse(AUTHORIZE_URL).buildUpon()
            .appendQueryParameter("client_id", CLIENT_ID)
            .appendQueryParameter("redirect_uri", REDIRECT_URI)
            .appendQueryParameter("response_type", "code")
            .appendQueryParameter("state", state)
            .build()
        return u.toString()
    }

    /** Exchange `code` (returned from the WebView redirect) for an access token. */
    suspend fun exchangeCodeForToken(code: String, state: String) = withContext(Dispatchers.IO) {
        // Path-style URL: /api/115/authCodeToToken/{code}?state={state}
        val u = Uri.parse(EXCHANGE_BASE).buildUpon()
            .appendPath(code)
            .appendQueryParameter("state", state)
            .build()
        val req = Request.Builder().url(u.toString()).get().build()
        Log.i(TAG, "exchange code url=$u")
        HttpClient.client.newCall(req).execute().use { resp ->
            val body = resp.body?.string().orEmpty()
            require(resp.isSuccessful) { "exchange failed http=${resp.code} body=$body" }
            persistFromTokenJson(JSONObject(body))
        }
    }

    /** Refresh the current token using the stored refresh_token. */
    suspend fun refresh() = withContext(Dispatchers.IO) {
        val rt = refreshToken ?: error("no refresh_token")
        val body = MultipartBody.Builder().setType(MultipartBody.FORM)
            .addFormDataPart("refresh_token", rt)
            .build()
        val req = Request.Builder().url(REFRESH_URL).post(body).build()
        Log.i(TAG, "refresh()")
        HttpClient.client.newCall(req).execute().use { resp ->
            val raw = resp.body?.string().orEmpty()
            require(resp.isSuccessful) { "refresh failed http=${resp.code} body=$raw" }
            persistFromTokenJson(JSONObject(raw))
        }
    }

    fun clear() {
        accessToken = null
        refreshToken = null
        expiresInSec = 0
        lastUpdateMs = 0
        prefs.edit().clear().apply()
    }

    /**
     * Both /authCodeToToken and /refreshToken responses wrap the actual token under a `data` field, e.g.:
     * { "state": true, "code": 0, "message": "", "data": { "access_token": ..., "refresh_token": ..., "expires_in": ... } }
     */
    private fun persistFromTokenJson(json: JSONObject) {
        val data = json.optJSONObject("data") ?: error("token response missing data: $json")
        val at = data.optString("access_token", null) ?: error("missing access_token")
        val rt = data.optString("refresh_token", null) ?: error("missing refresh_token")
        val exp = data.optInt("expires_in", 0)
        accessToken = at
        refreshToken = rt
        expiresInSec = exp
        lastUpdateMs = System.currentTimeMillis()
        prefs.edit()
            .putString(K_ACCESS, at)
            .putString(K_REFRESH, rt)
            .putInt(K_EXPIRES_IN, exp)
            .putLong(K_LAST_UPDATE, lastUpdateMs)
            .apply()
        Log.i(TAG, "persisted token expires_in=$exp")
    }

    private fun createPrefs(ctx: Context): SharedPreferences {
        return try {
            val key = MasterKey.Builder(ctx)
                .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                .build()
            EncryptedSharedPreferences.create(
                ctx,
                PREFS,
                key,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM,
            )
        } catch (t: Throwable) {
            Log.w(TAG, "EncryptedSharedPreferences unavailable, falling back to plain prefs", t)
            ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        }
    }

    private fun md5(s: String): String {
        val bytes = MessageDigest.getInstance("MD5").digest(s.toByteArray(Charsets.UTF_8))
        return bytes.joinToString("") { "%02x".format(it) }
    }
}

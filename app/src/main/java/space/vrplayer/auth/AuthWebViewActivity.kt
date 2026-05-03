package space.vrplayer.auth

import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.view.ViewGroup
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.LinearLayout
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContract
import space.vrplayer.cloud115.TokenManager115

/**
 * Hosts a WebView that loads the 115 OAuth page. When the page is redirected to
 * `https://vrplayer.space?code=...&state=...`, we capture the parameters and
 * finish with them in the result Intent.
 */
class AuthWebViewActivity : ComponentActivity() {
    private lateinit var webView: WebView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        title = "115"
        webView = WebView(this).apply {
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            )
            settings.apply {
                javaScriptEnabled = true
                domStorageEnabled = true
                cacheMode = WebSettings.LOAD_DEFAULT
                useWideViewPort = true
                loadWithOverviewMode = true
            }
            webViewClient = AuthClient(::handleRedirect)
        }
        setContentView(webView)
        webView.loadUrl(TokenManager115.buildAuthorizeUrl())
    }

    private fun handleRedirect(uri: Uri): Boolean {
        if (uri.host?.endsWith("vrplayer.space") != true) return false
        val code = uri.getQueryParameter("code")
        val state = uri.getQueryParameter("state")
        Log.i(TAG, "captured redirect code=${code?.take(8)}… state=$state")
        val data = Intent().apply {
            putExtra(EXTRA_CODE, code)
            putExtra(EXTRA_STATE, state)
        }
        setResult(RESULT_OK, data)
        finish()
        return true
    }

    override fun onDestroy() {
        if (::webView.isInitialized) {
            webView.stopLoading()
            webView.destroy()
        }
        super.onDestroy()
    }

    private class AuthClient(val onRedirect: (Uri) -> Boolean) : WebViewClient() {
        override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
            return onRedirect(request.url)
        }

        override fun onPageStarted(view: WebView?, url: String?, favicon: Bitmap?) {
            super.onPageStarted(view, url, favicon)
            if (url != null) onRedirect(Uri.parse(url))
        }
    }

    companion object {
        private const val TAG = "AuthWebViewActivity"
        const val EXTRA_CODE = "code"
        const val EXTRA_STATE = "state"
    }

    /** Launch contract: returns the captured code/state or null on cancel. */
    class Contract : ActivityResultContract<Unit, Pair<String, String>?>() {
        override fun createIntent(context: android.content.Context, input: Unit): Intent =
            Intent(context, AuthWebViewActivity::class.java)

        override fun parseResult(resultCode: Int, intent: Intent?): Pair<String, String>? {
            if (resultCode != RESULT_OK || intent == null) return null
            val code = intent.getStringExtra(EXTRA_CODE) ?: return null
            val state = intent.getStringExtra(EXTRA_STATE) ?: return null
            return code to state
        }
    }
}

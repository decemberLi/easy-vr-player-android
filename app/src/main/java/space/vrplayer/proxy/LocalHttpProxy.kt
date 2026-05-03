package space.vrplayer.proxy

import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import okhttp3.Call
import okhttp3.Request
import space.vrplayer.net.HttpClient
import java.io.BufferedOutputStream
import java.io.IOException
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.URLDecoder
import java.net.URLEncoder
import kotlin.math.min

/**
 * Local HTTP proxy that forwards `GET|HEAD /proxy?url=<encoded>` to a remote URL,
 * clamping Range to single 8 MiB chunks. Mirrors the iOS [LocalHTTPProxy].
 *
 * Why we need this: the 115 raw download URL is signed; passing it directly to
 * ExoPlayer works for short ranges but trips signature/bandwidth limits when the
 * player asks for the entire remainder of the file. Forcing 8 MiB chunks gives
 * the same edge-cache-friendly behaviour the iOS app already validated.
 */
object LocalHttpProxy {
    private const val TAG = "LocalHttpProxy"

    private const val MAX_CHUNK: Long = 8L * 1024 * 1024
    /** Headers that get copied verbatim from the upstream response. */
    private val FORWARD_HEADERS = listOf(
        "Content-Type",
        "Content-Length",
        "Accept-Ranges",
        "Content-Range",
        "ETag",
        "Last-Modified",
    )

    private var serverSocket: ServerSocket? = null
    private val supervisor = SupervisorJob()
    private val scope = CoroutineScope(Dispatchers.IO + supervisor)
    @Volatile var port: Int = 0
        private set

    @Synchronized
    fun start() {
        if (serverSocket != null) return
        val s = ServerSocket(0, 50, InetAddress.getByName("127.0.0.1"))
        serverSocket = s
        port = s.localPort
        Log.i(TAG, "ready on port=$port")
        scope.launch {
            while (isActive) {
                val client = try {
                    s.accept()
                } catch (e: IOException) {
                    if (isActive) Log.w(TAG, "accept error", e)
                    break
                }
                launch { runCatching { handle(client) }.onFailure { Log.w(TAG, "session error", it) } }
            }
        }
    }

    @Synchronized
    fun stop() {
        runCatching { serverSocket?.close() }
        serverSocket = null
        port = 0
        scope.coroutineContext.cancel()
    }

    /** Returns the loopback URL that wraps [remoteUrl]. */
    fun proxyUrl(remoteUrl: String): String {
        if (port == 0) start()
        // URLEncoder will percent-encode `%` itself as `%25`, which is what we want:
        // server-side decodes once and gets the original URL back.
        val encoded = URLEncoder.encode(remoteUrl, "UTF-8")
        return "http://127.0.0.1:$port/proxy?url=$encoded"
    }

    // -----------------------------------------------------------------------------
    // Connection handling

    private fun handle(socket: Socket) {
        socket.use { s ->
            s.tcpNoDelay = true
            val input = s.getInputStream()
            val output = BufferedOutputStream(s.getOutputStream())

            val request = readRequest(input) ?: return run {
                respondSimple(output, 400, "Bad Request")
            }

            if (request.method != "GET" && request.method != "HEAD") {
                respondSimple(output, 405, "Method Not Allowed")
                return
            }

            val target = request.target
            val pathOk = target.startsWith("/proxy")
            if (!pathOk) {
                respondSimple(output, 400, "Bad Request")
                return
            }

            val raw = extractUrlParam(target)
            if (raw.isNullOrEmpty()) {
                respondSimple(output, 400, "missing url param")
                return
            }
            val remote = try { URLDecoder.decode(raw, "UTF-8") } catch (_: Exception) { raw }

            val rangeHeader = request.headers["range"]
            val upstreamRange = computeClampedRange(rangeHeader, request.method)
            forward(output, request.method, remote, upstreamRange)
        }
    }

    private data class ParsedRequest(
        val method: String,
        val target: String,
        val headers: Map<String, String>,
    )

    private fun readRequest(input: java.io.InputStream): ParsedRequest? {
        val buf = ByteArray(16 * 1024)
        val acc = StringBuilder()
        while (true) {
            val n = input.read(buf)
            if (n <= 0) return null
            acc.append(String(buf, 0, n, Charsets.ISO_8859_1))
            val end = acc.indexOf("\r\n\r\n")
            if (end >= 0) {
                val headerBlock = acc.substring(0, end)
                val lines = headerBlock.split("\r\n")
                if (lines.isEmpty()) return null
                val request = lines[0].split(" ")
                if (request.size < 3) return null
                val headers = HashMap<String, String>()
                for (i in 1 until lines.size) {
                    val line = lines[i]
                    val colon = line.indexOf(':')
                    if (colon <= 0) continue
                    val name = line.substring(0, colon).trim().lowercase()
                    val value = line.substring(colon + 1).trim()
                    headers[name] = value
                }
                return ParsedRequest(request[0], request[1], headers)
            }
            if (acc.length > 64 * 1024) return null
        }
    }

    private fun extractUrlParam(target: String): String? {
        // Single-arg parsing: take everything after `url=` to support remote URLs that contain `&`.
        val idx = target.indexOf("url=")
        if (idx < 0) return null
        return target.substring(idx + "url=".length)
    }

    /** Clamp the client Range to [start, start + 8MiB - 1]; treat HEAD as a 0-0 probe. */
    private fun computeClampedRange(original: String?, method: String): String {
        if (method == "HEAD") return "bytes=0-0"
        if (original == null || !original.lowercase().startsWith("bytes=")) {
            return "bytes=0-${MAX_CHUNK - 1}"
        }
        val pat = Regex("""bytes=\s*(\d*)-(\d*)""", RegexOption.IGNORE_CASE)
        val m = pat.find(original) ?: return "bytes=0-${MAX_CHUNK - 1}"
        val startStr = m.groupValues[1]
        val endStr = m.groupValues[2]
        val hasStart = startStr.isNotEmpty()
        val hasEnd = endStr.isNotEmpty()
        if (hasStart) {
            val start = startStr.toLong()
            val rawEnd = if (hasEnd) endStr.toLong() else (start + MAX_CHUNK - 1)
            val end = min(rawEnd, start + MAX_CHUNK - 1)
            return "bytes=$start-$end"
        }
        if (hasEnd) {
            // bytes=-suffix → just pass through, "last N bytes" patterns are rare and small
            return original
        }
        return "bytes=0-${MAX_CHUNK - 1}"
    }

    private fun forward(
        output: BufferedOutputStream,
        clientMethod: String,
        remoteUrl: String,
        upstreamRange: String,
    ) {
        val req = Request.Builder()
            .url(remoteUrl)
            // Always use GET upstream (some 115 sources reject HEAD)
            .get()
            .header("Range", upstreamRange)
            .header("Accept", "*/*")
            .build()

        Log.i(TAG, "forward client=$clientMethod range=$upstreamRange url=$remoteUrl")
        var call: Call? = null
        try {
            call = HttpClient.client.newCall(req)
            call.execute().use { resp ->
                val status = resp.code
                val statusText = resp.message.ifEmpty { defaultStatusText(status) }
                val headerLines = StringBuilder("HTTP/1.1 $status $statusText\r\n")
                for (h in FORWARD_HEADERS) {
                    val v = resp.header(h) ?: continue
                    headerLines.append("$h: $v\r\n")
                }
                headerLines.append("Connection: close\r\n\r\n")
                output.write(headerLines.toString().toByteArray(Charsets.ISO_8859_1))
                output.flush()

                if (clientMethod != "HEAD") {
                    resp.body?.byteStream()?.use { src ->
                        val buf = ByteArray(64 * 1024)
                        while (true) {
                            val n = src.read(buf)
                            if (n <= 0) break
                            try {
                                output.write(buf, 0, n)
                            } catch (e: IOException) {
                                Log.w(TAG, "client closed mid-stream: ${e.message}")
                                return
                            }
                        }
                        output.flush()
                    }
                }
            }
        } catch (t: Throwable) {
            Log.w(TAG, "upstream error: $remoteUrl", t)
            runCatching { call?.cancel() }
            runCatching { respondSimple(output, 502, "Bad Gateway") }
        }
    }

    private fun respondSimple(output: BufferedOutputStream, status: Int, message: String) {
        val body = message.toByteArray(Charsets.UTF_8)
        val headers = StringBuilder("HTTP/1.1 $status ${defaultStatusText(status)}\r\n")
            .append("Content-Type: text/plain\r\n")
            .append("Content-Length: ${body.size}\r\n")
            .append("Connection: close\r\n\r\n")
        runCatching {
            output.write(headers.toString().toByteArray(Charsets.ISO_8859_1))
            output.write(body)
            output.flush()
        }
    }

    private fun defaultStatusText(code: Int): String = when (code) {
        200 -> "OK"
        206 -> "Partial Content"
        302 -> "Found"
        400 -> "Bad Request"
        403 -> "Forbidden"
        404 -> "Not Found"
        405 -> "Method Not Allowed"
        416 -> "Range Not Satisfiable"
        500 -> "Internal Server Error"
        502 -> "Bad Gateway"
        else -> "OK"
    }
}

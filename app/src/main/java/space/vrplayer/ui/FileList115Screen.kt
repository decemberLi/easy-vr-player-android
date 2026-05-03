package space.vrplayer.ui

import android.net.Uri
import android.util.Log
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.InsertDriveFile
import androidx.compose.material.icons.filled.Movie
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import space.vrplayer.cloud115.DataManager115
import space.vrplayer.cloud115.models.FileItem115
import space.vrplayer.cloud115.models.VideoUrl115
import space.vrplayer.proxy.LocalHttpProxy

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FileList115Screen(
    cid: String?,
    title: String,
    onOpenChild: (cid: String, title: String) -> Unit,
    onBack: () -> Unit,
) {
    val ctx = LocalContext.current
    val coroutineScope = rememberCoroutineScope()

    var items by remember(cid) { mutableStateOf<List<FileItem115>>(emptyList()) }
    var loading by remember(cid) { mutableStateOf(true) }
    var error by remember(cid) { mutableStateOf<String?>(null) }
    var qualityChoices by remember { mutableStateOf<Pair<FileItem115, List<VideoUrl115>>?>(null) }
    var resolving by remember { mutableStateOf(false) }
    var playbackError by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(cid) {
        loading = true
        error = null
        runCatching { DataManager115.getFileList(cid = cid, limit = 200, offset = 0) }
            .onSuccess { items = it.items }
            .onFailure { error = it.message ?: "load failed" }
        loading = false
    }

    Column(Modifier.fillMaxSize()) {
        TopAppBar(
            title = { Text(title) },
            navigationIcon = {
                IconButton(onClick = onBack) {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                }
            },
        )

        Box(Modifier.fillMaxSize()) {
            when {
                loading && items.isEmpty() -> CircularProgressIndicator(Modifier.align(Alignment.Center))
                error != null -> Text(
                    "加载失败：${error}",
                    color = MaterialTheme.colorScheme.error,
                    modifier = Modifier.align(Alignment.Center),
                )
                else -> LazyVerticalGrid(
                    columns = GridCells.Adaptive(minSize = 140.dp),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    items(items, key = { it.fid ?: it.fn ?: System.identityHashCode(it).toString() }) { item ->
                        FileTile(item) { handle ->
                            when {
                                handle.isFolder -> {
                                    val nextCid = handle.fid ?: return@FileTile
                                    onOpenChild(nextCid, handle.fn ?: title)
                                }
                                handle.isVideoFile -> {
                                    val pickCode = handle.pc
                                    if (pickCode.isNullOrEmpty()) {
                                        playbackError = "这个文件没有可播放的 pick_code"
                                        return@FileTile
                                    }
                                    resolving = true
                                    coroutineScope.launch {
                                        val urlsResult = runCatching { DataManager115.getVideoPlayUrls(pickCode) }
                                        val urls = urlsResult.getOrDefault(emptyList())
                                        urlsResult.exceptionOrNull()?.let { Log.w(TAG, "getVideoPlayUrls failed", it) }
                                        val downloadUrl = runCatching { DataManager115.getFileDownloadUrl(pickCode) }
                                            .onFailure { Log.w(TAG, "getFileDownloadUrl failed", it) }
                                            .getOrNull()
                                        val merged = buildList {
                                            addAll(urls.filter { it.url.isNotEmpty() })
                                            if (!downloadUrl.isNullOrEmpty()) add(VideoUrl115.original(downloadUrl))
                                        }
                                        resolving = false
                                        if (merged.isNotEmpty()) {
                                            qualityChoices = handle to merged
                                        } else {
                                            playbackError = urlsResult.exceptionOrNull()?.message ?: "没有获取到可播放地址"
                                        }
                                    }
                                }
                                else -> Unit // unsupported
                            }
                        }
                    }
                }
            }

            if (resolving) CircularProgressIndicator(Modifier.align(Alignment.Center))
        }
    }

    val choices = qualityChoices
    if (choices != null) {
        QualityChooserDialog(
            file = choices.first,
            options = choices.second,
            onCancel = { qualityChoices = null },
            onSelect = { picked ->
                qualityChoices = null
                val playUri = if (picked.title == "原画") {
                    Uri.parse(LocalHttpProxy.proxyUrl(picked.url))
                } else {
                    Uri.parse(picked.url)
                }
                PlayerLauncher.launch(ctx, playUri, title = choices.first.fn)
            },
        )
    }

    val err = playbackError
    if (err != null) {
        AlertDialog(
            onDismissRequest = { playbackError = null },
            title = { Text("无法播放") },
            text = { Text(err) },
            confirmButton = {
                TextButton(onClick = { playbackError = null }) { Text("确定") }
            },
        )
    }
}

@Composable
private fun FileTile(item: FileItem115, onClick: (FileItem115) -> Unit) {
    Card(
        modifier = Modifier.clickable { onClick(item) },
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
    ) {
        Column(
            Modifier
                .padding(12.dp)
                .fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            val icon = when {
                item.isFolder -> Icons.Filled.Folder
                item.isVideoFile -> Icons.Filled.Movie
                else -> Icons.Filled.InsertDriveFile
            }
            Icon(icon, contentDescription = null, modifier = Modifier.size(48.dp))
            Spacer(Modifier.height(8.dp))
            Text(
                item.fn ?: "Unknown",
                style = MaterialTheme.typography.bodyMedium,
                maxLines = 2,
                modifier = Modifier.fillMaxWidth(),
            )
        }
    }
}

private const val TAG = "FileList115Screen"

@Composable
private fun QualityChooserDialog(
    file: FileItem115,
    options: List<VideoUrl115>,
    onCancel: () -> Unit,
    onSelect: (VideoUrl115) -> Unit,
) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("选择视频质量") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(file.fn ?: "", style = MaterialTheme.typography.bodySmall)
                Spacer(Modifier.height(8.dp))
                options.forEach { o ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .clickable { onSelect(o) }
                            .padding(vertical = 12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        val label = if (o.title == "原画") "原画" else "${o.title}（${o.width}×${o.height}）"
                        Text(label, style = MaterialTheme.typography.titleMedium)
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onCancel) { Text("取消") } },
    )
}

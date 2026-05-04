package space.vrplayer.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Cloud
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import space.vrplayer.cloud115.TokenManager115
import space.vrplayer.local.SafVideoPicker
import space.vrplayer.auth.AuthWebViewActivity

@Composable
fun HomeScreen(
    onOpen115: () -> Unit,
    onOpenSettings: () -> Unit,
) {
    val ctx = LocalContext.current
    val coroutineScope = rememberCoroutineScope()
    var loginDialogVisible by remember { mutableStateOf(false) }
    var auth115InProgress by remember { mutableStateOf(false) }

    val pickLocal = SafVideoPicker.rememberLauncher { uri ->
        if (uri != null) {
            PlayerLauncher.launch(ctx, uri)
        }
    }

    val authLauncher = rememberLauncherForActivityResult(AuthWebViewActivity.Contract()) { result ->
        if (result == null) {
            auth115InProgress = false
            return@rememberLauncherForActivityResult
        }
        val (code, state) = result
        coroutineScope.launch {
            runCatching { TokenManager115.exchangeCodeForToken(code, state) }
                .onSuccess { onOpen115() }
                .onFailure { it.printStackTrace() }
            auth115InProgress = false
        }
    }

    Column(
        Modifier
            .fillMaxSize()
            .padding(48.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text("VR Player", style = MaterialTheme.typography.headlineLarge, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(48.dp))

        Row(horizontalArrangement = Arrangement.spacedBy(24.dp)) {
            HomeTile(label = "选择视频文件", icon = Icons.Filled.FolderOpen) { pickLocal() }
            HomeTile(
                label = "115 网盘",
                icon = Icons.Filled.Cloud,
                loading = auth115InProgress,
            ) {
                if (TokenManager115.isLoggedIn) {
                    onOpen115()
                } else {
                    auth115InProgress = true
                    loginDialogVisible = true
                }
            }
            HomeTile(label = "设置", icon = Icons.Filled.Settings) { onOpenSettings() }
        }
    }

    if (loginDialogVisible) {
        AlertDialog(
            onDismissRequest = {
                loginDialogVisible = false
                auth115InProgress = false
            },
            title = { Text("需要登录") },
            text = { Text("检测到您尚未登录 115 网盘，点击确定前往登录页面。") },
            confirmButton = {
                TextButton(onClick = {
                    loginDialogVisible = false
                    authLauncher.launch(Unit)
                }) { Text("确定") }
            },
            dismissButton = {
                TextButton(onClick = {
                    loginDialogVisible = false
                    auth115InProgress = false
                }) { Text("取消") }
            },
        )
    }
}

@Composable
private fun HomeTile(
    label: String,
    icon: ImageVector,
    loading: Boolean = false,
    onClick: () -> Unit,
) {
    Card(
        modifier = Modifier
            .size(180.dp)
            .clickable(enabled = !loading, onClick = onClick),
        elevation = CardDefaults.cardElevation(defaultElevation = 4.dp),
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp),
            contentAlignment = Alignment.Center,
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Box(modifier = Modifier.size(72.dp), contentAlignment = Alignment.Center) {
                    if (loading) {
                        CircularProgressIndicator(modifier = Modifier.size(56.dp))
                    } else {
                        Icon(icon, contentDescription = label, modifier = Modifier.size(72.dp))
                    }
                }
                Spacer(Modifier.height(12.dp))
                Text(label, style = MaterialTheme.typography.titleMedium)
            }
        }
    }
}

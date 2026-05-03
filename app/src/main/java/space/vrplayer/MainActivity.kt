package space.vrplayer

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import space.vrplayer.ui.FileList115Screen
import space.vrplayer.ui.HomeScreen
import space.vrplayer.ui.SettingsScreen

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background,
                ) {
                    AppRoot()
                }
            }
        }
    }
}

private sealed interface Route {
    data object Home : Route
    data class FileList115(val cid: String?, val title: String) : Route
    data object Settings : Route
}

@Composable
private fun AppRoot() {
    var stack by rememberSaveable(stateSaver = RouteStackSaver) { mutableStateOf(listOf<Route>(Route.Home)) }

    when (val current = stack.last()) {
        Route.Home -> HomeScreen(
            onOpen115 = { stack = stack + Route.FileList115(cid = null, title = "115") },
            onOpenSettings = { stack = stack + Route.Settings },
        )
        is Route.FileList115 -> FileList115Screen(
            cid = current.cid,
            title = current.title,
            onOpenChild = { dirCid, dirTitle ->
                stack = stack + Route.FileList115(cid = dirCid, title = dirTitle)
            },
            onBack = { stack = stack.dropLast(1).ifEmpty { listOf(Route.Home) } },
        )
        Route.Settings -> SettingsScreen(
            onBack = { stack = stack.dropLast(1).ifEmpty { listOf(Route.Home) } },
        )
    }
}

private val RouteStackSaver = androidx.compose.runtime.saveable.listSaver<List<Route>, Any?>(
    save = { stack ->
        stack.map { r ->
            when (r) {
                Route.Home -> listOf("home")
                is Route.FileList115 -> listOf("list", r.cid, r.title)
                Route.Settings -> listOf("settings")
            }
        }
    },
    restore = { saved ->
        saved.map { entry ->
            @Suppress("UNCHECKED_CAST")
            val list = entry as List<Any?>
            when (list[0] as String) {
                "home" -> Route.Home
                "settings" -> Route.Settings
                "list" -> Route.FileList115(cid = list[1] as String?, title = list[2] as String)
                else -> Route.Home
            }
        }
    },
)

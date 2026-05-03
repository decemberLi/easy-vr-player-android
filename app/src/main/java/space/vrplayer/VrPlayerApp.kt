package space.vrplayer

import android.app.Application
import android.util.Log
import space.vrplayer.cloud115.TokenManager115
import space.vrplayer.proxy.LocalHttpProxy

class VrPlayerApp : Application() {
    override fun onCreate() {
        super.onCreate()
        instance = this
        Log.i(TAG, "VrPlayerApp.onCreate")
        TokenManager115.init(this)
        LocalHttpProxy.start()
    }

    companion object {
        private const val TAG = "VrPlayerApp"
        lateinit var instance: VrPlayerApp
            private set
    }
}

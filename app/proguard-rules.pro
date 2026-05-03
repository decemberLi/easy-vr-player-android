# Add project specific ProGuard rules here.

# Keep JNI native methods accessible
-keepclasseswithmembers class space.vrplayer.immersive.NativeBridge {
    public *;
}
-keepclasseswithmembers class * {
    native <methods>;
}

# Kotlinx serialization
-keepclassmembers class kotlinx.serialization.json.** { *; }
-keepattributes Signature, *Annotation*, InnerClasses, EnclosingMethod
-keep,includedescriptorclasses class space.vrplayer.cloud115.models.** { *; }

# OkHttp
-dontwarn okhttp3.**
-dontwarn okio.**

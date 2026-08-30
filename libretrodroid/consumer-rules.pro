# JNI looks these up by name, so R8 must not rename or remove them.
#
# This has bitten twice in the consuming app, both times only in a release build:
# ClassLinker::FindClass and a failed GetFieldID both end in Runtime::Abort
# rather than an exception, and nothing in Kotlin references the names, so
# shrinking them out looks entirely safe to R8 and to a reader.
#
# ShaderConfig is read by JavaUtils::shaderFromJava, and MemoryDescriptor is
# constructed by the achievements JNI through FindClass and GetMethodID.
-keep class com.swordfish.libretrodroid.** { *; }
-keepclassmembers class com.swordfish.libretrodroid.** { *; }

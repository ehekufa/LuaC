#include <jni.h>
#include <string>
#include "luac.h"

extern bool androidVulkanCreate(JNIEnv *env, jobject surface);
extern void androidVulkanDraw();
extern void androidVulkanDestroy();

static std::string fromJava(JNIEnv *env, jstring value) {
    const char *bytes = env->GetStringUTFChars(value, nullptr);
    std::string result = bytes ? bytes : "";
    if (bytes) env->ReleaseStringUTFChars(value, bytes);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luac_studio_MainActivity_nativeVersion(JNIEnv *env, jclass) {
    return env->NewStringUTF(luac_version());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luac_studio_MainActivity_nativeCheck(JNIEnv *env, jclass, jstring source) {
    std::string code = fromJava(env, source);
    char *error = nullptr;
    int valid = luac_check(code.c_str(), "AndroidEditor.lua", &error);
    std::string result = valid ? "Syntax OK\nNative C ready; runtime includes cycle GC and weak tables." : error;
    luac_free(error);
    return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luac_studio_MainActivity_nativeGenerate(JNIEnv *env, jclass, jstring source) {
    std::string code = fromJava(env, source);
    char *error = nullptr;
    char *generated = luac_compile_c(code.c_str(), "AndroidEditor.lua", &error);
    std::string result = generated ? generated : (error ? error : "Code generation failed");
    luac_free(generated);
    luac_free(error);
    return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_luac_studio_MainActivity_nativeVulkanCreate(JNIEnv *env, jclass, jobject surface) {
    return androidVulkanCreate(env, surface) ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT void JNICALL
Java_com_luac_studio_MainActivity_nativeVulkanDraw(JNIEnv *, jclass) { androidVulkanDraw(); }
extern "C" JNIEXPORT void JNICALL
Java_com_luac_studio_MainActivity_nativeVulkanDestroy(JNIEnv *, jclass) { androidVulkanDestroy(); }

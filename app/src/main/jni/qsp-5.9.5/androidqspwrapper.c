/*
 * Questwave JNI adapter.
 *
 * This file is NOT part of QSP. It is Questwave's own glue code implementing
 * the historical Questopia "NativeMethods" interface on top of the public
 * QSP 5.9.5 default binding.
 *
 * Ownership of the engine build stays with QSP 5.9.5 (MPL-2.0); see
 * app/src/main/jni/qsp-5.9.5/README.md for the pinned revisions.
 */

#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

/* QSP 5.9.5, default binding. declarations.h pulls in qsp_config.h,
 * bindings/bindings_config.h and bindings/default/qsp_default.h. */
#include "qsp/declarations.h"
#include "qsp/actions.h"
#include "qsp/callbacks.h"
#include "qsp/common.h"
#include "qsp/errors.h"
#include "qsp/locations.h"
#include "qsp/objects.h"
#include "qsp/text.h"

#define ADAPTER_TAG "QuestwaveQSP"
#define ADAPTER_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ADAPTER_TAG, __VA_ARGS__)
#define ADAPTER_LOGI(...) __android_log_print(ANDROID_LOG_INFO, ADAPTER_TAG, __VA_ARGS__)

/* ------------------------------------------------------------------------- */
/* Global state                                                              */
/* ------------------------------------------------------------------------- */

static JavaVM *g_vm;
static jobject g_callbacks;
static jclass g_callbacksClass;

static jmethodID g_mRefreshInt;
static jmethodID g_mShowPicture;
static jmethodID g_mSetTimer;
static jmethodID g_mShowMessage;
static jmethodID g_mPlayFile;
static jmethodID g_mIsPlayingFile;
static jmethodID g_mCloseFile;
static jmethodID g_mOpenGame;
static jmethodID g_mSaveGame;
static jmethodID g_mInputBox;
static jmethodID g_mGetMSCount;
static jmethodID g_mAddMenuItem;
static jmethodID g_mShowMenu;
static jmethodID g_mDeleteMenu;
static jmethodID g_mWait;
static jmethodID g_mShowWindow;
static jmethodID g_mGetFileContents;
static jmethodID g_mChangeQuestPath;

static jclass g_actionClass;
static jfieldID g_actionName;
static jfieldID g_actionImage;

static jclass g_objectClass;
static jfieldID g_objectName;
static jfieldID g_objectImage;

static jclass g_errorClass;
static jfieldID g_errorLocName;
static jfieldID g_errorErrorNum;
static jfieldID g_errorIndex;
static jfieldID g_errorLine;

static jclass g_varsClass;
static jfieldID g_varsSuccess;
static jfieldID g_varsStringValue;
static jfieldID g_varsIntValue;

/* Legacy third argument of QSPLoadGameWorldFromData was the .qsp path.
 * 5.9.5 has no file system, so the adapter keeps the path to resolve
 * INCLUDE / OPENQST files requested by the engine. */
static QSP_CHAR *g_gamePath;
static QSP_CHAR *g_baseDir;

/* Menu selection, mirroring the old QSPSelectMenuItem() flow. */
static int g_pendingMenuIndex;

/* Content snapshots behind QSPIs*Changed(). 5.9.5 intentionally dropped the
 * "dirty" flags of 5.7; the host compares state. We keep hashes so that the
 * existing Java code keeps its efficient change detection. */
static uint64_t g_lastMainHash;
static uint64_t g_lastVarsHash;
static uint64_t g_lastActionsHash;
static uint64_t g_lastObjectsHash;

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

static JNIEnv *adapterEnv(void)
{
    JNIEnv *env = NULL;
    jint result;

    if (!g_vm) return NULL;
    result = (*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6);
    if (result == JNI_EDETACHED)
    {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK)
            return NULL;
    }
    else if (result != JNI_OK)
    {
        return NULL;
    }
    return env;
}

static void adapterClearException(JNIEnv *env)
{
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        ADAPTER_LOGE("Java exception in a QSP callback (see above)");
    }
}

/* Builds a Java string from a QSP string. QSP_CHAR is 2-byte wchar_t here
 * (-fshort-wchar), i.e. exactly a UTF-16 code unit, so no transcoding is
 * needed. Null QSP strings become empty Java strings. */
static jstring adapterNewString(JNIEnv *env, QSPString s)
{
    if (!s.Str || !s.End || s.End <= s.Str)
        return (*env)->NewString(env, (const jchar *)L"", 0);
    return (*env)->NewString(env, (const jchar *)s.Str, (jsize)(s.End - s.Str));
}

/* Same, but preserves the "no file name" case used by LOADGAME/SAVEGAME
 * without arguments. Java turns null into the file picker. */
static jstring adapterNewStringOrNull(JNIEnv *env, QSPString s)
{
    if (!s.Str || !s.End || s.End <= s.Str)
        return NULL;
    return (*env)->NewString(env, (const jchar *)s.Str, (jsize)(s.End - s.Str));
}

/* Copies a Java string into a newly allocated, NUL-terminated QSP_CHAR
 * buffer. Caller frees. */
static QSP_CHAR *adapterCopyJavaString(JNIEnv *env, jstring js, int *lengthOut)
{
    jsize length;
    QSP_CHAR *buffer;

    if (lengthOut) *lengthOut = 0;
    if (!js) return NULL;

    length = (*env)->GetStringLength(env, js);
    buffer = (QSP_CHAR *)malloc(((size_t)length + 1) * sizeof(QSP_CHAR));
    if (!buffer) return NULL;

    (*env)->GetStringRegion(env, js, 0, length, (jchar *)buffer);
    buffer[length] = 0;
    if (lengthOut) *lengthOut = (int)length;
    return buffer;
}

static QSPString adapterQspStringFromJava(JNIEnv *env, jstring js, QSP_CHAR **allocated)
{
    int length = 0;
    QSP_CHAR *buffer = adapterCopyJavaString(env, js, &length);

    *allocated = buffer;
    if (!buffer) return QSPStringFromLen((QSP_CHAR *)L"", 0);
    return QSPStringFromLen(buffer, length);
}

/* ------------------------------------------------------------------------- */
/* JNI string conversions for the 5.7-compatible NativeMethods               */
/* ------------------------------------------------------------------------- */

static jstring adapterStringFromQsp(JNIEnv *env, QSPString s)
{
    return adapterNewString(env, s);
}

static QSP_CHAR *adapterCopyQspString(QSPString s)
{
    size_t length = (s.Str && s.End && s.End > s.Str) ? (size_t)(s.End - s.Str) : 0;
    QSP_CHAR *buffer = (QSP_CHAR *)malloc((length + 1) * sizeof(QSP_CHAR));

    if (!buffer) return NULL;
    if (length) memcpy(buffer, s.Str, length * sizeof(QSP_CHAR));
    buffer[length] = 0;
    return buffer;
}

/* ------------------------------------------------------------------------- */
/* Content hashes (QSPIs*Changed)                                            */
/* ------------------------------------------------------------------------- */

static uint64_t adapterFnvInit(void)
{
    return 1469598103934665603ULL;
}

static uint64_t adapterFnvBytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t i;

    for (i = 0; i < size; ++i)
    {
        hash ^= (uint64_t)bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t adapterFnvString(uint64_t hash, QSPString s)
{
    size_t length = (s.Str && s.End && s.End > s.Str) ? (size_t)(s.End - s.Str) : 0;

    hash = adapterFnvBytes(hash, &length, sizeof(length));
    if (length) hash = adapterFnvBytes(hash, s.Str, length * sizeof(QSP_CHAR));
    return hash;
}

static uint64_t adapterHashMainDesc(void)
{
    uint64_t hash = adapterFnvInit();
    return adapterFnvString(hash, QSPGetMainDesc());
}

static uint64_t adapterHashVarsDesc(void)
{
    uint64_t hash = adapterFnvInit();
    return adapterFnvString(hash, QSPGetVarsDesc());
}

static uint64_t adapterHashActions(void)
{
    uint64_t hash = adapterFnvInit();
    int i;

    hash = adapterFnvBytes(hash, &qspCurActsCount, sizeof(qspCurActsCount));
    for (i = 0; i < qspCurActsCount; ++i)
    {
        hash = adapterFnvString(hash, qspCurActions[i].Desc);
        hash = adapterFnvString(hash, qspCurActions[i].Image);
    }
    return hash;
}

static uint64_t adapterHashObjects(void)
{
    uint64_t hash = adapterFnvInit();
    QSPObjectItem item;
    int i;

    hash = adapterFnvBytes(hash, &qspCurObjsCount, sizeof(qspCurObjsCount));
    for (i = 0; i < qspCurObjsCount; ++i)
    {
        if (!qspGetObjectInfoByIndex(i, &item))
        {
            item.Title = qspNullString;
            item.Image = qspNullString;
        }
        hash = adapterFnvString(hash, item.Title);
        hash = adapterFnvString(hash, item.Image);
    }
    return hash;
}

static QSP_BOOL adapterCheckChanged(uint64_t *last, uint64_t current)
{
    if (*last != current)
    {
        *last = current;
        return QSP_TRUE;
    }
    return QSP_FALSE;
}

static void adapterResetChangedState(void)
{
    g_lastMainHash = 0;
    g_lastVarsHash = 0;
    g_lastActionsHash = 0;
    g_lastObjectsHash = 0;
}

/* ------------------------------------------------------------------------- */
/* Game file path handling                                                   */
/* ------------------------------------------------------------------------- */

static QSP_CHAR *adapterDirName(const QSP_CHAR *path)
{
    size_t length;
    size_t i;
    size_t lastSeparator = (size_t)-1;

    if (!path) return NULL;
    length = (size_t)-1;
    for (i = 0; path[i]; ++i)
    {
        if (path[i] == L'/' || path[i] == L'\\') lastSeparator = i;
    }
    length = i;

    if (lastSeparator == (size_t)-1)
        return (QSP_CHAR *)calloc(1, sizeof(QSP_CHAR));

    if (lastSeparator == 0)
    {
        QSP_CHAR *root = (QSP_CHAR *)malloc(2 * sizeof(QSP_CHAR));
        if (!root) return NULL;
        root[0] = L'/';
        root[1] = 0;
        return root;
    }

    {
        QSP_CHAR *dir = (QSP_CHAR *)malloc((lastSeparator + 1) * sizeof(QSP_CHAR));
        if (!dir) return NULL;
        memcpy(dir, path, lastSeparator * sizeof(QSP_CHAR));
        dir[lastSeparator] = 0;
        return dir;
    }
}

static int adapterIsAbsolutePath(QSPString path)
{
    if (!path.Str || !path.End || path.End <= path.Str) return 0;
    if (path.Str[0] == L'/' || path.Str[0] == L'\\') return 1;
    if ((path.End - path.Str) >= 2 && path.Str[1] == L':') return 1;
    return 0;
}

/* Returns a newly allocated, NUL-terminated path with '/' separators. */
static QSP_CHAR *adapterResolvePath(QSPString file)
{
    size_t fileLength;
    size_t baseLength = 0;
    size_t i;
    size_t offset = 0;
    QSP_CHAR *result;
    int needsSeparator = 0;

    if (!file.Str || !file.End || file.End <= file.Str) return NULL;
    fileLength = (size_t)(file.End - file.Str);

    if (adapterIsAbsolutePath(file) || !g_baseDir || !g_baseDir[0])
    {
        result = (QSP_CHAR *)malloc((fileLength + 1) * sizeof(QSP_CHAR));
        if (!result) return NULL;
        memcpy(result, file.Str, fileLength * sizeof(QSP_CHAR));
        result[fileLength] = 0;
        for (i = 0; i < fileLength; ++i)
            if (result[i] == L'\\') result[i] = L'/';
        return result;
    }

    baseLength = (size_t)-1;
    for (i = 0; g_baseDir[i]; ++i) { }
    baseLength = i;

    needsSeparator = (baseLength > 0 && g_baseDir[baseLength - 1] != L'/' &&
                      g_baseDir[baseLength - 1] != L'\\');

    result = (QSP_CHAR *)malloc((baseLength + 1 + fileLength + 1) * sizeof(QSP_CHAR));
    if (!result) return NULL;

    memcpy(result, g_baseDir, baseLength * sizeof(QSP_CHAR));
    offset = baseLength;
    if (needsSeparator) result[offset++] = L'/';
    memcpy(result + offset, file.Str, fileLength * sizeof(QSP_CHAR));
    offset += fileLength;
    result[offset] = 0;

    for (i = 0; i < offset; ++i)
        if (result[i] == L'\\') result[i] = L'/';
    return result;
}

/* ------------------------------------------------------------------------- */
/* Java callbacks (engine -> Java)                                           */
/* ------------------------------------------------------------------------- */

static void adapterCallShowPicture(QSPString file)
{
    JNIEnv *env = adapterEnv();
    jstring value;

    if (!env || !g_callbacks) return;
    value = adapterNewString(env, file);
    (*env)->CallVoidMethod(env, g_callbacks, g_mShowPicture, value);
    (*env)->DeleteLocalRef(env, value);
    adapterClearException(env);
}

static void adapterCallSetTimer(int msecs)
{
    JNIEnv *env = adapterEnv();
    if (!env || !g_callbacks) return;
    (*env)->CallVoidMethod(env, g_callbacks, g_mSetTimer, (jint)msecs);
    adapterClearException(env);
}

static void adapterCallShowMessage(QSPString text)
{
    JNIEnv *env = adapterEnv();
    jstring value;

    if (!env || !g_callbacks) return;
    value = adapterNewString(env, text);
    (*env)->CallVoidMethod(env, g_callbacks, g_mShowMessage, value);
    (*env)->DeleteLocalRef(env, value);
    adapterClearException(env);
}

static void adapterCallPlayFile(QSPString file, int volume)
{
    JNIEnv *env = adapterEnv();
    jstring value;

    if (!env || !g_callbacks) return;
    value = adapterNewString(env, file);
    (*env)->CallVoidMethod(env, g_callbacks, g_mPlayFile, value, (jint)volume);
    (*env)->DeleteLocalRef(env, value);
    adapterClearException(env);
}

static QSP_BOOL adapterCallIsPlayingFile(QSPString file)
{
    JNIEnv *env = adapterEnv();
    jstring value;
    jboolean result = JNI_FALSE;

    if (!env || !g_callbacks) return QSP_FALSE;
    value = adapterNewString(env, file);
    result = (*env)->CallBooleanMethod(env, g_callbacks, g_mIsPlayingFile, value);
    (*env)->DeleteLocalRef(env, value);
    adapterClearException(env);
    return result == JNI_TRUE ? QSP_TRUE : QSP_FALSE;
}

static void adapterCallCloseFile(QSPString file)
{
    JNIEnv *env = adapterEnv();
    jstring value;

    if (!env || !g_callbacks) return;
    value = adapterNewString(env, file);
    (*env)->CallVoidMethod(env, g_callbacks, g_mCloseFile, value);
    (*env)->DeleteLocalRef(env, value);
    adapterClearException(env);
}

static void adapterCallOpenGameStatus(QSPString file)
{
    JNIEnv *env = adapterEnv();
    jstring value;

    if (!env || !g_callbacks) return;
    value = adapterNewStringOrNull(env, file);
    (*env)->CallVoidMethod(env, g_callbacks, g_mOpenGame, value);
    if (value) (*env)->DeleteLocalRef(env, value);
    adapterClearException(env);
}

static void adapterCallSaveGameStatus(QSPString file)
{
    JNIEnv *env = adapterEnv();
    jstring value;

    if (!env || !g_callbacks) return;
    value = adapterNewStringOrNull(env, file);
    (*env)->CallVoidMethod(env, g_callbacks, g_mSaveGame, value);
    if (value) (*env)->DeleteLocalRef(env, value);
    adapterClearException(env);
}

/* 5.7 notified Java about the directory of the loaded quest; that is how
 * Questopia updated asset resolution after OPENQST. 5.9.5 has no such
 * callback, so the adapter issues the same Java call itself. */
static void adapterCallChangeQuestPath(const QSP_CHAR *path)
{
    JNIEnv *env = adapterEnv();
    jstring value;
    size_t length = 0;

    if (!env || !g_callbacks || !path || !path[0] || !g_mChangeQuestPath) return;
    while (path[length]) ++length;
    value = (*env)->NewString(env, (const jchar *)path, (jsize)length);
    if (!value) return;
    (*env)->CallVoidMethod(env, g_callbacks, g_mChangeQuestPath, value);
    (*env)->DeleteLocalRef(env, value);
    adapterClearException(env);
}

static void adapterCallInputBox(QSPString text, QSP_CHAR *buffer, int maxLen)
{
    JNIEnv *env = adapterEnv();
    jstring prompt;
    jstring input;

    buffer[0] = 0;
    if (!env || !g_callbacks) return;

    prompt = adapterNewString(env, text);
    input = (jstring)(*env)->CallObjectMethod(env, g_callbacks, g_mInputBox, prompt);
    (*env)->DeleteLocalRef(env, prompt);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return;
    }

    if (input)
    {
        jsize length = (*env)->GetStringLength(env, input);
        jsize copyLength = length < maxLen ? length : maxLen;
        (*env)->GetStringRegion(env, input, 0, copyLength, (jchar *)buffer);
        buffer[copyLength] = 0;
        (*env)->DeleteLocalRef(env, input);
    }
}

static int adapterCallGetMSCount(void)
{
    JNIEnv *env = adapterEnv();
    jint result;

    if (!env || !g_callbacks) return 0;
    result = (*env)->CallIntMethod(env, g_callbacks, g_mGetMSCount);
    adapterClearException(env);
    return (int)result;
}

static int adapterCallShowMenu(QSPListItem *items, int count)
{
    JNIEnv *env = adapterEnv();
    int i;

    if (!env || !g_callbacks)
        return -1;

    (*env)->CallVoidMethod(env, g_callbacks, g_mDeleteMenu);
    adapterClearException(env);

    for (i = 0; i < count; ++i)
    {
        jstring name = adapterNewString(env, items[i].Name);
        jstring image = adapterNewString(env, items[i].Image);
        (*env)->CallVoidMethod(env, g_callbacks, g_mAddMenuItem, name, image);
        (*env)->DeleteLocalRef(env, name);
        (*env)->DeleteLocalRef(env, image);
        adapterClearException(env);
    }

    g_pendingMenuIndex = -1;
    (*env)->CallVoidMethod(env, g_callbacks, g_mShowMenu);
    adapterClearException(env);
    return g_pendingMenuIndex;
}

static void adapterCallRefresh(QSP_BOOL isForced, QSP_BOOL isNewDesc)
{
    JNIEnv *env = adapterEnv();
    (void)isForced;
    (void)isNewDesc;
    if (!env || !g_callbacks) return;
    (*env)->CallVoidMethod(env, g_callbacks, g_mRefreshInt);
    adapterClearException(env);
}

static void adapterCallSleep(int msecs)
{
    JNIEnv *env = adapterEnv();
    if (!env || !g_callbacks) return;
    (*env)->CallVoidMethod(env, g_callbacks, g_mWait, (jint)msecs);
    adapterClearException(env);
}

/* 5.7 sent the enum index; 5.9.5 sends a bit set. Java still uses the old
 * four-value enum. */
static int adapterMapWindowType(int type)
{
    if (type & QSP_WIN_ACTS) return 0;
    if (type & QSP_WIN_OBJS) return 1;
    if (type & QSP_WIN_VARS) return 2;
    if (type & QSP_WIN_INPUT) return 3;
    return -1;
}

static void adapterShowOneWindow(JNIEnv *env, int type, QSP_BOOL toShow)
{
    int mapped = adapterMapWindowType(type);

    if (mapped < 0) return;
    (*env)->CallVoidMethod(env, g_callbacks, g_mShowWindow,
                           (jint)mapped, toShow ? JNI_TRUE : JNI_FALSE);
    adapterClearException(env);
}

static void adapterCallShowWindow(int type, QSP_BOOL toShow)
{
    JNIEnv *env = adapterEnv();

    if (!env || !g_callbacks) return;

    /* 5.9.5 sends a bit set (for example all disabled windows at once).
     * Questopia's Java side expects one window per call. */
    if (type & QSP_WIN_ACTS) adapterShowOneWindow(env, QSP_WIN_ACTS, toShow);
    if (type & QSP_WIN_OBJS) adapterShowOneWindow(env, QSP_WIN_OBJS, toShow);
    if (type & QSP_WIN_VARS) adapterShowOneWindow(env, QSP_WIN_VARS, toShow);
    if (type & QSP_WIN_INPUT) adapterShowOneWindow(env, QSP_WIN_INPUT, toShow);
}

/* Reads a game file through Java (the engine has no file system access in
 * 5.9.5) and feeds it to the engine. */
static void adapterLoadGameFile(QSPString file, QSP_BOOL isNewGame)
{
    JNIEnv *env = adapterEnv();
    QSP_CHAR *resolved;
    QSP_CHAR *newGamePath = NULL;
    QSP_CHAR *newBaseDir = NULL;
    QSPString resolvedQsp;
    jstring javaPath;
    jbyteArray data;
    jsize length;
    void *buffer;

    if (!env || !g_callbacks) return;

    resolved = adapterResolvePath(file);
    if (!resolved)
    {
        qspSetError(QSP_ERR_CANTLOADFILE);
        return;
    }

    resolvedQsp = QSPStringFromC(resolved);
    if (isNewGame)
    {
        /* OPENQST switches the game. Later relative requests must be resolved
         * against the newly opened file, not against the original one. */
        newGamePath = adapterCopyQspString(resolvedQsp);
        newBaseDir = adapterDirName(resolved);
    }
    javaPath = adapterNewString(env, resolvedQsp);
    data = (jbyteArray)(*env)->CallObjectMethod(env, g_callbacks, g_mGetFileContents, javaPath);
    (*env)->DeleteLocalRef(env, javaPath);
    free(resolved);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        data = NULL;
    }

    if (!data)
    {
        ADAPTER_LOGE("Game file not found");
        free(newGamePath);
        free(newBaseDir);
        qspSetError(QSP_ERR_CANTLOADFILE);
        return;
    }

    length = (*env)->GetArrayLength(env, data);
    if (length <= 0)
    {
        (*env)->DeleteLocalRef(env, data);
        free(newGamePath);
        free(newBaseDir);
        qspSetError(QSP_ERR_CANTLOADFILE);
        return;
    }

    buffer = malloc((size_t)length);
    if (!buffer)
    {
        (*env)->DeleteLocalRef(env, data);
        free(newGamePath);
        free(newBaseDir);
        qspSetError(QSP_ERR_CANTLOADFILE);
        return;
    }

    (*env)->GetByteArrayRegion(env, data, 0, length, (jbyte *)buffer);
    (*env)->DeleteLocalRef(env, data);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        free(buffer);
        free(newGamePath);
        free(newBaseDir);
        qspSetError(QSP_ERR_CANTLOADFILE);
        return;
    }

    if (!QSPLoadGameWorldFromData(buffer, (int)length, isNewGame))
    {
        ADAPTER_LOGE("QSPLoadGameWorldFromData failed for an engine file request");
    }
    else
    {
        if (isNewGame && newGamePath && newBaseDir)
        {
            adapterCallChangeQuestPath(newBaseDir);
            free(g_gamePath);
            free(g_baseDir);
            g_gamePath = newGamePath;
            g_baseDir = newBaseDir;
            newGamePath = NULL;
            newBaseDir = NULL;
        }
        adapterResetChangedState();
    }

    free(buffer);
    free(newGamePath);
    free(newBaseDir);
}

/* ------------------------------------------------------------------------- */
/* Init / deinit                                                             */
/* ------------------------------------------------------------------------- */

static void adapterDeleteGlobalRefs(JNIEnv *env)
{
    if (g_callbacks) { (*env)->DeleteGlobalRef(env, g_callbacks); g_callbacks = NULL; }
    if (g_callbacksClass) { (*env)->DeleteGlobalRef(env, g_callbacksClass); g_callbacksClass = NULL; }
    if (g_actionClass) { (*env)->DeleteGlobalRef(env, g_actionClass); g_actionClass = NULL; }
    if (g_objectClass) { (*env)->DeleteGlobalRef(env, g_objectClass); g_objectClass = NULL; }
    if (g_errorClass) { (*env)->DeleteGlobalRef(env, g_errorClass); g_errorClass = NULL; }
    if (g_varsClass) { (*env)->DeleteGlobalRef(env, g_varsClass); g_varsClass = NULL; }
}

static jclass adapterFindClass(JNIEnv *env, const char *name)
{
    jclass local = (*env)->FindClass(env, name);
    jclass global;

    if (!local) return NULL;
    global = (jclass)(*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    return global;
}

static int adapterInitDto(JNIEnv *env)
{
    g_actionClass = adapterFindClass(env, "com/questwave/player/dto/libQP/ActionData");
    if (!g_actionClass) return 0;
    g_actionName = (*env)->GetFieldID(env, g_actionClass, "name", "Ljava/lang/String;");
    g_actionImage = (*env)->GetFieldID(env, g_actionClass, "image", "Ljava/lang/String;");

    g_objectClass = adapterFindClass(env, "com/questwave/player/dto/libQP/ObjectData");
    if (!g_objectClass) return 0;
    g_objectName = (*env)->GetFieldID(env, g_objectClass, "name", "Ljava/lang/String;");
    g_objectImage = (*env)->GetFieldID(env, g_objectClass, "image", "Ljava/lang/String;");

    g_errorClass = adapterFindClass(env, "com/questwave/player/dto/libQP/ErrorData");
    if (!g_errorClass) return 0;
    g_errorLocName = (*env)->GetFieldID(env, g_errorClass, "locName", "Ljava/lang/String;");
    g_errorErrorNum = (*env)->GetFieldID(env, g_errorClass, "errorNum", "I");
    g_errorIndex = (*env)->GetFieldID(env, g_errorClass, "index", "I");
    g_errorLine = (*env)->GetFieldID(env, g_errorClass, "line", "I");

    g_varsClass = adapterFindClass(env, "com/questwave/player/dto/libQP/GetVarValuesResponse");
    if (!g_varsClass) return 0;
    g_varsSuccess = (*env)->GetFieldID(env, g_varsClass, "isSuccess", "Z");
    g_varsStringValue = (*env)->GetFieldID(env, g_varsClass, "stringValue", "Ljava/lang/String;");
    g_varsIntValue = (*env)->GetFieldID(env, g_varsClass, "intValue", "I");

    return g_actionName && g_actionImage && g_objectName && g_objectImage &&
           g_errorLocName && g_errorErrorNum && g_errorIndex && g_errorLine &&
           g_varsSuccess && g_varsStringValue && g_varsIntValue;
}

static int adapterCacheCallbacks(JNIEnv *env)
{
    g_mRefreshInt = (*env)->GetMethodID(env, g_callbacksClass, "RefreshInt", "()V");
    g_mShowPicture = (*env)->GetMethodID(env, g_callbacksClass, "ShowPicture", "(Ljava/lang/String;)V");
    g_mSetTimer = (*env)->GetMethodID(env, g_callbacksClass, "SetTimer", "(I)V");
    g_mShowMessage = (*env)->GetMethodID(env, g_callbacksClass, "ShowMessage", "(Ljava/lang/String;)V");
    g_mPlayFile = (*env)->GetMethodID(env, g_callbacksClass, "PlayFile", "(Ljava/lang/String;I)V");
    g_mIsPlayingFile = (*env)->GetMethodID(env, g_callbacksClass, "IsPlayingFile", "(Ljava/lang/String;)Z");
    g_mCloseFile = (*env)->GetMethodID(env, g_callbacksClass, "CloseFile", "(Ljava/lang/String;)V");
    g_mOpenGame = (*env)->GetMethodID(env, g_callbacksClass, "OpenGame", "(Ljava/lang/String;)V");
    g_mSaveGame = (*env)->GetMethodID(env, g_callbacksClass, "SaveGame", "(Ljava/lang/String;)V");
    g_mInputBox = (*env)->GetMethodID(env, g_callbacksClass, "InputBox", "(Ljava/lang/String;)Ljava/lang/String;");
    g_mGetMSCount = (*env)->GetMethodID(env, g_callbacksClass, "GetMSCount", "()I");
    g_mAddMenuItem = (*env)->GetMethodID(env, g_callbacksClass, "AddMenuItem", "(Ljava/lang/String;Ljava/lang/String;)V");
    g_mShowMenu = (*env)->GetMethodID(env, g_callbacksClass, "ShowMenu", "()V");
    g_mDeleteMenu = (*env)->GetMethodID(env, g_callbacksClass, "DeleteMenu", "()V");
    g_mWait = (*env)->GetMethodID(env, g_callbacksClass, "Wait", "(I)V");
    g_mShowWindow = (*env)->GetMethodID(env, g_callbacksClass, "ShowWindow", "(IZ)V");
    g_mGetFileContents = (*env)->GetMethodID(env, g_callbacksClass, "GetFileContents", "(Ljava/lang/String;)[B");
    g_mChangeQuestPath = (*env)->GetMethodID(env, g_callbacksClass, "ChangeQuestPath", "(Ljava/lang/String;)V");

    return g_mRefreshInt && g_mShowPicture && g_mSetTimer && g_mShowMessage &&
           g_mPlayFile && g_mIsPlayingFile && g_mCloseFile && g_mOpenGame &&
           g_mSaveGame && g_mInputBox && g_mGetMSCount && g_mAddMenuItem &&
           g_mShowMenu && g_mDeleteMenu && g_mWait && g_mShowWindow &&
           g_mGetFileContents && g_mChangeQuestPath;
}

/* ------------------------------------------------------------------------- */
/* JNI: lifecycle                                                            */
/* ------------------------------------------------------------------------- */

JNIEXPORT void JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPInit(JNIEnv *env, jobject thiz)
{
    jclass nativeClass = (*env)->GetObjectClass(env, thiz);
    jfieldID callbacksField = (*env)->GetFieldID(
        env, nativeClass, "callbacks",
        "Lcom/questwave/player/model/libQP/LibQpCallbacks;");
    jobject callbacks;

    if (!callbacksField)
    {
        ADAPTER_LOGE("LibQpCallbacks field not found");
        return;
    }

    callbacks = (*env)->GetObjectField(env, thiz, callbacksField);
    if (!callbacks)
    {
        ADAPTER_LOGE("LibQpCallbacks instance is null");
        return;
    }

    if ((*env)->GetJavaVM(env, &g_vm) != JNI_OK) g_vm = NULL;

    g_callbacks = (*env)->NewGlobalRef(env, callbacks);
    g_callbacksClass = (jclass)(*env)->NewGlobalRef(env, (*env)->GetObjectClass(env, callbacks));

    if (!g_callbacks || !g_callbacksClass ||
        !adapterCacheCallbacks(env) || !adapterInitDto(env) ||
        ((*env)->ExceptionCheck(env)))
    {
        ADAPTER_LOGE("Cannot cache Java callback methods/classes");
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        adapterDeleteGlobalRefs(env);
        (*env)->DeleteLocalRef(env, callbacks);
        (*env)->DeleteLocalRef(env, nativeClass);
        return;
    }

    (*env)->DeleteLocalRef(env, callbacks);
    (*env)->DeleteLocalRef(env, nativeClass);

    g_pendingMenuIndex = -1;
    adapterResetChangedState();

    QSPInit();

    QSPSetCallback(QSP_CALL_REFRESHINT, (QSP_CALLBACK)adapterCallRefresh);
    QSPSetCallback(QSP_CALL_SHOWIMAGE, (QSP_CALLBACK)adapterCallShowPicture);
    QSPSetCallback(QSP_CALL_SHOWMSGSTR, (QSP_CALLBACK)adapterCallShowMessage);
    QSPSetCallback(QSP_CALL_SETTIMER, (QSP_CALLBACK)adapterCallSetTimer);
    QSPSetCallback(QSP_CALL_PLAYFILE, (QSP_CALLBACK)adapterCallPlayFile);
    QSPSetCallback(QSP_CALL_ISPLAYINGFILE, (QSP_CALLBACK)adapterCallIsPlayingFile);
    QSPSetCallback(QSP_CALL_CLOSEFILE, (QSP_CALLBACK)adapterCallCloseFile);
    QSPSetCallback(QSP_CALL_OPENGAMESTATUS, (QSP_CALLBACK)adapterCallOpenGameStatus);
    QSPSetCallback(QSP_CALL_SAVEGAMESTATUS, (QSP_CALLBACK)adapterCallSaveGameStatus);
    QSPSetCallback(QSP_CALL_INPUTBOX, (QSP_CALLBACK)adapterCallInputBox);
    QSPSetCallback(QSP_CALL_GETMSCOUNT, (QSP_CALLBACK)adapterCallGetMSCount);
    QSPSetCallback(QSP_CALL_SHOWMENU, (QSP_CALLBACK)adapterCallShowMenu);
    QSPSetCallback(QSP_CALL_SHOWWINDOW, (QSP_CALLBACK)adapterCallShowWindow);
    QSPSetCallback(QSP_CALL_SLEEP, (QSP_CALLBACK)adapterCallSleep);
    QSPSetCallback(QSP_CALL_OPENGAME, (QSP_CALLBACK)adapterLoadGameFile);

    ADAPTER_LOGI("QSP 5.9.5 initialized");
}

JNIEXPORT void JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPDeInit(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    QSPTerminate();

    if (g_gamePath) { free(g_gamePath); g_gamePath = NULL; }
    if (g_baseDir) { free(g_baseDir); g_baseDir = NULL; }

    adapterDeleteGlobalRefs(env);
    g_vm = NULL;
}

/* ------------------------------------------------------------------------- */
/* JNI: engine information                                                   */
/* ------------------------------------------------------------------------- */

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPIsInCallBack(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return qspIsInCallback ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPEnableDebugMode(JNIEnv *env, jobject thiz, jboolean isDebug)
{
    (void)env; (void)thiz;
    QSPEnableDebugMode(isDebug == JNI_TRUE ? QSP_TRUE : QSP_FALSE);
}

JNIEXPORT jobject JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetCurStateData(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return NULL; /* unused by Questopia; the old wrapper was a stub too */
}

JNIEXPORT jstring JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetVersion(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    return adapterStringFromQsp(env, QSPGetVersion());
}

JNIEXPORT jstring JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetCompiledDateTime(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    return adapterStringFromQsp(env, QSPGetCompiledDateTime());
}

JNIEXPORT jint JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetFullRefreshCount(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return (jint)qspFullRefreshCount;
}

JNIEXPORT jstring JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetQstFullPath(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    if (!g_gamePath)
        return (*env)->NewString(env, (const jchar *)L"", 0);
    return adapterStringFromQsp(env, QSPStringFromC(g_gamePath));
}

JNIEXPORT jstring JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetCurLoc(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    if (qspCurLoc >= 0 && qspCurLoc < qspLocsCount)
        return adapterStringFromQsp(env, qspLocs[qspCurLoc].Name);
    return (*env)->NewString(env, (const jchar *)L"", 0);
}

JNIEXPORT jobject JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetExprValue(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return NULL; /* unused by Questopia; the old wrapper was a stub too */
}

/* ------------------------------------------------------------------------- */
/* JNI: texts                                                                */
/* ------------------------------------------------------------------------- */

JNIEXPORT jstring JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetMainDesc(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    return adapterStringFromQsp(env, QSPGetMainDesc());
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPIsMainDescChanged(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return adapterCheckChanged(&g_lastMainHash, adapterHashMainDesc()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetVarsDesc(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    return adapterStringFromQsp(env, QSPGetVarsDesc());
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPIsVarsDescChanged(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return adapterCheckChanged(&g_lastVarsHash, adapterHashVarsDesc()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobject JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetVarValuesCount(JNIEnv *env, jobject thiz, jstring name)
{
    /* The old wrapper returned null here too; Questopia does not call it. */
    (void)env; (void)thiz; (void)name;
    return NULL;
}

JNIEXPORT jobject JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetVarValues(JNIEnv *env, jobject thiz,
                                                                    jstring name, jint ind)
{
    QSP_CHAR *nameBuffer = NULL;
    QSPString nameString;
    QSPVariant value;
    QSP_BOOL success;
    jobject result;
    jstring stringValue = NULL;
    QSP_CHAR textBuffer[128];
    jint intValue = 0;

    (void)thiz;
    if (!env || !g_varsClass) return NULL;

    nameString = adapterQspStringFromJava(env, name, &nameBuffer);
    success = QSPGetVarValue(nameString, (int)ind, &value);
    free(nameBuffer);

    textBuffer[0] = 0;
    if (success)
    {
        QSPConvertValueToString(value, textBuffer, (int)(sizeof(textBuffer) / sizeof(textBuffer[0])));
        textBuffer[(sizeof(textBuffer) / sizeof(textBuffer[0])) - 1] = 0;
        stringValue = adapterNewString(env, QSPStringFromC(textBuffer));

        if (QSP_ISNUM(value.Type))
        {
            intValue = (jint)QSP_NUM(value);
        }
        else if (QSP_ISSTR(value.Type))
        {
            QSP_BOOL valid = QSP_FALSE;
            QSP_BIGINT num = qspStrToNum(QSP_STR(value), &valid);
            intValue = valid ? (jint)num : 0;
        }
    }

    result = (*env)->AllocObject(env, g_varsClass);
    if (!result)
    {
        if (stringValue) (*env)->DeleteLocalRef(env, stringValue);
        return NULL;
    }

    (*env)->SetBooleanField(env, result, g_varsSuccess, success ? JNI_TRUE : JNI_FALSE);
    if (stringValue)
    {
        (*env)->SetObjectField(env, result, g_varsStringValue, stringValue);
        (*env)->DeleteLocalRef(env, stringValue);
    }
    (*env)->SetIntField(env, result, g_varsIntValue, intValue);
    return result;
}

JNIEXPORT jint JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetMaxVarsCount(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return 0; /* 5.9.5 has no fixed variable table; unused by Questopia */
}

JNIEXPORT jobject JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetVarNameByIndex(JNIEnv *env, jobject thiz, jint index)
{
    (void)env; (void)thiz; (void)index;
    return NULL; /* the old wrapper was a stub too */
}

JNIEXPORT void JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPSetInputStrText(JNIEnv *env, jobject thiz, jstring val)
{
    QSP_CHAR *buffer = NULL;
    QSPString text;

    (void)thiz;
    text = adapterQspStringFromJava(env, val, &buffer);
    QSPSetInputStrText(text);
    free(buffer);
}

/* ------------------------------------------------------------------------- */
/* JNI: actions                                                              */
/* ------------------------------------------------------------------------- */

JNIEXPORT jint JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetActionsCount(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return (jint)QSPGetActions(NULL, 0);
}

JNIEXPORT jobject JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetActionData(JNIEnv *env, jobject thiz, jint ind)
{
    QSPListItem items[QSP_MAXACTIONS];
    int count;
    jobject result;
    jstring name;
    jstring image;

    (void)thiz;
    if (!env || !g_actionClass) return NULL;

    count = QSPGetActions(items, QSP_MAXACTIONS);
    if (ind < 0 || ind >= count)
        return NULL;

    result = (*env)->AllocObject(env, g_actionClass);
    if (!result) return NULL;

    name = adapterNewString(env, items[ind].Name);
    image = adapterNewString(env, items[ind].Image);
    (*env)->SetObjectField(env, result, g_actionName, name);
    (*env)->SetObjectField(env, result, g_actionImage, image);
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, image);
    return result;
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPExecuteSelActionCode(JNIEnv *env, jobject thiz, jboolean isRefresh)
{
    (void)env; (void)thiz;
    return QSPExecuteSelActionCode(isRefresh == JNI_TRUE ? QSP_TRUE : QSP_FALSE) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPSetSelActionIndex(JNIEnv *env, jobject thiz,
                                                                         jint ind, jboolean isRefresh)
{
    (void)env; (void)thiz;
    return QSPSetSelActionIndex((int)ind, isRefresh == JNI_TRUE ? QSP_TRUE : QSP_FALSE) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetSelActionIndex(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return (jint)QSPGetSelActionIndex();
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPIsActionsChanged(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return adapterCheckChanged(&g_lastActionsHash, adapterHashActions()) ? JNI_TRUE : JNI_FALSE;
}

/* ------------------------------------------------------------------------- */
/* JNI: objects                                                              */
/* ------------------------------------------------------------------------- */

JNIEXPORT jint JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetObjectsCount(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return (jint)QSPGetObjects(NULL, 0);
}

JNIEXPORT jobject JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetObjectData(JNIEnv *env, jobject thiz, jint ind)
{
    QSPObjectItem item;
    jobject result;
    jstring name;
    jstring image;

    (void)thiz;
    if (!env || !g_objectClass) return NULL;
    if (!qspGetObjectInfoByIndex((int)ind, &item)) return NULL;

    result = (*env)->AllocObject(env, g_objectClass);
    if (!result) return NULL;

    name = adapterNewString(env, item.Title);
    image = adapterNewString(env, item.Image);
    (*env)->SetObjectField(env, result, g_objectName, name);
    (*env)->SetObjectField(env, result, g_objectImage, image);
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, image);
    return result;
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPSetSelObjectIndex(JNIEnv *env, jobject thiz,
                                                                         jint ind, jboolean isRefresh)
{
    (void)env; (void)thiz;
    return QSPSetSelObjectIndex((int)ind, isRefresh == JNI_TRUE ? QSP_TRUE : QSP_FALSE) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetSelObjectIndex(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return (jint)QSPGetSelObjectIndex();
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPIsObjectsChanged(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    return adapterCheckChanged(&g_lastObjectsHash, adapterHashObjects()) ? JNI_TRUE : JNI_FALSE;
}

/* ------------------------------------------------------------------------- */
/* JNI: code execution                                                       */
/* ------------------------------------------------------------------------- */

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPExecString(JNIEnv *env, jobject thiz,
                                                                  jstring s, jboolean isRefresh)
{
    QSP_CHAR *buffer = NULL;
    QSPString code;
    QSP_BOOL result;

    (void)thiz;
    code = adapterQspStringFromJava(env, s, &buffer);
    result = QSPExecString(code, isRefresh == JNI_TRUE ? QSP_TRUE : QSP_FALSE);
    free(buffer);
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPExecLocationCode(JNIEnv *env, jobject thiz,
                                                                        jstring name, jboolean isRefresh)
{
    QSP_CHAR *buffer = NULL;
    QSPString location;
    QSP_BOOL result;

    (void)thiz;
    location = adapterQspStringFromJava(env, name, &buffer);
    result = QSPExecLocationCode(location, isRefresh == JNI_TRUE ? QSP_TRUE : QSP_FALSE);
    free(buffer);
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPExecCounter(JNIEnv *env, jobject thiz, jboolean isRefresh)
{
    (void)env; (void)thiz;
    return QSPExecCounter(isRefresh == JNI_TRUE ? QSP_TRUE : QSP_FALSE) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPExecUserInput(JNIEnv *env, jobject thiz, jboolean isRefresh)
{
    (void)env; (void)thiz;
    return QSPExecUserInput(isRefresh == JNI_TRUE ? QSP_TRUE : QSP_FALSE) ? JNI_TRUE : JNI_FALSE;
}

/* ------------------------------------------------------------------------- */
/* JNI: errors                                                               */
/* ------------------------------------------------------------------------- */

JNIEXPORT jobject JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetLastErrorData(JNIEnv *env, jobject thiz)
{
    QSPErrorInfo info;
    jobject result;
    jstring location;

    (void)thiz;
    if (!env || !g_errorClass) return NULL;

    info = QSPGetLastErrorData();
    result = (*env)->AllocObject(env, g_errorClass);
    if (!result) return NULL;

    location = adapterNewString(env, info.LocName);
    (*env)->SetObjectField(env, result, g_errorLocName, location);
    (*env)->DeleteLocalRef(env, location);
    (*env)->SetIntField(env, result, g_errorErrorNum, (jint)info.ErrorNum);
    (*env)->SetIntField(env, result, g_errorIndex, (jint)info.ActIndex);
    (*env)->SetIntField(env, result, g_errorLine, (jint)info.IntLineNum);
    return result;
}

JNIEXPORT jstring JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPGetErrorDesc(JNIEnv *env, jobject thiz, jint errorNum)
{
    (void)thiz;
    return adapterStringFromQsp(env, QSPGetErrorDesc((int)errorNum));
}

/* ------------------------------------------------------------------------- */
/* JNI: game management                                                      */
/* ------------------------------------------------------------------------- */

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPLoadGameWorldFromData(JNIEnv *env, jobject thiz,
                                                                             jbyteArray data, jint dataSize,
                                                                             jstring fileName)
{
    jbyte *buffer;
    QSP_BOOL result;
    int pathLength = 0;

    (void)thiz;
    if (!env || !data || dataSize < 0) return JNI_FALSE;

    buffer = (jbyte *)malloc((size_t)dataSize);
    if (!buffer) return JNI_FALSE;
    (*env)->GetByteArrayRegion(env, data, 0, dataSize, buffer);

    /* Remember the game location for INCLUDE / OPENQST requests. 5.7 used
     * the third argument as a file name; 5.9.5 uses it as "isNewGame". */
    if (g_gamePath) { free(g_gamePath); g_gamePath = NULL; }
    if (g_baseDir) { free(g_baseDir); g_baseDir = NULL; }

    if (fileName)
    {
        g_gamePath = adapterCopyJavaString(env, fileName, &pathLength);
        if (g_gamePath) g_baseDir = adapterDirName(g_gamePath);
    }

    /* The engine call is made from Java's QSP thread; the world is replaced
     * (this is the equivalent of the old path-aware load). */
    result = QSPLoadGameWorldFromData(buffer, (int)dataSize, QSP_TRUE) ? JNI_TRUE : JNI_FALSE;

    /* Match 5.7: tell Java which directory the quest was loaded from. This
     * also keeps image/audio lookup inside the actual game folder. */
    if (result && g_baseDir && g_baseDir[0])
        adapterCallChangeQuestPath(g_baseDir);

    free(buffer);
    adapterResetChangedState();
    return result;
}

JNIEXPORT jbyteArray JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPSaveGameAsData(JNIEnv *env, jobject thiz,
                                                                      jboolean isRefresh)
{
    /* ONGSAVE runs inside every QSPSaveGameAsData() call, so a no-buffer
     * "measure" pass would execute game code twice and could duplicate
     * messages/sounds/counters. Start with a generous caller-owned buffer and
     * only retry for truly huge saves. */
    int size = 8 * 1024 * 1024;
    int attempt;
    void *buffer;
    jbyteArray result;

    (void)thiz;

    for (attempt = 0; attempt < 4; ++attempt)
    {
        int written = size;
        buffer = malloc((size_t)size);
        if (!buffer) return NULL;

        if (QSPSaveGameAsData(buffer, &written, isRefresh == JNI_TRUE ? QSP_TRUE : QSP_FALSE))
        {
            result = (*env)->NewByteArray(env, (jsize)written);
            if (result)
                (*env)->SetByteArrayRegion(env, result, 0, (jsize)written, (jbyte *)buffer);
            free(buffer);
            return result;
        }

        free(buffer);
        if (written <= size)
            return NULL; /* not a buffer-size problem */
        size = written;
    }

    return NULL;
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPOpenSavedGameFromData(JNIEnv *env, jobject thiz,
                                                                              jbyteArray data, jint dataSize,
                                                                              jboolean isRefresh)
{
    jbyte *buffer;
    QSP_BOOL result;

    (void)thiz;
    if (!env || !data || dataSize < 0) return JNI_FALSE;

    buffer = (jbyte *)malloc((size_t)dataSize);
    if (!buffer) return JNI_FALSE;
    (*env)->GetByteArrayRegion(env, data, 0, dataSize, buffer);

    /* The loaded UI state must always be re-read, even if it happens to
     * match the previous game's hash. */
    adapterResetChangedState();
    result = QSPOpenSavedGameFromData(buffer, (int)dataSize,
                                      isRefresh == JNI_TRUE ? QSP_TRUE : QSP_FALSE) ? JNI_TRUE : JNI_FALSE;

    free(buffer);
    return result;
}

JNIEXPORT jboolean JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPRestartGame(JNIEnv *env, jobject thiz, jboolean isRefresh)
{
    QSP_BOOL result;

    (void)env; (void)thiz;
    adapterResetChangedState();
    result = QSPRestartGame(isRefresh == JNI_TRUE ? QSP_TRUE : QSP_FALSE);
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_questwave_player_model_libQP_NativeMethods_QSPSelectMenuItem(JNIEnv *env, jobject thiz, jint index)
{
    (void)env; (void)thiz;
    g_pendingMenuIndex = (int)index;
}

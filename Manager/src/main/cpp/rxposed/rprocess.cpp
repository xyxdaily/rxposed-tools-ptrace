//
// Created by Intel on 2022/4/5.
//

#include <sys/wait.h>
#include "jni.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include "rprocess.h"
#include "android/log.h"
#include "android_shm.h"
#include <sys/mount.h>
#include <mntent.h>


rprocess * rprocess::instance_ =nullptr;

void rprocess::setProcessInfo(char* nice_name, uid_t uid, gid_t arg_gid) {
    currentUid = uid;
    processName = strdup(nice_name);
    this->gid = arg_gid;
}

string rprocess::getCurrentAppRxposedConfig(JNIEnv* env, string rxposed_providerName , string callName, string method , uid_t currentUid) {

    DEBUG();
    jstring key = env->NewStringUTF("ModuleList");
    DEBUG();
    string uid_str = std::to_string(currentUid);
    DEBUG();
    jobject obj_bundle = getConfigByProvider(env, rxposed_providerName, callName  , method, uid_str);
    DEBUG();
    jclass Bundle_cls = env->FindClass("android/os/Bundle");
    jmethodID Bundle_getStringArrayList_method = env->GetMethodID(Bundle_cls, "getStringArrayList","(Ljava/lang/String;)Ljava/util/ArrayList;");
    jclass ArrayList_cls = env->FindClass("java/util/ArrayList");
    jmethodID ArrayList_size_method = env->GetMethodID(ArrayList_cls, "size","()I");
    jmethodID ArrayList_get_method = env->GetMethodID(ArrayList_cls, "get","(I)Ljava/lang/Object;");
    jobject config = env->CallObjectMethod(obj_bundle, Bundle_getStringArrayList_method,key);
    if(config == nullptr){
        return "";
    }
//    string bask = "base.apk";
    jint size = env->CallIntMethod(config,ArrayList_size_method);
    string appinfoList;
    for(int i=0;i<size;i++){
        jstring element = static_cast<jstring>(env->CallObjectMethod(config, ArrayList_get_method,i));
        string appinfo = env->GetStringUTFChars(element,0);
        appinfoList = appinfoList +"|"+appinfo;
    }
    return appinfoList;
}


bool rprocess::InitModuleInfo(JNIEnv *env) {

    char* buf;
    U64 ufd = 0;
    int ret = create_shared_memory("rxposed",4096,-1,buf,ufd);
    if(ret==-1){
        return false;
    }
    pid_t pid = fork();

    if (pid == 0) {
        // 子进程读取数据
        std::string Provider_call_method = "getConfig";
        std::string appinfoList = getCurrentAppRxposedConfig(env, m_rxposed_providerName, processName  , Provider_call_method, currentUid);
        memcpy(buf,appinfoList.c_str(),appinfoList.length());
        _exit(0);
    }else if (pid > 0) {
        // 等待子进程结束
        wait(NULL);
        LOGE("shared_memory appinfoList :%s len:%zu",buf, strlen(buf));
        vector<string> vectorApp = string_split(buf,"|");
        string base = "base.apk";

        for(int i=0;i<vectorApp.size();i++){
            string appinfo = vectorApp[i];
            if(appinfo.empty()){
                continue;
            }
            vector<string> info = string_split(appinfo,":");
            string pkgName = "";

            string source(info[0]);
            string Entry_class = info[1];
            string Entry_method = info[2];
            string hide = info[3];
            string argument = info[4];
            size_t startPost = source.find(base);
            string NativelibPath = info[0].replace(startPost,base.length(),APK_NATIVE_LIB);
            LOGE("source:%s",source.c_str());
            LOGE("NativelibPath:%s",NativelibPath.c_str());
            AppinfoNative* appinfoNative = new AppinfoNative(pkgName,source,NativelibPath,Entry_class,Entry_method,hide,argument);
            AppinfoNative_vec.push_back(appinfoNative);
        }
    }
    close_shared_memory(ufd,buf);

    return true;
}

void rprocess::setAuthorityInfo(const char* arg_tmp){

    vector<string> arg = string_split(arg_tmp, ":");
    AUTHORITY = arg_tmp;
    hostUid = atoi(arg[0].c_str());
    LOGE("UID: %d",hostUid);
    m_rxposed_pkgName=arg[1];
    LOGE("m_rxposed_pkgName: %s", m_rxposed_pkgName.c_str());
    m_rxposed_providerName =arg[2];
    LOGE("m_rxposed_providerName: %s", m_rxposed_providerName.c_str());
}

bool rprocess::LoadModule(JNIEnv* env){
    DEBUG();
    for (auto appinfoNative : AppinfoNative_vec)
    {
        load_apk_And_Call_Class_Entry_Method(env, RxposedContext,
                                             appinfoNative->source,appinfoNative->NativelibPath,
                                             appinfoNative->Entry_class,
                                             appinfoNative->Entry_method,
                                             appinfoNative->hide,
                                             appinfoNative->argument
        );
        delete appinfoNative;
    }

    return true;
}


bool rprocess::is_isIsolatedProcess() {
    return (currentUid >= 99000 && currentUid <= 99999)|| (currentUid >= 90000 && currentUid <= 98999);
}

bool rprocess::is_HostProcess() {
    if( hostUid == currentUid){    // 管理进程(hostUid)
        return true;
    }
    return false;

}


bool rprocess::InitEnable(JNIEnv *pEnv) {
    DEBUG()
    LOGE("hostUid = %d currentUid = %d packaName = %s gid =%d",hostUid,currentUid,processName.c_str(),gid);

    if(is_isIsolatedProcess()) {   //也不能是is_isIsolatedProcess，目前不支持
        return false;
    }
//    hide_maps();
    return InitModuleInfo(pEnv);
    DEBUG()
}

const char* rprocess::getStatusAuthority() {
    return AUTHORITY.c_str();
}
bool rprocess::is_Start(JNIEnv* env, char * name) {
    DEBUG();
    RxposedContext = CreateApplicationContext(env, processName,currentUid);
    if(RxposedContext != nullptr){
        return true;
    }
    return false;

}

uint rprocess::getHostUid() {
    return hostUid;
}

void rprocess::clearAppinfoNative() {
    for (auto appinfoNative : AppinfoNative_vec)
    {
        delete appinfoNative;
    }
    AppinfoNative_vec.clear();
}

void rprocess::add_Rxposed_Status() {
    // 设置rxposed环境变量
    if (setenv("RXPOSED_ACTIVITY", "1", 1) == 0) {
        DEBUG("RXPOSED_ACTIVITY set to %s\n", getenv("RXPOSED_ACTIVITY"));
    } else {
        DEBUG("setenv failed");
    }
}

bool rprocess::is_Enable() {
    if(AppinfoNative_vec.size() >0){
        return true;
    }
    return false;
}


struct mntent *user_getmntent(FILE *fp, struct mntent *e, char *buf, int buf_len)
{
    memset(e, 0, sizeof(*e));
    while (fgets(buf, buf_len, fp) != nullptr)
    {
        // Entries look like "proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0".
        // That is: mnt_fsname mnt_dir mnt_type mnt_opts 0 0.
        int fsname0, fsname1, dir0, dir1, type0, type1, opts0, opts1;
        if (sscanf(buf, " %n%*s%n %n%*s%n %n%*s%n %n%*s%n %d %d",
                   &fsname0, &fsname1, &dir0, &dir1, &type0, &type1, &opts0, &opts1,
                   &e->mnt_freq, &e->mnt_passno) == 2)
        {
            e->mnt_fsname = &buf[fsname0];
            buf[fsname1] = '\0';
            e->mnt_dir = &buf[dir0];
            buf[dir1] = '\0';
            e->mnt_type = &buf[type0];
            buf[type1] = '\0';
            e->mnt_opts = &buf[opts0];
            buf[opts1] = '\0';
            return e;
        }
    }
    return nullptr;
}


bool rprocess::hide_maps() {

    char path[PATH_MAX];
    FILE *fp;
    char* cert_path = "/system/etc/security/cacerts";

    sprintf(path, "/proc/self/mounts");
    fp = fopen(path, "r");
    if (fp) {
        char buf[4096];
        mntent mentry{};
        while ((user_getmntent(fp,&mentry, buf, sizeof(buf))) != NULL) {
            {
                LOGE("Mounted on: %s\n", mentry.mnt_dir);
                if(strncmp(cert_path,mentry.mnt_dir, strlen(cert_path)) == 0){
                    LOGE("Filesystem: %s\n", mentry.mnt_fsname);
                    LOGE("Type: %s\n", mentry.mnt_type);
                    LOGE("Options: %s\n", mentry.mnt_opts);
                    LOGE("Dump frequency: %d\n", mentry.mnt_freq);
                    LOGE("Pass number: %d\n\n", mentry.mnt_passno);

                    if (umount2(mentry.mnt_dir, MNT_DETACH) != -1)
                        LOGD("hide: Unmounted (%s)\n", mentry.mnt_dir);
                }

            }
        }
    }


    return true;
}




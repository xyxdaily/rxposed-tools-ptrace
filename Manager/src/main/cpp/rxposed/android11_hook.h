//
// Created by thehepta on 2024/2/25.
//

#pragma once

namespace android11 {
    void zygote_nativeSpecializeAppProcess_hook();
    bool art_method_hook_init();
    jobject getConfigByProvider(JNIEnv* env, string providerHost_providerName , string callName, string method , string uid_str);

}
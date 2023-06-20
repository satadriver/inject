/*
 * hello.c
 *
 *  Created on: 2015年6月26日
 *      Author: Administrator
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <android/log.h>
#include <elf.h>
#include <fcntl.h>

#define LOG_TAG "[liujinguang]"
#define LOGD(fmt, args...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##args)

int hook_entry(char * param){
    LOGD("Hook 64 success, pid = %d\n", getpid());
    LOGD("Hello %s\n", param);
    return 0;
}

#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <zend_exceptions.h>
#include "string_hash.h"

static void saveLog(int level, const char *logDir, const char *file, int line, const char *fmt, ... TSRMLS_DC);

#ifndef LOG_ERROR
#define LOG_ERROR if(PULSEFLOW_G(log_enable)){ \
    saveLog(4,PULSEFLOW_G(log_dir),__FILE__,__LINE__,"%s : %s",__FUNCTION__ , strerror(errno) TSRMLS_CC); \
    }
#endif

ZEND_DECLARE_MODULE_GLOBALS(PulseFlow)

// init class name hash lists
static zend_always_inline void Init_Class_Disable_Hash_List(TSRMLS_D) {

    unsigned long strhash = 0;
    int i = 0;

    if (strlen(PULSEFLOW_G(disable_trace_class))) {

        char *pSave = NULL;
        char *blockClass = strtok_r(PULSEFLOW_G(disable_trace_class), ",", &pSave); // strtok_r thread safe

        while (blockClass != NULL) {

            strhash = BKDRHash(blockClass, strlen(blockClass));

            if (i < CLASS_DISABLED_HASH_LIST_SIZE) {

                PULSEFLOW_G(classDisableHashList)[i] = strhash;

            } else {

                break;

            }

            i++;

            blockClass = strtok_r(NULL, ",", &pSave);

        }
    }

    PULSEFLOW_G(classDisableHashListSize) = i;

}


static zend_always_inline void Init_Func_Disable_Hash_List(TSRMLS_D) {

    unsigned long strhash = 0;
    int i = 0;

    if (strlen(PULSEFLOW_G(disable_trace_functions))) {

        char *pSave = NULL;
        char *blockfunc = strtok_r(PULSEFLOW_G(disable_trace_functions), ",", &pSave);

        while (blockfunc != NULL) {

            strhash = BKDRHash(blockfunc, strlen(blockfunc));

            if (i < FUNC_DISABLED_HASH_LIST_SIZE) {  //此处需要修改为宏定义

                PULSEFLOW_G(FuncDisableHashList)[i] = strhash;

            } else {
                break;
            }

            i++;

            blockfunc = strtok_r(NULL, ",", &pSave);

        }
    }

    PULSEFLOW_G(FuncDisableHashListSize) = i;

}


static zend_always_inline int
Exist_In_Hash_List(unsigned long strhash, unsigned long *hashList, int hashListLen TSRMLS_DC) {  //1:存在 0：不存在

    int i = 0;

    for (i = 0; i < hashListLen; i++) {

        if (hashList[i] == strhash) {
            return 1;
        }

    }

    return 0;

}

static zend_always_inline void
Simple_Trace_Performance_Begin(struct timeval *CpuTimeStart, size_t *useMemoryStart, unsigned int funcArrayPointer
                               TSRMLS_DC) {

    gettimeofday(CpuTimeStart, 0 TSRMLS_CC);

    PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].refcount++;

    *useMemoryStart = zend_memory_usage(0 TSRMLS_CC);

    //这个判断条件很重要，否则会造成函数间循环调用BUG 初始化函数超时值
    //  if (PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].cpuTimeUse == 0) {
    // PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].cpuTimeUse +=
    //         PULSEFLOW_G(exec_process_err_flag) * 1000;
    // }

}

static zend_always_inline void
Simple_Trace_Performance_End(struct timeval *CpuTimeStart, size_t *useMemoryStart, unsigned int funcArrayPointer
                             TSRMLS_DC) {

    /*
     * The overall memory consumption will have a negative value, which is correct,
     * if the function does the memory release work,
     * but in order to adapt to the company's business,
     * the memory consumption of the memory-released function is set to zero.
     */

    long currentFuncMemory = zend_memory_usage(0 TSRMLS_CC) - (*useMemoryStart);

    long currentFuncTotalMemory =
            (long) PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].memoryUse + currentFuncMemory;

    if (currentFuncTotalMemory > 0) {

        PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].memoryUse = currentFuncTotalMemory;

    } else {

        PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].memoryUse = 0;

    }

    struct timeval endTime;

    gettimeofday(&endTime, 0);

    //CPU time : ms
    //  if (PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].cpuTimeUse ==
    // PULSEFLOW_G(exec_process_err_flag) * 1000) {
    //  PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].cpuTimeUse -=
    //         PULSEFLOW_G(exec_process_err_flag) * 1000;
    //  }

    PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].cpuTimeUse +=
            (((endTime).tv_sec - (*CpuTimeStart).tv_sec) * 1000.0f +
             ((endTime).tv_usec - (*CpuTimeStart).tv_usec) / 1000.0f);


}

static zend_always_inline int
getFuncArrayId(zend_string *funcName, zend_string *className, unsigned long funcNameHash, unsigned long classNameHash
               TSRMLS_DC) {
    int funcCurrentPointer = PULSEFLOW_G(Function_Prof_List_current_Size);

    int funcArrayId = -1;

    int i;
    for (i = 0; i < funcCurrentPointer; ++i) {

        if ((PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].funcNameHash == funcNameHash) &&
            (PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].classNameHash == classNameHash)) {

            funcArrayId = i;

            return funcArrayId;

        }

    }

    if ((funcArrayId == -1) && (funcCurrentPointer < FUNCTION_PROF_LIST_SIZE)) { //空间未满 未找到Hash所在位置

        PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcCurrentPointer].funcNameHash = funcNameHash;

        strncpy(PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcCurrentPointer].functionName,
                funcName->val,
                FUNC_NAME_MAX_SIZE - 1);

        //add funcname zero flag
        int funNameLen = funcName->len;
        if (funNameLen >= FUNC_NAME_MAX_SIZE) {

            PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcCurrentPointer].functionName[FUNC_NAME_MAX_SIZE -
                                                                                            1] = '\0';
        } else {

            PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcCurrentPointer].functionName[funNameLen] = '\0';

        }

        if ((className != NULL) && classNameHash) {

            PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcCurrentPointer].classNameHash = classNameHash;

            strncpy(PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcCurrentPointer].className, className->val,
                    CLASS_NAME_MAX_SIZE - 1);

            int classNameLen = className->len;
            if (classNameLen >= CLASS_NAME_MAX_SIZE) {

                PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcCurrentPointer].className[CLASS_NAME_MAX_SIZE -
                                                                                             1] = '\0';
            } else {

                PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcCurrentPointer].className[classNameLen] = '\0';

            }

        } else {

            PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcCurrentPointer].classNameHash = 0;

            PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcCurrentPointer].className[0] = '\0';

        }


        PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcCurrentPointer].refcount = 0;
        funcArrayId = funcCurrentPointer;
        PULSEFLOW_G(Function_Prof_List_current_Size)++;

        return funcArrayId;
    }

    return funcArrayId;

}

// ret: ( -1 : function err ) ( 0 : msg send failure)
static zend_always_inline int SendDataToSVIPC(TSRMLS_D) {

    key_t server_queue_key, server_qid;

    int ret = -1;

    if ((server_queue_key = ftok(PULSEFLOW_G(svipc_name), PULSEFLOW_G(svipc_gj_id))) != -1) {

        if ((server_qid = msgget(server_queue_key, 0)) != -1) {

            PULSEFLOW_G(Func_Prof_Data).message_type = 1;

            int func_chunk_size = PULSEFLOW_G(func_chunk_size); //func_chunck size: 0 - disable func chunk check
            int current_func_list_count = PULSEFLOW_G(Function_Prof_List_current_Size);

            if (!func_chunk_size) {
                //不进行分块发送
                PULSEFLOW_G(Func_Prof_Data).size = current_func_list_count;
                unsigned int msgsize = sizeof(PULSEFLOW_G(Func_Prof_Data).size) +
                                       sizeof(PULSEFLOW_G(Func_Prof_Data).opts) +
                                       (sizeof(Function_Prof_Data) * current_func_list_count);

                ret = msgsnd(server_qid, &PULSEFLOW_G(Func_Prof_Data), msgsize, IPC_NOWAIT);

//                if (PULSEFLOW_G(is_web_display_trace_list)) {
//
//                    php_printf("\n<br /> PulseFlow <br /> \n ");
//
//                    int i;
//                    for (i = 0; i < current_func_list_count; ++i) {
//
//                        php_printf("[PID: %d ][ %d ]: [ %s => %s ] [ %u 次] [ %u BYTE ] [ %.1f MS] <br />\n", getpid(), i,
//                                   PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].className,
//                                   PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].functionName,
//                                   PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].refcount,
//                                   PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].memoryUse,
//                                   PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].cpuTimeUse);
//                    }
//
//                }

                if (ret == -1) {

                    ret = 0; //发送失败

                    LOG_ERROR

                }

            } else if (func_chunk_size > 0) {
                //进行分块发送
                unsigned int divListSize, modListSize;

                divListSize = current_func_list_count / func_chunk_size;

                modListSize = current_func_list_count % func_chunk_size;

                //  php_printf("%d = %d * %d + %d", current_func_list_count, func_chunk_size, divListSize, modListSize);

                int i;
                for (i = 0; i < divListSize; i++) {

                    if (i > 0) {
                        //不是首元素 进行内存拷贝
                        memcpy(&PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[0],
                               &PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i * func_chunk_size],
                               sizeof(Function_Prof_Data) * func_chunk_size);
                    }

                    PULSEFLOW_G(Func_Prof_Data).size = func_chunk_size;
                    unsigned int msgsize = sizeof(PULSEFLOW_G(Func_Prof_Data).size) +
                                           sizeof(PULSEFLOW_G(Func_Prof_Data).opts) +
                                           (sizeof(Function_Prof_Data) * func_chunk_size);

                    ret = msgsnd(server_qid, &PULSEFLOW_G(Func_Prof_Data), msgsize, IPC_NOWAIT);

//                    if (PULSEFLOW_G(is_web_display_trace_list)) {
//
//                        php_printf("\n<br /> PulseFlow <br /> \n ");
//
//                        int i;
//                        for (i = 0; i < current_func_list_count; ++i) {
//
//                            php_printf("[PID: %d ][ %d ]: [ %s => %s ] [ %u 次] [ %u BYTE ] [ %.1f MS] <br />\n", getpid(), i,
//                                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].className,
//                                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].functionName,
//                                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].refcount,
//                                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].memoryUse,
//                                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].cpuTimeUse);
//                        }
//
//                    }

                    if (ret == -1) {

                        ret = 0; //发送失败

                        LOG_ERROR

                    }
                }

                if (modListSize > 0) {
                    //拷贝剩余元素的内存
                    if (divListSize > 0) {
                        memcpy(&PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[0],
                               &PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i * func_chunk_size],
                               sizeof(Function_Prof_Data) * modListSize);
                    }

                    PULSEFLOW_G(Func_Prof_Data).size = modListSize;
                    unsigned int msgsize = sizeof(PULSEFLOW_G(Func_Prof_Data).size) +
                                           sizeof(PULSEFLOW_G(Func_Prof_Data).opts) +
                                           (sizeof(Function_Prof_Data) * modListSize);

                    ret = msgsnd(server_qid, &PULSEFLOW_G(Func_Prof_Data), msgsize, IPC_NOWAIT);

//                    if (PULSEFLOW_G(is_web_display_trace_list)) {
//
//                        php_printf("\n<br /> PulseFlow <br /> \n ");
//
//                        int i;
//                        for (i = 0; i < current_func_list_count; ++i) {
//
//                            php_printf("[PID: %d ][ %d ]: [ %s => %s ] [ %u 次] [ %u BYTE ] [ %.1f MS] <br />\n", getpid(), i,
//                                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].className,
//                                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].functionName,
//                                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].refcount,
//                                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].memoryUse,
//                                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].cpuTimeUse);
//                        }
//
//                    }

                    if (ret == -1) {

                        ret = 0; //发送失败

                        LOG_ERROR

                    }

                }


            }

        } else {

            LOG_ERROR

        }

    } else {

        LOG_ERROR

    }

    return ret;

}

static zend_always_inline int checkUrlIsEnable(TSRMLS_D) {

    int ret = -1;

    //根据URL 参数判断是否需要进行监控
    char *uri_query_string = SG(request_info).query_string;

    if (!((uri_query_string == NULL) || (uri_query_string != NULL && uri_query_string[0] == '\0'))) {

        if (strstr(uri_query_string, URI_REQUEST_ENABLED_PARM_ON)) {  //如果找到请求监控开启参数

            ret = 1;

        }

        if (strstr(uri_query_string, URI_REQUEST_ENABLED_PARM_OFF)) {  //如果找到请求监控开启参数

            ret = 0;

        }

    }

    return ret;
}


static zend_always_inline int checkUrlHaveGetParm(const char *parm TSRMLS_DC) {

    int ret = 0;

    //根据URL 参数判断是否需要进行监控
    char *uri_query_string = SG(request_info).query_string;

    if (!((uri_query_string == NULL) || (uri_query_string != NULL && uri_query_string[0] == '\0'))) {

        if (strstr(uri_query_string, parm)) {  //如果找到指定参数

            ret = 1;

        }

    }

    return ret;
}

static zend_always_inline int getRequestRandom(long sampling_rate, int isUrlEnabled TSRMLS_DC) {

    int ret = -1;

    if (isUrlEnabled == 1 || sampling_rate == 1) {

        return REQUEST_SAMPLING_RATE_FLAG;
    }

    if (PULSEFLOW_G(sampling_rate) > 1) {

        //产生随机数
        srand((unsigned) time(NULL));

        //产生范围随机数[1,max]
        int min = 1, max = PULSEFLOW_G(sampling_rate), req_rand = rand();

        int range = max - min + 1; //保障余数个数范围

        return (req_rand - (req_rand / range) * range) + min;  //(0,b]

    }

    return ret;

}
//check uri request has pulseflowswitch
//
//    zend_llist_position pos;
//    sapi_header_struct* h;
//    h = zend_llist_get_first_ex(&SG(sapi_headers).headers, &pos);
//    for (; h;h= (sapi_header_struct*)zend_llist_get_next_ex(&SG(sapi_headers).headers, &pos))
//    {
//              php_printf("SAPI! %d, %s <br/>", h->header_len, h->header);
//    }
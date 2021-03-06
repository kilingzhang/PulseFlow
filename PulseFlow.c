/*
 +----------------------------------------------------------------------+
 | PHP Version 7                                                        |
 +----------------------------------------------------------------------+
 | Copyright (c) 1997-2018 The PHP Group                                |
 +----------------------------------------------------------------------+
 | This source file is subject to version 3.01 of the PHP license,      |
 | that is bundled with this package in the file LICENSE, and is        |
 | available through the world-wide-web at the following url:           |
 | http://www.php.net/license/3_01.txt                                  |
 | If you did not receive a copy of the PHP license and are unable to   |
 | obtain it through the world-wide-web, please send a note to          |
 | license@php.net so we can mail you a copy immediately.               |
 +----------------------------------------------------------------------+
 | Author:  corerman   2018.10.12.add_alone_function_monitor            |
 +----------------------------------------------------------------------+
 */
/* $Id$ */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <zend_compile.h>
#include <SAPI.h>
#include "php.h"
#include "SAPI.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_PulseFlow.h"
#include "tracing.h"
#include "loger.h"

ZEND_DECLARE_MODULE_GLOBALS(PulseFlow)

PHP_INI_BEGIN()
                STD_PHP_INI_ENTRY
                ("PulseFlow.enabled", "0", PHP_INI_ALL, OnUpdateBool, enabled,
                 zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.disable_trace_functions", "", PHP_INI_ALL, OnUpdateString, disable_trace_functions,
                 zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.disable_trace_class", "", PHP_INI_ALL, OnUpdateString, disable_trace_class,
                 zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.svipc_name", "/PulseFlow_sv_ipc", PHP_INI_ALL, OnUpdateString, svipc_name,
                 zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.svipc_pj_id", "1000", PHP_INI_ALL, OnUpdateLong, svipc_gj_id,
                 zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.max_package_size", "0", PHP_INI_ALL, OnUpdateLong, max_package_size,
                 zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.log_dir", "", PHP_INI_ALL, OnUpdateString, log_dir,
                 zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.sampling_rate", "100", PHP_INI_ALL, OnUpdateLong, sampling_rate,
                 zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.exec_process_err_flag", "90", PHP_INI_ALL, OnUpdateLong, exec_process_err_flag,
                 zend_PulseFlow_globals, PulseFlow_globals)

PHP_INI_END()

static void (*_zend_execute_ex)(zend_execute_data *execute_data);

//static void (*_zend_execute_internal)(zend_execute_data *execute_data, zval *return_value);

//ZEND_DLEXPORT void PulseFlow_xhprof_execute_internal(zend_execute_data *execute_data, zval *return_value);

ZEND_DLEXPORT void PulseFlow_xhprof_execute_ex(zend_execute_data *execute_data);

PHP_MINIT_FUNCTION (PulseFlow) {
    REGISTER_INI_ENTRIES();

    Init_Class_Disable_Hash_List(TSRMLS_C);
    Init_Func_Disable_Hash_List(TSRMLS_C);

    memset(&PULSEFLOW_G(Func_Prof_Data), 0, sizeof(SVIPC_Func_Prof_Message));

    /*分块发送参数初始化
     * 根据参数进行数据分块，因为修改消息队列大小 对于不同系统可能需要重启操作，所以可以进行模拟分块发送
     *如果消息队列分块大小值 > SVIPC_Func_Prof_Message 体积大小，则参数失效，设置为0 (因为已经超过插件允许内存的最大值)
     */
    if (PULSEFLOW_G(max_package_size) > sizeof(SVIPC_Func_Prof_Message)) {

        PULSEFLOW_G(max_package_size) = 0;

    }

    //获取还有多少空间可以分配函数状态信息元
    int funcBlockSize = PULSEFLOW_G(max_package_size) -
                        (sizeof(PULSEFLOW_G(Func_Prof_Data)) - sizeof(PULSEFLOW_G(Func_Prof_Data).Function_Prof_List));

    if (funcBlockSize > 0) {

        //设置可用的函数分块大小
        PULSEFLOW_G(func_chunk_size) = funcBlockSize / sizeof(Function_Prof_Data);

    } else {

        PULSEFLOW_G(func_chunk_size) = 0; //没有可用空间进行函数分块 设置为0 禁止分块

    }

    //handle zend execute flow
    _zend_execute_ex = zend_execute_ex;
    zend_execute_ex = PulseFlow_xhprof_execute_ex;


    //check is open log switch
    PULSEFLOW_G(log_enable) = 0;

    if (strlen(PULSEFLOW_G(log_dir))) {
        PULSEFLOW_G(log_enable) = 1;
    }

    return SUCCESS;
}

//每一个执行和一个return对应，这种是为了避免可能存在的不明判断条件：万事不能错失PHP代码执行
ZEND_DLEXPORT void PulseFlow_xhprof_execute_ex(zend_execute_data *execute_data) {

    zend_execute_data *real_execute_data = execute_data;

    if (!PULSEFLOW_G(enabled) || !(PULSEFLOW_G(request_sampling_rate) == REQUEST_SAMPLING_RATE_FLAG ||
                                   PULSEFLOW_G(request_sampling_rate) == PULSEFLOW_G(sampling_rate))) {

        _zend_execute_ex(execute_data);

        return;

    } else {
        unsigned long classNameHash = 0;
        unsigned long funcNameHash = 0;

        zend_string *className = NULL, *funcName = NULL;

        if (real_execute_data->func->common.scope != NULL) {

            className = real_execute_data->func->common.scope->name;
            classNameHash = BKDRHash(className->val, className->len TSRMLS_CC);

        }

        if (real_execute_data->func->common.function_name) {

            funcName = real_execute_data->func->common.function_name;
            funcNameHash = BKDRHash(funcName->val, funcName->len TSRMLS_CC);

        }

        // if (funcName == NULL || className == NULL || classNameHash == 0 || funcNameHash == 0) {
        if (funcName == NULL || funcNameHash == 0) {

            _zend_execute_ex(execute_data TSRMLS_CC);

            return;

        } else if (Exist_In_Hash_List(funcNameHash, PULSEFLOW_G(FuncDisableHashList),
                                      PULSEFLOW_G(FuncDisableHashListSize) TSRMLS_CC)) {

            _zend_execute_ex(execute_data TSRMLS_CC);

            return;

        } else if ((className != NULL) && classNameHash &&
                   Exist_In_Hash_List(classNameHash, PULSEFLOW_G(classDisableHashList),
                                      PULSEFLOW_G(classDisableHashListSize) TSRMLS_CC)) {

            _zend_execute_ex(execute_data TSRMLS_CC);

            return;

        } else {

            // int currentFuncSize = PULSEFLOW_G(Function_Prof_List_current_Size); //current monitor functions count

            //   int func_chunk_size = PULSEFLOW_G(func_chunk_size); //func_chunck size: 0 - disable func chunk check

            // (如果模拟分块大小大于0(开启分块)，并且当前函数总量大于分块大小) || (当前函数监控总量已经大于扩展存储空间上限)
//            if ((func_chunk_size && (currentFuncSize >= func_chunk_size)) || (currentFuncSize >= FUNCTION_PROF_LIST_SIZE)) {
//
//                SendDataToSVIPC(TSRMLS_C);
//
//                //发送状态不进行监控 如果发送失败也丢弃
//                PULSEFLOW_G(Function_Prof_List_current_Size) = 0;
//
//            }

            int funcArrayPointer = getFuncArrayId(funcName, className, funcNameHash, classNameHash TSRMLS_CC);

            if (funcArrayPointer != -1) {

                struct timeval CpuTimeStart;
                size_t useMemoryStart;

                Simple_Trace_Performance_Begin(&CpuTimeStart, &useMemoryStart, funcArrayPointer TSRMLS_CC);

                _zend_execute_ex(execute_data TSRMLS_CC);

                Simple_Trace_Performance_End(&CpuTimeStart, &useMemoryStart, funcArrayPointer TSRMLS_CC);

//                if (PULSEFLOW_G(is_web_display_trace_list)) {
//
//                    php_printf("[ %d ] :[PID: %d ] [ %s => %s ] [ %u 次] [ %u BYTE ] [ %.1f MS] <br />\n",
//                               funcArrayPointer, getpid(),
//                               PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].className,
//                               PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].functionName,
//                               PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].refcount,
//                               PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].memoryUse,
//                               PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[funcArrayPointer].cpuTimeUse);
//
//                }
                return;
            }

        }

    }

    //Else State
    _zend_execute_ex(execute_data TSRMLS_CC);

    return;
}


PHP_MSHUTDOWN_FUNCTION (PulseFlow) {

    return SUCCESS;

}


PHP_RINIT_FUNCTION (PulseFlow) {

#if defined(COMPILE_DL_PULSEFLOW) && defined(ZTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif
    //针对每个php请求备份enabled变量值，因为用户可以在PHP脚本里调用函数进行开关控制 这种用户自定义行为会影响下次请求
    PULSEFLOW_G(enabled_request_bak) = PULSEFLOW_G(enabled);

    PULSEFLOW_G(Function_Prof_List_current_Size) = 0;

    PULSEFLOW_G(request_sampling_rate) = -1;

    PULSEFLOW_G(is_web_display_trace_list) = 0;

    PULSEFLOW_G(url_enable_flag) = checkUrlIsEnable(TSRMLS_C);

    //if php.ini enabled is true
    if (PULSEFLOW_G(enabled)) {

        //Check is Url Open?
        int isUrlEnabled = PULSEFLOW_G(url_enable_flag);

        if (isUrlEnabled == 0) {

            PULSEFLOW_G(enabled) = 0;

        }
    }

    if (PULSEFLOW_G(enabled)) {

        PULSEFLOW_G(request_sampling_rate) = getRequestRandom(PULSEFLOW_G(sampling_rate),
                                                              PULSEFLOW_G(url_enable_flag) TSRMLS_CC);

    }

    if (checkUrlHaveGetParm(WEB_PRINT_MONITOR_LIST_ON) == 1) {

        //开启页面打印
        PULSEFLOW_G(is_web_display_trace_list) = 1;

    }

    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION (PulseFlow) {

    if (PULSEFLOW_G(Function_Prof_List_current_Size) > 0) {

        SendDataToSVIPC(TSRMLS_C);

        memset(&PULSEFLOW_G(Func_Prof_Data), 0, sizeof(SVIPC_Func_Prof_Message));

    }

    PULSEFLOW_G(enabled) = PULSEFLOW_G(enabled_request_bak);
    return SUCCESS;

}

static zend_always_inline int PulseFlow_info_print(const char *str TSRMLS_DC) {

    return php_output_write(str, strlen(str));

}


PHP_MINFO_FUNCTION (PulseFlow) {

    php_info_print_table_start();

    if (PULSEFLOW_G(enabled)) {

        php_info_print_table_header(2, "PulseFlow support", "enabled");

    } else {

        php_info_print_table_header(2, "PulseFlow support", "disabled");

    }

    php_info_print_table_end();

    php_info_print_box_start(0);

    if (!sapi_module.phpinfo_as_text) {

        PulseFlow_info_print("<a href=\"https://github.com/gitsrc/PulseFlow\"><img border=0 src=\"" TSRMLS_CC);

        PulseFlow_info_print(PulseFLow_LOGO_URI "\" alt=\"PulseFlow logo\" /></a>\n" TSRMLS_CC);

    }

    PulseFlow_info_print("PulseFlow is a PHP Profiler, Monitoring and Trace PHP ." TSRMLS_CC);

    PulseFlow_info_print(!sapi_module.phpinfo_as_text ? "<br /><br />" : "\n\n" TSRMLS_CC);

    PulseFlow_info_print(
            "The 'PulseFlow' extension optimized fork of the XHProf extension from tideways_xhprof and Facebook as open-source. <br /><br />&nbsp;&nbsp;(c) Tideways GmbH 2014-2017 <br /> &nbsp;&nbsp;(c) Facebook 2009"
            TSRMLS_CC);

    if (!sapi_module.phpinfo_as_text) {
        PulseFlow_info_print(
                "<br /><br /><strong>Source Code : https://github.com/gitsrc/PulseFlow  </strong>" TSRMLS_CC);

    }

    php_info_print_box_end();

}

PHP_FUNCTION (pulseflow_enable) {

    PULSEFLOW_G(enabled) = 1;

    int isUrlEnabled = PULSEFLOW_G(url_enable_flag);

    PULSEFLOW_G(request_sampling_rate) = getRequestRandom(PULSEFLOW_G(sampling_rate), isUrlEnabled TSRMLS_CC);


}

PHP_FUNCTION (pulseflow_disable) {

    PULSEFLOW_G(enabled) = 0;

}

PHP_FUNCTION (pulseflow_set_options) {

    zval *val;

    ZEND_PARSE_PARAMETERS_START(1, 1);  //解析参数
            Z_PARAM_ARRAY(val);
    ZEND_PARSE_PARAMETERS_END();

    zend_string *key;
    zval *key_val;
    HashTable *arr_hash;

    arr_hash = Z_ARRVAL_P(val); //把zval数据结构转换为 hashtable

    int currentPointer = 0;

    const int OPTS_STR_MEM_SIZE = sizeof(PULSEFLOW_G(Func_Prof_Data).opts); //获取opts实际内存长度

    char tempstr[OPTS_STR_MEM_SIZE];

    memset(PULSEFLOW_G(Func_Prof_Data).opts, 0, OPTS_STR_MEM_SIZE); //初始化opts内存 保障 每次使用函数内存干净

    ZEND_HASH_FOREACH_STR_KEY_VAL(arr_hash, key, key_val)
            {
                if (key && key_val && Z_TYPE_P(key_val) == IS_STRING) {

                    int len = snprintf(tempstr, OPTS_STR_MEM_SIZE, "%s=%s&", key->val, key_val->value.str->val);

                    if ((len < OPTS_STR_MEM_SIZE - 1) && ((len + currentPointer) < (OPTS_STR_MEM_SIZE - 1))) {  //长度安全

                        if (strncpy(&PULSEFLOW_G(Func_Prof_Data).opts[currentPointer], tempstr, len)) {

                            currentPointer += len;

                        }

                    }
                }
            }
    ZEND_HASH_FOREACH_END();

}

PHP_FUNCTION (pulseflow_output_trace_list) {
    if (PULSEFLOW_G(is_web_display_trace_list) && (PULSEFLOW_G(Function_Prof_List_current_Size) > 0)) {

        php_printf("\n<br /> PulseFlow <br /> \n ");

        int i;
        for (i = 0; i < PULSEFLOW_G(Function_Prof_List_current_Size); ++i) {
            php_printf("[ %d ]: [PID: %d ][ %s => %s ] [ %u 次] [ %u byte ] [ %.1f ms] <br />\n", i, getpid(),
                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].className,
                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].functionName,
                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].refcount,
                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].memoryUse,
                       PULSEFLOW_G(Func_Prof_Data).Function_Prof_List[i].cpuTimeUse);
        }

    }

}

const zend_function_entry PulseFlow_functions[] = {

        PHP_FE(pulseflow_enable, NULL)

        PHP_FE(pulseflow_disable, NULL)

        PHP_FE(pulseflow_set_options, NULL)

        PHP_FE(pulseflow_output_trace_list, NULL)

        PHP_FE_END /* Must be the last line in PulseFlow_functions[] */

};

zend_module_entry PulseFlow_module_entry = {
        STANDARD_MODULE_HEADER, "PulseFlow", PulseFlow_functions,
        PHP_MINIT(PulseFlow),
        PHP_MSHUTDOWN(PulseFlow),
        PHP_RINIT(PulseFlow), /* Replace with NULL if there's nothing to do at request start */
        PHP_RSHUTDOWN(PulseFlow), /* Replace with NULL if there's nothing to do at request end */
        PHP_MINFO(PulseFlow),
        PHP_PULSEFLOW_VERSION,
        STANDARD_MODULE_PROPERTIES};

#ifdef COMPILE_DL_PULSEFLOW
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(PulseFlow)
#endif
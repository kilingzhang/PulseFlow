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
 | Author:                                                              |
 +----------------------------------------------------------------------+
 */
/* $Id$ */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <zend_compile.h>
#include "php.h"
#include "SAPI.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_PulseFlow.h"
#include "tracing.h"

ZEND_DECLARE_MODULE_GLOBALS(PulseFlow)

PHP_INI_BEGIN()
                STD_PHP_INI_ENTRY
                ("PulseFlow.enabled", "0", PHP_INI_ALL, OnUpdateBool, enabled,
                 zend_PulseFlow_globals, PulseFlow_globals)

//                STD_PHP_INI_ENTRY
//                ("PulseFlow.debug", "0", PHP_INI_ALL, OnUpdateBool, debug,
//                 zend_PulseFlow_globals, PulseFlow_globals)
                STD_PHP_INI_ENTRY
                ("PulseFlow.disable_trace_functions", "", PHP_INI_ALL, OnUpdateString, disable_trace_functions,
                 zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.disable_trace_class", "", PHP_INI_ALL, OnUpdateString, disable_trace_class,
                 zend_PulseFlow_globals, PulseFlow_globals)

//                STD_PHP_INI_ENTRY
//                ("PulseFlow.encode_type", "json", PHP_INI_ALL, OnUpdateString, encode_type,
//                 zend_PulseFlow_globals, PulseFlow_globals)

//                STD_PHP_INI_ENTRY
//                ("PulseFlow.send_type", "posix", PHP_INI_ALL, OnUpdateString, send_type,
//                 zend_PulseFlow_globals, PulseFlow_globals)

//                STD_PHP_INI_ENTRY
//                ("PulseFlow.posix_name", "/PulseFlow_posix_ipc", PHP_INI_ALL, OnUpdateString, posix_name,
//                 zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.svipc_name", "/dev/shm/PulseFlow_sv_ipc", PHP_INI_ALL, OnUpdateString, svipc_name,
                 zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.svipc_pj_id", "1000", PHP_INI_ALL, OnUpdateLong, svipc_gj_id,
                 zend_PulseFlow_globals, PulseFlow_globals)

PHP_INI_END()

static void (*_zend_execute_ex)(zend_execute_data *execute_data);

//static void (*_zend_execute_internal)(zend_execute_data *execute_data, zval *return_value);

//ZEND_DLEXPORT void PulseFlow_xhprof_execute_internal(zend_execute_data *execute_data, zval *return_value);

ZEND_DLEXPORT void PulseFlow_xhprof_execute_ex(zend_execute_data *execute_data);

PHP_MINIT_FUNCTION (PulseFlow) {
    REGISTER_INI_ENTRIES();
    Init_Class_Disable_Hash_List();
    Init_Func_Disable_Hash_List();

    memset(&PULSEFLOW_G(Func_Prof_Data), 0, sizeof(SVIPC_Func_Prof_Message));

    _zend_execute_ex = zend_execute_ex;
    zend_execute_ex = PulseFlow_xhprof_execute_ex;
    return SUCCESS;
}

ZEND_DLEXPORT void PulseFlow_xhprof_execute_ex(zend_execute_data *execute_data) {
    if (!PULSEFLOW_G(enabled)) {

        _zend_execute_ex(execute_data);

    } else {

        unsigned long classNameHash = 0;
        unsigned long funcNameHash = 0;
        if (execute_data != NULL) {

            zend_string *className = NULL, *funcName = NULL; // = execute_data->func->common.scope->name;

            if (execute_data->func->common.scope != NULL) {
                className = execute_data->func->common.scope->name;
            }

            if (execute_data->func->common.function_name) {
                funcName = execute_data->func->common.function_name;
            }

            if (funcName == NULL || className == NULL) {

                _zend_execute_ex(execute_data TSRMLS_CC);

            } else if ((funcNameHash = BKDRHash(funcName->val, funcName->len)) &&
                       Exist_In_Hash_List(funcNameHash, PULSEFLOW_G(FuncDisableHashList),
                                          PULSEFLOW_G(FuncDisableHashListSize))) {

                _zend_execute_ex(execute_data TSRMLS_CC);

            } else if ((classNameHash = BKDRHash(className->val, className->len)) &&
                       Exist_In_Hash_List(classNameHash, PULSEFLOW_G(classDisableHashList),
                                          PULSEFLOW_G(classDisableHashListSize))) {

                _zend_execute_ex(execute_data TSRMLS_CC);

            } else {

                unsigned char funcArrayPointer = getFuncArrayId(funcName, className, funcNameHash);

                struct timeval CpuTimeStart;

                size_t useMemoryStart;

                Simple_Trace_Performance_Begin(&CpuTimeStart, &useMemoryStart TSRMLS_CC);

                _zend_execute_ex(execute_data TSRMLS_CC);

                Simple_Trace_Performance_End(&CpuTimeStart, &useMemoryStart , funcArrayPointer TSRMLS_CC);

            }

        }
    }
}


PHP_MSHUTDOWN_FUNCTION (PulseFlow) {

    return SUCCESS;

}


PHP_RINIT_FUNCTION (PulseFlow) {
#if defined(COMPILE_DL_PULSEFLOW) && defined(ZTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif

    PULSEFLOW_G(Function_Prof_List_current_Size) = 0;
    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION (PulseFlow) {

    if (PULSEFLOW_G(Function_Prof_List_current_Size)) {
        SendDataToSVIPC(TSRMLS_C);
    }
    return SUCCESS;

}

static zend_always_inline int PulseFlow_info_print(const char *str) {
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

        PulseFlow_info_print("<a href=\"https://github.com/gitsrc/PulseFlow\"><img border=0 src=\"");

        PulseFlow_info_print(PulseFLow_LOGO_URI "\" alt=\"PulseFlow logo\" /></a>\n");

    }

    PulseFlow_info_print("PulseFlow is a PHP Profiler, Monitoring and Trace PHP .");

    PulseFlow_info_print(!sapi_module.phpinfo_as_text ? "<br /><br />" : "\n\n");

    PulseFlow_info_print(
            "The 'PulseFlow' extension optimized fork of the XHProf extension from tideways_xhprof and Facebook as open-source. <br /><br />&nbsp;&nbsp;(c) Tideways GmbH 2014-2017 <br /> &nbsp;&nbsp;(c) Facebook 2009");

    if (!sapi_module.phpinfo_as_text) {
        PulseFlow_info_print(
                "<br /><br /><strong>Source Code : https://github.com/gitsrc/PulseFlow  </strong>");

    }

    php_info_print_box_end();

}

PHP_FUNCTION (pulseflow_enable) {

    PULSEFLOW_G(enabled) = 1;

}

PHP_FUNCTION (pulseflow_disable) {

    PULSEFLOW_G(enabled) = 0;

}


const zend_function_entry PulseFlow_functions[] = {

        PHP_FE(pulseflow_enable, NULL)

        PHP_FE(pulseflow_disable, NULL)

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
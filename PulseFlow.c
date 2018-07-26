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
//#include "common.c"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <zend_compile.h>
#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_PulseFlow.h"
#include "tracing.h"

ZEND_DECLARE_MODULE_GLOBALS(PulseFlow)

/* True global resources - no need for thread safety here */
static int le_PulseFlow;

PHP_INI_BEGIN()
                STD_PHP_INI_ENTRY("PulseFlow.enabled", "0", PHP_INI_ALL, OnUpdateBool, enabled,
                                  zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY("PulseFlow.debug", "0", PHP_INI_ALL, OnUpdateBool, debug,
                                  zend_PulseFlow_globals, PulseFlow_globals)

                STD_PHP_INI_ENTRY
                ("PulseFlow.disable_trace_functions", "", PHP_INI_ALL, OnUpdateString, disable_trace_functions,
                 zend_PulseFlow_globals, PulseFlow_globals)

PHP_INI_END()

static void (*_zend_execute_ex)(zend_execute_data *execute_data);

//static void (*_zend_execute_internal)(zend_execute_data *execute_data, zval *return_value);

//ZEND_DLEXPORT void PulseFlow_xhprof_execute_internal(zend_execute_data *execute_data, zval *return_value);

ZEND_DLEXPORT void PulseFlow_xhprof_execute_ex(zend_execute_data *execute_data);

/* {{{ php_PulseFlow_init_globals
 */
/* Uncomment this function if you have INI entries
 static void php_PulseFlow_init_globals(zend_PulseFlow_globals *PulseFlow_globals)
 {
 PulseFlow_globals->global_value = 0;
 PulseFlow_globals->global_string = NULL;
 }
 */
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION (PulseFlow) {

    REGISTER_INI_ENTRIES();

    //_zend_execute_internal = zend_execute_internal;
    // zend_execute_internal = PulseFlow_xhprof_execute_internal;

    _zend_execute_ex = zend_execute_ex;

    zend_execute_ex = PulseFlow_xhprof_execute_ex;

    return SUCCESS;
}

ZEND_DLEXPORT void PulseFlow_xhprof_execute_ex(zend_execute_data *execute_data) {

    if (!PULSEFLOW_G(enabled)) {

        _zend_execute_ex(execute_data);

    } else {

        if (execute_data != NULL) {

            zend_string *className = tracing_get_class_name(execute_data TSRMLS_CC);
            zend_string *funcName = tracing_get_function_name(execute_data TSRMLS_CC);
            if (funcName == NULL || className == NULL) {

                _zend_execute_ex(execute_data TSRMLS_CC);

            } else if (funcName != NULL && zend_hash_exists(PULSEFLOW_G(disable_trace_functions_hash), funcName)) {

                _zend_execute_ex(execute_data TSRMLS_CC);

            } else {
                struct timeval t0;
                getlinuxTime(&t0 TSRMLS_CC);

                int begin_m = getlinuxMemory(TSRMLS_CC);

                _zend_execute_ex(execute_data TSRMLS_CC);

                int end_m = getLinuxMemoryUse(begin_m TSRMLS_DC);

                float elapsed = getLinuxTimeUse(&t0 TSRMLS_CC);
                if (PULSEFLOW_G(debug)) {
                    php_printf("[ %s->%s ] Using [ CPU in %f milliseconds ] [ Memory %d bytes ]<br />\n",
                               className->val, funcName->val, elapsed, end_m);
                }

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

    INIT_disable_trace_functions_hash(TSRMLS_C);

    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION (PulseFlow) {

    FREE_disable_trace_functions_hash(TSRMLS_C);

    return SUCCESS;

}

PHP_MINFO_FUNCTION (PulseFlow) {
    php_info_print_table_start();
    if (PULSEFLOW_G(enabled)) {
        php_info_print_table_header(2, "PulseFlow support", "enabled");
    } else {
        php_info_print_table_header(2, "PulseFlow support", "disabled");
    }
    php_info_print_table_end();
}

const zend_function_entry PulseFlow_functions[] = {
//PHP_FE(confirm_PulseFlow_compiled,	NULL)		/* For testing, remove later. */
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

//PHP_FUNCTION (confirm_PulseFlow_compiled) {
//
//    char *arg = NULL;
//
//    size_t arg_len, len;
//
//    zend_string *strg;
//
//    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &arg, &arg_len)
//        == FAILURE) {
//        return;
//    }
//
//    strg =
//            strpprintf(0,
//                       "Congrsatulations! You have successfully modified ext/%.78s/config.m4. Module %.78s is now compiled into PHP.",
//                       "PulseFlow", arg);
//
//    RETURN_STR(strg);
//}
static zend_always_inline zend_string *tracing_get_class_name(zend_execute_data *data TSRMLS_DC) {
    zend_function *curr_func;

    if (!data) {
        return NULL;
    }

    curr_func = data->func;

    if (curr_func->common.scope != NULL) {
        zend_string_addref(curr_func->common.scope->name);

        return curr_func->common.scope->name;
    }

    return NULL;
}


static zend_always_inline zend_string *tracing_get_function_name(zend_execute_data *data TSRMLS_DC) {
    zend_function *curr_func;

    if (!data) {
        return NULL;
    }

    curr_func = data->func;

    if (!curr_func->common.function_name) {
        return NULL;
    }

    zend_string_addref(curr_func->common.function_name);

    return curr_func->common.function_name;
}

static zend_always_inline float timedifference_msec(struct timeval *t0 , struct timeval *t1 TSRMLS_DC) {

    return ((*t1).tv_sec - (*t0).tv_sec) * 1000.0f + ((*t1).tv_usec - (*t0).tv_usec) / 1000.0f;

}

static zend_always_inline void getlinuxTime(struct timeval *t TSRMLS_DC) {

    gettimeofday(t TSRMLS_CC, 0);

}

static zend_always_inline float getLinuxTimeUse(struct timeval *begin TSRMLS_DC) {

    struct timeval end;

    getlinuxTime(&end TSRMLS_CC);

    return timedifference_msec(begin , &end TSRMLS_CC);

}

static zend_always_inline int getlinuxMemory(TSRMLS_D) {

    return zend_memory_usage(0 TSRMLS_CC);

}

static zend_always_inline int getLinuxMemoryUse(int beginMemory TSRMLS_DC) {

    return zend_memory_usage(0 TSRMLS_CC) - beginMemory;

}
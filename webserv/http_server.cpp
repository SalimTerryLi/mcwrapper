//
// Created by salimterryli on 2021/3/13.
//

#include "http_server.h"
#include "../utils.h"

#include <csignal>
#include <cstring>
#include <libwebsockets.h>
#include <pthread.h>

pthread_t lws_thread = {};

static bool _should_lws_exit = false;
pthread_mutex_t _should_lws_exit_mutex = {};
struct lws_context *context;


static const struct lws_http_mount mount = {
        /* .mount_next */ NULL,         /* linked-list "next" */
        /* .mountpoint */ "/",          /* mountpoint URL */
        /* .origin */ "./mount-origin", /* serve from dir */
        /* .def */ "index.html",        /* default filename */
        /* .protocol */ NULL,
        /* .cgienv */ NULL,
        /* .extra_mimetypes */ NULL,
        /* .interpret */ NULL,
        /* .cgi_timeout */ 0,
        /* .cache_max_age */ 0,
        /* .auth_mask */ 0,
        /* .cache_reusable */ 0,
        /* .cache_revalidate */ 0,
        /* .cache_intermediaries */ 0,
        /* .origin_protocol */ LWSMPRO_FILE, /* files in a dir */
        /* .mountpoint_len */ 1,             /* char count */
        /* .basic_auth_login_file */ NULL,
};

void *lws_thread_func(void *arg);

int start_http_server() {
	int ret = 0;
	ret = pthread_mutex_init(&_should_lws_exit_mutex, nullptr);
	if (ret != 0) {
		eprintf("mutex_init failed: %d\n", ret);
		return ret;
	}

	struct lws_context_creation_info info;
	int logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE
	        /* for LLL_ verbosity above NOTICE to be built into lws,
	 * lws must have been configured and built with
	 * -DCMAKE_BUILD_TYPE=DEBUG instead of =RELEASE */
	        /* | LLL_INFO */ /* | LLL_PARSER */ /* | LLL_HEADER */
	        /* | LLL_EXT */ /* | LLL_CLIENT */  /* | LLL_LATENCY */
	        /* | LLL_DEBUG */;
	lws_set_log_level(logs, NULL);
	lwsl_user("LWS minimal http server | visit http://localhost:7681\n");

	memset(&info, 0, sizeof info); /* otherwise uninitialized garbage */
	info.port = 7681;
	info.mounts = &mount;
	info.error_document_404 = "/404.html";
	info.options =
	        LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

	context = lws_create_context(&info);
	if (!context) {
		lwsl_err("lws init failed\n");
		return 1;
	}

	ret = pthread_create(&lws_thread, nullptr, &lws_thread_func, nullptr);
	if (ret != 0) {
		eprintf("thread starting failed: %d\n", ret);
		lws_context_destroy(context);
	}

	return ret;
}

void *lws_thread_func(void *arg) {
	int ret;
	while (true) {
		pthread_mutex_lock(&_should_lws_exit_mutex);
		bool should_exit = _should_lws_exit;
		pthread_mutex_unlock(&_should_lws_exit_mutex);
		if (should_exit) { break; }

		ret = lws_service(context, 0);
		if (ret != 0) { break; }
	}
	pthread_exit(nullptr);
}

int stop_http_server() {
	pthread_mutex_lock(&_should_lws_exit_mutex);
	_should_lws_exit = true;
	pthread_mutex_unlock(&_should_lws_exit_mutex);
	lws_cancel_service(context);
	pthread_join(lws_thread, nullptr);

	pthread_mutex_destroy(&_should_lws_exit_mutex);
	return 0;
}
//
// Created by salimterryli on 2021/3/13.
//

#include "http_server.h"
#include "../ServerInfo.h"
#include "../utils.h"

#include <csignal>
#include <cstring>
#include <libwebsockets.h>
#include <pthread.h>

pthread_t lws_thread = {};// store thread obj for lws_service()

static bool should_lws_exit = false;
pthread_mutex_t should_lws_exit_mutex = {};
struct lws_context *context;// static context obj

static const struct lws_http_mount get_mount = {
        /* .mount_next */ nullptr, /* linked-list "next" */
        /* .mountpoint */ "/get",  /* mountpoint URL */
        /* .origin */ nullptr,     /* protocol */
        /* .def */ nullptr,
        /* .protocol */ "http",
        /* .cgienv */ nullptr,
        /* .extra_mimetypes */ nullptr,
        /* .interpret */ nullptr,
        /* .cgi_timeout */ 0,
        /* .cache_max_age */ 0,
        /* .auth_mask */ 0,
        /* .cache_reusable */ 0,
        /* .cache_revalidate */ 0,
        /* .cache_intermediaries */ 0,
        /* .origin_protocol */ LWSMPRO_CALLBACK, /* dynamic */
        /* .mountpoint_len */ 4,                 /* char count */
        /* .basic_auth_login_file */ nullptr,
};

static const struct lws_http_mount static_mount = {
        /* .mount_next */ &get_mount, /* linked-list "next" */
        /* .mountpoint */ "/",        /* mountpoint URL */
        /* .origin */ "./web_root",   /* serve from dir */
        /* .def */ "index.html",      /* default filename */
        /* .protocol */ nullptr,
        /* .cgienv */ nullptr,
        /* .extra_mimetypes */ nullptr,
        /* .interpret */ nullptr,
        /* .cgi_timeout */ 0,
        /* .cache_max_age */ 0,
        /* .auth_mask */ 0,
        /* .cache_reusable */ 0,
        /* .cache_revalidate */ 0,
        /* .cache_intermediaries */ 0,
        /* .origin_protocol */ LWSMPRO_FILE, /* files in a dir */
        /* .mountpoint_len */ 1,             /* char count */
        /* .basic_auth_login_file */ nullptr,
};

static int callback_dynamic_http(struct lws *wsi, enum lws_callback_reasons reason,
                                 void *user, void *in, size_t len);

struct pss {
	char path[128];
};

static const struct lws_protocols defprot =
        {"", lws_callback_http_dummy, 0, 0};
static const struct lws_protocols protocol =
        {"http", callback_dynamic_http, sizeof(struct pss), 0};

static const struct lws_protocols *pprotocols[] = {&defprot, &protocol, nullptr};

void *lws_thread_func(void *arg);

int start_http_server() {
	int ret = 0;
	ret = pthread_mutex_init(&should_lws_exit_mutex, nullptr);
	if (ret != 0) {
		eprintf("mutex_init failed: %d\n", ret);
		return ret;
	}

	struct lws_context_creation_info info {};
	int logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE
	        /* for LLL_ verbosity above NOTICE to be built into lws,
	 * lws must have been configured and built with
	 * -DCMAKE_BUILD_TYPE=DEBUG instead of =RELEASE */
	        /* | LLL_INFO */ /* | LLL_PARSER */ /* | LLL_HEADER */
	        /* | LLL_EXT */ /* | LLL_CLIENT */  /* | LLL_LATENCY */
	        /* | LLL_DEBUG */;
	lws_set_log_level(logs, nullptr);
	lwsl_user("LWS minimal http server | visit http://localhost:7681\n");

	memset(&info, 0, sizeof info); /* otherwise uninitialized garbage */
	info.port = 7681;
	info.pprotocols = pprotocols;
	info.mounts = &static_mount;
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
		pthread_mutex_lock(&should_lws_exit_mutex);
		bool should_exit = should_lws_exit;
		pthread_mutex_unlock(&should_lws_exit_mutex);
		if (should_exit) { break; }

		ret = lws_service(context, 0);
		if (ret != 0) { break; }
	}
	pthread_exit(nullptr);
}

int stop_http_server() {
	pthread_mutex_lock(&should_lws_exit_mutex);
	should_lws_exit = true;
	pthread_mutex_unlock(&should_lws_exit_mutex);
	lws_cancel_service(context);
	pthread_join(lws_thread, nullptr);

	pthread_mutex_destroy(&should_lws_exit_mutex);
	return 0;
}

static int
callback_dynamic_http(struct lws *wsi, enum lws_callback_reasons reason,
                      void *user, void *in, size_t len) {
	struct pss *pss = (struct pss *) user;
	uint8_t buf[LWS_PRE + 2048], *start = &buf[LWS_PRE], *p = start,
	                             *end = &buf[sizeof(buf) - LWS_PRE - 1];
	time_t t;
	struct tm *local_t = {};
	char timebuf[32] = {};
	lws_write_protocol response_stage_state = LWS_WRITE_HTTP;

	switch (reason) {
		case LWS_CALLBACK_HTTP:

			/*
			 * If you want to know the full url path used, you can get it
			 * like this
			 *
			 * response_stage_state = lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI);
			 *
			 * The base path is the first (response_stage_state - strlen((const char *)in))
			 * chars in buf.
			 */

			/*
			 * In contains the url part after the place the mount was
			 * positioned at, eg, if positioned at "/dyn" and given
			 * "/dyn/mypath", in will contain /mypath
			 */
			lws_snprintf(pss->path, sizeof(pss->path), "%s",
			             (const char *) in);

			lws_get_peer_simple(wsi, (char *) buf, sizeof(buf));
			lwsl_notice("%s: HTTP: connection %s, path %s\n", __func__,
			            (const char *) buf, pss->path);

			/*
			 * prepare and write http headers... with regards to content-
			 * length, there are three approaches:
			 *
			 *  - http/1.0 or connection:close: no need, but no pipelining
			 *  - http/1.1 or connected:keep-alive
			 *     (keep-alive is default for 1.1): content-length required
			 *  - http/2: no need, LWS_WRITE_HTTP_FINAL closes the stream
			 *
			 * giving the api below LWS_ILLEGAL_HTTP_CONTENT_LEN instead of
			 * a content length forces the connection response headers to
			 * send back "connection: close", disabling keep-alive.
			 *
			 * If you know the final content-length, it's always OK to give
			 * it and keep-alive can work then if otherwise possible.  But
			 * often you don't know it and avoiding having to compute it
			 * at header-time makes life easier at the server.
			 */
			if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
			                                "application/json",
			                                LWS_ILLEGAL_HTTP_CONTENT_LEN, /* no content len */
			                                &p, end))
				return 1;

			if (lws_finalize_write_http_header(wsi, start, &p, end))
				return 1;

			/* write the body separately */
			lws_callback_on_writable(wsi);

			return 0;

		case LWS_CALLBACK_HTTP_WRITEABLE:
			/*
				 * to work with http/2, we must take care about LWS_PRE
				 * valid behind the buffer we will send.
				 */
			if (strstr(pss->path, "/info")) {
				pthread_mutex_lock(&serverHolder_mutex);
				local_t = localtime(&serverHolder.boot_ts);
				strftime(timebuf, 32, "%c", local_t);
				p += lws_snprintf((char *) p, end - p, R"({
"status":"OK",
"version":"%s",
"default_mode":"%s",
"booting_elapsed_s":%.2f,
"online_since":"%s",
"player_count":%d
})",
				                  serverHolder.version, serverHolder.default_mode, serverHolder.bootup_time, timebuf, count_online_player(false));
				pthread_mutex_unlock(&serverHolder_mutex);
			} else if (strstr(pss->path, "/players")) {
				p += lws_snprintf((char *) p, end - p, R"({"status":"OK","players":[)");
				pthread_mutex_lock(&serverHolder_mutex);
				for (Player &player : serverHolder.players) {
					if (player.isOnline) {
						p += lws_snprintf((char *) p, end - p, R"("%s")", player.name);
					}
					if (serverHolder.players.back() != player) {
						p += lws_snprintf((char *) p, end - p, R"(,)");
					}
				}
				pthread_mutex_unlock(&serverHolder_mutex);
				p += lws_snprintf((char *) p, end - p, R"(]})");
			} else if (strstr(pss->path, "/player/")) {
				pthread_mutex_lock(&serverHolder_mutex);
				for (Player &player : serverHolder.players) {
					if (player.isOnline && strstr(player.name, pss->path + 8) == player.name) {
						local_t = localtime(&serverHolder.boot_ts);
						strftime(timebuf, 32, "%c", local_t);
						p += lws_snprintf((char *) p, end - p, R"({
"status":"OK",
"name":"%s",
"uuid":"%s",
"login_pos":[%.5f,%.5f,%.5f],
"login_time":"%s"
})",
						                  player.name, player.uuid, player.login_pos[0], player.login_pos[1], player.login_pos[2], timebuf);
						break;
					}
				}
				pthread_mutex_unlock(&serverHolder_mutex);
			} else {
				p += lws_snprintf((char *) p, end - p, R"({
"status":"ERROR",
"request":"%s"
})",
				                  pss->path);
			}

			if (lws_write(wsi, (uint8_t *) start, lws_ptr_diff(p, start), response_stage_state) !=
			    lws_ptr_diff(p, start))
				return 1;

			response_stage_state = LWS_WRITE_HTTP_FINAL;
			/*
			 * HTTP/1.0 no keepalive: close network connection
			 * HTTP/1.1 or HTTP1.0 + KA: wait / process next transaction
			 * HTTP/2: stream ended, parent connection remains up
			 */
			if (response_stage_state == LWS_WRITE_HTTP_FINAL) {
				if (lws_http_transaction_completed(wsi))
					return -1;
			} else
				lws_callback_on_writable(wsi);

			return 0;

		default:
			break;
	}

	return lws_callback_http_dummy(wsi, reason, user, in, len);
}
//
// Created by salimterryli on 2021/3/9.
//

#include "utils.h"

#include <cstring>

int store_buf(GBN_Buffer &buf, char *p, size_t size) {
	if (buf.line_buf_index_end + size > sizeof(buf.line_buf)) {
		return -1;
	}
	memcpy(buf.line_buf + buf.line_buf_index_end, p, size);
	buf.line_buf_index_end += size;
	return 0;
}

void clear_buf(GBN_Buffer &buf) {
	buf.line_buf_index_begin = 0;
	buf.line_buf_index_end = 0;
}

char *getline(GBN_Buffer &buf) {
	size_t index = buf.line_buf_index_begin;
	bool meet_first_valid = false;

	while (index < buf.line_buf_index_end) {
		if (!meet_first_valid) {
			if ((buf.line_buf[index] == '\r') || (buf.line_buf[index] == '\n') ||
			    (buf.line_buf[index] == '\0')) {
				++index;
				++buf.line_buf_index_begin;
			} else {
				meet_first_valid = true;
			}
		} else {
			if ((buf.line_buf[index] == '\r') || (buf.line_buf[index] == '\n') ||
			    (buf.line_buf[index] == '\0')) {
				buf.line_buf[index] = '\0';
				size_t tmp = buf.line_buf_index_begin;
				if (index == buf.line_buf_index_end - 1) {// buffer cleared
					clear_buf(buf);
				} else {
					buf.line_buf_index_begin = index;
				}
				return buf.line_buf + tmp;
			} else {
				++index;
			}
		}
	}

	if (buf.line_buf_index_begin !=
	    buf.line_buf_index_end) {// only happens if a string detected but
		                         // without any ending.
		// that means buffer cannot accept next package, but there still parts of
		// string to be read. push that part into the beginning.
		char tmp_buf[sizeof(buf.line_buf)] = {};
		memcpy(tmp_buf, buf.line_buf + buf.line_buf_index_begin,
		       buf.line_buf_index_end - buf.line_buf_index_begin);
		buf.line_buf_index_end -= buf.line_buf_index_begin;
		buf.line_buf_index_begin = 0;
		memcpy(buf.line_buf, tmp_buf, buf.line_buf_index_end);
	}

	return nullptr;
}
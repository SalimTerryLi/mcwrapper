//
// Created by salimterryli on 2021/3/8.
//

#ifndef MCWRAPPER_UTILS_H
#define MCWRAPPER_UTILS_H

#include <cstdio>
#define eprintf(...) fprintf (stderr, __VA_ARGS__)

struct GBN_Buffer{
	char line_buf[1024]={};
	size_t line_buf_index_begin=0;
	size_t line_buf_index_end=0;    // treated as size, but not decreasing when pop.
};

/*
 * pass in pointer and size. return 0 if succeed or -1 when buffer full
 */
int store_buf(GBN_Buffer &buf, char *p, size_t size);

/*
 * clear previous stored data, reset buffer states.
 */
void clear_buf(GBN_Buffer &buf);

/*
 * return the start byte of string, terminated with '\0'.
 * return nullptr if no new line available
 */
char *getline(GBN_Buffer &buf);

#endif //MCWRAPPER_UTILS_H

//
// Created by salimterryli on 2021/3/9.
//

#include <cstring>
#include "utils.h"

static char line_buf[1024]={};
static size_t line_buf_index_begin=0;
static size_t line_buf_index_end=0;    // treated as size, but not decreasing when pop.

int store_buf(char *p, size_t size){
	if (line_buf_index_end + size > sizeof(line_buf)){
		return -1;
	}
	memcpy(line_buf+line_buf_index_end,p,size);
	line_buf_index_end+=size;
	return 0;
}

void clear_buf(){
	line_buf_index_begin=0;
	line_buf_index_end=0;
}

char *getline(){
	size_t index=line_buf_index_begin;
	bool meet_first_valid=false;

	while(index < line_buf_index_end){
		if (!meet_first_valid){
			if((line_buf[index]=='\r')||(line_buf[index]=='\n')||(line_buf[index]=='\0')){
				++index;
				++line_buf_index_begin;
			}else{
				meet_first_valid=true;
			}
		}else{
			if((line_buf[index]=='\r')||(line_buf[index]=='\n')||(line_buf[index]=='\0')){
				line_buf[index]='\0';
				size_t tmp=line_buf_index_begin;
				if (index==line_buf_index_end-1){   // buffer cleared
					clear_buf();
				}else{
					line_buf_index_begin=index;
				}
				return line_buf+tmp;
			}else{
				++index;
			}
		}
	}

	return nullptr;
}
#ifndef __COMMON_H__
#define __COMMON_H__

#include <curl/curl.h>

int my_trace(CURL *handle, curl_infotype type, char *data, size_t size,
	     void *userp);

#ifdef _WIN32
int gettimeofday(struct timeval *tp, void *tzp);
#endif

#ifdef _WIN32
#define FORMAT_SIZE_T "Iu"
#else
#define FORMAT_SIZE_T "zu"
#endif

#endif

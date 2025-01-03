/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 1998 - 2021, Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/
/* <DESC>
 * Multiplexed HTTP/2 uploads over a single connection
 * </DESC>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <time.h>
#else
#include <sys/eventfd.h>
/* somewhat unix-specific */
#include <sys/time.h>
#include <unistd.h>
#endif

/* curl stuff */
#include <curl/curl.h>
#include <curl/mprintf.h>

#include "common.h"

#ifndef CURLPIPE_MULTIPLEX
/* This little trick will just make sure that we don't enable pipelining for
   libcurls old enough to not have this symbol. It is _not_ defined to zero in
   a recent libcurl header. */
#define CURLPIPE_MULTIPLEX 0
#endif

#define NUM_HANDLES 1000

#define SERVER_URL "https://127.5.5.5:8081"
#define PATH_GET SERVER_URL "/longPolling"
#define PATH_POST SERVER_URL "/post"

#define POST_FILENAME "dummy.dat"

struct input
{
	FILE *in;
	CURL *hnd;
	struct curl_slist *headers;
};

static size_t read_callback(char *ptr, size_t size, size_t nmemb, void *userp)
{
	struct input *i = userp;

	/* read 100 bytes per each callback */
	size_t retcode = fread(ptr, 1, 100, i->in);
	printf("\nread %" FORMAT_SIZE_T " bytes from file\n\n", retcode);

	return retcode;
}

static void setup_post(struct input *i, const char *upload)
{
	struct stat file_info;
	curl_off_t uploadsize;
	CURL *hnd;

	hnd = i->hnd = curl_easy_init();
	i->headers = NULL;

	/* get the file size of the local file */
	if (stat(upload, &file_info))
	{
		fprintf(stderr, "error: could not stat file %s: %s\n", upload,
			strerror(errno));
		exit(1);
	}

	uploadsize = (curl_off_t)file_info.st_size;
	printf("\nuploadsize = %" CURL_FORMAT_CURL_OFF_T "\n\n", uploadsize);

	i->in = fopen(upload, "rb");
	if (!i->in)
	{
		fprintf(stderr,
			"error: could not open file %s for reading: %s\n",
			upload, strerror(errno));
		exit(1);
	}

	printf("\n> PATH " PATH_POST "\n\n");

	/* we want to use our own read function */
	curl_easy_setopt(hnd, CURLOPT_POST, 1L);
	curl_easy_setopt(hnd, CURLOPT_READFUNCTION, read_callback);
	/* read from this file */
	curl_easy_setopt(hnd, CURLOPT_READDATA, i);

	/* send in the URL to store the upload as */
	curl_easy_setopt(hnd, CURLOPT_URL, PATH_POST);

	/* please be verbose */
	curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(hnd, CURLOPT_DEBUGFUNCTION, my_trace);
	curl_easy_setopt(hnd, CURLOPT_DEBUGDATA, i);

	curl_easy_setopt(hnd, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(hnd, CURLOPT_AUTOREFERER, 1L);

	/* HTTP/2 please */
	curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

	/* we use a self-signed test server, skip verification during debugging */
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYHOST, 0L);

	/* dummy headers */
	i->headers = curl_slist_append(
	    i->headers,
	    "Content-Type: multipart/form-data; boundary=nugusdk.boundary.24dfa2ab02147913");
	curl_easy_setopt(hnd, CURLOPT_HTTPHEADER, i->headers);

#if (CURLPIPE_MULTIPLEX > 0)
	/* wait for pipe connection to confirm */
	curl_easy_setopt(hnd, CURLOPT_PIPEWAIT, 1L);
#endif
}

static void setup_get(struct input *i)
{
	CURL *hnd;

	hnd = i->hnd = curl_easy_init();
	i->headers = NULL;

	printf("\n> GET " PATH_GET "\n\n");

	/* set the same URL */
	curl_easy_setopt(hnd, CURLOPT_URL, PATH_GET);

	/* please be verbose */
	curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(hnd, CURLOPT_DEBUGFUNCTION, my_trace);
	curl_easy_setopt(hnd, CURLOPT_DEBUGDATA, i);

	/* HTTP/2 please */
	curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

	/* we use a self-signed test server, skip verification during debugging */
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYHOST, 0L);

#if (CURLPIPE_MULTIPLEX > 0)
	/* wait for pipe connection to confirm */
	curl_easy_setopt(hnd, CURLOPT_PIPEWAIT, 1L);
#endif
}

/*
 * Upload all files over HTTP/2, using the same physical connection!
 */
int main(int argc, char **argv)
{
	struct input trans[NUM_HANDLES] = {0};
	CURLM *multi_handle;
	int i;
	int still_running = 0; /* keep number of running handles */
	int num_transfers = 2;
	static time_t base_time;
	static time_t now;
	int fired = 0;

	/* init a multi stack */
	multi_handle = curl_multi_init();

	/* First request */
	setup_get(&trans[0]);
	curl_multi_add_handle(multi_handle, trans[0].hnd);

	curl_multi_setopt(multi_handle, CURLMOPT_PIPELINING,
			  CURLPIPE_MULTIPLEX);

	/* We do HTTP/2 so let's stick to one connection per host */
	//curl_multi_setopt(multi_handle, CURLMOPT_MAX_HOST_CONNECTIONS, 1L);

	time(&base_time);

	do
	{
		CURLMcode mc = curl_multi_perform(multi_handle, &still_running);

		if (still_running == 0)
			break;

		/* wait for activity, timeout or "nothing" */
		mc = curl_multi_poll(multi_handle, NULL, 0, 1000, NULL);

		time(&now);

		printf(".\n");

		/* after 7 seconds, POST request start */
		if (now - base_time >= 7 && fired == 0)
		{
			setup_post(&trans[1], POST_FILENAME);
			curl_multi_add_handle(multi_handle, trans[1].hnd);
			fired = 1;
		}

		if (mc)
			break;

	} while (still_running);

	curl_multi_cleanup(multi_handle);

	for (i = 0; i < num_transfers; i++)
	{
    if(trans[i].hnd) {
		  curl_multi_remove_handle(multi_handle, trans[i].hnd);
      if(trans[i].headers)
		    curl_slist_free_all(trans[i].headers);
		  curl_easy_cleanup(trans[i].hnd);
    }
	}

	return 0;
}

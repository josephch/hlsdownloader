/*
 * If not stated otherwise in this file or this component's license file the
 * following copyright and licenses apply:
 *
 * Copyright 2019 Christo Joseph
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include <assert.h>
#include <ctype.h>
#include <curl/curl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <list>
#include <string>

using namespace std;

#define BASE_URL_MAX_SIZE 1024
#define PATH_MAX_SIZE 1024
#define URL_MAX_SIZE (BASE_URL_MAX_SIZE + PATH_MAX_SIZE)
#define OUTPUT_DIR "out"
#define OUTPUT_FILE_PATH_SIZE (PATH_MAX_SIZE + 4)
#define MANIFEST_EXT ".m3u8"
#define MANIFEST_EXT_LEN 5
#define TS_EXT ".ts"
#define TS_EXT_LEN 3
//#define MAX_DOWNLOADS_PER_THREAD 50

#define MAX_ASYNC_FETCHES 128

struct fetch_item
{
	char path[PATH_MAX_SIZE];
	char base_url[BASE_URL_MAX_SIZE];
	char base_directory[BASE_URL_MAX_SIZE];
	fetch_item* next;
};

static int async_fetch_idx = 0;

static bool is_live = 0;

static bool main_list_processed = false;

static int g_maximum_downloads_per_profile = INT_MAX;

static bool g_interactive_download = false;

static size_t curl_write(void* ptr, size_t size, size_t nmemb, FILE* fp)
{
	size_t len = fwrite(ptr, size, nmemb, fp);
	return len;
}

void download_and_process_list(fetch_item* list);

static void* fetcher_thread(void* arg)
{
	fetch_item* list = static_cast<fetch_item*>(arg);
	download_and_process_list(list);
	return NULL;
}

void process(const char* file, fetch_item* item, const char* base_url)
{
	FILE* fp;
	size_t len = 0;
	ssize_t read_len;
	char* line = NULL;
	int main_manifest = 0;
	fetch_item* last_item = NULL;
	std::list<fetch_item*> manifest_items;
	pthread_t fetcherThreads[MAX_ASYNC_FETCHES];
	int fetcher_thread_count = 0;
	printf("process file %s\n", file);

	fp = fopen(file, "r");
	if (NULL == fp)
	{
		printf("File open failed. file = %s \n", file);
		return;
	}
	if (main_list_processed == false)
	{
		main_manifest = 1;
		main_list_processed = true;
	}
	while ((read_len = getline(&line, &len, fp)) != -1)
	{
		if (main_manifest)
		{
			printf("Main manifest: line:%s\n", line);
		}
		else
		{
			// printf("Playlist: line:%s\n", line);
		}
		{
			int i = read_len - 1;
			for (; i > 0; i--)
			{
				if (isspace(line[i]))
					line[i] = '\0';
				else
					break;
			}
		}
		if ('#' == line[0])
		{
			char* tagstart = strstr(line, "URI");
			if (tagstart)
			{
				char* uristart = strstr(tagstart, "\"");
				if (uristart)
				{
					uristart++;
					for (int i = 0; uristart[i] != '\0'; i++)
					{
						if (uristart[i] == '"')
						{
							uristart[i] = '\0';
							break;
						}
					}
					char temp_url[URL_MAX_SIZE];
					strcpy(temp_url, uristart);
					strcpy(line, temp_url);
				}
			}
			char* seq = strstr(line, "EXT-X-MEDIA-SEQUENCE");
			if (seq)
			{
			}
			char* stream = strstr(line, "EXT-X-STREAM-INF");
			if (stream)
			{
				main_manifest = 1;
			}
		}
		if ('#' != line[0] && (!isspace(line[0])))
		{
			if (main_manifest && g_interactive_download)
			{
				char buf[124];
				printf("Download %s ? y/n\n", line);
				if (fgets(buf, sizeof(buf), stdin))
				{
					if (buf[0] != 'y')
					{
						printf("Skip %s\n", line);
						continue;
					}
				}
			}
			fetch_item* item_new = new fetch_item();
			item_new->path[0] = '\0';
			strcat(item_new->path, line);
			strcpy(item_new->base_url, base_url);
			strcpy(item_new->base_directory, item->base_directory);
			{
				int path_len = strlen(item->path);
				for (int i = path_len - 1; i > 0; i--)
				{
					if (item->path[i] == '/')
					{
						strcat(item_new->base_directory, "/");
						strncat(item_new->base_directory, item->path, i);
						break;
					}
				}
			}
			item_new->next = NULL;

			if (!main_manifest)
			{
				if (NULL == last_item)
				{
					last_item = item;
					while (last_item->next)
					{
						last_item = last_item->next;
					}
				}
				last_item->next = item_new;
				last_item = item_new;
			}
			else
			{
				manifest_items.push_back(item_new);
			}
		}
	}
	fclose(fp);

	if (main_manifest)
	{
		for (auto it = manifest_items.begin(); it != manifest_items.end(); ++it)
		{
			fetch_item* item_new = *it;
			async_fetch_idx++;
			printf("Creating new thread for [%s] async_fetch_idx = %d\n",
				   item_new->path, async_fetch_idx);
			assert(async_fetch_idx < MAX_ASYNC_FETCHES);
			pthread_create(&fetcherThreads[fetcher_thread_count], NULL,
						   &fetcher_thread, item_new);
			fetcher_thread_count++;
		}
	}

	if (fetcher_thread_count)
	{
		for (int i = 0; i < fetcher_thread_count; i++)
		{
			pthread_join(fetcherThreads[i], NULL);
		}
		printf("joined all async fetcher threads\n");
	}

	if (line)
	{
		free(line);
	}
}

int ends_with_ext(const char* url, const char* file_ext, int file_ext_len)
{
	int ret = 0;
	int len = strlen(url);
	if (len > file_ext_len)
	{
		const char* ext = &url[len - file_ext_len];
		if (strcmp(file_ext, ext) == 0)
		{
			ret = 1;
		}
	}
	return ret;
}

void merge_manifest_files(const char* origfile, const char* outfile)
{
	printf("Merge  %s and %s - Not yet supported\n", origfile, outfile);
}

#define is_manifest(file_path)                          \
	((!ends_with_ext(file_path, TS_EXT, TS_EXT_LEN)) && \
	 (strstr(file_path, ".m3u8")))

void download_and_process_item(fetch_item* item)
{
	CURLcode res = CURLE_OK;
	char url[URL_MAX_SIZE];
	char outfile[OUTPUT_FILE_PATH_SIZE];
	char origfile[OUTPUT_FILE_PATH_SIZE];
	char effectiveUrl[URL_MAX_SIZE];
	struct stat st = {0};
	int download_file = 1;
	int merge_manifest = 0;
	strcpy(outfile, OUTPUT_DIR);
	strcat(outfile, "/");
	char file_path_a[PATH_MAX_SIZE];
	char* file_path = file_path_a;
	strncpy(file_path, item->path, PATH_MAX_SIZE);
	// printf("%s:%d  item->base_url %s item->path %s\n", __FUNCTION__,
	// __LINE__, item->base_url, item->path);
	if (0 == memcmp(file_path, "http://", 7))
	{
		file_path = &file_path[7];
		url[0] = '\0';
	}
	else if (0 == memcmp(file_path, "https://", 8))
	{
		file_path = &file_path[8];
		url[0] = '\0';
	}
	else if (file_path[0] != '/')
	{
		strcpy(url, item->base_url);
		strcat(url, "/");
	}
	else
	{
		strcpy(url, item->base_url);
		file_path = &file_path[1];
	}
	strcat(outfile, item->base_directory);
	strcat(outfile, "/");
	/*Truncate at .m3u8 for hls manifest/ playlists*/
	char* queryStart = strchr(file_path, '?');
	if (queryStart)
	{
		*queryStart = '\0';
	}
	strncat(outfile, file_path, 255);
	strcat(url, item->path);

	if (0 == stat(outfile, &st))
	{
		if (is_manifest(url))
		{
			int i;
			download_file = 1;
			strcpy(origfile, outfile);
			char tmpfile[OUTPUT_FILE_PATH_SIZE];
			for (i = 0; i < 1024; i++)
			{
				sprintf(tmpfile, "%s-%04d.m3u8", outfile, i);
				if (-1 == stat(tmpfile, &st))
				{
					break;
				}
			}
			if (1024 == i)
			{
				exit(0);
			}
			strcpy(outfile, tmpfile);
			merge_manifest = 1;
		}
		else
		{
			download_file = 0;
		}
	}

	if (download_file)
	{
		/*Create directory for new files if required.*/
		for (unsigned int i = 0; i < strlen(outfile); i++)
		{
			if (outfile[i] == '/')
			{
				outfile[i] = '\0';
				if (-1 == stat(outfile, &st))
				{
					mkdir(outfile, 0777);
				}
				outfile[i] = '/';
			}
		}
		printf("Downloading url %s as %s\n", url, outfile);
		FILE* fp = fopen(outfile, "wb");
		if (NULL == fp)
		{
			printf("File open failed. outfile = %s \n", outfile);
			return;
		}
		CURL* curl = curl_easy_init();
		if (curl)
		{
			curl_easy_setopt(curl, CURLOPT_URL, url);
			curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30 * 60);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			res = curl_easy_perform(curl);
			if (CURLE_OK != res)
			{
				printf("Curl perform failed. url = %s \n", url);
			}
			else
			{
				char* effectiveUrlPtr = NULL;
				res = curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL,
										&effectiveUrlPtr);
				strncpy(effectiveUrl, effectiveUrlPtr, URL_MAX_SIZE - 1);
				effectiveUrl[URL_MAX_SIZE - 1] = '\0';
			}
			curl_easy_cleanup(curl);
		}
		fclose(fp);
		if (CURLE_OK != res)
		{
			remove(outfile);
		}
	}
	else
	{
		printf("Not downloading url %s as %s since already available \n", url,
			   outfile);
		if (is_manifest(url))
		{
			strncpy(effectiveUrl, url, URL_MAX_SIZE);
		}
	}
	if (is_manifest(url))
	{
		if (merge_manifest)
		{
			merge_manifest_files(origfile, outfile);
		}

		int i = strlen(effectiveUrl) - 1;
		// printf("%s:%d  effectiveUrl %s\n", __FUNCTION__, __LINE__,
		// effectiveUrl);
		for (; i > 0; i--)
		{
			if (effectiveUrl[i] == '/')
			{
				effectiveUrl[i] = '\0';
				break;
			}
		}
		process(outfile, item, effectiveUrl);
	}
}

void download_and_process_list(fetch_item* list)
{
	int count = 0;
	while (NULL != list)
	{
		fetch_item* temp = list;
		download_and_process_item(list);
		list = list->next;
		delete temp;
		count++;
		if (count > g_maximum_downloads_per_profile)
		{
			printf("download_and_process - Max count reached , returning\n");
			return;
		}
	}
}

#define DELAY_BW_PLAYLIST_UPDATES_MS 4500
#define MAX_ITERATIONS_FOR_LIVE_STREAM 20

void print_usage(const char* name)
{
	printf("Usage : %s <url> [options]\n", name);
	printf("Option : -l for live\n");
	printf(
		"Option : -i for interactive selection of profiles to be downloaded\n");
	printf("Option : -m<val> to set maximum download per profile\n");
}

int main(int argc, char* argv[])
{
	int iteration = 1;
	fetch_item* base_item;
	char playlist_base_url[BASE_URL_MAX_SIZE];
	char base_url[BASE_URL_MAX_SIZE];
	char base_directory[BASE_URL_MAX_SIZE];

	const char* url = nullptr;
	for (int i = 1; i < argc; i++)
	{
		if (0 == strncmp(argv[i], "http", 4))
		{
			url = argv[i];
		}
		else if (0 == strncmp(argv[i], "-m", 2))
		{
			if (1 == sscanf(argv[i], "-m%d", &g_maximum_downloads_per_profile))
			{
				printf("Maximum downloads per profile set to %d\n",
					   g_maximum_downloads_per_profile);
			}
			else
			{
				printf("Error parsing max downloads per profile %s\n", argv[i]);
			}
		}
		else if (0 == strcmp(argv[i], "-i"))
		{
			g_interactive_download = true;
		}
		else if (0 == strcmp(argv[i], "-l"))
		{
			is_live = true;
		}
		else
		{
			printf("Invalid option %s\n", argv[i]);
			print_usage(argv[0]);
			return -1;
		}
	}

	if (!url)
	{
		print_usage(argv[0]);
		return -1;
	}

	printf(
		"url = %s live = %d interactive download = %d maximum download per "
		"profile = %d\n",
		url, is_live, g_interactive_download, g_maximum_downloads_per_profile);

	strcpy(base_url, url);
	int i = strlen(url) - 1;
	int last_slash_idx = i;

	for (; i > 1; i--)
	{
		if (base_url[i] == '/')
		{
			if (base_url[i - 1] == '/')
			{
				base_url[last_slash_idx] = '\0';
				break;
			}
			last_slash_idx = i;
		}
	}
	strcpy(playlist_base_url, url);
	i = strlen(url) - 1;

	for (; i > 0; i--)
	{
		if (playlist_base_url[i] == '/')
		{
			playlist_base_url[i] = '\0';
			break;
		}
	}
	printf("base_url %s playlist_base_url %s \n", base_url, playlist_base_url);
	// exit(0);
	base_item = new fetch_item();
	strcpy(base_item->path, &playlist_base_url[i + 1]);

	i = strlen(playlist_base_url) - 1;
	for (; i > 0; i--)
	{
		if (playlist_base_url[i] == '/')
		{
			strcpy(base_directory, &playlist_base_url[i + 1]);
			break;
		}
	}
	printf("base_url = %s base_dir %s\n", playlist_base_url, base_directory);
	base_item->next = NULL;
	do
	{
		long int start_time_ms, end_time_ms, diff_time_ms;
		struct timeval tp;
		gettimeofday(&tp, NULL);
		start_time_ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
		fetch_item* item = new fetch_item();
		memcpy(item, base_item, sizeof(fetch_item));
		strcpy(item->base_url, playlist_base_url);
		strcpy(item->base_directory, base_directory);
		printf("%s : Starting iteration %d\n", argv[0], iteration);

		fetch_item* list = item;
		async_fetch_idx++;
		download_and_process_list(list);

		iteration++;
		gettimeofday(&tp, NULL);
		end_time_ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
		diff_time_ms = end_time_ms - start_time_ms;
		printf(
			"%s : Completed iteration %d is_live =  %d time taken = %ld ms\n",
			argv[0], iteration, is_live, diff_time_ms);
		if (diff_time_ms < DELAY_BW_PLAYLIST_UPDATES_MS)
		{
			usleep(1000 * (DELAY_BW_PLAYLIST_UPDATES_MS - diff_time_ms));
		}
	} while (is_live && iteration < MAX_ITERATIONS_FOR_LIVE_STREAM);
	delete base_item;
	return 0;
}

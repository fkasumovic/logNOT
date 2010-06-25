#ifndef LOG_MONIT_MAIN_H
#define LOG_MONIT_MAIN_H

#include <stdio.h>
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <exception>
#include <queue>

#include <errno.h>
#include <stdlib.h>

#include <pthread.h>
#include <assert.h>
#include <time.h>

#define PROGRAM_CODENAME "LogsMonitoring"
#define PROGRAM_NAME "lognot"
#define PROGRAM_DESCRIPTION \
	" DESCRIPTION: logNOT is application that implements simple\n" \
	" tool that can be instructed to track and measure frequency\n" \
	" of textual log items, and to trigger specific action/command\n" \
	" when ever frequency of monitored log item or items\n" \
	" goes above or bellow defined frequency limits.\n" \
	" Frequency is defined as number of item occurrence per\n" \
	" time period. logNOT can process logs in “real-time” at\n" \
	" moment immediately after logs are writhed to log file.\n" \
	" Action or command that will be triggered by logNOT is\n" \
	" defined by user, can be any executable program or shell\n" \
	" script. What this action will do depends on user.\n"

#define PROGRAM_VERSION "0.1 0"
#define PID_FILES_LOCATION "/var/run/"

#define PATH_SEPARATOR '/'

#define CROP_CSTR(STR, MAX_LEN) \
	(MAX_LEN >= 0 && strlen(STR) > MAX_LEN )?STR[MAX_LEN]='\0':

#define MSEC_TO_SEC(u) ((u)/1000)
	
#define MAX(a,b) ((a)<(b))?(b):(a)
#define MIN(a,b) ((a)>(b))?(b):(a)
	
#define MAX_INI_FILE_LINE_LENGTH 1024

#define STD_TRIM_CHAR_SET		" \n\r\t"
#define MSP				" "
#define EOM				"\0"

#define CONF_SECTION_GENERAL		"general"

#define CONF_TMPDIR			"tmpdir"
#define CONF_LOG			"logfacility"
#define CONF_LOGFILE			"logfile"
#define CONF_UID			"uid"
#define CONF_GID			"gid"
#define CONF_LOADTIME			"loadtime"
#define CONF_UPBOUND_ACTION		"upbound_action"
#define CONF_DOWNBOUND_ACTION		"downbound_action"
#define CONF_UPBOUND_FREQ		"upbound_freq"
#define CONF_DOWNBOUND_FREQ		"downbound_freq"
#define CONF_ITEM_REGEX 		"regex"
#define CONF_FILE_TYPE			"file_type"
#define CONF_FILE_PATH			"path"
#define CONF_ATHREAD_COUNT		"athread_count"
#define CONF_SEPARATOR			"separator"
#define CONF_USECRC			"usecrc"
#define CONF_SIZE			"size"
#define CONF_SIZE_ACTION		"size_action"

#define INIT_VALID_OPTIONS_MAP \
	std::map<std::string, int> CONF_VALID_OPTIONS_MAP; \
	CONF_VALID_OPTIONS_MAP[CONF_TMPDIR] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_LOG] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_LOGFILE] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_UID] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_GID] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_LOADTIME] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_UPBOUND_ACTION] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_DOWNBOUND_ACTION] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_UPBOUND_FREQ] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_DOWNBOUND_FREQ] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_ITEM_REGEX] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_FILE_TYPE] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_FILE_PATH] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_ATHREAD_COUNT] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_SEPARATOR] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_USECRC] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_SIZE] = 1; \
	CONF_VALID_OPTIONS_MAP[CONF_SIZE_ACTION] = 1


// DEFAULT CONFIGURATION VALUES TO USE IF NOT OTHERWISE DEFINDED
#define CONF_DEFAULT_CONFIG		"/etc/"
#define CONF_DEFAULT_TMPDIR		"/tmp"
#define CONF_DEFAULT_LOGFILE		"/var/log/" PROGRAM_NAME ".log"
#define CONF_DEFAULT_LOG 		"file"
#define CONF_DEFAULT_UID 		"0"
#define CONF_DEFAULT_GID 		"0"
#define CONF_DEFAULT_A_TIMEOUT		"0"
#define CONF_DEFAULT_REGEX		"/.*/"
#define CONF_DEFAULT_FREQ		"0/1"
#define CONF_DEFAULT_FILE_TYPE		"file"
#define CONF_DEFAULT_AT_COUNT		"2"
#define CONF_DEFAULT_SEPARATOR		"\n"
#define CONF_DEFAULT_USECRC		"0"
#define CONF_DEFAULT_DOWNBOUND_ACTION	""
#define CONF_DEFAULT_SIZE		"0"
#define CONF_DEFAULT_SIZE_ACTION	""

#endif // LOG_MONIT_MAIN_H



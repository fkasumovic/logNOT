#include "debug.hpp"
#include "utility.hpp"
#include "core.hpp"

#include <syslog.h>
#include <stdarg.h>

using namespace std;

FILE* LNLog::log_file_descriptor = NULL;
bool LNLog::log_to_stdout = false;
bool LNLog::debug_is_on = false;
LNLog::LogFacility LNLog::log_facility = LNLog::STDOUT_LOG;
unsigned int LNLog::prev_log_crc = 0;
unsigned int LNLog::prev_log_count = 0;
time_t LNLog::prev_log_time = 0;


// static pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;

static LNMutex log_mtx = LNMutex();

#define LOCK_LOG \
	log_mtx.lock()
	
#define UNLOCK_LOG \
	log_mtx.unlock()

inline FILE* get_log_file_descriptor(LNLog::LNLogType type)
{
	if (LNLog::log_to_stdout) {
		switch (type) {
		case LNLog::DLOG_INFO:
		case LNLog::DLOG_DEBUG:
			return stdout;
			break;
		case LNLog::DLOG_WARNING:
		case LNLog::DLOG_ERROR:
		case LNLog::DLOG_FATAL:
			return stderr;
			break;
		default:
			return stdout;
			break;
		}
	} else if (LNLog::log_file_descriptor) {
		return LNLog::log_file_descriptor;
	}
	return 0;
}

//-----------------------------------------------------------------------------------------------
// LNLog

int LNLog::log(LNLogType type, const char *message)
{
	FILE* fd = NULL;
	time_t curtime = time(NULL), time_min = curtime/60;
	std::string timestr = ctime(&curtime);
	timestr = timestr.substr(0, timestr.find_last_of('\n')); // remove new line at end
	unsigned int _crc = 0;

	LOCK_LOG;
	
	if(type != DLOG_DEBUG) {
		// skip writhing same message in short time again and again, just count it
		_crc = LNUtility::_crc((unsigned char*)message, strlen(message));
		if( _crc == LNLog::prev_log_crc && time_min == LNLog::prev_log_time ) {
			LNLog::prev_log_count++;
			UNLOCK_LOG;
			return 0;
		}
		LNLog::prev_log_crc = _crc;
	}
	LNLog::prev_log_time = time_min;
	if(LNLog::prev_log_count) { // print how many times previous log repeated
		LNLog::prev_log_count++;
		if ( (fd = get_log_file_descriptor(type)) ) {
			if(0 >= fprintf(fd,
					"INFO [%s]: Last message repeated %d times.\n",
					timestr.c_str(), LNLog::prev_log_count)) {
				printf("ERROR: %s\n", strerror(errno));
			} else {
				fflush(fd);
			}
		} else {
			syslog(LOG_INFO, "%s",
			       LNUtility::formatString(
					       "Last message repeated %d times.", 
					       LNLog::prev_log_count).c_str());
		}
		LNLog::prev_log_count =  0;
	}

	switch (type) {
	case DLOG_WARNING:
		if ((fd = get_log_file_descriptor(type))) {
			if(0 >= fprintf(fd, "WARNING [%s]: %s\n", timestr.c_str(), message)) {
				printf("ERROR: %s\n", strerror(errno));
			} else {
				fflush(fd);
			}
		} else {
			syslog(LOG_WARNING, "WARNING: %s", message);
		}
		break;
	case DLOG_ERROR:
		if ((fd = get_log_file_descriptor(type)) ){
			if(0 >= fprintf(fd, "ERROR [%s]: %s\n", timestr.c_str(), message)) {
				printf("ERROR: %s\n", strerror(errno));
			} else {
				fflush(fd);
			}
		} else {
			syslog(LOG_ERR, "ERROR: %s", message);
		}
		break;
	case DLOG_FATAL:
		if ( (fd = get_log_file_descriptor(type)) ) {
			if(0 >= fprintf(fd, "FATAL [%s]: %s\n", timestr.c_str(), message)) {
				printf("ERROR: %s\n", strerror(errno));
			} else {
				fflush(fd);
			}
		} else {
			syslog(LOG_CRIT, "FATAL: %s", message);
		}
		UNLOCK_LOG;
#ifdef _DEBUG
		assert(0);
#else
		exit(1);
#endif
		break;
	case DLOG_DEBUG:
		if ( (fd = get_log_file_descriptor(type)) ) {
			if(0 >= fprintf(fd, "DEBUG [%s]: %s\n", timestr.c_str(), message)) {
				printf("ERROR: %s\n", strerror(errno));
			} else {
				fflush(fd);
			}
		} else {
			syslog(LOG_WARNING, "DEBUG: %s", message);
		}
		break;
	default:
		if ( (fd = get_log_file_descriptor(type)) ) {
			if(0 >= fprintf(fd, "INFO [%s]: %s\n", timestr.c_str(), message)) {
				printf("ERROR: %s\n", strerror(errno));
			} else {
				fflush(fd);
			}
		} else {
			syslog(LOG_INFO, "INFO: %s", message);
		}
		break;
	}
	UNLOCK_LOG;
	return 0;
}


int LNLog::logInfo(const char *format, ...) {
	string message;
	int size = 256, rqsize = 0;
	va_list vargs;
	va_start (vargs, format);
	rqsize = _formatString (message, format, vargs, size);
	while(rqsize > size) {
		size = 2*rqsize;
		va_end(vargs);
		va_start(vargs, format);
		rqsize = _formatString (message, format, vargs, size);
	}
	va_end (vargs);
	return LNLog::log(LNLog::DLOG_INFO, message.c_str());
}

int LNLog::logWarning(const char *format, ...) {
	string message;
	int size = 256, rqsize = 0;
	va_list vargs;
	va_start (vargs, format);
	rqsize = _formatString (message, format, vargs, size);
	while(rqsize > size) {
		size = 2*rqsize;
		va_end(vargs);
		va_start(vargs, format);
		rqsize = _formatString (message, format, vargs, size);
	}
	va_end (vargs);
	return LNLog::log(LNLog::DLOG_WARNING, message.c_str());
}

int LNLog::logDebug(const char *format, ...) {
	if(debug_is_on) {
		string message;
		int size = 256, rqsize = 0;
		va_list vargs;
		va_start (vargs, format);
		rqsize = _formatString (message, format, vargs, size);
		while(rqsize > size) {
			size = 2*rqsize;
			va_end(vargs);
			va_start(vargs, format);
			rqsize = _formatString (message, format, vargs, size);
		}
		va_end (vargs);
		return LNLog::log(LNLog::DLOG_DEBUG, message.c_str());
	} else {
		return 0;
	}
}

int LNLog::logError(const char *format, ...) {
	string message;
	int size = 256, rqsize = 0;
	va_list vargs;
	va_start (vargs, format);
	rqsize = _formatString (message, format, vargs, size);
	while(rqsize > size) {
		size = 2*rqsize;
		va_end(vargs);
		va_start(vargs, format);
		rqsize = _formatString (message, format, vargs, size);
	}
	return LNLog::log(LNLog::DLOG_ERROR, message.c_str());
}

int LNLog::logFatal(const char *format, ...) {
	string message;
	int size = 256, rqsize = 0;
	va_list vargs;
	va_start (vargs, format);
	rqsize = _formatString (message, format, vargs, size);
	while(rqsize > size) {
		size = rqsize;
		va_end(vargs);
		va_start(vargs, format);
		rqsize = _formatString (message, format, vargs, size);
	}
	LNLog::log(LNLog::DLOG_FATAL, message.c_str());
	exit(1);
	return 0;
}

bool LNLog::setupLogFacility(LogFacility facility, FILE* fd)
{
	LOCK_LOG;
	switch (facility) {
	case FILE_LOG:
		if (!fd) {
			return false;
		}
		_close_log();
		LNLog::log_file_descriptor = fd;
		LNLog::log_facility = FILE_LOG;
		break;
	case STDOUT_LOG:
		_close_log();
		LNLog::log_to_stdout = true;
		LNLog::log_facility = STDOUT_LOG;
		break;
	default:
		_close_log();
		openlog(PROGRAM_NAME, LOG_PID | LOG_NDELAY | LOG_CONS, LOG_USER);
		LNLog::log_facility = SYSTEM_LOG;
		break;
	}
	UNLOCK_LOG;
	return true;
}

void LNLog::setDebugMode(bool debug) {
	LNLog::debug_is_on = debug;
}

void LNLog::_close_log() {
	switch (log_facility) {
	case FILE_LOG:
		fclose(LNLog::log_file_descriptor);
		LNLog::log_file_descriptor = 0;
		break;
	case STDOUT_LOG:
		LNLog::log_to_stdout = false;
		break;
	default:
		closelog();
		break;
	}
}

void LNLog::closeLog()
{
	LOCK_LOG;
	_close_log();
	UNLOCK_LOG;
}

LNLog::LogFacility LNLog::getLogFacility()
{
	return LNLog::log_facility;
}


//-----------------------------------------------------------------------------------------------
// LNException

LNException::LNException(const char *desc) throw() {
	m_desc = desc;
}

//---------------------------------------------------------------------------------------


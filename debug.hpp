#ifndef LOG_NOT_DEBUG_H
#define LOG_NOT_DEBUG_H

#include "lognot.hpp"

//----------------------------------------------------------------------------------------

class LNLog {
public:
	typedef enum LNLogType {
		DLOG_INFO=1,
		DLOG_WARNING,
		DLOG_ERROR,
		DLOG_FATAL,
		DLOG_DEBUG
	} Type;
	
	typedef enum LNLogFacility {
		STDOUT_LOG = 1,
		FILE_LOG,
		SYSTEM_LOG
	} LogFacility;
	
	static int log(LNLogType type, const char *message);
	static int logInfo(const char *format, ...);
	static int logWarning(const char *format, ...);
	static int logDebug(const char *format, ...);
	static int logError(const char *format, ...);
	static int logFatal(const char *format, ...);
	
	static bool setupLogFacility(LogFacility facility, FILE* fd=0);
	static LNLog::LogFacility getLogFacility();

	static void setDebugMode(bool debug);

	static void closeLog();
protected:
	static void _close_log();

public:
	static FILE *log_file_descriptor;
	static bool log_to_stdout, debug_is_on;
	static LogFacility log_facility;
	static unsigned int prev_log_crc;
	static unsigned int prev_log_count;
	static time_t prev_log_time;
};

//----------------------------------------------------------------------------------------

class LNException : public std::exception {
public:
	LNException(const char *desc) throw();
	virtual ~LNException() throw() { ; }

	virtual const  char *what() const throw() {
		return m_desc.c_str();
	}
private:
	std::string m_desc;
};

#endif // LOG_NOT_DEBUG_H

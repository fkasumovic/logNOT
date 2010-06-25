#ifndef LOG_NOT_CORE_H
#define LOG_NOT_CORE_H

#include "lognot.hpp"

typedef int(*lnprocessCallback)(void *pargs);

typedef void*(*lnthreadCallback)(void *pargs);

//---------------------------------------------------------------------------------------

class LNMutex {
	friend class LNMutexLocker;
public:
	LNMutex();
	virtual ~LNMutex();
	
	bool lock() const;
	bool unlock() const;

	bool tryLock() const;

	static void disable();
	static void enable();
	static bool enabled();
	
protected:
	pthread_mutex_t m_mtx;
	static bool m_enabled;
};

class LNDisableMutex {
	LNDisableMutex() {
		LNMutex::disable();
	}
	~LNDisableMutex() {
		LNMutex::enable();
	}
};

//---------------------------------------------------------------------------------------

class LNCondition : protected LNMutex {
public:
	LNCondition();
	~LNCondition();

	bool wait();
	bool timedWait(int ms);
	bool signal();
	bool broadcast();
private:
	pthread_cond_t m_cond;
};

//---------------------------------------------------------------------------------------

class LNMutexLocker {
public:
	LNMutexLocker(const LNMutex &mtx);
	~LNMutexLocker();
protected:
	const LNMutex *pmtx;
};

//---------------------------------------------------------------------------------------

class LNProcess {
public:
	LNProcess();
	virtual ~LNProcess();
	
	bool fork(lnprocessCallback mainFnc, void *pargs);
	bool terminate();
	bool wait();
	bool isRunning();
	bool signal(int signal);
	pid_t getPID();
	pid_t getSID();

	bool recordPID();
	bool removePID();
	
	std::string getPIDFilePath();

	void setPIDFilePath(const std::string &path);
	
private:
	pid_t m_pid, m_sid;
	std::string m_pidPath;
};

//---------------------------------------------------------------------------------------

typedef struct {
	lnthreadCallback mainFnc;
	void *pargs;
	LNMutex *prunning;
} internal_thread_args;

class LNThread {
public:
	LNThread();
	~LNThread();
	
	bool run(lnthreadCallback mainFnc, void *pargs);
	bool wait();
	bool isRunning();

	/* TO-DO: Implement cancel and mechanism for terminating thread
	   */
	
private:
	LNMutex m_running;
	pthread_t m_thread;
	internal_thread_args *m_iargs;
};

//---------------------------------------------------------------------------------------

class LNIniSection : public std::map<std::string, std::string> {
public:
	LNIniSection() {
		this->clear();
	}
	LNIniSection(const char *section_name);
	virtual ~LNIniSection();

	std::string getName() const;
	void setName(const char *sectionName);
private:
	std::string m_name;
};

//---------------------------------------------------------------------------------------

/*! This class can load whole ini file into intuitive to use collection.
 *  Example:
 *      std::string item_value = inifile["section_name"]["item_name"];
 *  Primary use in this project is to load and read current asterisk settings.
 *
 *  Class is not thread safe, when used Lock and Unlock methods must be called.
 *  Or use LNMutexLocker for simple automatic lock/unlock of critical sections.
 */
class LNIniFile : public std::map<std::string, LNIniSection>, public LNMutex {
public:
	LNIniFile();
	virtual ~LNIniFile();
	
	bool parse(std::string filePath);
	bool parseBuffer(const char *pbuf);
	std::string getFilePath();
	
private:
	void  parseLine(std::string &line, std::string &curSection, std::string &match,
			std::string &filePath, long lineNumber,
			void *pcomment, void *psection, void *pitem);
	std::string m_filePath;
	void *m_pComment, *m_pItem, *m_pSection;
};

extern LNIniFile g_mainConfig;

//---------------------------------------------------------------------------------------
/*! This class is to provide way to keep all global values in single 
 * and concurrent safe place.
 */
class LNGlobals : public LNMutex {
public:
	LNGlobals() { ; }
	virtual ~LNGlobals() { ; }

	static std::string get(const std::string &key);
	static void set(const std::string &key, const std::string &value);
	static size_t size();
	static bool isSet(const std::string &key);
protected:
	static std::map<std::string, std::string> m_storage;
	static LNMutex m_mtxGlobals;
};

//---------------------------------------------------------------------------------------

#endif // LOG_NOT_CORE_H

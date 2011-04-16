#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pcre.h>

#include "debug.hpp"
#include "utility.hpp"
#include "core.hpp"

const int OVCOUNT=16;
const char *MATCH_ERR_FMT = "PCRE matching error (%d). [line: %ld  file: %s]";

LNIniFile g_mainConfig;

std::map<std::string, std::string> LNGlobals::m_storage;
LNMutex LNGlobals::m_mtxGlobals;

bool LNMutex::m_enabled = true;

//---------------------------------------------------------------------------------------
// LNProcess

LNProcess::LNProcess() {
	m_pid = 0;
	if (!LNGlobals::isSet(PROGRAM_NAME)) {
		LNGlobals::set(PROGRAM_NAME, PROGRAM_NAME);
	}
	if (!LNGlobals::isSet(PID_FILES_LOCATION)) {
		LNGlobals::set(PID_FILES_LOCATION, PID_FILES_LOCATION);
	}
}

LNProcess::~LNProcess() {

}

bool LNProcess::fork(lnprocessCallback mainFnc, void *pargs) {
	if (m_pid > 0) {
		terminate();
	}

	m_pid = ::fork();

	if (m_pid < 0) {
		LNLog::logError("Failed to fork child process. line: %d  file: %s",
				__LINE__, __FILE__);
		return false;
	} else if(!m_pid) { // this is child process
		m_pid = getpid();

		this->recordPID();

		/* Change the file mode mask */
		umask(0);
		/* Create a new SID for the child process */
		m_sid = setsid();
		if (m_sid < 0) {
			/* Log any failure here */
			LNLog::logFatal("Unable to set child process session id.");
		}
		mainFnc(pargs);

		this->removePID();
	}
	return true;
}

bool LNProcess::terminate() {
	if (m_pid > 0) {
		signal(SIGKILL);
		removePID();
		return wait();
	}
	return false;
}

bool LNProcess::wait() {
	if (m_pid > 0) {
		pid_t wpid = waitpid(m_pid, NULL, 0);
		m_pid = 0;
		return (wpid > 0);
	}
	return false;
}

bool LNProcess::isRunning() {
	if (m_pid > 0) {
		int status = 0;
		pid_t wpid = waitpid(m_pid, &status, WNOHANG);
		if (wpid > 0) {
			return !(WIFEXITED(status) | WIFSIGNALED(status) |
				 WCOREDUMP(status) );
		}
		m_pid = 0;
	}
	return false;
}

bool LNProcess::signal(int signal) {
	if (m_pid > 0) {
		return !kill(m_pid, signal);
	}
	return false;
}

pid_t LNProcess::getPID() {
	return m_pid;
}

pid_t LNProcess::getSID() {
	return m_sid;
}

bool LNProcess::recordPID() {
	if (m_pid > 0) {
		if (m_pidPath.length() <= 0) {
			std::string pid_file_name;
			pid_file_name = 
				 LNUtility::formatString("%s.pid",
						LNGlobals::get(PROGRAM_NAME).c_str());
			m_pidPath =
			LNUtility::trimString(LNGlobals::get(PID_FILES_LOCATION));

			if ( m_pidPath[m_pidPath.length() -1] != PATH_SEPARATOR ) {
				m_pidPath += PATH_SEPARATOR + pid_file_name;
			} else {
				m_pidPath += pid_file_name;
			}
		}
		LNLog::logInfo("Recording PID >%s< ... ", m_pidPath.c_str());
		FILE *fp = fopen(m_pidPath.c_str(), "w");
		if (fp) {
			fprintf(fp, "%ld\n", (long)m_pid);
			fclose(fp);
			return true;
		}
		LNLog::logFatal("Failed to record pid file  '%s'. "
				"%s [line: %ld  file: %s]",
				m_pidPath.c_str(), strerror(errno), __LINE__, __FILE__);
	}
	return false;
}

bool LNProcess::removePID() {
	if (remove(m_pidPath.c_str())) {
		LNLog::logError("Failed to remove pid file  '%s'. "
				"%s [line: %ld  file: %s]",
				m_pidPath.c_str(), strerror(errno), __LINE__, __FILE__);
		return false;
	}
	m_pidPath = "";
	return true;
}

std::string LNProcess::getPIDFilePath() {
	return m_pidPath;
}

void LNProcess::setPIDFilePath(const std::string &path) {
	m_pidPath = path;
}

//---------------------------------------------------------------------------------------
// LNThread

void *internal_thread_fnc(void *pargs) {
	void *pret = NULL;

	internal_thread_args *iargs = (internal_thread_args*)pargs;
	LNMutexLocker lock(*(iargs->prunning));

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	pret = iargs->mainFnc(iargs->pargs);

	delete[] iargs;

	pthread_exit(pret);
	return pret;
}

LNThread::LNThread() {
	m_thread = 0;
	m_iargs = NULL;
}

LNThread::~LNThread() {
	if(isRunning()) {
		this->wait();
	}
}

bool LNThread::run(lnthreadCallback mainFnc, void *pargs) {
	m_iargs = new internal_thread_args;
	m_iargs->mainFnc = mainFnc;
	m_iargs->pargs = pargs;
	m_iargs->prunning = &m_running;
	int r = pthread_create(&m_thread, NULL, internal_thread_fnc, m_iargs);
	if (r != 0) {
		delete[] m_iargs;
		m_iargs = NULL;
		LNLog::logError("Failed to create thread. %s "
				"[error_code: %d  line: %d file: %s]", 
				strerror(r), r, __LINE__, __FILE__);
		return false;
	}
	return true;
}

bool LNThread::wait() {
	int r = pthread_join(m_thread, NULL);
	if (r != 0) {
		LNLog::logError("Failed to wait thread. %s "
				"[error_code: %d  line: %d file: %s]", 
				strerror(r), r, __LINE__, __FILE__);
		return false;
	}
	return true;
}

bool LNThread::isRunning() {
	if (!m_running.tryLock()) {
		return true;
	}
	m_running.unlock();
	return false;
}

void LNThread::terminate() {
	if (isRunning()) {
		pthread_cancel(m_thread);
	}
}

//---------------------------------------------------------------------------------------
// LNMutex

LNMutex::LNMutex() {
	//PTHREAD_MUTEX_RECURSIVE
	int r = 0;
	pthread_mutexattr_t mtxAttr;
	r = pthread_mutexattr_init(&mtxAttr);
	if (r != 0) {
		LNLog::logFatal("Failed mutex attributes init. %s "
				"[error_code: %d  line: %d file: %s]",
				strerror(r), r, __LINE__, __FILE__);
		return;
	}
	r = pthread_mutexattr_settype(&mtxAttr, PTHREAD_MUTEX_RECURSIVE);
	if (r != 0) {
		LNLog::logFatal("Failed mutex attributes settype. %s "
				"[error_code: %d  line: %d file: %s]",
				strerror(r), r, __LINE__, __FILE__);
		pthread_mutexattr_destroy(&mtxAttr);
		return;
	}
	r = pthread_mutex_init(&m_mtx, &mtxAttr);
	if (r != 0) {
		LNLog::logFatal("Failed mutex init. %s [error_code: %d  line: %d file: %s]",
				strerror(r), r, __LINE__, __FILE__);
		pthread_mutexattr_destroy(&mtxAttr);
		return;
	}
	r = pthread_mutexattr_destroy(&mtxAttr);
	if (r != 0) {
		LNLog::logFatal("Failed mutex attributes destroy. %s "
				"[error_code: %d  line: %d file: %s]",
				strerror(r), r, __LINE__, __FILE__);
		pthread_mutexattr_destroy(&mtxAttr);
		return;
	}
}

LNMutex::~LNMutex() {
	int r = pthread_mutex_destroy(&m_mtx);
	if (r != 0) {
		LNLog::logError("Failed mutex destroy. %s "
				"[error_code: %d  line: %d file: %s]",
				strerror(r), r, __LINE__, __FILE__);
	}
}

bool LNMutex::lock() const {
	if(!LNMutex::m_enabled) { // only for signal handlers
		return true;
	}
#ifdef _DEBUG
	struct timespec lockTimeout;
	clock_gettime(CLOCK_REALTIME, &lockTimeout);
	lockTimeout.tv_sec += 10;
	lockTimeout.tv_nsec = 0;
	int r = pthread_mutex_timedlock((pthread_mutex_t*)&m_mtx, &lockTimeout);
	if (r == ETIMEDOUT) {
		LNLog::logDebug("Deadlock detected. %s.", strerror(ETIMEDOUT));
		assert(0); // dump core here
	}
#else
	int r = pthread_mutex_lock((pthread_mutex_t*)&m_mtx);
#endif
	if (r != 0) {
		LNLog::logError("Failed mutex lock. %s [error_code: %d  line: %d file: %s]",
				strerror(r), r, __LINE__, __FILE__);
		return false;
	}
	return true;
}

bool LNMutex::unlock() const {
	if(!LNMutex::m_enabled) { // only for signal handlers
		return true;
	}
	int r = pthread_mutex_unlock((pthread_mutex_t*)&m_mtx);
	if (r != 0) {
		LNLog::logFatal("Failed mutex unlock. %s [error_code: %d  line: %d file: %s]",
				strerror(r), r, __LINE__, __FILE__);
		return false;
	}
	return true;
}

bool LNMutex::tryLock() const {
	if(!LNMutex::m_enabled) { // only for signal handlers
		return true;
	}
	int r = pthread_mutex_trylock((pthread_mutex_t*)&m_mtx);
	if(r == EBUSY) {
		return false;
	} else if (r != 0) {
		LNLog::logError("Failed mutex try lock. %s "
				"[error_code: %d line: %d file: %s]", 
				strerror(r), r, __LINE__, __FILE__);
		return false;
	}
	return true;
}

void LNMutex::enable() {
	m_enabled = true;
}

void LNMutex::disable() {
	m_enabled = false;
}

bool LNMutex::enabled() {
	return m_enabled;
}

//---------------------------------------------------------------------------------------
// LNCondition

LNCondition::LNCondition() {
	int r = pthread_cond_init(&m_cond, NULL);
	if (r) {
		LNLog::logError("Failed condition init %s [error_code: %d line: %d file: %s]",
				strerror(r), r, __LINE__, __FILE__);
	}
}

LNCondition::~LNCondition() {
	int r = pthread_cond_destroy(&m_cond);
	if(r) {
		LNLog::logError("Failed condition destroy. %s "
				"[error_code: %d line: %d file: %s]", 
				strerror(r), r, __LINE__, __FILE__);
	}
}

bool LNCondition::wait() {
	int r = 0;
	this->lock();
	r = pthread_cond_wait(&m_cond, &m_mtx);
	this->unlock();
	if (r) {
		LNLog::logError("Failed condition wait. %s "
				"[error_code: %d  line: %d file: %s]", 
				strerror(r), r, __LINE__, __FILE__);
		return false;
	}
	return true;
}

bool LNCondition::timedWait(int ms) {
	int r = 0;
	struct timespec timeout;
	timeout.tv_sec = long(ms/1e+3);
	timeout.tv_nsec = long((ms - timeout.tv_sec)*1e+6);

	this->lock();
	r = pthread_cond_timedwait(&m_cond, &m_mtx, &timeout);
	this->lock();
	if ( r && r != ETIMEDOUT) {
		LNLog::logError("Failed condition wait. %s "
				"[error_code: %d  line: %d file: %s]", 
				strerror(r), r, __LINE__, __FILE__);
	}
	return !r;
}

bool LNCondition::signal() {
	int r = 0;
	this->lock();
	r = pthread_cond_signal(&m_cond);
	this->unlock();
	if (r) {
		LNLog::logError("Failed condition signal. %s "
				"[error_code: %d  line: %d file: %s]", 
				strerror(r), r, __LINE__, __FILE__);
		return false;
	}
	return true;
}

bool LNCondition::broadcast() {
	int r = 0;
	this->lock();
	r = pthread_cond_broadcast(&m_cond);
	this->unlock();
	if (r) {
		LNLog::logError("Failed condition signal. %s "
				"[error_code: %d  line: %d file: %s]", 
				strerror(r), r, __LINE__, __FILE__);
		return false;
	}
	return true;
}

//---------------------------------------------------------------------------------------
// LNMutexLocker

LNMutexLocker::LNMutexLocker(const LNMutex &mtx) {
	pmtx = &mtx;
	mtx.lock();
}

LNMutexLocker::~LNMutexLocker() {
	pmtx->unlock();
}

//---------------------------------------------------------------------------------------
// LNIniSection

LNIniSection::LNIniSection(const char * sectionName) {
	this->setName(sectionName);
}

LNIniSection::~LNIniSection() {
	this->clear();
}

std::string LNIniSection::getName() const {
	return m_name;
}

void LNIniSection::setName(const char *sectionName) {
	m_name = sectionName;
}

//---------------------------------------------------------------------------------------
// LNIniFile

LNIniFile::LNIniFile() {
	const char *errMsg = "No error.";
	const char *errMsgFmt =
		"Failed to compile PCRE expression at offset %d. %s [line: %ld  file: %s]";
	int errOffset = 0;

	m_pComment = (void*)pcre_compile("^([ \t]*[;#])", 0, &errMsg, &errOffset, NULL);
	if (!m_pComment) {
		LNLog::logFatal(errMsgFmt, errOffset, errMsg, __LINE__, __FILE__);
	}

	m_pSection = (void*)pcre_compile("^[\t ]*\\[[\t ]*([^\\] \t]*)[\t ]*\\][\t ]*$", 
				       0, &errMsg, &errOffset, NULL);
	if (!m_pSection) {
		LNLog::logFatal(errMsgFmt, errOffset, errMsg, __LINE__, __FILE__);
	}

	m_pItem = (void*)pcre_compile("^([^=^\t]+)[\t ]*=[\t ]*(.+)$", 
				    0, &errMsg, &errOffset, NULL);
	if (!m_pItem) {
		LNLog::logFatal(errMsgFmt, errOffset, errMsg, __LINE__, __FILE__);
	}
}

LNIniFile::~LNIniFile() {
	// Release memory used for the compiled patterns
	pcre_free((pcre*)m_pComment);
	pcre_free((pcre*)m_pItem);
	pcre_free((pcre*)m_pSection);
	this->clear();
}

std::string LNIniFile::getFilePath() {
	return this->m_filePath;
}

void LNIniFile::parseLine(std::string &line, std::string &curSection, std::string &match,
			  std::string &filePath, long lineNumber,
			  void *pcomment, void *psection, void *pitem) {
	int ovector[OVCOUNT], pcreResult = 0;
	pcre *pcreComment 	= (pcre*)pcomment;
	pcre *pcreSection 	= (pcre*)psection;
	pcre *pcreItem		= (pcre*)pitem;
	size_t lineLength = line.find_first_not_of(STD_TRIM_CHAR_SET);
	if (lineLength != std::string::npos) {
		line = line.substr(lineLength,
				   line.find_last_not_of(STD_TRIM_CHAR_SET) - lineLength + 1);
	} else {
		line = "";
	}

	if (!(lineLength = line.size())) {
		return; // skip empty lines
	}

	pcreResult = pcre_exec(pcreComment, NULL, line.c_str(), lineLength,
				0, 0, ovector, OVCOUNT);
	if (pcreResult >= 0) {
		return; // skip comments
	} else if (pcreResult != PCRE_ERROR_NOMATCH) {
		LNLog::logWarning(MATCH_ERR_FMT, pcreResult, __LINE__, __FILE__);
	}

	pcreResult = pcre_exec(pcreSection, NULL, line.c_str(),
				lineLength, 0, 0, ovector, OVCOUNT);
	if (pcreResult >= 0) {
		match = line.substr(ovector[2], ovector[3]-ovector[2]);
		match = LNUtility::trimString(match);
		if (!count(match)) {
			(*this)[match].setName(match.c_str());
		}
		curSection = match;
		return;
	} else if (pcreResult != PCRE_ERROR_NOMATCH) {
		LNLog::logWarning(MATCH_ERR_FMT, pcreResult, __LINE__, __FILE__);
	}

	pcreResult = pcre_exec(pcreItem, NULL, line.c_str(),
				lineLength, 0, 0, ovector, OVCOUNT);
	if (pcreResult >= 0) {
		match =  line.substr(ovector[2], ovector[3] - ovector[2]);
		match = LNUtility::trimString(match);
		(*this)[curSection][match] = line.substr(ovector[4], ovector[5] - ovector[4]);
		return;
	} else if (pcreResult != PCRE_ERROR_NOMATCH) {
		LNLog::logWarning(MATCH_ERR_FMT, pcreResult, __LINE__, __FILE__);
	} else {
		LNLog::logWarning("Malformed ini file '%s' entry at line '%ld'. "
				  "[line: %ld  file: %s]",  filePath.c_str(), lineNumber,
				  __LINE__, __FILE__);
	}
}

bool LNIniFile::parse(std::string filePath) {
	long lineNumber = 0;
	char lineBuf[MAX_INI_FILE_LINE_LENGTH];
	FILE *pf = NULL;
	std::string line, match, curSection = CONF_SECTION_GENERAL;

	this->clear();

	(*this)[curSection].setName(curSection.c_str());

	pf = fopen(filePath.c_str(), "r");
	if (!pf) {
		LNLog::logError("Failed to open file for reading "\
				"'%s'. %s [line: %ld  file: %s]", 
				filePath.c_str(), strerror(errno), __LINE__, __FILE__);
		return false;
	}
	while (fgets(lineBuf, MAX_INI_FILE_LINE_LENGTH, pf)) {
		lineNumber++;
		line = lineBuf;
		this->parseLine(line, curSection, match, filePath, lineNumber,
				m_pComment, m_pSection, m_pItem);
	}
	this->m_filePath = filePath;
	fclose(pf);
	return true;
}

bool LNIniFile::parseBuffer(const char *pbuf) {
	const char *lineSep = "\r\n";
	const char *lineBuf = NULL;
	char *tokp = NULL;
	char *tmp = NULL;
	long lineNumber = 0;
	size_t bufSize = 0;
	std::string filePath = "_Memory_Buffer_";

	if (!pbuf) {
		LNLog::logWarning("Passed NULL buffer to LNIniFile::parseBuffer. "
				  "[file: %s  line: %d]",
				  __FILE__, __LINE__);
		return false;
	}
	bufSize = strlen(pbuf);
	if (bufSize > 0) {
		tmp = new char[bufSize];
		strcpy(tmp, pbuf);
	} else {
		return true;
	}

	std::string line, match, curSection = CONF_SECTION_GENERAL;

	(*this)[curSection].setName(curSection.c_str());

	lineBuf = strtok_r(tmp, lineSep, &tokp);
	while(lineBuf) {
		lineNumber++;
		line = lineBuf;
		this->parseLine(line, curSection, match, filePath, lineNumber,
				m_pComment, m_pSection, m_pItem);
		lineBuf = strtok_r(NULL, lineSep, &tokp);
	}
	this->m_filePath = filePath;
	delete[] tmp;

	return true;
}

//---------------------------------------------------------------------------------------
// LNGlobals

std::string LNGlobals::get(const std::string &key) {
	LNMutexLocker lock(m_mtxGlobals);
	std::map<std::string, std::string>::iterator i;

	if ((i = m_storage.find(key)) != m_storage.end()) {
		return i->second;
	}
	return "";
}

void LNGlobals::set(const std::string &key, const std::string &value) {
	LNMutexLocker lock(m_mtxGlobals);
	m_storage[key] = value;
}

size_t LNGlobals::size() {
	LNMutexLocker lock(m_mtxGlobals);
	return m_storage.size();
}

bool LNGlobals::isSet(const std::string &key) {
	LNMutexLocker lock(m_mtxGlobals);
	return (m_storage.count(key) > 0);
}

//---------------------------------------------------------------------------------------

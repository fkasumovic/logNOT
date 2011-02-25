#include "debug.hpp"
#include "monitor.hpp"
#include "utility.hpp"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>

#include <unistd.h>
#include <fcntl.h>
#include <pcre.h>
#include <libgen.h>

#define MAX_OVCOUNT	256

#define TRASH_HOLD_SIZE	2048
#define MAX_LOG_ITEM_SIZE	4096

#define MAX_EVENTS_PER_TURN 100

#define EVENT_SIZE  (sizeof (struct inotify_event))
#define EVENT_BUF_LEN        (1024 * (EVENT_SIZE + 16))

void *LNActionPreprocessor::m_pcre = NULL;

std::map<int , std::string> ineMap;
//----------------------------------------------------------------------------

LNEventQueue::~LNEventQueue() {
	while(!empty()) {
		pop();
	}
}

LNFreq::LNFreq() {
	this->m_count = 0;
	this->m_time = 1;
}

LNFreq::LNFreq(const std::string &freq) {
	this->SetFromString(freq);
}

LNFreq::LNFreq(const std::string &count, const std::string &time) {
	this->setCount(count);
	this->setTimePeriod(time);
}

LNFreq::LNFreq(unsigned long count, unsigned long time) {
	this->setCount(count);
	this->setTimePeriod(time);
}

void LNFreq::setCount(const std::string &count) {
	this->m_count = atoi(count.c_str());
}

void LNFreq::setTimePeriod(const std::string &time) {
	this->m_time = atoi(time.c_str());
}

std::string LNFreq::ToString() const {
	return LNUtility::formatString("%d/%d", m_count, m_time);
}

bool LNFreq::SetFromString(const std::string &freq) {
	std::vector<std::string> params;
	LNUtility::explodeString(params, freq, "/");
	if(params.size() < 2) {
		LNLog::logWarning("Invalid log frequency format.");
		return false;
	}
	m_count = atoi(params[0].c_str());
	m_time = atoi(params[1].c_str());
	return true;
}

void LNFreq::pushEvent(unsigned int crc) {
	time_t currTime = time(NULL);
	std::map<unsigned int, LNEventQueue*>::iterator i =
			m_events.find(crc);
	LNEventQueue *pQueue = (i != m_events.end())?i->second:NULL;
	if(!pQueue) {
		pQueue = new LNEventQueue();
		if(!pQueue) {
			LNLog::logError("No enough memory. [ line: %d  file: %s ]",
					__LINE__, __FILE__);
			return;
		}
		m_events[crc] = pQueue;
	}
	// check hard limit (to prevent high memory usage in case of frequent events)
	unsigned long hard_limit = m_count + 10;
	if(pQueue->size() > hard_limit) {
		hard_limit = pQueue->size() - hard_limit;
		for(unsigned long i=0; i < hard_limit; i++) {
			pQueue->pop();
		}
	}
	pQueue->push(currTime);
}

void LNFreq::removeDeprecated(time_t currTime) {
	time_t deprTime = currTime - m_time;
	LNEventQueue *pQueue = NULL;
	std::map<unsigned int, LNEventQueue*>::iterator i, prev;
	for (prev = i = m_events.begin(); i != m_events.end(); i++) {
		pQueue = i->second;
		while ((pQueue->size() > 0) && (pQueue->front() < deprTime)) {
			pQueue->pop();
		}
		if (! (pQueue->size()) ) {
			delete pQueue;
			m_events.erase(i);
			if(i != prev) {
				i = m_events.begin();
			}
		}
		prev = i;
	}
}

unsigned long LNFreq::getMessuringCount(unsigned int crc) const {
	unsigned long c = 0;
	LNEventQueue *pQueue = NULL;
	if (!crc) {
		std::map<unsigned int, LNEventQueue*>::const_iterator i;
		for (i = m_events.begin(); i != m_events.end(); i++) {
			c += i->second->size();
		}
	} else {
		pQueue = m_events.find(crc)->second;
		if(!pQueue) {
			return 0;
		}
		c = pQueue->size();
	}
	return c;
}

void LNFreq::resetMessuring(unsigned int crc) {
	LNEventQueue *pQueue = NULL;
	if (crc) {
		pQueue = m_events.find(crc)->second;
		if (!pQueue) {
			return;
		}
		while (!(pQueue->empty())) {
			pQueue->pop();
		}
	} else {
		std::map<unsigned int, LNEventQueue*>::const_iterator i;
		for (i = m_events.begin(); i != m_events.end(); i++) {
			pQueue = i->second;
			while (!(pQueue->empty())) {
				pQueue->pop();
			}
		}
	}
}

//----------------------------------------------------------------------------

LNLogItem::LNLogItem(
		const std::string &name,
		const std::string &regex,
		const std::string &up_bound_action,
		const std::string &down_bound_action,
		const std::string &sizeAction,
		const LNFreq &up_bound,
		const LNFreq &down_bound,
		size_t size,
		bool usecrc
	) {

	this->m_name = name;
	this->m_regex = regex;
	this->m_upbaction = up_bound_action;
	this->m_downbaction = down_bound_action;
	this->m_sizeAction = sizeAction;
	this->m_upfreq = up_bound;
	this->m_downfreq = down_bound;
	this->m_usecrc = usecrc;
	this->m_size = size;

	m_quotient = 0;
	// compile PCRE
	const char *errmsg = "No error.";
	const char *errmsgfmt = 
		"Failed to compile PCRE expression at offset %d. %s [line: %ld  file: %s]";
	int erroffset = 0;

	LNLog::logDebug("LogItem: %s | RegEx: %s", name.c_str(), regex.c_str());

	this->m_pcre = (void*)pcre_compile(regex.c_str(), PCRE_NEWLINE_ANYCRLF, &errmsg, &erroffset, NULL);
	if (!m_pcre) {
		LNLog::logFatal(errmsgfmt, erroffset, errmsg, __LINE__, __FILE__);
	}
}

LNLogItem::~LNLogItem() {
	LNMutexLocker lock(*this);
	// Release memory used for the compiled patterns
	pcre_free((pcre*)m_pcre);
}

std::string LNLogItem::getName() {
	LNMutexLocker lock(*this);
	return this->m_name;
}

std::string LNLogItem::getRegEx() {
	LNMutexLocker lock(*this);
	return this->m_regex;
}

std::string LNLogItem::getUpBoundAction() {
	LNMutexLocker lock(*this);
	return this->m_upbaction;
}

std::string LNLogItem::getDownBoundAction() {
	LNMutexLocker lock(*this);
	return this->m_downbaction;
}

const LNFreq &LNLogItem::getUpBoundFreq() {
	LNMutexLocker lock(*this);
	return m_upfreq;
}

const LNFreq &LNLogItem::getDownBoundFreq() {
	LNMutexLocker lock(*this);
	return m_downfreq;
}

size_t LNLogItem::getSize() {
	LNMutexLocker lock(*this);
	return m_size;
}

std::string LNLogItem::getSizeAction() {
	LNMutexLocker lock(*this);
	return m_sizeAction;
}

int LNLogItem::Match(const std::string &log, std::vector<std::string> &matches) {
	LNMutexLocker lock(*this);
	int ovector[MAX_OVCOUNT], pcre_result = 0;
	matches.clear();
	pcre_result = pcre_exec((const pcre*)m_pcre, NULL, log.c_str(), 
				log.length(), 0, 0, ovector, MAX_OVCOUNT);
	if(pcre_result >= 0) {
		for(int m = 0, mc = 0; mc < pcre_result; m+=2, mc++) {
			matches.push_back(
					log.substr(ovector[m], ovector[m+1] - ovector[m]) );
		}
		unsigned int crc = 0;
		if(m_usecrc) {
			crc = LNUtility::crci(matches[0]);
		}
		m_upfreq.pushEvent(crc);
		m_downfreq.pushEvent(0);
	} else if (pcre_result != PCRE_ERROR_NOMATCH) {
		LNLog::logWarning("Log match error (%d). LogRule: %s.",
				  pcre_result, m_name.c_str());
	}
	return pcre_result;
}

bool LNLogItem::upBoundFreqExcess(const std::string &match) {
	LNMutexLocker lock(*this);
	unsigned int crc = m_usecrc?LNUtility::crci(match):0;
	return (m_upfreq.getMessuringCount(crc) > m_upfreq.getCount());
}

bool LNLogItem::downBoundFreqExcess() {
	LNMutexLocker lock(*this);
	return (m_downfreq.getMessuringCount(0) < m_downfreq.getCount());
}

size_t LNLogItem::getSizeQuotient() {
	LNMutexLocker lock(*this);
	return m_quotient;
}

void LNLogItem::setSizeQuotient(size_t q) {
	LNMutexLocker lock(*this);
	m_quotient = q;
}

bool LNLogItem::sizeExcess(size_t size) {
	LNMutexLocker lock(*this);
	if (!m_size) {
		return false;
	}
	size_t cur_q = size/m_size;
	if (cur_q > m_quotient) {
		m_quotient = cur_q;
		return true;
	}
	m_quotient = cur_q;
	return false;
}

//----------------------------------------------------------------------------------------

std::string LNActionPreprocessor::run(const std::string &action, const std::string &line,
				      const std::vector<std::string> &matches) {
	std::string result = action;

	// is regular expression compiled already?
	if(!m_pcre) {
		const char *errmsg = "No error.";
		const char *errmsgfmt =
			"Failed to compile PCRE expression at offset %d."
			" %s [line: %ld  file: %s]";
		int erroffset = 0;
		m_pcre = (void*)pcre_compile("[^\\\\](\\$[0-9@]+)", PCRE_NEWLINE_ANYCRLF,
					     &errmsg, &erroffset, NULL);
		if (!m_pcre) {
			LNLog::logFatal(errmsgfmt, erroffset, errmsg, __LINE__, __FILE__);
		}
	}
	int ovector[MAX_OVCOUNT], pcre_result = 0;
	memset(ovector, 0, sizeof(ovector));
	pcre_result = pcre_exec((const pcre*)m_pcre, NULL, result.c_str(),
				result.length(), 0, PCRE_NEWLINE_ANYCRLF,
				ovector, MAX_OVCOUNT);
	std::string smi;
	while(pcre_result > 1) {
		for(int m = 2, mc = 1; mc < pcre_result; m+=2, mc++) {
			std::string mstr = result.substr(ovector[m],
							 ovector[m+1] -
							 ovector[m]).c_str();
			smi = result.substr(ovector[m]+1, ovector[m+1] - (ovector[m]+1));
			size_t mi = 0;
			if(smi != "@") {
				mi = atoi(smi.c_str());
			} else {
				result = LNUtility::replaceString(mstr, line,
								  result);
				continue;
			}
			if(!(mi >= matches.size())) {
				result = LNUtility::replaceString(mstr, matches[mi],
								  result);
			} else {
				result = LNUtility::replaceString(
						result.substr(ovector[m],
							      ovector[m+1] - ovector[m]),
						" ", result);
			}
		}
		memset(ovector, 0, sizeof(ovector));
		pcre_result = pcre_exec((const pcre*)m_pcre, NULL, result.c_str(),
					result.length(), 0, PCRE_NEWLINE_ANYCRLF, ovector,
					MAX_OVCOUNT);
	}
	if (pcre_result < 0 && pcre_result != PCRE_ERROR_NOMATCH) {
		LNLog::logWarning("RegEx match error (%d). LogRule: Action preprocessor.",
				  pcre_result);
	}
	return LNUtility::replaceString("\\$", "$", result);
}

//---------------------------------------------------------------------------------------

LNLogItems::LNLogItems() {
	// position at beginning of initial empty position in case
	// someone tries to iterate
	m_i = m_by_name.begin();
}

LNLogItems::~LNLogItems() {
	this->removeAll();
}

bool LNLogItems::add(LNLogItem *pli) {
	LNMutexLocker lock(*this);
	if(pli) {
		m_by_name[pli->getName()] = pli;
	}
	return false;
}

LNLogItem* LNLogItems::add(
		const std::string &name,
		const std::string &regex,
		const std::string &up_bound_action,
		const std::string &down_bound_action,
		const std::string &sizeAction,
		const LNFreq &up_bound,
		const LNFreq &down_bound,
		const size_t size,
		bool usecrc
	) {
	LNMutexLocker lock(*this);
	LNLogItem *pli = new LNLogItem(
		name,
		regex,
		up_bound_action,
		down_bound_action,
		sizeAction,
		up_bound,
		down_bound,
		size,
		usecrc
		);

	if(!pli) {
		LNLog::logError("No enough memory. [line: %ld  file: %s]", __LINE__, __FILE__);
	}
	return pli;
}

LNLogItem *LNLogItems::find(const std::string &name) {
	LNMutexLocker lock(*this);
	std::map<std::string, LNLogItem *>::iterator res = m_by_name.find(name);
	if(res != m_by_name.end()) {
		return res->second;
	}
	return NULL;
}

LNLogItem * LNLogItems::remove(const std::string &name) {
	LNMutexLocker lock(*this);
	LNLogItem *pli;
	std::map<std::string, LNLogItem *>::iterator res = m_by_name.find(name);
	if(res != m_by_name.end()) {
		pli = res->second;
		m_by_name.erase(res);
		return pli;
	}
	return NULL;
}

bool LNLogItems::removeAndDelete(const std::string &name) {
	LNMutexLocker lock(*this);
	LNLogItem *pli;
	std::map<std::string, LNLogItem *>::iterator res = m_by_name.find(name);
	if(res != m_by_name.end()) {
		pli = res->second;
		m_by_name.erase(res);
		delete pli;
		return true;
	}
	return false;
}

bool LNLogItems::removeAll() {
	LNMutexLocker lock(*this);
	LNLogItem *pli = NULL;
	for(m_i = m_by_name.begin(); m_i != m_by_name.end(); ) {
		pli = m_i->second;
		m_by_name.erase(m_i);
		m_i = m_by_name.begin();
	}
	return true;
}

bool  LNLogItems::removeAndDeleteAll() {
	LNMutexLocker lock(*this);
	LNLogItem *pli = NULL;
	for(m_i = m_by_name.begin(); m_i != m_by_name.end(); ) {
		pli = m_i->second;
		m_by_name.erase(m_i);
		m_i = m_by_name.begin();
		delete pli;
	}
	return true;
}

void LNLogItems::reset() {
	LNMutexLocker lock(*this);
	m_i = m_by_name.begin();
}

LNLogItem *LNLogItems::getNext() {
	LNMutexLocker lock(*this);
	LNLogItem *res = NULL;
	if(m_i != m_by_name.end() ) {
		res =  m_i->second;
		m_i++;
	}
	return res;
}

//----------------------------------------------------------------------------

LNLogFile::LNLogFile() {
	// initialize file descriptor to none
	m_fd = 0;
	
	// Avoid frequent memory reallocation most of logs are less
	// then 512 bytes
	m_trash_hold.reserve(TRASH_HOLD_SIZE);
};

LNLogFile::LNLogFile(const std::string &filep, LNLogFile::Type type,
		     const std::string &sep) {
	std::vector<std::string>::iterator scic, scip;
	std::string _sep;
	this->m_fd = 0;
	
	// Avoid frequent memory reallocation most of logs are less
	// then 512 bytes
	m_trash_hold.reserve(TRASH_HOLD_SIZE);

	this->open(filep, type, sep);
}

LNLogFile::~LNLogFile() {
	this->close();
}

bool LNLogFile::isOpen() {
	LNMutexLocker lock(*this);
	return (m_fd != 0?true:false);
}

int LNLogFile::getFileDescriptor() {
	LNMutexLocker lock(*this);
	return m_fd;
}

bool LNLogFile::open(const std::string &path, LNLogFile::Type type, const std::string &sep,
		     bool seekEnd) {
	LNMutexLocker lock(*this);
	bool open = false;
	int retc = 0, fd = 0;
	struct stat finfo;

	if(m_fd) {
		this->close();
	}

	if(type == USOCK) {
		LNLog::logError("Unix socket are not supported yet.");
		return false;
	}
	
	// check if file exist
	retc = ::stat( path.c_str(), &finfo );
	retc = ( retc?errno:0 );
	if(retc && retc == ENOENT) {
		// create file
		switch(type) {
		case FIFO:
			retc = ( (::mkfifo(path.c_str(), 0600))?errno:0 );
			if(retc && retc != EEXIST) {
				LNLog::logError("Failed to create FIFO  '%s'. "
						"%s [line: %ld  file: %s]", 
						path.c_str(), strerror(errno),
						__LINE__, __FILE__ );
				return false;
			}
			open = true;
			LNLog::logInfo("Created FIFO file '%s'", path.c_str());
			break;
		case FILE:
			LNLog::logError("Specified log file '%s' dose not exist.",
					path.c_str());
			return false;
			break;
		case USOCK:
			break;
		default:
			LNLog::logFatal("Unknown file type! '%s' [line: %ld  file: %s]",
					path.c_str(), strerror(errno), __LINE__, __FILE__);
			break;
		}
	} else if(retc) {
		// its some problem, report error and return
		LNLog::logError("Access to log file failed '%s'. %s [line: %ld  file: %s]",
				path.c_str(), strerror(errno), __LINE__, __FILE__ );
		return false;
	}
	if(open || type == FILE || type == FIFO) {
		fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
		retc = (fd != -1?0:errno);
		if(retc) {
			LNLog::logError("Failed to access file '%s'. %s [line: %ld  file: %s]",
				path.c_str(), strerror(errno), __LINE__, __FILE__ );
			return false;
		}
		if(type == FILE && seekEnd) {
			lseek(fd, 0, SEEK_END);
		}
	}

	// get file name and directory
	m_fpath = path;

	char *pathBuff = new char[m_fpath.size()+1];
	if(!pathBuff) {
		LNLog::logFatal("No enough memory. [ file: %s  line: %ld ]",
				__FILE__, __LINE__);
	}
	strcpy(pathBuff, m_fpath.c_str());
	m_fdir = dirname(pathBuff);

	strcpy(pathBuff, m_fpath.c_str());
	m_fname = basename(pathBuff);

	delete[] pathBuff;

	m_fd = fd;
	m_type = type;
	m_sep = LNUtility::replaceString("\\n", "\n", sep);
	m_sep = LNUtility::replaceString("\\r", "\r", m_sep);
	LNLog::logInfo("File '%s' is opened.", m_fname.c_str());
	return true;
}

bool LNLogFile::reopen(bool seekEnd) {
	if(!m_fpath.size()) {
		LNLog::logError("Can't reopen file was never open before.");
		return false;
	}
	return open(m_fpath, m_type, m_sep, seekEnd);
}

bool LNLogFile::close() {
	LNMutexLocker lock(*this);
	int retc;
	if(m_fd) {
		retc = ((::close(m_fd))?errno:0);
		m_fd = 0;
		if(retc) {
			LNLog::logError("Failed to close file  '%s'. %s [line: %ld  file: %s]",
			       m_fpath.c_str(), strerror(errno), __LINE__, __FILE__ );
			return false;
		}
	}
	return true;
}

bool LNLogFile::fetchNextLog(std::string &item) {
	LNMutexLocker lock(*this);
	char *trash = NULL;
	ssize_t bytes_read = 0;
	bool item_extracted = false;

	if(!m_fd) {
		LNLog::logError("Log file is not opened. [line: %ld  file: %s]",
				__LINE__, __FILE__ );
		return false;
	}
	// first check previous trash hold
	if(m_trash_hold.length() && 
	   ( trash = strstr((char*)m_trash_hold.c_str(), m_sep.c_str()) ) ) {
		memset(trash, 0, m_sep.size()*sizeof(char));
		trash += m_sep.size()*sizeof(char);
		item = m_trash_hold;
		m_trash_hold = trash;
		return true; // item extracted
	}

	do {
		if(this->m_trash_hold.length() > TRASH_HOLD_SIZE) {
			LNLog::logWarning("Extracted log item seems to long. "
					  "Check your separator settings.");
		}
		if(this->m_trash_hold.length() > MAX_LOG_ITEM_SIZE) {
			LNLog::logError("Log item exceeded length limit. "
					"Check your separator settings.");
			this->m_trash_hold = "";
			return false;
		}

		memset(m_read_buf, 0, sizeof(m_read_buf));
		do {
			bytes_read = read(m_fd, m_read_buf,
					  sizeof(m_read_buf) - sizeof(char));
		} while(errno == EAGAIN && bytes_read <= 0);

		if(bytes_read <= 0) {
			break; // end of file;
		} else if(bytes_read < 0 && errno != EAGAIN) {
			LNLog::logError("Failed to fetch log item. '%s' %s "
					"[%d] [line: %ld  file: %s]",
					m_fpath.c_str(), strerror(errno), errno,
					__LINE__, __FILE__);
			return false;
		}

		if( (trash = strstr(m_read_buf, m_sep.c_str()) ) ) {
			memset(trash, 0, m_sep.size()*sizeof(char));
			trash += m_sep.size()*sizeof(char);
			item = m_trash_hold + m_read_buf;
			m_trash_hold = trash;
			item_extracted = true;
			break;
		} else if (!(*m_read_buf)) {
			// empty log, zeros written to file
			item = m_trash_hold + m_read_buf;
			trash = m_read_buf;
			m_trash_hold = trash;
			item_extracted = true;
			break;
		} else {
			m_trash_hold += m_read_buf;
		}
	} while(!item_extracted);
	return item_extracted;
}

size_t LNLogFile::getPosition() {
	LNMutexLocker lock(*this);
	if(m_fd) {
		return lseek(m_fd, 0, SEEK_CUR);
	}
	return 0;
}

void LNLogFile::handleIfTruncated() {
	LNMutexLocker lock(*this);
	if(!m_fd) {
		return;
	}
	fstat(m_fd, &m_fstats);
	off_t cur_pos = lseek(m_fd, 0, SEEK_CUR);
	if(cur_pos > m_fstats.st_size) {
		// REPOSITION AT END OF FILE
		lseek(m_fd, 0, SEEK_END);
		LNLog::logInfo("File '%s' was truncated to %d bytes. "
			       "logNOT repositioned.", m_fname.c_str(), m_fstats.st_size);
	}
}

std::string LNLogFile::getFilePath() {
	LNMutexLocker lock(*this);
	return m_fpath;
}

std::string LNLogFile::getFileName() {
	LNMutexLocker lock(*this);
	return m_fname;
}

std::string LNLogFile::getDirectoryPath() {
	LNMutexLocker lock(*this);
	return m_fdir;
}


LNLogItems & LNLogFile::getAttachedItems() {
	LNMutexLocker lock(*this);
	return this->m_items;
}

bool LNLogFile::exist(const std::string &path) {
	return !(::access(path.c_str(), F_OK));
}

//----------------------------------------------------------------------------

LNLogFiles::LNLogFiles() {
	m_curPos = this->m_byDesc.begin();
}


LNLogFiles::~LNLogFiles() {
	this->closeAll();
}

LNLogFile *LNLogFiles::open(const std::string &path, LNLogFile::Type type, const std::string &sep) {
	LNMutexLocker lock(*this);
	LNLogFile *pResult = NULL;
	int fd = 0;

	if(m_byPath.count(path)) {
		return m_byPath[path];
	}

	pResult = new LNLogFile();
	if(!pResult) {
		LNLog::logError("No enough memory. [line: %d   file: %s]", __FILE__, __LINE__);
		return NULL;
	}
	if(!pResult->open(path, type, sep)) {
		delete pResult; // free failed file from memory
		return NULL;
	}
	fd = pResult->getFileDescriptor();

	// map opened file by file descriptor and path
	m_byDesc[fd] = pResult;
	m_byPath[path] = pResult;

	return pResult;
}

bool LNLogFiles::close(int fd) {
	LNMutexLocker lock(*this);
	bool res=false;
	std::map<int , LNLogFile*>::iterator item = m_byDesc.find(fd);
	if(item != m_byDesc.end()) {
		LNLogFile *pf = item->second;
		m_byPath.erase(pf->getFilePath());
		m_byDesc.erase(item);
		delete pf;
		res = true;
	}
	LNLog::logWarning("Required to close non existing file by descriptor %d.", fd);
	return res;
}

bool LNLogFiles::close(const std::string &path) {
	LNMutexLocker lock(*this);
	bool res=false;
	std::map<std::string , LNLogFile*>::iterator item = m_byPath.find(path);
	if(item != m_byPath.end()) {
		LNLogFile *pf = item->second;
		m_byDesc.erase(pf->getFileDescriptor());
		m_byPath.erase(item);
		delete pf;
		res = true;
	}
	LNLog::logWarning("Required to close non existing file by path '%s'.", path.c_str());
	return res;
}

bool LNLogFiles::closeAll() {
	LNMutexLocker lock(*this);
	bool res = true;
	for(std::map<int, LNLogFile*>::iterator i = m_byDesc.begin(); i != m_byDesc.end(); ) {
		m_byPath.erase(i->second->getFilePath());
		m_byDesc.erase(i);
		delete i->second;
		i = m_byDesc.begin();
	}
	return res;
}

LNLogFile *LNLogFiles::find(int fd) {
	LNMutexLocker lock(*this);
	LNLogFile *pRes = NULL;
	std::map<int , LNLogFile*>::iterator res = m_byDesc.find(fd);
	if(res != m_byDesc.end()) {
		pRes = res->second;
	}
	return pRes;
}

LNLogFile *LNLogFiles::find(const std::string &path) {
	LNMutexLocker lovk(*this);
	LNLogFile *pRes = NULL;
	std::map<std::string , LNLogFile*>::iterator res = m_byPath.find(path);
	if(res != m_byPath.end()) {
		pRes = res->second;
	}
	return pRes;
}

void LNLogFiles::reset() {
	LNMutexLocker lock(*this);
	m_curPos = this->m_byDesc.begin();
}

LNLogFile *LNLogFiles::getNext() {
	LNMutexLocker lock(*this);
	LNLogFile *res = NULL;
	if(m_curPos != this->m_byDesc.end() && this->m_byDesc.size() > 0) {
		res = m_curPos->second;
		m_curPos++;
	}
	return res;
}

size_t LNLogFiles::count() {
	LNMutexLocker lock(*this);
	return m_byDesc.size();
}
//----------------------------------------------------------------------------
// LNLogEvents

LNLogEvents::LNLogEvents() {
	m_cpos = m_files.begin();
}

LNLogEvents::~LNLogEvents() {
	this->clear();
}

void LNLogEvents::clear() {
	LNMutexLocker lock(*this);
	m_files.clear();
}

void LNLogEvents::append(LNLogFile *lf) {
	LNMutexLocker lock(*this);
	m_files.push_back(lf);
}

void LNLogEvents::reset() {
	LNMutexLocker lock(*this);
	m_cpos = m_files.begin();
}

LNLogFile* LNLogEvents::getNext() {
	LNMutexLocker lock(*this);
	LNLogFile *res = NULL;
	if(m_cpos != m_files.end() && m_files.size() > 0) {
		res = *m_cpos;
		m_cpos++;
	}
	return res;
}

//--------------------------------------------------------------------------------------------------
// LNInotify

LNInotify::LNInotify() {
	m_eventsBuffer = new char[EVENT_BUF_LEN];
	if (!m_eventsBuffer) {
		LNLog::logFatal("No enough memory. [ file: %s  line: %s ]", __FILE__, __LINE__);
	}
	memset(m_eventsBuffer, 0, EVENT_BUF_LEN*sizeof(char));
	m_fd = inotify_init();
	if (m_fd == -1) {
		LNLog::logFatal("Failed to initialize file system monitoring. (%s)",
				strerror(errno));
	}
}

LNInotify::~LNInotify() {
	if (-1 == close(m_fd)) {
		LNLog::logError("Failed to close inotify fd. (%s)", strerror(errno));
	}
	delete [] m_eventsBuffer;
}

bool LNInotify::addWatch(LNLogFile *pFile, bool watchDir) {
	const char * pathToWatch =
			watchDir?pFile->getDirectoryPath().c_str()\
				:pFile->getFilePath().c_str();
	int wd = inotify_add_watch(m_fd, pathToWatch, //IN_ALL_EVENTS);
				   IN_MODIFY | IN_DELETE | IN_MOVE | IN_ATTRIB | IN_MOVE_SELF);
	if (wd == -1) {
		LNLog::logError("Failed to monitor file '%s'.", pFile->getFilePath().c_str());
		LNLog::logError("%s", strerror(errno));
		return false;
	}
	m_watchMap[wd] = pFile;
	return true;
}

bool LNInotify::rmWatch(LNLogFile *pFile,  bool watchDir) {
	const char * pathToWatch =
			watchDir?pFile->getDirectoryPath().c_str()\
				:pFile->getFilePath().c_str();

	int wd = inotify_add_watch(m_fd, pathToWatch, 0);
	if (wd == -1 && errno != ENOENT) {
		LNLog::logError("%s [ errno: %d ]", strerror(errno), errno);
		return false;
	}
	int rc = inotify_rm_watch(m_fd, wd);
	if (rc == -1) {
		LNLog::logError("Failed to stop monitoring on file '%s'.",
				pFile->getFilePath().c_str());
		LNLog::logError("%s", strerror(errno));
		return false;
	}
	m_watchMap.erase(wd);
	return true;
}

bool LNInotify::rmWatch(int wd) {
	int rc = inotify_rm_watch(m_fd, wd);
	if (rc == -1) {
		LNLog::logError("Failed to stop monitoring on file's wd '%d'.", wd);
		LNLog::logError("%s", strerror(errno));
		return false;
	}
	m_watchMap.erase(wd);
	return true;
}

void LNInotify::rmAll() {
	std::map<int, LNLogFile *>::iterator i;
	for (i = m_watchMap.begin(); i != m_watchMap.end();) {
		this->rmWatch(i->second);
		i = m_watchMap.begin();
	}
}

void event_description(struct inotify_event *pEvent) {
	int event = pEvent->mask;
	if(event & IN_OPEN) {
		LNLog::logDebug("- File '%s' was opened.",
				(pEvent->len)?pEvent->name:"*");
	}
	if(event & IN_ACCESS) {
		LNLog::logDebug("- File '%s' was read from.",
				(pEvent->len)?pEvent->name:"*");
	}
	if(event & IN_MODIFY) {
		LNLog::logDebug("- File '%s' was written to.",
				(pEvent->len)?pEvent->name:"*");
	}
	if(event & IN_ATTRIB) {
		LNLog::logDebug("- File's '%s' metadata (inode or xattr) was changed.",
				(pEvent->len)?pEvent->name:"*");
	}
	if(event & IN_CLOSE_WRITE) {
		LNLog::logDebug("- File '%s' was closed (and was open for writing).",
				(pEvent->len)?pEvent->name:"*");
	}
	if(event & IN_CLOSE_NOWRITE) {
		LNLog::logDebug("- File '%s' was closed (and was open for reading).",
				(pEvent->len)?pEvent->name:"*");
	}
	if(event & IN_MOVED_FROM) {
		LNLog::logDebug("- File '%s' was moved away from watch.",
				(pEvent->len)?pEvent->name:"*");
	}
	if(event & IN_MOVED_TO) {
		LNLog::logDebug("- File '%s' was moved to watch.",
				(pEvent->len)?pEvent->name:"*");
	}
	if(event & IN_DELETE) {
		LNLog::logDebug("- File '%s' was deleted.",
				(pEvent->len)?pEvent->name:"*");
	}
	if(event & IN_CLOEXEC) {
		LNLog::logDebug("- File '%s' was IN_CLOEXEC.",
				(pEvent->len)?pEvent->name:"*");
	}
	if(event & IN_IGNORED) {
		LNLog::logDebug("- File '%s' was IN_IGNORED.",
				(pEvent->name)?pEvent->name:"*");
	}
	if(event & IN_MOVE_SELF) {
		LNLog::logDebug("- File '%s' was IN_MOVE_SELF.",
				(pEvent->len)?pEvent->name:"*");
	}
	if(event & IN_DELETE_SELF) {
		LNLog::logDebug("- The watch '%s' itself was deleted.",
				(pEvent->len)?pEvent->name:"*");
	}
}

bool LNInotify::handleIfFileRemoved(LNLogFile *pFile, int wd, int mask) {
	if (!LNLogFile::exist(pFile->getFilePath())) {
		LNLog::logInfo("Monitored file '%s' removed, possible logrotate.",
			       pFile->getFilePath().c_str());
		if (pFile->isOpen()) {
			pFile->close();
			if ( mask & IN_MOVE_SELF ) {
				this->rmWatch(wd);
			} else {
				m_watchMap.erase(wd);
			}
			LNLog::logInfo("Watch directory '%s'.",
				       pFile->getDirectoryPath().c_str());
			this->addWatch(pFile, true);
			/* From here directory where file is placed will be monitored
			 * if file with same name is created again it will be
			 * handled and re-opened, this is important for logrotate.
			 */
		} else {
			// file's directory is removed
			LNLog::logWarning("File and its parent directory "
					  "are removed now.");
			LNLog::logWarning("File '%s' will not be monitored "
					  "anymore.",
					  pFile->getFilePath().c_str());
			return false;
		}
	} else {
		// simple reinitialize monitoring of file.
		pFile->reopen(true);
		if ( mask & IN_MOVE_SELF ) {
			this->rmWatch(wd);
		} else {
			m_watchMap.erase(wd);
		}
		this->addWatch(pFile);

		LNLog::logWarning("Monitored file '%s' attributes changed.",
				  pFile->getFilePath().c_str());
		return false;
	}
	return true;
}

LNLogFile* LNInotify::handleEvent(LNLogFile *pFile, int wd, int mask, const char *fname) {
	LNLogFile *pResult = NULL;
#ifdef _DEBUG
	if(fname) {
		LNLog::logDebug("fname = %s", fname);
	}
#endif
	if(!(pFile->isOpen()) && fname) {

		if ( pFile->getFileName() == fname) {
			LNLog::logInfo("Monitored file '%s' is created back, "
				       "trying to re-open.",
				       pFile->getFilePath().c_str());
			// This is most common to follow after logrotate.
			pFile->reopen(false);
			this->rmWatch(wd);
			this->addWatch(pFile);
			pResult =  pFile;
		}
	} else {
		pResult = pFile;
	}
	return pResult;
}

LNLogEvents * LNInotify::readEvents() {
	LNLogEvents *pResult = NULL;
	LNLogFile *pFile = NULL;
	struct inotify_event *pEvent = NULL;
	int len = read(m_fd, m_eventsBuffer, EVENT_BUF_LEN);
	std::map<int, LNLogFile *>::iterator i;
	bool fremoved = false;

	pResult = new LNLogEvents();
	if (!pResult) {
		LNLog::logFatal("No enough memory. [ file: %s  line: %s ]",
				__FILE__, __LINE__);
		return pResult;
	}

	if (len <= 0) {
		LNLog::logError("%s  [ file: %s  line: %d ]", strerror(errno),
				__FILE__, __LINE__);
		return pResult;
	}

	int k = 0, count = 0;
	while (k < len) {
		pEvent = (struct inotify_event *) &m_eventsBuffer[k];
		if ((pEvent->mask) & IN_IGNORED) {
			k += EVENT_SIZE + pEvent->len;
			continue;
		}
#ifdef _DEBUG
		event_description(pEvent);
#endif
		i = m_watchMap.find(pEvent->wd);
		if ( i != m_watchMap.end() ) {
			pFile = i->second;
		} else {
			LNLog::logError("Inotify monitored file not found.");
			k += EVENT_SIZE + pEvent->len;
			continue;
		}

		if ((pEvent->mask) & IN_ATTRIB || (pEvent->mask) & IN_MOVED_FROM
		    || (pEvent->mask) & IN_MOVE_SELF) {
			// check if file removed
			fremoved = this->handleIfFileRemoved(pFile, pEvent->wd, pEvent->mask);
		}
		if(!fremoved) {
			pFile = this->handleEvent(pFile, pEvent->wd, pEvent->mask,
					  (pEvent->len)?pEvent->name:NULL);
			if(pFile) {
				pResult->append(pFile);
			}
		}

		k += EVENT_SIZE + pEvent->len;
		count++;
	}

	LNLog::logDebug("Number of valid events: %d", count);

	return pResult;
}

int LNInotify::getFileDescriptor() {
	return m_fd;
}

//--------------------------------------------------------------------------------------------------
// LMFileListener

LNFilesListener::LNFilesListener() {
	m_epfd = 0;
	m_events = NULL;
}

LNFilesListener::~LNFilesListener() {
	if (m_events) {
		this->stopListening();
		delete [] m_events;
		m_events = NULL;
	}
}

bool LNFilesListener::startListening(LNLogFiles *files) {
	LNMutexLocker lock(*this);
	LNLogFile *file = NULL;
	struct epoll_event event;

	if (m_epfd) {
		LNLog::logWarning("Files listener already started.");
		return false;
	}

	if(!m_events) {
		m_events = new struct epoll_event[MAX_EVENTS_PER_TURN];
		if (!m_events) {
			LNLog::logFatal("No enough memory. [ file: %s  line: %d ]",
					__FILE__, __LINE__);
		}
	}

	this->m_epfd = epoll_create(100);
	if( this->m_epfd  < 0) {
		LNLog::logFatal("Failed to create epoll fd. %s [line: %ld  file: %s]",
			strerror(errno), __LINE__, __FILE__);
	}

	event.data.fd = m_inotify.getFileDescriptor();
	event.events = EPOLLIN | EPOLLERR | EPOLLRDHUP;

	files->reset();
	while(NULL != (file = files->getNext())) {
		m_inotify.addWatch(file);
	}

	if(epoll_ctl(this->m_epfd, EPOLL_CTL_ADD, event.data.fd, &event)) {
		LNLog::logError("Can not monitor fd. %s [line: %ld  file: %s]",
				strerror(errno), __LINE__, __FILE__);
		return false;
	}

	return true;
}

bool LNFilesListener::stopListening() {
	LNMutexLocker lock(*this);
	if (!m_epfd) {
		LNLog::logWarning("Files listener already stopped.");
		return false;
	}
	epoll_ctl(m_epfd, EPOLL_CTL_DEL, m_inotify.getFileDescriptor(), NULL);
	close(m_epfd);
	m_inotify.rmAll();
	m_epfd = 0;
	return true;
}

LNLogEvents *LNFilesListener::waitForEvents(int timeout) {
	LNMutexLocker lock(*this);
	int rc = 0;

	if (!m_epfd) {
		LNLog::logError("No files initialized for listen. Did you forget "
				"startListening() ?");
		return NULL;
	}

	rc = epoll_wait(m_epfd, m_events, MAX_EVENTS_PER_TURN, timeout);

	if( rc < 0) {
		if(errno != EINTR) {
			LNLog::logError("%s (%d) [line: %ld  file: %s]",
					strerror(errno), errno, __LINE__, __FILE__);
		}
		return new LNLogEvents();
	}
	
	for ( int i = 0; i < rc; i++ ) {
		return m_inotify.readEvents();
	}

	return NULL;
}

//----------------------------------------------------------------------------------------

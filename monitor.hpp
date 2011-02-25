#ifndef LOG_NOT_LOG_MONITOR_H
#define LOG_NOT_LOG_MONITOR_H

#include <sys/epoll.h>
#include <sys/stat.h>
#include "core.hpp"

#define READ_BUFFER_SIZE	1024

#define LF_DEF_SEPARATOR "\n"


//----------------------------------------------------------------------------

class LNEventQueue : public std::queue<time_t> {
public:
	LNEventQueue() {
		;
	}
	~LNEventQueue();

};

/*! This class is to define frequency of discrete events.  Frequency
 * is defined as the number of events in some finite time
 * period. According to that, object of this class have two
 * properties: 1. Count - as number of events 2. Time Period - time in
 * seconds Object can be serialized or deserialized to/from string in
 * format "Count/Time". Define count per defined time period to get
 * desired frequency.
 */
class LNFreq {
public:
	LNFreq();
	LNFreq(const std::string &freq);
	LNFreq(const std::string &count, const std::string &time);
	LNFreq(unsigned long count, unsigned long time);
	~LNFreq() { ; }

	unsigned long getCount() const { return m_count; }
	unsigned long getTimePeriod() const { return m_time; }

	void setCount(const std::string &number);
	void setTimePeriod(const std::string &time);
	void setCount(unsigned long number) { m_count = number; }
	void setTimePeriod(unsigned long time) { m_time = time; }

	std::string ToString() const;
	bool SetFromString(const std::string &freq);

	void pushEvent(unsigned int crc);
	void removeDeprecated(time_t currTime);
	unsigned long getMessuringCount(unsigned int crc) const;
	void resetMessuring(unsigned int crc);
private:
	unsigned long m_count;
	unsigned long m_time;
	std::map<unsigned int, LNEventQueue*> m_events;
};

//----------------------------------------------------------------------------

/*! Simple configuration holder for single specified log item.  In
 * addition object can match string against defined monitored log item
 * match criteria.
 */
class LNLogItem : public LNMutex {
public:
	LNLogItem(
		const std::string &name,
		const std::string &regex,
		const std::string &up_bound_action,
		const std::string &down_bound_action,
		const std::string &size_action,
		const LNFreq &up_bound,
		const LNFreq &down_bound,
		const size_t size = 0,
		bool usecrc = false
		);
	virtual ~LNLogItem();

	std::string getName();
	std::string getRegEx();
	std::string getUpBoundAction();
	std::string getDownBoundAction();
	const LNFreq &getUpBoundFreq();
	const LNFreq &getDownBoundFreq();

	size_t getSize();
	std::string getSizeAction();

	bool upBoundFreqExcess(const std::string &match);
	bool downBoundFreqExcess();

	int Match(const std::string &logitem, std::vector<std::string> &matches);

	void removeDeprecatedEvents() {
		LNMutexLocker lock(*this);
		time_t tnow = time(NULL);
		m_downfreq.removeDeprecated(tnow);
		m_upfreq.removeDeprecated(tnow);
	}

	void resetDownBoundFreqMessuring() {
		LNMutexLocker lock(*this);
		m_downfreq.resetMessuring(0);
	}

	void resetUpBoundFreqMessuring(unsigned int crc) {
		LNMutexLocker lock(*this);
		m_upfreq.resetMessuring(crc);
	}

	size_t getSizeQuotient();
	void setSizeQuotient(size_t q);

	bool sizeExcess(size_t size);

private:
	std::string m_name;
	std::string m_upbaction;
	std::string m_downbaction;
	std::string m_sizeAction;
	std::string m_regex;
	LNFreq m_upfreq, m_downfreq;
	void *m_pcre;
	size_t m_size;
	size_t m_quotient;
	bool m_usecrc;
};

//----------------------------------------------------------------------------------------

class LNActionPreprocessor {
public:
	static std::string run(const std::string &action, const std::string &line,
			       const std::vector<std::string> &matches);
private:
	static void *m_pcre;
};

//----------------------------------------------------------------------------------------

/*! Collection of defined log items. Its search able per log item
 *  name. You can iterate trough collection of this items by using
 *  Reset() to position at beginning of collection and FetchNext to
 *  fetch item at current position and move forward next to item
 *  position.
 */
class LNLogItems : public LNMutex {
public:
	LNLogItems();
	virtual ~LNLogItems();

	bool add(LNLogItem *pli);
	LNLogItem *add(
		const std::string &name,
		const std::string &regex,
		const std::string &up_bound_action,
		const std::string &down_bound_action,
		const std::string &size_action,
		const LNFreq &up_bound,
		const LNFreq &down_bound,
		const size_t size = 0,
		bool usecrc = false
		);
	LNLogItem *find(const std::string &name);

	LNLogItem * remove(const std::string &name);
	bool removeAndDelete(const std::string &name);
	bool removeAll();
	bool removeAndDeleteAll();

	void reset();
	LNLogItem* getNext();
private:
	std::map<std::string, LNLogItem*> m_by_name;
	std::map<std::string, LNLogItem*>::iterator m_i;
};

//----------------------------------------------------------------------------
/*! Used to attach to log file and to read available items. It's
 * capable parsing out one by one item, call FetchNextLog() for
 * that. Can handle regular text files, FIFO and unix sockets.  It
 * support defining custom log items separator, if separator is not
 * default '\n' or '\r'. Separator marks end of single log item
 * (message) and beginning of next.
 */
class LNLogFile : public LNMutex {
public:
	enum Type {
		FILE = 0,
		FIFO = 1,
		USOCK = 2
	};

	LNLogFile();
	LNLogFile(const std::string &path, LNLogFile::Type type,
		  const std::string &sep=LF_DEF_SEPARATOR);
	virtual ~LNLogFile();

// TO-DO: Support defining multiple separators put default separators
// to be '\n' or '\r'.
	bool open(const std::string &path, LNLogFile::Type type,
		  const std::string &sep=LF_DEF_SEPARATOR, bool seekEnd = true);
	bool reopen( bool seekEnd = true);
	bool isOpen();

	bool close();

	bool fetchNextLog(std::string &result);
	size_t getPosition();

	void handleIfTruncated();

	int getFileDescriptor();

	std::string getFilePath();
	std::string getDirectoryPath();
	std::string getFileName();

	LNLogItems & getAttachedItems();

	static bool exist(const std::string &path);

private:
	int m_fd;
	std::string m_trash_hold;
	std::string m_fpath, m_fname, m_fdir;
	std::string m_sep;
	LNLogFile::Type m_type;
//	std::vector<std::string> m_separators;
	LNLogItems m_items;
	char m_read_buf[READ_BUFFER_SIZE];
	struct stat m_fstats;
	off_t m_curPos;
};

//----------------------------------------------------------------------------

/*! Collection of monitored files. Maps files by path and file
 *  descriptor. File descriptor mapping is required by look up from
 *  LMFileListener to access specific log file object.
 */
class LNLogFiles : public LNMutex {
public:
	LNLogFiles();
	virtual ~LNLogFiles();

	LNLogFile * open(const std::string &path, LNLogFile::Type type,
			 const std::string &sep=LF_DEF_SEPARATOR);

	bool close(int fd);
	bool close(const std::string &path);

	bool closeAll();

	void reset();
	LNLogFile *getNext();

	size_t count();

	LNLogFile *find(int fd);
	LNLogFile *find(const std::string &path);

private:
	std::map<int, LNLogFile*> m_byDesc;
	std::map<std::string, LNLogFile*> m_byPath;
	std::map<int, LNLogFile*>::iterator m_curPos;
};

//----------------------------------------------------------------------------
/*! Meant to hold collection of files with events. This is used to
 *  return collection of files from LMFileListener that have available
 *  data to read. This collection will not destroy referenced file
 *  objects after destroyed it self. That's the main reason for
 *  implementing this simple class. Otherwise if files with available
 *  data returned with LNLogFiles collection object, file objects
 *  would be destroyed after drooping collection, causing program to
 *  crash. So its used fro safe handling of file events.
 */
class LNLogEvents : public LNMutex {
public:
	LNLogEvents();
	virtual ~LNLogEvents();

	void reset();
	LNLogFile *getNext();
	void append(LNLogFile *lfile);

	// Clear the whole collection.
	void clear();
private:
	std::vector<LNLogFile*> m_files;
	std::vector<LNLogFile*>::iterator m_cpos;
};

//----------------------------------------------------------------------------

class LNInotify {
public:
	LNInotify();
	virtual ~LNInotify();

	bool addWatch(LNLogFile *pf, bool watchDir = false);
	bool rmWatch(LNLogFile *pf,  bool watchDir = false);
	bool rmWatch(int wd);
	void rmAll();
	LNLogEvents * readEvents();
	int getFileDescriptor();
private:

	bool handleIfFileRemoved(LNLogFile *pFile, int wd, int mask);
	LNLogFile* handleEvent(LNLogFile *pFile, int wd, int mask, const char *fname);

	char *m_eventsBuffer;
	int m_fd;
	std::map<int, LNLogFile*> m_watchMap;
};

//----------------------------------------------------------------------------

class LNFilesListener : public LNMutex {
public:
	LNFilesListener();
	~LNFilesListener();

	bool startListening(LNLogFiles *lfiles);
	bool stopListening();

	LNLogEvents* waitForEvents(int timeout = -1);
	
private:
	struct epoll_event *m_events;
	int m_epfd;
	LNLogFiles *m_pfiles;
	LNInotify m_inotify;
};

//----------------------------------------------------------------------------
#endif	// LOG_NOT_LOG_MONITOR_H

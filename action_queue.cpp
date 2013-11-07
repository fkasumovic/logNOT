#include <unistd.h>

#include "debug.hpp"
#include "monitor.hpp"
#include "action_queue.hpp"

//---------------------------------------------------------------------------------------
// LNCommandQueue

LNActionQueue::LNActionQueue() {
}

LNActionQueue::~LNActionQueue() {
}

void LNActionQueue::push(const std::string &command) {
	LNMutexLocker lock(*this);
	m_queue.push(command);
	m_cond.signal();
}

std::string LNActionQueue::pop() {
	bool empty;
	std::string result;

	this->lock();
	empty = (m_queue.size() <= 0);
	this->unlock();

	if(empty) {
		// no commands simple block and wait for next one to be pushed
		m_cond.wait();
	}

	this->lock();
	if(m_queue.size() > 0) {
		result = m_queue.front();
		m_queue.pop();
	} else {
		result = "";
	}
	this->unlock();

	return result;
}

//---------------------------------------------------------------------------------------
// LNActionThreads

void *_executePendingActions(void *pargs) {
	LNActionQueue *pActions = (LNActionQueue*)pargs;
	std::string action;
	int ret_code = 0;
	action.reserve(256);

	do {
		// wait here if no action
		action = pActions->pop();
		if(action.size() <= 0) {
			continue;
		}
		// code to actually execute specified action
		ret_code = system(action.c_str());
		LNLog::logInfo(" -- Action '%s' executed with return code %d.",
				action.c_str(),
				ret_code);
	} while(true);

	return NULL;
}

void *_executeDownBoundFreqCheck(void *pargs) {
	LNLogItems *pLogItems = (LNLogItems*)pargs;
	LNLogItem *pItem = NULL;
	unsigned long start_time = time(NULL), uptime = 0;
	time_t tnow = start_time;
	while(pLogItems) {
		// check every 5 seconds for all log items
		sleep(5);
		pLogItems->lock();
		pLogItems->reset();
		tnow = time(NULL);
		uptime = tnow - start_time;
		while((pItem = pLogItems->getNext())) {
			const LNFreq &f = pItem->getDownBoundFreq();
			pItem->removeDeprecatedEvents();
			if(f.getCount() > 0 && uptime > f.getTimePeriod() &&
			   pItem->downBoundFreqExcess()) {
				LNActionThreads::enqueue(pItem->getDownBoundAction());
				pItem->resetDownBoundFreqMessuring();
				start_time = tnow;
			}
		}
		pLogItems->unlock();
	}

	return NULL;
}

LNActionQueue LNActionThreads::m_actions;
LNThread* LNActionThreads::m_threads = NULL;
unsigned int LNActionThreads::m_nt = 0;

LNActionThreads::LNActionThreads() {
}

LNActionThreads::~LNActionThreads() {

}


void LNActionThreads::startThreads(unsigned int nt, LNLogItems *pLogItems) {
	// TO-DO: stop any already running threads before continue
	if(!nt) {
		LNLog::logFatal("Miss use of LNActionThreads need at least one thread to run.");
		return;
	}

	// create +1 thread for down bound frequencies check
	m_threads = new LNThread[nt+1];
	if(!m_threads) {
		LNLog::logFatal("No enough memory to allocate action threads.");
		return;
	}

	unsigned int i;
	for(i = 0; i < nt; i++) {
		if(!m_threads[i].run(_executePendingActions, &m_actions)) {
			LNLog::logFatal("Failed to create all specified action threads.");
		}
	}
	// Now run down bound frequency check thread
	if(!m_threads[i].run(_executeDownBoundFreqCheck, pLogItems)) {
		LNLog::logFatal("Failed to create all specified action threads.");
	}
	m_nt = nt+1;
	return;
}

void LNActionThreads::enqueue(std::string action) {
	m_actions.push(action);
}

//---------------------------------------------------------------------------------------

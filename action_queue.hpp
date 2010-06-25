#ifndef LOG_NOT_COMMAND_QUEUE_H
#define LOG_NOT_COMMAND_QUEUE_H

#include "lognot.hpp"
#include <queue>
#include "core.hpp"

//----------------------------------------------------------------------------------------

class LNActionQueue : public LNMutex {
public:
	LNActionQueue();
	~LNActionQueue();

	void push(const std::string &command);
	std::string pop();

private:
	std::queue<std::string> m_queue;
	LNCondition m_cond;
};

//----------------------------------------------------------------------------------------

class LNActionThreads {
public:
	LNActionThreads();
	~LNActionThreads();

	static void startThreads(unsigned int nt, LNLogItems *pLogItems);
	// TO-DO: static void stopThreads();

	static void enqueue(std::string action);
private:
	static unsigned int m_nt;
	static LNActionQueue m_actions;
	static LNThread *m_threads;
};

//----------------------------------------------------------------------------------------
#endif	// LOG_NOT_COMMAND_QUEUE_H

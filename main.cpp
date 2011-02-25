#include <unistd.h>
#include <getopt.h>
#include <signal.h>

#include <sys/prctl.h>

#include "lognot.hpp"
#include "debug.hpp"
#include "core.hpp"
#include "monitor.hpp"
#include "configuration.hpp"
#include "action_queue.hpp"

//std::string program_name;
static std::map<std::string,std::string> cl_opts;
struct sigaction new_action, old_action;
uid_t g_uid;
gid_t g_gid;

FILE *lfp = NULL;
bool reload;

LNConfig config;
LNFilesListener listener;

bool loadConfiguration(bool reload);

/*! Purpose of this function is to stop gracefully on signal.
 *  When program is killed system will cleanup memory, but most
 *  important is to close database connections and all other
 *  socket descriptors cause they can be left behind some time,
 *  by system and mysql to.
 */
void terminationHandler (int signum) {
	// we need to skip pthread_mutex_lock
	// all other threads are suspended
	// if anyone holds lock this handler will dead-lock
	LNDisableMutex dm();

	int ret_code = 0;

	// do proper cleanup
	switch(signum) {
	case SIGHUP:
		// re-open log file for case of log rotation
		// applies only if actual file is used for logNOT logs
		if(LNLog::FILE_LOG == LNLog::getLogFacility()) {
			lfp = fopen(cl_opts["logfile"].c_str(), "a");
			if(!lfp) {
				LNLog::setupLogFacility(LNLog::SYSTEM_LOG);
				LNLog::logError("Failed to open/create log file '%s'. %s\n",
						cl_opts["logfile"].c_str(),
						strerror(errno));
			} else {
				LNLog::setupLogFacility(LNLog::FILE_LOG, lfp);
			}
			LNLog::logInfo("Log file re-opened.");
		}
		return;
		break;
	case SIGTERM:
		LNLog::logInfo("Received signal SIGTERM. Terminating ...");
		break;
	case SIGUSR1:
		// reload configuration
		reload = true;
		return;
		break;
	case SIGUSR2:
		return;
		break;
	default:
		return;
		break;
	}
	LNLog::logInfo("%s exit.", LNGlobals::get(PROGRAM_NAME).c_str());
	LNLog::closeLog();
	exit(ret_code);
}

/*! Just to registering signal handlers for stopping dialer.
 *
 */
void initTerminationHandler() {
	/* Set up the structure to specify the new action. */
	new_action.sa_handler = terminationHandler;
	sigemptyset (&new_action.sa_mask);
	new_action.sa_flags = 0;

	sigaction (SIGHUP, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction (SIGHUP, &new_action, NULL);
	sigaction (SIGTERM, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction (SIGTERM, &new_action, NULL);

	sigaction (SIGUSR1, &new_action, NULL);
	sigaction (SIGUSR2, &new_action, NULL);
}

void printUsage(FILE* out) {
	fprintf(out,
		"Usage: \n  %s --config <filename> [ options ...]\n"
		"           %s --test   <reg exp>\n\n",
		LNGlobals::get(PROGRAM_NAME).c_str(),
		LNGlobals::get(PROGRAM_NAME).c_str());
	fprintf(out, "Options:\n"
		"  -h  --help                Display this usage information and exit.\n"
		"  -c  --config  <filename>  This is configuration file location\n"
		"                            (required).\n"
		"  -l  --logfile <filename>  Write logs in to this file.\n"
		"  -d  --daemon              Start as background application.\n"
		"  -s  --stdout              Write logs to standard output.\n"
		"  -u  --uid     <uid>       Change uid\n"
		"  -g  --gid     <gid>       Change gid\n"
		"  -w  --chdir   <directory> Change working directory\n"
		"  -p  --pid     <full path> Set process pid file path. Applies with\n"\
		"                            --daemon only.\n"
		"  -t  --test    <filename>  Test specified configuration file.\n"\
		"  -T  --retest  <reg expr>  Test specified regular expression on\n"\
		"                            line/s from standard input.\n"
		"  -v  --verbose             Run as debug. (Careful! frequent logs)\n"
		"  -V  --version             Print version information and exit.\n\n");
}

int regularExpTest(const std::string &regex) {
	char test_buffer[1024];
	std::vector<std::string> matches;
	LNLogItem testItem(
			"test",
			regex,
			"","", "",
			LNFreq(0,0), LNFreq(0,0)
			);
	int line = 1;
	while(fgets(test_buffer, 1024, stdin)) {
		testItem.Match(test_buffer, matches);
		std::vector<std::string>::iterator m;
		printf("Line %d matches: \n", line);
		int i = 0;
		for(m = matches.begin(); m != matches.end(); m++, i++) {
			printf(" $%d = %s \n", i , (*m).c_str());
		}
		if(!i) {
			printf(" No match.\n");
		}
		printf("\n");
		line++;
	}
	return 0;
}

/*! This will put all command line arguments to stl map.  Its more
 * simple to handle. It will place result in global object cl_opts,
 * so it can be referenced anywhere in the program.  Important to
 * mention is that cl_opts is not thread safe so take care, never use
 * it in threads, or wrap it to something that can be synchronized
 * with mutex.
 */
std::map<std::string,std::string> &parse_cl_options(int argc, char **argv) {
	int next_option = 0;
	const char *OPT_SET = "true";
	/* A string listing valid short options letters.  */
	const char* const short_options = "hvc:l:g:u:w:p:dt:T:sV";
	/* An array describing valid long options.  */
	const struct option long_options[] = {
		{ "help",      0, NULL, 'h' },
		{ "verbose",   0, NULL, 'v' },
		{ "config",    1, NULL, 'c' },
		{ "logfile",   1, NULL, 'l' },
		{ "gid",       1, NULL, 'g' },
		{ "uid",       1, NULL, 'u' },
		{ "chdir",     1, NULL, 'w' },
		{ "pid",       1, NULL, 'p' },
		{ "daemon",    0, NULL, 'd' },
		{ "stdout",    0, NULL, 's' },
		{ "test",      1, NULL, 't' },
		{ "retest",    1, NULL, 'T' },
		{ "version",   0, NULL, 'V' },
		{ NULL,        0, NULL, 0   }  /* Required at end of array. */
	};

	LNGlobals::set(PROGRAM_NAME, basename(argv[0]));
	
	do {
		next_option = getopt_long (argc, argv, short_options,
                               long_options, NULL);
		switch(next_option) {
		case 'h':
			cl_opts["help"] = OPT_SET;
			break;
		case 'v':
			cl_opts["verbose"] = OPT_SET;
			break;
		case 'c':
			cl_opts["config"] = optarg;
			break;
		case 'l':
			cl_opts["logfile"] = optarg;
			break;
		case 'd':
			cl_opts["daemon"] = OPT_SET;
			break;
		case 's':
			cl_opts["stdout"] = OPT_SET;
			break;
		case 'V':
			cl_opts["version"] = OPT_SET;
			break;
		case 'u':
			cl_opts["uid"] = optarg;
			break;
		case 'g':
			cl_opts["gid"] = optarg;
			break;
		case 'w':
			cl_opts["chdir"] = optarg;
			break;
		case 'p':
			cl_opts["pid"] = optarg;
			break;
		case 't':
			cl_opts["test"] = optarg;
			break;
		case 'T':
			cl_opts["retest"] = optarg;
			break;
		case '?':
		case ':':
			// this is unknown option just ignore it
			printUsage(stdout);
			exit(1);
			break;
		case -1:
			// no more options
			break;
		default:
			printf ("?? getopt returned character code 0%o ??\n",
				next_option);
			break;
		}

	} while(next_option != -1);

	return cl_opts;
}

bool processLogFile(LNLogFile *pFile) {
	if(!pFile) {
		return false;
	}
	std::vector<std::string> matches;
	std::string line, upb_action;
	LNLogItem *pItem = NULL;
	LNLogItems & ai = pFile->getAttachedItems();
	int lineCount = 0;
	while (pFile->fetchNextLog(line)) {
		lineCount++;
		ai.reset();
		size_t size = pFile->getPosition(); // current file size
		while ( (pItem = ai.getNext()) ) {
			// size check
			if(pItem->sizeExcess(size)) {
				LNActionThreads::enqueue(pItem->getSizeAction());
			}
			upb_action = pItem->getUpBoundAction();
			if(!upb_action.size() || !line.size()) {
				// no reason to match and spend cpu time
				continue;
			}
			if ( (0 < pItem->Match(line, matches))
				&& (pItem->upBoundFreqExcess(matches[0])) ) {

				LNActionThreads::enqueue(LNActionPreprocessor::run(
						upb_action, line, matches));

				// start to messuer all again, or notification will be
				// executed on each next log that match.
				pItem->resetUpBoundFreqMessuring(0);
			}
		}
	}
	if(!lineCount) {
		pFile->handleIfTruncated();
	}

	// there is more continue
	return true;
}

bool testConfiguration(const std::string &confFile) {
	LNConfig testConfig;
	return testConfig.load(confFile);
}

bool loadConfiguration(bool reload=false) {
	// Load configuration
	if(reload) {
		LNLog::logInfo("Configuration reloading...");
		LNLog::logDebug("Performing configuration test.");
		if(!testConfiguration(cl_opts["config"])) {
			LNLog::logError("Configuration reload failed.");
			LNLog::logInfo("Continuing with previous configuration.");
			return false;
		}
		LNLog::logDebug("Configuration test successful.");
		listener.stopListening();
	} else {
		LNLog::logInfo("Configuration loading...");
	}
	if(!config.load(cl_opts["config"])) {
		LNLog::logFatal("Configuration load failed.");
	}
	if(!reload) {
		if(!g_gid) {
			g_gid = config.getGID();
		}
		if(!g_uid) {
			g_uid = config.getUID();
		}

		if(g_uid != 0) {
			LNLog::logInfo("Setting process user id %d", g_uid);
			if(setuid(g_uid)) {
				LNLog::logFatal("setuid(%d): %s", g_uid, strerror(errno));
			}
		}
		if(g_gid != 0) {
			LNLog::logInfo("Setting process group id %d", g_gid);
			if(setgid(g_gid)) {
				LNLog::logFatal("setgid(%d): %s", g_gid, strerror(errno));
			}
		}
	}
	if(!reload) {
		// start threads for execution of pending actions
		LNActionThreads::startThreads(2, config.getLogItems());
	}

	// initialize listener for specified files
	listener.startListening(config.getLogFiles());

	if(reload) {
		LNLog::logInfo("Configuration reloaded.");
	} else {
		LNLog::logInfo("Configuration loaded.");
	}
	return true;
}

int main_proc(void *args) {
	LNLogEvents *pEvents = NULL;

	initTerminationHandler();
	reload = false;

	// Load configuration
	loadConfiguration(reload);

	LNLog::logInfo("%s started.", LNGlobals::get(PROGRAM_NAME).c_str());
	// Wait for events on monitored files
	while((pEvents = listener.waitForEvents(-1))) {
		if(reload) {
			loadConfiguration(reload);
			reload = false;
			delete pEvents;
			continue;
		}
		pEvents->reset(); // revert to beginning of list
		// iterate list
		while(processLogFile(pEvents->getNext())) {
			;
		}
		delete pEvents;
	}
	return 0;
}

int main(int argc, char **argv) {
	LNLog::setupLogFacility(LNLog::SYSTEM_LOG);
	LNGlobals::set(PROGRAM_NAME, PROGRAM_NAME);

	parse_cl_options(argc, argv);

	if(cl_opts.count("help")) {
		printf("\n %s version: " PROGRAM_VERSION "\n",
		       LNGlobals::get(PROGRAM_NAME).c_str());
		printf("\n" PROGRAM_DESCRIPTION "\n\n");
		printUsage(stdout);
		exit(0);
	}

	if(cl_opts.count("version")) {
		printf("\n %s version: " PROGRAM_VERSION "\n\n",
		       LNGlobals::get(PROGRAM_NAME).c_str());
		return 0;
	}

	if (cl_opts.count("retest")) {
		if (! cl_opts["retest"].size() ) {
			fprintf(stderr, "Error: Regular expression must be specified.\n");
			return 1;
		}
		return regularExpTest(cl_opts["retest"]);
	}

	if(cl_opts.count("verbose")) {
		LNLog::setDebugMode(true);
	}

	if(cl_opts.count("logfile")) {
		if((lfp = fopen(cl_opts["logfile"].c_str(), "a+"))) {
			LNLog::setupLogFacility(LNLog::FILE_LOG, lfp);
		} else {
			LNLog::logFatal("Failed to initialize logs. %s", strerror(errno));
		}
	}
	if(cl_opts.count("stdout")) {
		LNLog::setupLogFacility(LNLog::STDOUT_LOG);
	}

	if (cl_opts.count("test")) {
		if (! cl_opts["test"].size() ) {
			fprintf(stderr, "Error: Configuration file must be specified.\n");
			return 1;
		}
		LNLog::setupLogFacility(LNLog::STDOUT_LOG);
		bool tr = testConfiguration(cl_opts["test"]);
		if(tr) {
			LNLog::logInfo("Configuration test completed with success.");
			exit(0);
		}
		exit(1);
	}

	if(!cl_opts.count("config")) {
		std::string def_config = CONF_DEFAULT_CONFIG
					 + LNGlobals::get(PROGRAM_NAME) + ".conf";
		LNLog::logInfo("Looking  up configuration file at '%s'.",
			       def_config.c_str());
		if(LNLogFile::exist(def_config)) {
			cl_opts["config"] = def_config;
			LNLog::logInfo("Configuration file located.");
		} else {
			// print usage
			LNLog::logError("%s requires config file location to be specified.\n\n",
					LNGlobals::get(PROGRAM_NAME).c_str());
			printUsage(stderr);
			exit(1);
		}
	}

	// cl options should overwrite config option
	if (cl_opts.count("uid")) {
		g_uid = atoi(cl_opts["uid"].c_str());
	}

	if (cl_opts.count("gid")) {
		g_gid = atoi(cl_opts["gid"].c_str());
	}

#ifdef _DEBUG
	if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) < 0) {
		LNLog::logWarning("Unable to setup process for "\
				  "core dumps. %s", strerror(errno));
	}
#endif
	if (cl_opts.count("chdir")) {
		LNLog::logInfo("Changing working directory to '%s' ...",
			       cl_opts["chdir"].c_str());
		if(chdir(cl_opts["chdir"].c_str())) {
			LNLog::logFatal("chdir '%s': %s",
					cl_opts["chdir"].c_str(), strerror(errno));
		}
	}

	if(cl_opts.count("daemon")) {
		LNProcess lnProc;
		LNLog::logInfo("%s starting as daemon...",
			       LNGlobals::get(PROGRAM_NAME).c_str());
		if(cl_opts.count("pid")) {
			lnProc.setPIDFilePath(cl_opts["pid"]);
		}
		lnProc.fork(main_proc, NULL);
	} else {
		LNLog::logInfo("%s starting...", LNGlobals::get(PROGRAM_NAME).c_str());
		return main_proc(NULL);
	}
	return 0;
}

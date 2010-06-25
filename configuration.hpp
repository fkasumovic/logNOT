#ifndef LOG_NOT_CONFIGURATION_H
#define LOG_NOT_CONFIGURATION_H

#include "utility.hpp"
#include "core.hpp"
#include "monitor.hpp"

class LNConfig : public LNMutex {
public:
	LNConfig();
	~LNConfig();

	bool load(const std::string &file_path);
	
	LNLogItems *getLogItems();
	LNLogFiles *getLogFiles();

	std::string readGeneralConfig(const std::string &param);

	uid_t getUID();
	gid_t getGID();
	
private:
	
	bool unload();
	bool loadLogItem(LNIniSection &section, const std::string &name);
	std::string readSectionParam(LNIniSection &section, const std::string &param);

	bool checkForUnknownOptions(LNIniFile &iniFile);

	LNLogFiles m_lFiles;
	LNLogItems m_lItems;
	LNIniFile m_iniFile;

	std::map<std::string, std::string> def_config;
	std::map<std::string, LNLogFile::Type> ft_map;

	uid_t m_uid;
	gid_t m_gid;

	bool m_param_error;
};

#endif	// LOG_NOT_CONFIGURATION_H

#include "debug.hpp"
#include "configuration.hpp"

#include <strings.h>

//------------------------------------------------------------------------------
// LNConfig

bool boolean_value(const std::string &val) {
	if(val.length() > 1) {
		return (!strcasecmp(val.c_str(), "yes"))
				|| (!strcasecmp(val.c_str(), "true"));
	}
	return bool(atoi(val.c_str()));
}

size_t size_value(const std::string &val) {
	char *num = new char[val.size()];
	char unit = 'B', c;
	size_t result = 0, i;

	for (i=0; i < val.size(); i++) {
		c = val[i];
		if(c <= '9' && c >= '0') {
			num[i] = c;
		} else {
			break;
		}
	}

	if (i) {
		result = atoi(num);
		if ( i < val.size() ) {
			unit = val[i];
		}
		switch(unit) {
		case 'B':
			break;
		case 'K':
			result *= 0x400;
			break;
		case 'M':
			result *= 0x100000;
			break;
#ifdef __x86_64
		case 'G':
			result *= 0x10000000000;
			break;
#endif // __x86_64
		default:
			LNLog::logWarning("Invalid file size specification.");
			break;
		}
	} else {
		LNLog::logWarning("Invalid file size specification.");
	}

	return result;
}

LNConfig::LNConfig() {
	// Define default config parameters
	def_config[CONF_TMPDIR] 		= CONF_DEFAULT_TMPDIR;
	def_config[CONF_LOG] 			= CONF_DEFAULT_LOG;
	def_config[CONF_LOGFILE] 		= CONF_DEFAULT_LOGFILE;
	def_config[CONF_UID] 			= CONF_DEFAULT_UID;
	def_config[CONF_GID] 			= CONF_DEFAULT_GID;
	def_config[CONF_ITEM_REGEX] 		= CONF_DEFAULT_REGEX;
	def_config[CONF_DOWNBOUND_FREQ] 	= CONF_DEFAULT_FREQ;
	def_config[CONF_UPBOUND_FREQ] 		= CONF_DEFAULT_FREQ;
	def_config[CONF_FILE_TYPE] 		= CONF_DEFAULT_FILE_TYPE;
	def_config[CONF_ATHREAD_COUNT] 		= CONF_DEFAULT_AT_COUNT;
	def_config[CONF_SEPARATOR] 		= CONF_DEFAULT_SEPARATOR;
	def_config[CONF_USECRC] 		= CONF_DEFAULT_USECRC;
	def_config[CONF_DOWNBOUND_ACTION]	= CONF_DEFAULT_DOWNBOUND_ACTION;
	def_config[CONF_SIZE]			= CONF_DEFAULT_SIZE;
	def_config[CONF_SIZE_ACTION] 		= CONF_DEFAULT_SIZE_ACTION;

	// create file type map
	ft_map["file"]  = LNLogFile::FILE;
	ft_map["fifo"]  = LNLogFile::FIFO;
	ft_map["usock"] = LNLogFile::USOCK;
	m_gid = 0;
	m_uid = 0;
	m_param_error = false;
}

LNConfig::~LNConfig() {
	unload();
}

bool LNConfig::checkForUnknownOptions(LNIniFile &iniFile) {
	INIT_VALID_OPTIONS_MAP;
	LNIniFile::iterator s;
	LNIniSection::iterator i;
	int err = 0;
	for(s = iniFile.begin(); s != iniFile.end(); s++) {
		LNIniSection &iniSection = s->second;
		for(i = iniSection.begin(); i != iniSection.end(); i++) {
			if(!CONF_VALID_OPTIONS_MAP.count(i->first)) {
				LNLog::logError("Unknown option '%s' at section '%s'",
						i->first.c_str(), s->first.c_str());
				err++;
			}
		}
	}
	return (err == 0);
}

bool LNConfig::load(const std::string &m_filePath) {
	unload();

	if(!m_iniFile.parse(m_filePath)) {
		LNLog::logError("Failed to load configuration.");
		return false;
	}

	if(!checkForUnknownOptions(m_iniFile)) {
		return false;
	}

	m_iniFile[CONF_SECTION_GENERAL][CONF_LOADTIME] = 
		LNUtility::formatString("%d", time(NULL));

	LNIniFile::iterator i;

	for( i = m_iniFile.begin(); i != m_iniFile.end(); i++) {
		if(i->first == CONF_SECTION_GENERAL) {
			continue;
		}
		if(!loadLogItem(i->second, i->first)) {
			return false;
		}
	}

	// check should we switch this process to the specific user or/and group
	if(m_iniFile.count(CONF_SECTION_GENERAL)) {
		if(m_iniFile[CONF_SECTION_GENERAL].count(CONF_GID)) {
			m_gid = atoi(m_iniFile[CONF_SECTION_GENERAL][CONF_GID].c_str());
		}

		if(g_mainConfig[CONF_SECTION_GENERAL].count(CONF_UID)) {
			m_uid = atoi(m_iniFile[CONF_SECTION_GENERAL][CONF_UID].c_str());
		}
	}

	return true;
}

std::string LNConfig::readGeneralConfig(const std::string &param) {
	if(m_iniFile[CONF_SECTION_GENERAL].count(param)) {
		return m_iniFile[CONF_SECTION_GENERAL][param];
	}
	// map to default value
	if(def_config.count(param)) {
		return def_config[param];
	}
	LNLog::logError("Undefined configuration parameter '%s'."\
			" [ file: %s  line: %d ]",
			param.c_str(), __FILE__, __LINE__);
	m_param_error = true;
	return "";
}

std::string LNConfig::readSectionParam(LNIniSection &section,
				       const std::string &param) {
	LNIniSection::iterator i = section.find(param);
	if( i == section.end()) {
		return readGeneralConfig(param);
	}
	return i->second;
}

bool LNConfig::loadLogItem(LNIniSection &section, const std::string &name) {
	LNLogFile *pFile = NULL;
	std::string lfType = "file", m_filePath;
	LNLogItem *pItem = new LNLogItem(
		name,
		readSectionParam(section, CONF_ITEM_REGEX),
		readSectionParam(section, CONF_UPBOUND_ACTION),
		readSectionParam(section, CONF_DOWNBOUND_ACTION),
		readSectionParam(section, CONF_SIZE_ACTION),
		LNFreq(readSectionParam(section, CONF_UPBOUND_FREQ)),
		LNFreq(readSectionParam(section, CONF_DOWNBOUND_FREQ)),
		size_value(readSectionParam(section, CONF_SIZE)),
		boolean_value(readSectionParam(section, CONF_SIZE_ACTION))
		);

	if(!pItem) {
		LNLog::logFatal("No enough memory.");
		return false;
	}

	if(m_param_error) {
		m_param_error = false; // reset
		delete pItem;
		return false;
	}

	lfType = readSectionParam(section, CONF_FILE_TYPE);
	if(!ft_map.count(lfType)) {
		LNLog::logError("Unknown file type '%s' for log item '%s' specified.",
				lfType.c_str(), name.c_str());
		delete pItem;
		return false;
	}
	m_filePath = readSectionParam(section, CONF_FILE_PATH);
	if(!(pFile = m_lFiles.find(m_filePath))) {
		pFile = m_lFiles.open(m_filePath, ft_map[lfType],
			readSectionParam(section, CONF_SEPARATOR));
		if(!pFile) {
			LNLog::logError("Failed to access log file.");
			delete pItem;
			return false;
		}
	}
	LNLogItems & attached_items = pFile->getAttachedItems();
	attached_items.add(pItem);
	m_lItems.add(pItem);
	return true;
}

bool LNConfig::unload() {
	if(!m_lItems.removeAndDeleteAll()) {
		LNLog::logError("Configuration unload failed. [ file: %s  line: %d ]",
				__FILE__, __LINE__);
		return false;
	}
	m_lFiles.closeAll();
	return true;
}

uid_t LNConfig::getUID() {
	return m_uid;
}

gid_t LNConfig::getGID() {
	return m_gid;
}

LNLogItems * LNConfig::getLogItems() {
	return &m_lItems;
}

LNLogFiles * LNConfig::getLogFiles() {
	return &m_lFiles;
}

//------------------------------------------------------------------------------

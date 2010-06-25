#ifndef LOG_NOT_UTILITY_H
#define LOG_NOT_UTILITY_H

#include "lognot.hpp"

int _formatString(std::string &str, const char *fmt, va_list &vargs, int size);

class LNUtility {
public:
	static std::string formatString(const char *format, ...);
	static std::vector<std::string> &explodeString(std::vector<std::string> &result,
						       const std::string &str, 
						       const std::string &separator);
	static std::string trimString(const std::string &str,
				      std::string char_set = STD_TRIM_CHAR_SET);

	static std::string replaceString(const std::string &search,
					 const std::string &replace,
					 const std::string &subject);

	static std::string toUpper(const std::string &str);
	static std::string toLower(const std::string &str);

	static std::string getUUID();

	static unsigned int _update_crc(unsigned int crc, const unsigned char *buf, int len);
	static unsigned int _crc(const unsigned char *buf, int len);
	static std::string crc(const std::string &s);
	static unsigned int crci(const std::string &s);
};

#endif // LOG_NOT_UTILITY_H

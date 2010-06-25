#include <stdarg.h>
#include <algorithm>

#include "utility.hpp"
#include "debug.hpp"

/* Table of CRCs of all 8-bit messages. */
unsigned int crc_table[256];

/* Flag: has the table been computed? Initially false. */
int crc_table_computed = 0;

int _formatString(std::string &str, const char *fmt, va_list &vargs, int size) {
	int rqsize = 0;
	char *p = new char[size+1];
	rqsize = vsnprintf(p, size, fmt, vargs);
	if (rqsize > size) {
		delete[] p;
		return rqsize;
	}
	str = p;
	delete[] p;
	return 0;
}

std::string LNUtility::formatString(const char *format, ...) {
	std::string result;
	int size = 256, rqsize = 0;
	va_list vargs;
	va_start (vargs, format);
	rqsize = _formatString (result, format, vargs, size);
	while(rqsize > size) {
		size = 2*rqsize;
		va_end(vargs);
		va_start(vargs, format);
		rqsize = _formatString (result, format, vargs, size);
	}
	return result;
}


std::vector<std::string> &LNUtility::explodeString(std::vector<std::string> &result, const std::string &str, const std::string &separator) {
	size_t frag_start = 0;
	size_t sep_pos = 0;
	std::string subs;
	result.clear();
	if (!str.size()) {
		result.push_back(str);
		return result;
	}
	sep_pos = str.find(separator);
	while(sep_pos != std::string::npos) {
		subs = str.substr(frag_start, sep_pos-frag_start);
		result.push_back(subs);
		frag_start = sep_pos + separator.size();
		sep_pos = str.find(separator, frag_start);
	}
	subs = str.substr(frag_start);
	if (0 < subs.size()) {
		result.push_back(subs);
	}
	return result;
}

std::string LNUtility::trimString(const std::string &str, std::string char_set) {
	size_t str_pos = str.find_first_not_of(char_set);

	if (str_pos != std::string::npos) {
		return str.substr(str_pos, str.find_last_not_of(char_set) - str_pos + 1);
	} else {
		return "";
	}
}

std::string LNUtility::replaceString(const std::string &search,
				     const std::string &replace,
				     const std::string &subject) {
	size_t pos = 0;
	std::string retVal = subject;
	const char *rb = replace.c_str();
	while(std::string::npos != (pos = retVal.find(search, 0)) ) {
		retVal = retVal.replace(pos, search.length(), rb);
	}
	return retVal;
}

std::string LNUtility::toUpper(const std::string &str) {
	std::string res;
	res.resize(str.size());

	std::transform(str.begin(), str.end(), res.begin(), (int(*)(int))toupper);
	return res;
}

std::string LNUtility::toLower(const std::string &str) {
	std::string res;
	res.resize(str.size());

	std::transform(str.begin(), str.end(), res.begin(), (int(*)(int))tolower);
	return res;
}

std::string LNUtility::getUUID() {
	int32_t uuid[4];
	size_t r = 0;
	int32_t tnow = (int32_t)time(NULL);
	FILE *fp = fopen("/dev/urandom", "r");
	if (!fp) {
		LNLog::logFatal("Failed to access /dev/urandom. '%s'. %s.", strerror(errno));
	}
	r = fread(uuid, sizeof(int32_t), 4, fp);
	if (r != 4) {
		LNLog::logError("Failed to generate proper UUID (%d).  [ line: %ld  file: %s ]",
				r, __LINE__, __FILE__);
	}
	fclose(fp);
	
	// end of uuid is affected by current time, this will reduce
	// already small probability to get two equal uuids for
	// particular case, better to be end cause indexing in
	// database
	memcpy( ( (&uuid[3]) + sizeof(int32_t) - sizeof(int32_t)), &tnow, sizeof(int32_t)); 
	return LNUtility::formatString("%x%x%x%x", uuid[0], uuid[1], uuid[2], uuid[3]);
}

/* Make the table for a fast CRC. */
void make_crc_table(void) {
	unsigned int c;
	int n, k;

	for (n = 0; n < 256; n++) {
		c = (unsigned int) n;
		for (k = 0; k < 8; k++) {
			if (c & 1) {
				c = 0xedb88320L ^ (c >> 1);
			} else {
				c = c >> 1;
			}
		}
		crc_table[n] = c;
	}
	crc_table_computed = 1;
}

unsigned int LNUtility::_update_crc(unsigned int crc, const unsigned char *buf, int len) {
	unsigned int c = crc ^ 0xffffffffL;
	int n;

	if (!crc_table_computed)
		make_crc_table();
	for (n = 0; n < len; n++) {
		c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c ^ 0xffffffffL;
}

unsigned int LNUtility::_crc(const unsigned char *buf, int len) {
	return LNUtility::_update_crc(0L, buf, len);
}

std::string LNUtility::crc(const std::string &s) {
	unsigned char *ps = (unsigned char*)(s.c_str());
	unsigned int crc = _crc(ps, s.length());
	unsigned char *pr = (unsigned char*)&crc;
	char out[sizeof(crc)*2 + 1], *wp;

	memset(out, 0, sizeof(out));
	wp = out;
	for (unsigned int n = 0; n < sizeof(crc); n++) {
		sprintf(wp, "%02x", pr[n]);
		wp += 2;
	}
	return out;
}

unsigned int LNUtility::crci(const std::string &s) {
	unsigned char *ps = (unsigned char*)(s.c_str());
	return _crc(ps, s.length());
}

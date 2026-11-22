// MEX - définitions de base partagées par tout le moteur.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

typedef std::string String;
typedef std::string StringName;
typedef float real_t;
typedef uint64_t ObjectID;

enum Error {
	OK,
	FAILED,
	ERR_FILE_NOT_FOUND,
	ERR_FILE_CANT_OPEN,
	ERR_FILE_CANT_WRITE,
	ERR_PARSE_ERROR,
	ERR_INVALID_PARAMETER,
	ERR_DOES_NOT_EXIST,
	ERR_ALREADY_EXISTS,
	ERR_UNAVAILABLE,
	ERR_CANT_CREATE,
	ERR_INVALID_DATA,
	ERR_CANT_CONNECT,
	ERR_CANT_RESOLVE,
	ERR_CONNECTION_ERROR,
	ERR_ALREADY_IN_USE,
	ERR_UNCONFIGURED,
};

// ---------------------------------------------------------------------------
// Journalisation (console + éditeur)
// ---------------------------------------------------------------------------
enum PrintLevel {
	PRINT_INFO,
	PRINT_WARNING,
	PRINT_ERROR,
};

typedef std::function<void(PrintLevel, const String &)> PrintHandler;

void print_line(const String &p_text);
void print_warning(const String &p_text);
void print_error(const String &p_text);
int add_print_handler(PrintHandler p_handler);
void remove_print_handler(int p_id);

String vformat(const char *p_fmt, ...);

#define ERR_PRINT(m_msg) print_error(String(m_msg))
#define WARN_PRINT(m_msg) print_warning(String(m_msg))

#define ERR_FAIL_COND(m_cond)                                                                   \
	do {                                                                                        \
		if (m_cond) {                                                                           \
			print_error(vformat("%s:%d - condition \"%s\" vraie.", __FILE__, __LINE__, #m_cond)); \
			return;                                                                             \
		}                                                                                       \
	} while (0)

#define ERR_FAIL_COND_V(m_cond, m_ret)                                                          \
	do {                                                                                        \
		if (m_cond) {                                                                           \
			print_error(vformat("%s:%d - condition \"%s\" vraie.", __FILE__, __LINE__, #m_cond)); \
			return m_ret;                                                                       \
		}                                                                                       \
	} while (0)

#define ERR_FAIL_COND_MSG(m_cond, m_msg) \
	do {                                 \
		if (m_cond) {                    \
			print_error(String(m_msg));  \
			return;                      \
		}                                \
	} while (0)

#define ERR_FAIL_COND_V_MSG(m_cond, m_ret, m_msg) \
	do {                                          \
		if (m_cond) {                             \
			print_error(String(m_msg));           \
			return m_ret;                         \
		}                                         \
	} while (0)

#define ERR_FAIL_NULL(m_ptr) ERR_FAIL_COND((m_ptr) == nullptr)
#define ERR_FAIL_NULL_V(m_ptr, m_ret) ERR_FAIL_COND_V((m_ptr) == nullptr, m_ret)
#define ERR_FAIL_INDEX(m_i, m_size) ERR_FAIL_COND((m_i) < 0 || (m_i) >= (int)(m_size))
#define ERR_FAIL_INDEX_V(m_i, m_size, m_ret) ERR_FAIL_COND_V((m_i) < 0 || (m_i) >= (int)(m_size), m_ret)

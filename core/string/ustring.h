// Utilitaires de chaînes (UTF-8) et de chemins.
#pragma once

#include "core/typedefs.h"

String itos(int64_t p_val);
String rtos(double p_val); // représentation courte et relisible

bool str_begins_with(const String &p_s, const String &p_prefix);
bool str_ends_with(const String &p_s, const String &p_suffix);
std::vector<String> str_split(const String &p_s, const String &p_delim, bool p_allow_empty = true);
String str_join(const std::vector<String> &p_parts, const String &p_sep);
String str_strip(const String &p_s);
String str_replace(const String &p_s, const String &p_from, const String &p_to);
String str_to_lower(const String &p_s);
String str_to_upper(const String &p_s);
String str_capitalize(const String &p_s); // "wait_time" -> "Wait Time"
String str_c_escape(const String &p_s);
bool str_is_valid_identifier(const String &p_s);
bool str_match_nocase(const String &p_s, const String &p_filter); // recherche de sous-chaîne insensible à la casse

String path_get_extension(const String &p_path); // en minuscules, sans le point
String path_get_file(const String &p_path);
String path_get_base_dir(const String &p_path);
String path_get_basename(const String &p_path); // sans extension
String path_join(const String &p_a, const String &p_b);
String path_simplify(const String &p_path);

std::vector<uint32_t> utf8_decode(const String &p_s);
String utf8_encode(uint32_t p_codepoint);

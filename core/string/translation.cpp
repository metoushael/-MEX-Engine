#include "translation.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"

TranslationServer *TranslationServer::singleton = nullptr;

TranslationServer::TranslationServer() {
	singleton = this;
}

TranslationServer::~TranslationServer() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

String TTR_GAME(const String &p_key) {
	TranslationServer *server = TranslationServer::get_singleton();
	return server ? server->translate(p_key) : p_key;
}

// « fr-FR », « fr_FR.UTF-8 » -> « fr_FR ».
String TranslationServer::standardize_locale(const String &p_locale) {
	String out;
	for (char c : p_locale) {
		if (c == '.' || c == '@') {
			break;
		}
		out += c == '-' ? '_' : c;
	}
	const size_t underscore = out.find('_');
	for (size_t i = 0; i < out.size(); i++) {
		const bool country = underscore != String::npos && i > underscore;
		out[i] = (char)(country ? std::toupper((unsigned char)out[i]) : std::tolower((unsigned char)out[i]));
	}
	return out;
}

std::vector<std::vector<String>> TranslationServer::split_csv(const String &p_text, char p_delimiter) {
	std::vector<std::vector<String>> rows;
	std::vector<String> row;
	String field;
	bool quoted = false;
	bool any = false;
	size_t i = 0;
	// Marque d'ordre d'octets UTF-8 en tête de fichier (tableurs).
	if (p_text.size() >= 3 && (unsigned char)p_text[0] == 0xEF && (unsigned char)p_text[1] == 0xBB && (unsigned char)p_text[2] == 0xBF) {
		i = 3;
	}
	for (; i < p_text.size(); i++) {
		const char c = p_text[i];
		if (quoted) {
			if (c == '"') {
				if (i + 1 < p_text.size() && p_text[i + 1] == '"') {
					field += '"'; // guillemet doublé
					i++;
				} else {
					quoted = false;
				}
			} else {
				field += c;
			}
			continue;
		}
		if (c == '"') {
			quoted = true;
			any = true;
		} else if (c == p_delimiter) {
			row.push_back(field);
			field.clear();
			any = true;
		} else if (c == '\n' || c == '\r') {
			if (c == '\r' && i + 1 < p_text.size() && p_text[i + 1] == '\n') {
				i++;
			}
			if (any || !field.empty()) {
				row.push_back(field);
				rows.push_back(row);
			}
			row.clear();
			field.clear();
			any = false;
		} else {
			field += c;
		}
	}
	if (any || !field.empty()) {
		row.push_back(field);
		rows.push_back(row);
	}
	return rows;
}

Error TranslationServer::parse_csv(const String &p_text) {
	const std::vector<std::vector<String>> rows = split_csv(p_text);
	if (rows.empty() || rows[0].size() < 2) {
		return ERR_PARSE_ERROR;
	}
	std::vector<String> columns;
	for (size_t i = 1; i < rows[0].size(); i++) {
		columns.push_back(standardize_locale(rows[0][i]));
	}
	for (size_t r = 1; r < rows.size(); r++) {
		const std::vector<String> &row = rows[r];
		if (row.empty() || row[0].empty()) {
			continue;
		}
		for (size_t i = 1; i < row.size() && i - 1 < columns.size(); i++) {
			if (columns[i - 1].empty() || row[i].empty()) {
				continue; // case vide : la langue de repli sera utilisée
			}
			// « \n » écrit en toutes lettres dans le fichier devient un retour à la ligne.
			String text;
			for (size_t k = 0; k < row[i].size(); k++) {
				if (row[i][k] == '\\' && k + 1 < row[i].size() && row[i][k + 1] == 'n') {
					text += '\n';
					k++;
				} else {
					text += row[i][k];
				}
			}
			add_message(columns[i - 1], row[0], text);
		}
	}
	return OK;
}

Error TranslationServer::load_csv(const String &p_path) {
	Error error = OK;
	const String text = FileAccess::get_file_as_string(p_path, &error);
	if (error != OK) {
		print_error("Traductions : impossible de lire '" + p_path + "'.");
		return error;
	}
	error = parse_csv(text);
	if (error != OK) {
		print_error("Traductions : '" + p_path + "' n'est pas un tableau CSV de traductions (première ligne : keys,fr,en...).");
	}
	return error;
}

void TranslationServer::add_message(const String &p_locale, const String &p_key, const String &p_text) {
	const String name = standardize_locale(p_locale);
	if (name.empty()) {
		return;
	}
	if (!messages.count(name)) {
		locales.push_back(name);
	}
	messages[name][p_key] = p_text;
	generation++;
}

void TranslationServer::add_translation(const String &p_locale, const Dictionary &p_messages) {
	for (const auto &kv : p_messages.map()) {
		add_message(p_locale, kv.first.as_string(), kv.second.as_string());
	}
}

void TranslationServer::clear() {
	messages.clear();
	locales.clear();
	generation++;
}

Array TranslationServer::get_loaded_locales() const {
	Array out;
	for (const String &name : locales) {
		out.push_back(Variant(name));
	}
	return out;
}

String TranslationServer::find_closest_locale(const String &p_locale) const {
	const String name = standardize_locale(p_locale);
	if (messages.count(name)) {
		return name;
	}
	const String language = name.substr(0, name.find('_'));
	if (messages.count(language)) {
		return language;
	}
	// Une variante régionale de la même langue (« fr » demandé, « fr_CA » chargé).
	for (const String &candidate : locales) {
		if (candidate.substr(0, candidate.find('_')) == language) {
			return candidate;
		}
	}
	return String();
}

void TranslationServer::set_locale(const String &p_locale) {
	const String name = standardize_locale(p_locale);
	if (name.empty() || name == locale) {
		return;
	}
	locale = name;
	generation++;
	emit_signal("locale_changed");
}

void TranslationServer::set_fallback_locale(const String &p_locale) {
	fallback = standardize_locale(p_locale);
	generation++;
}

const String *TranslationServer::_find(const String &p_locale, const String &p_key) const {
	const String closest = find_closest_locale(p_locale);
	if (closest.empty()) {
		return nullptr;
	}
	const auto &table = messages.at(closest);
	auto it = table.find(p_key);
	return it == table.end() ? nullptr : &it->second;
}

String TranslationServer::translate(const String &p_key) const {
	if (messages.empty() || p_key.empty()) {
		return p_key;
	}
	if (const String *text = _find(locale, p_key)) {
		return *text;
	}
	if (const String *text = _find(fallback, p_key)) {
		return *text;
	}
	return p_key;
}

bool TranslationServer::has_message(const String &p_key) const {
	return _find(locale, p_key) != nullptr || _find(fallback, p_key) != nullptr;
}

void TranslationServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_locale", "locale"), &TranslationServer::set_locale);
	ClassDB::bind_method(D_METHOD("get_locale"), &TranslationServer::get_locale);
	ClassDB::bind_method(D_METHOD("set_fallback_locale", "locale"), &TranslationServer::set_fallback_locale);
	ClassDB::bind_method(D_METHOD("get_fallback_locale"), &TranslationServer::get_fallback_locale);
	ClassDB::bind_method(D_METHOD("get_loaded_locales"), &TranslationServer::get_loaded_locales);
	ClassDB::bind_method(D_METHOD("add_message", "locale", "key", "text"), &TranslationServer::add_message);
	ClassDB::bind_method(D_METHOD("add_translation", "locale", "messages"), &TranslationServer::add_translation);
	ClassDB::bind_method(D_METHOD("load_csv", "path"), &TranslationServer::load_csv);
	ClassDB::bind_method(D_METHOD("clear"), &TranslationServer::clear);
	ClassDB::bind_method(D_METHOD("translate", "key"), &TranslationServer::translate);
	ClassDB::bind_method(D_METHOD("has_message", "key"), &TranslationServer::has_message);
	ADD_SIGNAL(MethodInfo("locale_changed"));
}

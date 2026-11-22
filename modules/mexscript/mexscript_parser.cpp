#include "mexscript_parser.h"

#include "core/string/ustring.h"

#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// Analyse lexicale
// ---------------------------------------------------------------------------
enum TokenType {
	TK_EOF,
	TK_NEWLINE,
	TK_INDENT,
	TK_DEDENT,
	TK_IDENT,
	TK_NUMBER,
	TK_STRING,
	TK_NODEPATH,
	TK_ANNOTATION,
	TK_SYMBOL,
};

struct Token {
	TokenType type = TK_EOF;
	String text;
	Variant value;
	int line = 1;
};

struct Tokenizer {
	const String &src;
	size_t pos = 0;
	int line = 1;
	int paren_depth = 0;
	std::vector<int> indents = { 0 };
	std::vector<Token> tokens;
	String error;
	int error_line = 0;

	Tokenizer(const String &p_src) :
			src(p_src) {}

	void fail(const String &p_msg) {
		if (error.empty()) {
			error = p_msg;
			error_line = line;
		}
	}

	void push(TokenType p_type, const String &p_text = String(), const Variant &p_value = Variant()) {
		Token t;
		t.type = p_type;
		t.text = p_text;
		t.value = p_value;
		t.line = line;
		tokens.push_back(t);
	}

	static bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (unsigned char)c >= 0x80; }
	static bool is_digit(char c) { return c >= '0' && c <= '9'; }
	static bool is_ident_char(char c) { return is_ident_start(c) || is_digit(c); }

	// Traite l'indentation en début de ligne logique. Retourne faux en fin de fichier.
	bool handle_line_start() {
		while (true) {
			int column = 0;
			size_t p = pos;
			while (p < src.size() && (src[p] == ' ' || src[p] == '\t')) {
				column += src[p] == '\t' ? 4 : 1;
				p++;
			}
			if (p >= src.size()) {
				pos = p;
				return false;
			}
			if (src[p] == '\r') {
				p++;
			}
			if (p < src.size() && src[p] == '\n') { // ligne vide
				pos = p + 1;
				line++;
				continue;
			}
			if (p < src.size() && src[p] == '#') { // ligne de commentaire
				while (p < src.size() && src[p] != '\n') {
					p++;
				}
				pos = p;
				continue;
			}
			pos = p;
			if (column > indents.back()) {
				indents.push_back(column);
				push(TK_INDENT);
			} else {
				while (column < indents.back()) {
					indents.pop_back();
					push(TK_DEDENT);
				}
				if (column != indents.back()) {
					fail("Indentation incohérente.");
				}
			}
			return true;
		}
	}

	void read_string(char p_quote) {
		pos++; // guillemet ouvrant
		String out;
		while (pos < src.size() && src[pos] != p_quote) {
			char c = src[pos];
			if (c == '\n') {
				line++;
			}
			if (c == '\\' && pos + 1 < src.size()) {
				pos++;
				switch (src[pos]) {
					case 'n': out += '\n'; break;
					case 't': out += '\t'; break;
					case 'r': out += '\r'; break;
					case '0': out += '\0'; break;
					case '\n': line++; break; // continuation
					default: out += src[pos]; break;
				}
				pos++;
				continue;
			}
			out += c;
			pos++;
		}
		if (pos >= src.size()) {
			fail("Chaîne de caractères non terminée.");
			return;
		}
		pos++; // guillemet fermant
		push(TK_STRING, out, Variant(out));
	}

	void read_number() {
		size_t start = pos;
		bool is_float = false;
		if (src[pos] == '0' && pos + 1 < src.size() && (src[pos + 1] == 'x' || src[pos + 1] == 'X')) {
			pos += 2;
			while (pos < src.size() && (isxdigit((unsigned char)src[pos]) || src[pos] == '_')) {
				pos++;
			}
			String digits = str_replace(src.substr(start + 2, pos - start - 2), "_", "");
			push(TK_NUMBER, src.substr(start, pos - start), Variant((int64_t)strtoll(digits.c_str(), nullptr, 16)));
			return;
		}
		while (pos < src.size() && (is_digit(src[pos]) || src[pos] == '_')) {
			pos++;
		}
		if (pos < src.size() && src[pos] == '.' && pos + 1 < src.size() && is_digit(src[pos + 1])) {
			is_float = true;
			pos++;
			while (pos < src.size() && (is_digit(src[pos]) || src[pos] == '_')) {
				pos++;
			}
		} else if (pos < src.size() && src[pos] == '.' && !(pos + 1 < src.size() && (is_ident_start(src[pos + 1]) || src[pos + 1] == '.'))) {
			is_float = true; // "1."
			pos++;
		}
		if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
			size_t save = pos;
			pos++;
			if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) {
				pos++;
			}
			if (pos < src.size() && is_digit(src[pos])) {
				is_float = true;
				while (pos < src.size() && is_digit(src[pos])) {
					pos++;
				}
			} else {
				pos = save;
			}
		}
		String text = str_replace(src.substr(start, pos - start), "_", "");
		if (is_float) {
			push(TK_NUMBER, text, Variant(strtod(text.c_str(), nullptr)));
		} else {
			push(TK_NUMBER, text, Variant((int64_t)strtoll(text.c_str(), nullptr, 10)));
		}
	}

	void run() {
		bool at_line_start = true;
		while (error.empty()) {
			if (at_line_start && paren_depth == 0) {
				if (!handle_line_start()) {
					break;
				}
				at_line_start = false;
			}
			if (pos >= src.size()) {
				break;
			}
			char c = src[pos];
			if (c == ' ' || c == '\t' || c == '\r') {
				pos++;
				continue;
			}
			if (c == '#') {
				while (pos < src.size() && src[pos] != '\n') {
					pos++;
				}
				continue;
			}
			if (c == '\\' && pos + 1 < src.size() && (src[pos + 1] == '\n' || src[pos + 1] == '\r')) {
				pos++;
				while (pos < src.size() && src[pos] != '\n') {
					pos++;
				}
				pos++;
				line++;
				continue; // continuation de ligne
			}
			if (c == '\n') {
				pos++;
				if (paren_depth == 0) {
					if (!tokens.empty() && tokens.back().type != TK_NEWLINE) {
						push(TK_NEWLINE);
					}
					at_line_start = true;
				}
				line++;
				continue;
			}
			if (c == '"' || c == '\'') {
				read_string(c);
				continue;
			}
			if (is_digit(c) || (c == '.' && pos + 1 < src.size() && is_digit(src[pos + 1]) && (tokens.empty() || tokens.back().type == TK_SYMBOL || tokens.back().type == TK_NEWLINE || tokens.back().type == TK_INDENT))) {
				read_number();
				continue;
			}
			if (is_ident_start(c)) {
				size_t start = pos;
				while (pos < src.size() && is_ident_char(src[pos])) {
					pos++;
				}
				push(TK_IDENT, src.substr(start, pos - start));
				continue;
			}
			if (c == '@') {
				pos++;
				size_t start = pos;
				while (pos < src.size() && is_ident_char(src[pos])) {
					pos++;
				}
				push(TK_ANNOTATION, src.substr(start, pos - start));
				continue;
			}
			if (c == '$') {
				pos++;
				if (pos < src.size() && (src[pos] == '"' || src[pos] == '\'')) {
					read_string(src[pos]);
					if (!tokens.empty() && tokens.back().type == TK_STRING) {
						tokens.back().type = TK_NODEPATH;
					}
				} else {
					size_t start = pos;
					while (pos < src.size() && (is_ident_char(src[pos]) || src[pos] == '/' || (src[pos] == '.' && pos + 1 < src.size() && src[pos + 1] == '.') || (src[pos] == '.' && pos > start && src[pos - 1] == '.'))) {
						pos++;
					}
					if (pos == start) {
						fail("Chemin de nœud attendu après '$'.");
					}
					push(TK_NODEPATH, src.substr(start, pos - start));
				}
				continue;
			}
			// Symboles
			static const char *two_chars[] = { "==", "!=", "<=", ">=", "+=", "-=", "*=", "/=", "%=", "->", "&&", "||", "<<", ">>", ":=", "**" };
			bool matched = false;
			for (const char *sym : two_chars) {
				if (src.compare(pos, 2, sym) == 0) {
					push(TK_SYMBOL, sym);
					pos += 2;
					matched = true;
					break;
				}
			}
			if (matched) {
				continue;
			}
			if (strchr("+-*/%=<>()[]{},:.!&|^~;", c)) {
				if (c == '(' || c == '[' || c == '{') {
					paren_depth++;
				} else if (c == ')' || c == ']' || c == '}') {
					paren_depth = std::max(0, paren_depth - 1);
				}
				push(TK_SYMBOL, String(1, c));
				pos++;
				continue;
			}
			fail(String("Caractère inattendu : '") + c + "'.");
		}
		if (!tokens.empty() && tokens.back().type != TK_NEWLINE) {
			push(TK_NEWLINE);
		}
		while (indents.size() > 1) {
			indents.pop_back();
			push(TK_DEDENT);
		}
		push(TK_EOF);
	}
};

// ---------------------------------------------------------------------------
// Analyse syntaxique
// ---------------------------------------------------------------------------
struct Parser {
	std::vector<Token> tokens;
	size_t pos = 0;
	MXParseResult &result;

	Parser(MXParseResult &r_result) :
			result(r_result) {}

	const Token &peek(int p_offset = 0) const {
		size_t i = std::min(pos + (size_t)p_offset, tokens.size() - 1);
		return tokens[i];
	}
	const Token &advance() {
		const Token &t = tokens[pos];
		if (pos < tokens.size() - 1) {
			pos++;
		}
		return t;
	}
	bool failed() const { return result.has_error; }

	void fail(const String &p_msg, int p_line = -1) {
		if (!result.has_error) {
			result.has_error = true;
			result.error = p_msg;
			result.error_line = p_line >= 0 ? p_line : peek().line;
		}
	}

	bool is_symbol(const char *p_sym, int p_offset = 0) const {
		const Token &t = peek(p_offset);
		return t.type == TK_SYMBOL && t.text == p_sym;
	}
	bool is_keyword(const char *p_kw, int p_offset = 0) const {
		const Token &t = peek(p_offset);
		return t.type == TK_IDENT && t.text == p_kw;
	}
	bool accept_symbol(const char *p_sym) {
		if (is_symbol(p_sym)) {
			advance();
			return true;
		}
		return false;
	}
	bool accept_keyword(const char *p_kw) {
		if (is_keyword(p_kw)) {
			advance();
			return true;
		}
		return false;
	}
	void expect_symbol(const char *p_sym) {
		if (!accept_symbol(p_sym)) {
			fail(String("'") + p_sym + "' attendu" + describe_found());
		}
	}
	String describe_found() const {
		const Token &t = peek();
		switch (t.type) {
			case TK_EOF: return " (fin du fichier atteinte).";
			case TK_NEWLINE: return " (fin de ligne trouvée).";
			case TK_INDENT: return " (indentation inattendue).";
			case TK_DEDENT: return " (fin de bloc trouvée).";
			default: return " ('" + t.text + "' trouvé).";
		}
	}
	String expect_identifier(const char *p_what) {
		if (peek().type != TK_IDENT) {
			fail(String(p_what) + " attendu" + describe_found());
			return String();
		}
		return advance().text;
	}
	void end_statement() {
		if (accept_symbol(";")) {
			if (peek().type == TK_NEWLINE) {
				advance();
			}
			return;
		}
		if (peek().type == TK_NEWLINE) {
			advance();
		} else if (peek().type != TK_EOF && peek().type != TK_DEDENT) {
			fail("Fin de ligne attendue" + describe_found());
		}
	}

	MXNodeRef make(MXNode::Type p_type, int p_line = -1) {
		MXNodeRef n = std::make_shared<MXNode>();
		n->type = p_type;
		n->line = p_line >= 0 ? p_line : peek().line;
		return n;
	}

	// Ignore une annotation de type : Nom, Nom[Type], Nom.SousNom
	String parse_type() {
		String name = expect_identifier("Nom de type");
		while (accept_symbol(".")) {
			name = expect_identifier("Nom de type");
		}
		if (accept_symbol("[")) {
			int depth = 1;
			while (depth > 0 && peek().type != TK_EOF && peek().type != TK_NEWLINE) {
				if (is_symbol("[")) {
					depth++;
				} else if (is_symbol("]")) {
					depth--;
				}
				advance();
			}
		}
		return name;
	}

	// --- Expressions ---
	MXNodeRef parse_expression() { return parse_ternary(); }

	MXNodeRef parse_ternary() {
		MXNodeRef value = parse_or();
		if (is_keyword("if") && !failed()) {
			int line = peek().line;
			advance();
			MXNodeRef cond = parse_or();
			if (!accept_keyword("else")) {
				fail("'else' attendu dans l'expression conditionnelle" + describe_found());
				return value;
			}
			MXNodeRef other = parse_ternary();
			MXNodeRef n = make(MXNode::TERNARY, line);
			n->children = { cond, value, other };
			return n;
		}
		return value;
	}

	MXNodeRef binary(int p_op, const MXNodeRef &p_a, const MXNodeRef &p_b, int p_line) {
		MXNodeRef n = make(MXNode::BINARY, p_line);
		n->op = p_op;
		n->children = { p_a, p_b };
		return n;
	}

	MXNodeRef parse_or() {
		MXNodeRef left = parse_and();
		while (!failed() && (is_keyword("or") || is_symbol("||"))) {
			int line = advance().line;
			left = binary(MXNode::OP_OR, left, parse_and(), line);
		}
		return left;
	}

	MXNodeRef parse_and() {
		MXNodeRef left = parse_not();
		while (!failed() && (is_keyword("and") || is_symbol("&&"))) {
			int line = advance().line;
			left = binary(MXNode::OP_AND, left, parse_not(), line);
		}
		return left;
	}

	MXNodeRef parse_not() {
		if (is_keyword("not") || is_symbol("!")) {
			int line = advance().line;
			MXNodeRef n = make(MXNode::UNARY, line);
			n->op = MXNode::OP_NOT;
			n->children = { parse_not() };
			return n;
		}
		return parse_in();
	}

	MXNodeRef parse_in() {
		MXNodeRef left = parse_comparison();
		while (!failed()) {
			if (is_keyword("in")) {
				int line = advance().line;
				left = binary(Variant::OP_IN, left, parse_comparison(), line);
			} else if (is_keyword("not") && is_keyword("in", 1)) {
				int line = advance().line;
				advance();
				left = binary(MXNode::OP_NOT_IN, left, parse_comparison(), line);
			} else {
				break;
			}
		}
		return left;
	}

	MXNodeRef parse_comparison() {
		MXNodeRef left = parse_bit_or();
		while (!failed()) {
			int op = -1;
			if (is_symbol("==")) {
				op = Variant::OP_EQ;
			} else if (is_symbol("!=")) {
				op = Variant::OP_NE;
			} else if (is_symbol("<")) {
				op = Variant::OP_LT;
			} else if (is_symbol("<=")) {
				op = Variant::OP_LE;
			} else if (is_symbol(">")) {
				op = Variant::OP_GT;
			} else if (is_symbol(">=")) {
				op = Variant::OP_GE;
			} else if (is_keyword("is")) {
				int line = advance().line;
				bool negate = accept_keyword("not");
				MXNodeRef type_node = make(MXNode::IDENTIFIER);
				type_node->name = parse_type();
				left = binary(negate ? MXNode::OP_IS_NOT : MXNode::OP_IS, left, type_node, line);
				continue;
			} else if (is_keyword("as")) {
				advance();
				parse_type(); // conversion statique : sans effet à l'exécution
				continue;
			}
			if (op < 0) {
				break;
			}
			int line = advance().line;
			left = binary(op, left, parse_bit_or(), line);
		}
		return left;
	}

	MXNodeRef parse_bit_or() {
		MXNodeRef left = parse_bit_xor();
		while (!failed() && is_symbol("|")) {
			int line = advance().line;
			left = binary(MXNode::OP_BIT_OR, left, parse_bit_xor(), line);
		}
		return left;
	}

	MXNodeRef parse_bit_xor() {
		MXNodeRef left = parse_bit_and();
		while (!failed() && is_symbol("^")) {
			int line = advance().line;
			left = binary(MXNode::OP_BIT_XOR, left, parse_bit_and(), line);
		}
		return left;
	}

	MXNodeRef parse_bit_and() {
		MXNodeRef left = parse_shift();
		while (!failed() && is_symbol("&")) {
			int line = advance().line;
			left = binary(MXNode::OP_BIT_AND, left, parse_shift(), line);
		}
		return left;
	}

	MXNodeRef parse_shift() {
		MXNodeRef left = parse_additive();
		while (!failed() && (is_symbol("<<") || is_symbol(">>"))) {
			int op = is_symbol("<<") ? MXNode::OP_SHIFT_LEFT : MXNode::OP_SHIFT_RIGHT;
			int line = advance().line;
			left = binary(op, left, parse_additive(), line);
		}
		return left;
	}

	MXNodeRef parse_additive() {
		MXNodeRef left = parse_multiplicative();
		while (!failed() && (is_symbol("+") || is_symbol("-"))) {
			int op = is_symbol("+") ? Variant::OP_ADD : Variant::OP_SUB;
			int line = advance().line;
			left = binary(op, left, parse_multiplicative(), line);
		}
		return left;
	}

	MXNodeRef parse_multiplicative() {
		MXNodeRef left = parse_unary();
		while (!failed() && (is_symbol("*") || is_symbol("/") || is_symbol("%"))) {
			int op = is_symbol("*") ? Variant::OP_MUL : (is_symbol("/") ? Variant::OP_DIV : Variant::OP_MOD);
			int line = advance().line;
			left = binary(op, left, parse_unary(), line);
		}
		return left;
	}

	MXNodeRef parse_unary() {
		if (is_symbol("-") || is_symbol("+") || is_symbol("~")) {
			const Token &t = advance();
			MXNodeRef operand = parse_unary();
			// Replie les littéraux négatifs.
			if (t.text == "-" && operand && operand->type == MXNode::LITERAL && operand->value.is_num()) {
				if (operand->value.get_type() == Variant::INT) {
					operand->value = Variant(-operand->value.as_int());
				} else {
					operand->value = Variant(-operand->value.as_float());
				}
				return operand;
			}
			MXNodeRef n = make(MXNode::UNARY, t.line);
			n->op = t.text == "-" ? (int)Variant::OP_NEG : (t.text == "~" ? (int)MXNode::OP_BIT_NOT : (int)MXNode::OP_POSITIVE);
			n->children = { operand };
			return n;
		}
		return parse_postfix();
	}

	void parse_arguments(MXNodeRef &r_call, const char *p_close) {
		while (!failed() && !is_symbol(p_close)) {
			r_call->children.push_back(parse_expression());
			if (!accept_symbol(",")) {
				break;
			}
		}
		expect_symbol(p_close);
	}

	MXNodeRef parse_postfix() {
		MXNodeRef node = parse_primary();
		while (!failed()) {
			if (is_symbol("(")) {
				int line = advance().line;
				MXNodeRef call = make(MXNode::CALL, line);
				call->children.push_back(node);
				parse_arguments(call, ")");
				node = call;
			} else if (is_symbol("[")) {
				int line = advance().line;
				MXNodeRef index = make(MXNode::INDEX, line);
				index->children = { node, parse_expression() };
				expect_symbol("]");
				node = index;
			} else if (is_symbol(".")) {
				int line = advance().line;
				MXNodeRef attr = make(MXNode::ATTRIBUTE, line);
				attr->name = expect_identifier("Nom de membre");
				attr->children = { node };
				node = attr;
			} else {
				break;
			}
		}
		return node;
	}

	MXNodeRef parse_primary() {
		const Token &t = peek();
		switch (t.type) {
			case TK_NUMBER:
			case TK_STRING: {
				MXNodeRef n = make(MXNode::LITERAL);
				n->value = t.value;
				advance();
				return n;
			}
			case TK_NODEPATH: {
				MXNodeRef n = make(MXNode::GET_NODE);
				n->name = t.text;
				advance();
				return n;
			}
			case TK_IDENT: {
				if (t.text == "true" || t.text == "false") {
					MXNodeRef n = make(MXNode::LITERAL);
					n->value = Variant(t.text == "true");
					advance();
					return n;
				}
				if (t.text == "null") {
					MXNodeRef n = make(MXNode::LITERAL);
					advance();
					return n;
				}
				if (t.text == "self") {
					MXNodeRef n = make(MXNode::SELF);
					advance();
					return n;
				}
				if (t.text == "func") {
					return parse_lambda();
				}
				static const char *reserved[] = { "if", "elif", "else", "for", "while", "match", "return", "break", "continue", "pass", "var", "const", "signal", "extends", "and", "or", "not", "in", "is", "enum", "class_name", "static" };
				for (const char *kw : reserved) {
					if (t.text == kw) {
						fail("Expression attendue, mot-clé '" + t.text + "' trouvé.");
						return make(MXNode::LITERAL);
					}
				}
				MXNodeRef n = make(MXNode::IDENTIFIER);
				n->name = t.text;
				advance();
				return n;
			}
			case TK_SYMBOL: {
				if (t.text == "(") {
					advance();
					MXNodeRef n = parse_expression();
					expect_symbol(")");
					return n;
				}
				if (t.text == "[") {
					MXNodeRef n = make(MXNode::ARRAY);
					advance();
					parse_arguments(n, "]");
					return n;
				}
				if (t.text == "{") {
					MXNodeRef n = make(MXNode::DICTIONARY);
					advance();
					while (!failed() && !is_symbol("}")) {
						MXNodeRef key;
						// Style Lua : { nom = valeur }
						if (peek().type == TK_IDENT && is_symbol("=", 1)) {
							key = make(MXNode::LITERAL);
							key->value = Variant(advance().text);
							advance();
						} else {
							key = parse_expression();
							expect_symbol(":");
						}
						n->children.push_back(key);
						n->children.push_back(parse_expression());
						if (!accept_symbol(",")) {
							break;
						}
					}
					expect_symbol("}");
					return n;
				}
				break;
			}
			default:
				break;
		}
		fail("Expression attendue" + describe_found());
		return make(MXNode::LITERAL);
	}

	void parse_parameters(MXFunction &r_func) {
		expect_symbol("(");
		while (!failed() && !is_symbol(")")) {
			r_func.params.push_back(expect_identifier("Nom de paramètre"));
			r_func.param_types.push_back(String());
			if (accept_symbol(":")) {
				r_func.param_types.back() = parse_type();
			}
			if (accept_symbol("=") || accept_symbol(":=")) {
				r_func.defaults.push_back(parse_expression());
			} else if (!r_func.defaults.empty()) {
				fail("Un paramètre sans valeur par défaut ne peut pas suivre un paramètre qui en a une.");
			}
			if (!accept_symbol(",")) {
				break;
			}
		}
		expect_symbol(")");
		if (accept_symbol("->")) {
			parse_type();
		}
	}

	MXNodeRef parse_lambda() {
		int line = advance().line; // func
		MXFunction func;
		func.line = line;
		if (peek().type == TK_IDENT) {
			func.name = advance().text;
		} else {
			func.name = "<lambda>";
		}
		parse_parameters(func);
		expect_symbol(":");
		// Corps sur une seule ligne uniquement (à l'intérieur d'une expression).
		MXNodeRef body = make(MXNode::BLOCK);
		if (!failed()) {
			body->children.push_back(parse_simple_statement(false));
		}
		func.body = body;
		MXNodeRef n = make(MXNode::LAMBDA, line);
		n->function_index = (int)result.functions.size();
		result.functions.push_back(func);
		return n;
	}

	// --- Instructions ---
	MXNodeRef parse_block() {
		MXNodeRef block = make(MXNode::BLOCK);
		if (peek().type == TK_NEWLINE) {
			advance();
			if (peek().type != TK_INDENT) {
				fail("Bloc indenté attendu" + describe_found());
				return block;
			}
			advance();
			while (!failed() && peek().type != TK_DEDENT && peek().type != TK_EOF) {
				block->children.push_back(parse_statement());
			}
			if (peek().type == TK_DEDENT) {
				advance();
			}
		} else {
			// Bloc sur la même ligne : `if x: return`
			block->children.push_back(parse_statement());
		}
		return block;
	}

	MXNodeRef parse_var_statement() {
		MXNodeRef n = make(MXNode::VAR);
		advance(); // var
		n->name = expect_identifier("Nom de variable");
		if (accept_symbol(":")) {
			if (!is_symbol("=")) {
				n->value = Variant(parse_type());
			}
		}
		if (accept_symbol("=") || accept_symbol(":=")) {
			n->children.push_back(parse_expression());
		}
		return n;
	}

	MXNodeRef parse_simple_statement(bool p_end = true) {
		MXNodeRef n;
		if (is_keyword("var")) {
			n = parse_var_statement();
		} else if (is_keyword("return")) {
			n = make(MXNode::RETURN);
			advance();
			if (peek().type != TK_NEWLINE && peek().type != TK_EOF && peek().type != TK_DEDENT && !is_symbol(";") && !is_symbol(")") && !is_symbol(",")) {
				n->children.push_back(parse_expression());
			}
		} else if (is_keyword("break")) {
			n = make(MXNode::BREAK);
			advance();
		} else if (is_keyword("continue")) {
			n = make(MXNode::CONTINUE);
			advance();
		} else if (is_keyword("pass")) {
			n = make(MXNode::PASS);
			advance();
		} else {
			int line = peek().line;
			MXNodeRef expr = parse_expression();
			static const struct {
				const char *sym;
				int op;
			} assign_ops[] = {
				{ "=", -1 }, { "+=", Variant::OP_ADD }, { "-=", Variant::OP_SUB }, { "*=", Variant::OP_MUL }, { "/=", Variant::OP_DIV }, { "%=", Variant::OP_MOD },
			};
			bool assigned = false;
			for (auto &ao : assign_ops) {
				if (is_symbol(ao.sym)) {
					advance();
					if (expr->type != MXNode::IDENTIFIER && expr->type != MXNode::ATTRIBUTE && expr->type != MXNode::INDEX) {
						fail("La cible de l'affectation n'est pas valide.", line);
					}
					n = make(MXNode::ASSIGN, line);
					n->op = ao.op;
					n->children = { expr, parse_expression() };
					assigned = true;
					break;
				}
			}
			if (!assigned) {
				n = make(MXNode::EXPRESSION, line);
				n->children = { expr };
			}
		}
		if (p_end) {
			end_statement();
		}
		return n;
	}

	MXNodeRef parse_if() {
		MXNodeRef n = make(MXNode::IF);
		advance(); // if / elif
		n->children.push_back(parse_expression());
		expect_symbol(":");
		n->children.push_back(parse_block());
		if (is_keyword("elif")) {
			n->children.push_back(parse_if());
		} else if (accept_keyword("else")) {
			expect_symbol(":");
			n->children.push_back(parse_block());
		}
		return n;
	}

	MXNodeRef parse_statement() {
		if (failed()) {
			advance();
			return make(MXNode::PASS);
		}
		if (is_keyword("if")) {
			return parse_if();
		}
		if (is_keyword("while")) {
			MXNodeRef n = make(MXNode::WHILE);
			advance();
			n->children.push_back(parse_expression());
			expect_symbol(":");
			n->children.push_back(parse_block());
			return n;
		}
		if (is_keyword("for")) {
			MXNodeRef n = make(MXNode::FOR);
			advance();
			n->name = expect_identifier("Nom de variable");
			if (accept_symbol(":")) {
				parse_type();
			}
			if (!accept_keyword("in")) {
				fail("'in' attendu dans la boucle for" + describe_found());
			}
			n->children.push_back(parse_expression());
			expect_symbol(":");
			n->children.push_back(parse_block());
			return n;
		}
		if (is_keyword("match")) {
			MXNodeRef n = make(MXNode::MATCH);
			advance();
			n->children.push_back(parse_expression());
			expect_symbol(":");
			if (peek().type != TK_NEWLINE) {
				fail("Fin de ligne attendue après 'match'" + describe_found());
				return n;
			}
			advance();
			if (peek().type != TK_INDENT) {
				fail("Bloc indenté attendu après 'match'" + describe_found());
				return n;
			}
			advance();
			while (!failed() && peek().type != TK_DEDENT && peek().type != TK_EOF) {
				MXNodeRef patterns = make(MXNode::ARRAY);
				if (is_keyword("_") && is_symbol(":", 1)) {
					advance(); // motif générique
				} else {
					do {
						patterns->children.push_back(parse_expression());
					} while (!failed() && accept_symbol(","));
				}
				expect_symbol(":");
				n->children.push_back(patterns);
				n->children.push_back(parse_block());
			}
			if (peek().type == TK_DEDENT) {
				advance();
			}
			return n;
		}
		return parse_simple_statement();
	}

	// --- Niveau script ---
	void parse_function() {
		MXFunction func;
		func.line = peek().line;
		advance(); // func
		func.name = expect_identifier("Nom de fonction");
		parse_parameters(func);
		expect_symbol(":");
		if (failed()) {
			return;
		}
		func.body = parse_block();
		if (result.function_map.count(func.name)) {
			fail("La fonction '" + func.name + "' est déjà définie.", func.line);
			return;
		}
		result.function_map[func.name] = (int)result.functions.size();
		result.functions.push_back(func);
	}

	void parse_enum() {
		int line = advance().line; // enum
		String enum_name;
		if (peek().type == TK_IDENT) {
			enum_name = advance().text;
		}
		expect_symbol("{");
		int64_t next_value = 0;
		MXNodeRef dict = make(MXNode::DICTIONARY, line);
		while (!failed() && !is_symbol("}")) {
			String name = expect_identifier("Nom de valeur d'énumération");
			if (accept_symbol("=")) {
				MXNodeRef value = parse_expression();
				if (value->type != MXNode::LITERAL || value->value.get_type() != Variant::INT) {
					fail("Les valeurs d'énumération doivent être des entiers littéraux.", line);
					return;
				}
				next_value = value->value.as_int();
			}
			MXNodeRef literal = make(MXNode::LITERAL, line);
			literal->value = Variant(next_value++);
			if (enum_name.empty()) {
				MXConstant c;
				c.name = name;
				c.init = literal;
				c.line = line;
				result.constants.push_back(c);
			} else {
				MXNodeRef key = make(MXNode::LITERAL, line);
				key->value = Variant(name);
				dict->children.push_back(key);
				dict->children.push_back(literal);
			}
			if (!accept_symbol(",")) {
				break;
			}
		}
		expect_symbol("}");
		if (!enum_name.empty()) {
			MXConstant c;
			c.name = enum_name;
			c.init = dict;
			c.line = line;
			result.constants.push_back(c);
		}
		end_statement();
	}

	void parse_script() {
		bool exported = false;
		bool onready = false;
		PropertyHint hint = PROPERTY_HINT_NONE;
		String hint_string;
		auto reset_annotations = [&]() {
			exported = false;
			onready = false;
			hint = PROPERTY_HINT_NONE;
			hint_string.clear();
		};

		while (!failed() && peek().type != TK_EOF) {
			const Token &t = peek();
			if (t.type == TK_NEWLINE) {
				advance();
				continue;
			}
			if (t.type == TK_INDENT || t.type == TK_DEDENT) {
				fail("Indentation inattendue au niveau du script.");
				break;
			}
			if (t.type == TK_ANNOTATION) {
				String name = advance().text;
				std::vector<Variant> args;
				if (accept_symbol("(")) {
					while (!failed() && !is_symbol(")")) {
						MXNodeRef arg = parse_expression();
						if (arg->type == MXNode::LITERAL) {
							args.push_back(arg->value);
						} else {
							fail("Les arguments d'annotation doivent être des littéraux.");
						}
						if (!accept_symbol(",")) {
							break;
						}
					}
					expect_symbol(")");
				}
				if (name == "export") {
					exported = true;
				} else if (name == "export_range") {
					exported = true;
					hint = PROPERTY_HINT_RANGE;
					std::vector<String> parts;
					for (const Variant &a : args) {
						parts.push_back(a.stringify());
					}
					hint_string = str_join(parts, ",");
				} else if (name == "export_enum") {
					exported = true;
					hint = PROPERTY_HINT_ENUM;
					std::vector<String> parts;
					for (const Variant &a : args) {
						parts.push_back(a.as_string());
					}
					hint_string = str_join(parts, ",");
				} else if (name == "export_file") {
					exported = true;
					hint = PROPERTY_HINT_FILE;
					std::vector<String> parts;
					for (const Variant &a : args) {
						parts.push_back(str_replace(a.as_string(), "*.", ""));
					}
					hint_string = str_join(parts, ",");
				} else if (name == "export_multiline") {
					exported = true;
					hint = PROPERTY_HINT_MULTILINE_TEXT;
				} else if (name == "onready") {
					onready = true;
				} else if (name == "tool" || name == "icon") {
					// ignoré
				} else {
					fail("Annotation inconnue : @" + name);
				}
				if (peek().type == TK_NEWLINE) {
					advance();
				}
				continue;
			}
			if (t.type != TK_IDENT) {
				fail("Déclaration attendue" + describe_found());
				break;
			}
			if (t.text == "extends") {
				advance();
				if (peek().type == TK_STRING) {
					fail("L'héritage entre scripts (extends \"chemin\") n'est pas encore pris en charge.");
					break;
				}
				result.extends = expect_identifier("Nom de classe");
				end_statement();
			} else if (t.text == "class_name") {
				advance();
				result.class_name = expect_identifier("Nom de classe");
				end_statement();
			} else if (t.text == "export") {
				advance();
				exported = true;
				if (accept_symbol("(")) { // export(int) à l'ancienne
					while (!is_symbol(")") && peek().type != TK_NEWLINE && peek().type != TK_EOF) {
						advance();
					}
					expect_symbol(")");
				}
				continue;
			} else if (t.text == "onready") {
				advance();
				onready = true;
				continue;
			} else if (t.text == "var") {
				MXMember m;
				m.line = t.line;
				advance();
				m.name = expect_identifier("Nom de variable");
				if (accept_symbol(":")) {
					if (!is_symbol("=")) {
						m.type_hint = parse_type();
					}
				}
				if (accept_symbol("=") || accept_symbol(":=")) {
					m.init = parse_expression();
				}
				m.exported = exported;
				m.onready = onready;
				m.hint = hint;
				m.hint_string = hint_string;
				for (const MXMember &other : result.members) {
					if (other.name == m.name) {
						fail("La variable '" + m.name + "' est déjà déclarée.", m.line);
					}
				}
				result.members.push_back(m);
				// Accesseurs `: set = ..., get = ...` non pris en charge : on s'arrête à la fin de ligne.
				end_statement();
			} else if (t.text == "const") {
				MXConstant c;
				c.line = t.line;
				advance();
				c.name = expect_identifier("Nom de constante");
				if (accept_symbol(":")) {
					if (!is_symbol("=")) {
						parse_type();
					}
				}
				if (!accept_symbol("=") && !accept_symbol(":=")) {
					fail("Une constante doit avoir une valeur.");
					break;
				}
				c.init = parse_expression();
				result.constants.push_back(c);
				end_statement();
			} else if (t.text == "signal") {
				advance();
				MethodInfo mi;
				mi.name = expect_identifier("Nom de signal");
				if (accept_symbol("(")) {
					while (!failed() && !is_symbol(")")) {
						String arg = expect_identifier("Nom de paramètre");
						if (accept_symbol(":")) {
							parse_type();
						}
						mi.arguments.push_back(PropertyInfo(Variant::NIL, arg));
						if (!accept_symbol(",")) {
							break;
						}
					}
					expect_symbol(")");
				}
				result.signals.push_back(mi);
				end_statement();
			} else if (t.text == "enum") {
				parse_enum();
			} else if (t.text == "static" && is_keyword("func", 1)) {
				advance();
				parse_function();
			} else if (t.text == "func") {
				parse_function();
			} else if (t.text == "pass") {
				advance();
				end_statement();
			} else {
				fail("Déclaration attendue (var, const, func, signal...), '" + t.text + "' trouvé.");
				break;
			}
			reset_annotations();
		}
	}
};

} // namespace

void MexScriptParser::parse(const String &p_source, MXParseResult &r_result) {
	r_result = MXParseResult();
	Tokenizer tokenizer(p_source);
	tokenizer.run();
	if (!tokenizer.error.empty()) {
		r_result.has_error = true;
		r_result.error = tokenizer.error;
		r_result.error_line = tokenizer.error_line;
		return;
	}
	Parser parser(r_result);
	parser.tokens = tokenizer.tokens;
	parser.parse_script();
}

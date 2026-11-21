// MexScript : analyse lexicale et syntaxique (langage à indentation, proche de GDScript).
#pragma once

#include "core/object/object.h"

struct MXNode;
typedef std::shared_ptr<MXNode> MXNodeRef;

struct MXNode {
	enum Type {
		// Expressions
		LITERAL, // value
		IDENTIFIER, // name
		SELF,
		ARRAY, // children = éléments
		DICTIONARY, // children = clé, valeur, clé, valeur...
		UNARY, // op, children[0]
		BINARY, // op, children[0], children[1]
		TERNARY, // children = condition, si vrai, si faux
		CALL, // children[0] = cible (IDENTIFIER, ATTRIBUTE ou expression), puis arguments
		ATTRIBUTE, // children[0].name
		INDEX, // children[0][children[1]]
		GET_NODE, // $name
		LAMBDA, // func(params): corps -> function_index
		// Instructions
		BLOCK,
		VAR, // name, children[0] = valeur initiale (facultative), value = type déclaré (String) ou nul
		ASSIGN, // op (-1 : affectation simple), children = cible, valeur
		IF, // children = condition, bloc, [sinon]
		WHILE, // children = condition, bloc
		FOR, // name, children = itérable, bloc
		MATCH, // children[0] = sujet, puis paires (ARRAY de motifs, bloc) ; ARRAY vide = "_"
		RETURN, // children[0] facultatif
		BREAK,
		CONTINUE,
		PASS,
		EXPRESSION, // children[0]
	};

	// Opérateurs propres au langage (les autres sont des Variant::Operator).
	enum Op {
		OP_AND = 100,
		OP_OR,
		OP_NOT,
		OP_NOT_IN,
		OP_IS,
		OP_IS_NOT,
		OP_BIT_AND,
		OP_BIT_OR,
		OP_BIT_XOR,
		OP_BIT_NOT,
		OP_SHIFT_LEFT,
		OP_SHIFT_RIGHT,
		OP_POSITIVE,
	};

	Type type = PASS;
	int line = 0;
	int op = -1;
	String name;
	Variant value;
	int function_index = -1;
	std::vector<MXNodeRef> children;
};

struct MXFunction {
	String name;
	std::vector<String> params;
	std::vector<String> param_types; // type déclaré ("" = dynamique)
	std::vector<MXNodeRef> defaults; // pour les derniers paramètres
	MXNodeRef body;
	int line = 0;
};

struct MXMember {
	String name;
	MXNodeRef init;
	String type_hint;
	bool exported = false;
	bool onready = false;
	PropertyHint hint = PROPERTY_HINT_NONE;
	String hint_string;
	int line = 0;
};

struct MXConstant {
	String name;
	MXNodeRef init;
	int line = 0;
};

struct MXParseResult {
	String extends = "Node";
	String class_name;
	std::vector<MXMember> members;
	std::vector<MXConstant> constants;
	std::vector<MethodInfo> signals;
	std::vector<MXFunction> functions; // inclut les lambdas
	std::unordered_map<String, int> function_map;

	bool has_error = false;
	String error;
	int error_line = 0;
};

class MexScriptParser {
public:
	static void parse(const String &p_source, MXParseResult &r_result);
};

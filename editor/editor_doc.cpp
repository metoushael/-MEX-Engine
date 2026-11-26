// « Référence des classes » : documentation intégrée à l'éditeur (Aide, ou F1). La liste des
// classes, leurs propriétés, méthodes, signaux et constantes sont lus dans le moteur lui-même
// (ClassDB) : la page d'une classe décrit donc toujours la version en cours d'exécution.
#include "editor_node.h"

#include "core/object/class_db.h"
#include "core/string/ustring.h"
#include "editor_ui.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <map>

namespace {

// Présentation de chaque classe, en une phrase.
const std::map<String, const char *> &descriptions() {
	static const std::map<String, const char *> table = {
		{ "Object", "Base de toutes les classes : propriétés, méthodes appelables par leur nom, signaux." },
		{ "RefCounted", "Objet libéré automatiquement quand plus rien ne le référence." },
		{ "Resource", "Donnée chargée depuis un fichier (image, son, scène, script...) et partagée entre les nœuds." },
		{ "Script", "Code attaché à un objet." },
		{ "MexScript", "Script écrit en MexScript (fichier .mx)." },
		{ "InputEvent", "Événement d'entrée reçu par _input() : touche, souris, écran tactile, manette." },
		{ "InputEventKey", "Touche du clavier pressée ou relâchée." },
		{ "InputEventMouse", "Base des événements de souris : position dans la fenêtre." },
		{ "InputEventMouseButton", "Bouton ou molette de la souris." },
		{ "InputEventMouseMotion", "Déplacement de la souris." },
		{ "InputEventScreenTouch", "Doigt posé sur l'écran tactile, ou relevé." },
		{ "InputEventScreenDrag", "Doigt qui glisse sur l'écran tactile." },
		{ "InputEventJoypadButton", "Bouton de manette." },
		{ "InputEventJoypadMotion", "Axe de manette (stick, gâchette)." },
		{ "Engine", "Singleton : cadence, échelle du temps, version du moteur." },
		{ "OS", "Singleton : système d'exploitation, fenêtre, presse-papiers, dossiers." },
		{ "ProjectSettings", "Singleton : réglages du projet (fichier project.mex)." },
		{ "InputMap", "Singleton : actions du jeu et touches, boutons ou axes qui les déclenchent." },
		{ "Input", "Singleton : état du clavier, de la souris, des manettes et de l'écran tactile." },
		{ "TranslationServer", "Singleton : langue courante et traductions chargées ; voir tr()." },
		{ "Performance", "Singleton : mesures (images par seconde, nœuds, appels de dessin)." },
		{ "AStar2D", "Recherche du plus court chemin dans un graphe de points 2D." },
		{ "AStarGrid2D", "Recherche du plus court chemin sur une grille." },
		{ "StreamPeerTCP", "Connexion TCP : envoi et réception d'octets et de textes." },
		{ "TCPServer", "Serveur TCP : attend et accepte des connexions." },
		{ "PacketPeerUDP", "Envoi et réception de paquets UDP." },
		{ "JSONSingleton", "Singleton JSON : JSON.stringify() et JSON.parse_string()." },
		{ "Node", "Brique de base d'une scène : un nom, un parent, des enfants, _ready() et _process()." },
		{ "SceneTree", "Arbre des nœuds du jeu en cours : changement de scène, pause, groupes, minuteries." },
		{ "SceneTreeTimer", "Minuterie à usage unique créée par get_tree().create_timer()." },
		{ "CanvasItem", "Base de tout ce qui se dessine en 2D : visibilité, couleur, ordre, dessin personnalisé." },
		{ "CanvasLayer", "Calque 2D indépendant de la caméra : interface, fond." },
		{ "Timer", "Minuterie : émet timeout après un délai, une fois ou en boucle." },
		{ "AnimationPlayer", "Joue des animations : propriétés qui changent au fil du temps selon des pistes." },
		{ "Texture2D", "Image utilisable par les sprites, l'interface et les matériaux." },
		{ "Font", "Police de caractères (fichier .ttf ou .otf)." },
		{ "PackedScene", "Scène enregistrée (fichier .scn) ; instantiate() en crée une copie." },
		{ "Shape2D", "Base des formes de collision 2D." },
		{ "RectangleShape2D", "Forme de collision rectangulaire." },
		{ "CircleShape2D", "Forme de collision circulaire." },
		{ "CapsuleShape2D", "Forme de collision en capsule." },
		{ "ConvexPolygonShape2D", "Forme de collision : polygone convexe." },
		{ "Theme", "Apparence partagée des contrôles : couleurs, polices, marges." },
		{ "Tween", "Animation créée en script : fait glisser une propriété vers une valeur." },
		{ "Tweener", "Base des étapes d'un Tween." },
		{ "PropertyTweener", "Étape de Tween : interpole une propriété." },
		{ "IntervalTweener", "Étape de Tween : attente." },
		{ "CallbackTweener", "Étape de Tween : appel d'une fonction." },
		{ "MethodTweener", "Étape de Tween : appelle une méthode avec une valeur interpolée." },
		{ "Shader", "Programme de dessin (GLSL simplifié) exécuté par la carte graphique." },
		{ "CanvasItemMaterial", "Matériau 2D : mode de fusion." },
		{ "ShaderMaterial", "Matériau piloté par un Shader et ses paramètres." },
		{ "AudioEffect", "Base des effets appliqués à un bus audio." },
		{ "AudioStream", "Base des sons." },
		{ "AudioStreamWAV", "Son au format WAV." },
		{ "AudioStreamOggVorbis", "Son au format Ogg Vorbis." },
		{ "AudioStreamMP3", "Son au format MP3." },
		{ "AudioServer", "Singleton : bus audio, volumes, effets." },
		{ "Shape3D", "Base des formes de collision 3D." },
		{ "Mesh", "Base des maillages 3D." },
		{ "PrimitiveMesh", "Base des maillages générés : boîte, sphère, cylindre..." },
		{ "ArrayMesh", "Maillage construit à partir de tableaux de sommets (modèles glTF)." },
		{ "Material", "Base des matériaux." },
		{ "StandardMaterial3D", "Matériau 3D : couleur, texture, métal, rugosité, émission." },
		{ "Environment", "Ciel, lumière ambiante et brouillard d'une scène 3D." },
		{ "Node2D", "Nœud 2D : position, rotation, échelle." },
		{ "Marker2D", "Repère 2D invisible en jeu : point d'apparition, cible." },
		{ "Sprite2D", "Affiche une image, entière ou découpée en cases." },
		{ "AnimatedSprite2D", "Sprite animé image par image." },
		{ "TouchScreenButton", "Bouton pour écran tactile, qui déclenche une action." },
		{ "VirtualJoystick", "Stick virtuel pour écran tactile." },
		{ "CPUParticles2D", "Particules 2D : fumée, étincelles, pluie." },
		{ "CanvasModulate", "Teinte toute la scène 2D (nuit, ambiance)." },
		{ "PointLight2D", "Lumière 2D." },
		{ "LightOccluder2D", "Obstacle qui projette une ombre sous les lumières 2D." },
		{ "Camera2D", "Caméra 2D : suit un nœud, zoom, limites, lissage." },
		{ "TileMap", "Niveau construit avec des tuiles, avec collisions." },
		{ "CollisionObject2D", "Base des objets 2D qui portent des formes de collision." },
		{ "PhysicsBody2D", "Base des corps physiques 2D." },
		{ "StaticBody2D", "Corps 2D immobile : sol, mur." },
		{ "CharacterBody2D", "Corps 2D déplacé par le script avec move_and_slide() : personnage." },
		{ "RigidBody2D", "Corps 2D soumis à la gravité et aux chocs." },
		{ "Area2D", "Zone 2D qui détecte ce qui y entre et en sort." },
		{ "CollisionShape2D", "Donne une forme de collision à son parent." },
		{ "RayCast2D", "Rayon 2D : détecte le premier objet rencontré." },
		{ "Node3D", "Nœud 3D : position, rotation, échelle." },
		{ "Marker3D", "Repère 3D invisible en jeu." },
		{ "MeshInstance3D", "Affiche un maillage 3D." },
		{ "Camera3D", "Caméra 3D : perspective ou orthogonale." },
		{ "Light3D", "Base des lumières 3D." },
		{ "DirectionalLight3D", "Lumière du soleil, avec ombres." },
		{ "OmniLight3D", "Lumière ponctuelle qui éclaire dans toutes les directions." },
		{ "SpotLight3D", "Projecteur." },
		{ "WorldEnvironment", "Applique un Environment à la scène 3D." },
		{ "CollisionObject3D", "Base des objets 3D qui portent des formes de collision." },
		{ "PhysicsBody3D", "Base des corps physiques 3D." },
		{ "StaticBody3D", "Corps 3D immobile." },
		{ "CharacterBody3D", "Corps 3D déplacé par le script avec move_and_slide()." },
		{ "RigidBody3D", "Corps 3D soumis à la gravité et aux chocs." },
		{ "Area3D", "Zone 3D qui détecte ce qui y entre et en sort." },
		{ "CollisionShape3D", "Donne une forme de collision 3D à son parent." },
		{ "RayCast3D", "Rayon 3D." },
		{ "Skeleton3D", "Squelette d'un modèle 3D animé." },
		{ "Control", "Base de l'interface : ancres, taille minimale, focus, thème." },
		{ "Label", "Texte." },
		{ "Button", "Bouton : émet pressed." },
		{ "ColorRect", "Rectangle de couleur." },
		{ "Panel", "Fond de panneau." },
		{ "TextureRect", "Image dans l'interface." },
		{ "ProgressBar", "Barre de progression." },
		{ "LineEdit", "Champ de saisie d'une ligne." },
		{ "Container", "Base des conteneurs, qui placent leurs enfants." },
		{ "BoxContainer", "Base des conteneurs en ligne ou en colonne." },
		{ "VBoxContainer", "Place ses enfants en colonne." },
		{ "HBoxContainer", "Place ses enfants en ligne." },
		{ "MarginContainer", "Ajoute des marges autour de ses enfants." },
		{ "CenterContainer", "Centre ses enfants." },
		{ "GridContainer", "Place ses enfants en grille." },
		{ "ScrollContainer", "Fait défiler un contenu plus grand que lui." },
		{ "TabContainer", "Onglets : un enfant visible à la fois." },
		{ "CheckBox", "Case à cocher." },
		{ "TextureButton", "Bouton dessiné avec des images." },
		{ "Slider", "Base des curseurs." },
		{ "OptionButton", "Liste déroulante." },
		{ "ItemList", "Liste d'éléments sélectionnables." },
		{ "NinePatchRect", "Image étirée en gardant ses bords intacts." },
		{ "AudioStreamPlayer", "Joue un son." },
		{ "AudioStreamPlayer2D", "Joue un son placé dans l'espace 2D." },
		{ "AudioStreamPlayer3D", "Joue un son placé dans l'espace 3D." },
		{ "AudioListener3D", "Point d'écoute des sons 3D." },
	};
	return table;
}

bool matches(const String &p_text, const String &p_filter) {
	return p_filter.empty() || str_to_lower(p_text).find(p_filter) != String::npos;
}

} // namespace

void EditorNode::_open_doc(const String &p_class) {
	doc_open = true;
	doc_focus = true;
	if (!p_class.empty() && ClassDB::class_exists(p_class)) {
		doc_class = p_class;
	}
}

void EditorNode::_draw_doc() {
	if (!doc_open) {
		return;
	}
	using EditorUI::col;
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(EditorUI::dialog_size(980, 640), ImGuiCond_Appearing);
	if (doc_focus) {
		ImGui::SetNextWindowFocus();
		doc_focus = false;
	}
	if (!ImGui::Begin(ICON_HELP "  Référence des classes###Doc", &doc_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}
	std::vector<String> classes;
	ClassDB::get_class_list(&classes);
	std::sort(classes.begin(), classes.end());
	if (doc_class.empty() || !ClassDB::class_exists(doc_class)) {
		doc_class = "Node";
	}
	const String filter = str_to_lower(str_strip(doc_filter));

	// --- Liste des classes ---
	ImGui::BeginChild("##classes", ImVec2(ES(250), 0), ImGuiChildFlags_Borders);
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##filtre", ICON_SEARCH "  classe, propriété, méthode...", &doc_filter);
	ImGui::Separator();
	ImGui::BeginChild("##liste");
	if (filter.empty()) {
		// Arbre d'héritage.
		std::map<String, std::vector<String>> children;
		std::vector<String> roots;
		for (const String &name : classes) {
			const String parent = ClassDB::get_parent_class(name);
			if (parent.empty()) {
				roots.push_back(name);
			} else {
				children[parent].push_back(name);
			}
		}
		std::function<void(const String &)> draw_class = [&](const String &p_name) {
			const auto found = children.find(p_name);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (found == children.end()) {
				flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			}
			if (p_name == doc_class) {
				flags |= ImGuiTreeNodeFlags_Selected;
			}
			if (p_name == "Object" || p_name == "Node" || ClassDB::is_parent_class(doc_class, p_name)) {
				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			}
			const bool opened = ImGui::TreeNodeEx(p_name.c_str(), flags);
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
				doc_class = p_name;
			}
			if (opened && found != children.end()) {
				for (const String &child : found->second) {
					draw_class(child);
				}
				ImGui::TreePop();
			}
		};
		for (const String &root : roots) {
			draw_class(root);
		}
	} else {
		// Recherche : classes dont le nom, une propriété, une méthode ou un signal correspond.
		int shown = 0;
		for (const String &name : classes) {
			String member;
			bool found = matches(name, filter);
			if (!found) {
				if (ClassInfo *info = ClassDB::get_class_info(name)) {
					for (const PropertyInfo &property : info->property_list) {
						if (!(property.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_CATEGORY)) && matches(property.name, filter)) {
							member = property.name;
							break;
						}
					}
					if (member.empty()) {
						for (const auto &method : info->methods) {
							if (matches(method.first, filter)) {
								member = method.first + "()";
								break;
							}
						}
					}
					if (member.empty()) {
						for (const MethodInfo &signal : info->signals) {
							if (matches(signal.name, filter)) {
								member = signal.name;
								break;
							}
						}
					}
					found = !member.empty();
				}
			}
			if (!found) {
				continue;
			}
			shown++;
			if (ImGui::Selectable(name.c_str(), name == doc_class)) {
				doc_class = name;
			}
			if (!member.empty()) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s", member.c_str());
			}
		}
		if (shown == 0) {
			ImGui::TextDisabled("Aucun résultat.");
		}
	}
	ImGui::EndChild();
	ImGui::EndChild();

	// --- Page de la classe ---
	ImGui::SameLine();
	ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	const String shown_class = doc_class; // un clic sur un lien change doc_class pendant le dessin
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
	ImGui::TextColored(col(palette.accent), "%s", shown_class.c_str());
	ImGui::PopFont();
	const auto link = [&](const String &p_name) {
		ImGui::PushStyleColor(ImGuiCol_Text, col(palette.info));
		ImGui::TextUnformatted(p_name.c_str());
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}
		if (ImGui::IsItemClicked()) {
			doc_class = p_name;
		}
	};
	// Chaîne d'héritage.
	{
		String parent = ClassDB::get_parent_class(shown_class);
		if (!parent.empty()) {
			ImGui::TextDisabled("Hérite de");
			while (!parent.empty()) {
				ImGui::SameLine();
				link(parent);
				parent = ClassDB::get_parent_class(parent);
				if (!parent.empty()) {
					ImGui::SameLine();
					ImGui::TextDisabled("<");
				}
			}
		}
		std::vector<String> heirs;
		for (const String &name : classes) {
			if (ClassDB::get_parent_class(name) == shown_class) {
				heirs.push_back(name);
			}
		}
		if (!heirs.empty()) {
			ImGui::TextDisabled("Héritée par");
			const float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ES(24);
			for (const String &heir : heirs) {
				ImGui::SameLine();
				if (ImGui::GetCursorScreenPos().x + ImGui::CalcTextSize(heir.c_str()).x > right) {
					ImGui::NewLine();
				}
				link(heir);
			}
		}
	}
	ImGui::Spacing();
	const auto description = descriptions().find(shown_class);
	if (description != descriptions().end()) {
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(description->second);
		ImGui::PopTextWrapPos();
	}
	if (!ClassDB::can_instantiate(shown_class)) {
		ImGui::TextDisabled("Classe de base ou singleton : elle ne se crée pas avec .new().");
	}
	ImGui::Spacing();

	const auto members = [&](const String &p_class, bool p_inherited) {
		ClassInfo *info = ClassDB::get_class_info(p_class);
		if (!info) {
			return;
		}
		ImGui::PushID(p_class.c_str());
		std::vector<const PropertyInfo *> properties;
		for (const PropertyInfo &property : info->property_list) {
			if (!(property.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_CATEGORY))) {
				properties.push_back(&property);
			}
		}
		if (!properties.empty()) {
			ImGui::SeparatorText(p_inherited ? "Propriétés héritées" : "Propriétés");
			if (ImGui::BeginTable("##proprietes", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("##type", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("##nom", ImGuiTableColumnFlags_WidthStretch, 1.6f);
				ImGui::TableSetupColumn("##valeurs", ImGuiTableColumnFlags_WidthStretch, 2.4f);
				for (const PropertyInfo *property : properties) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextColored(col(palette.info), "%s", Variant::get_type_name(property->type).c_str());
					ImGui::TableNextColumn();
					if (font_mono) {
						ImGui::PushFont(font_mono, 0.0f);
					}
					ImGui::TextUnformatted(property->name.c_str());
					if (font_mono) {
						ImGui::PopFont();
					}
					ImGui::TableNextColumn();
					if (!property->hint_string.empty()) {
						ImGui::TextDisabled("%s", property->hint_string.size() > 90 ? (property->hint_string.substr(0, 90) + "...").c_str() : property->hint_string.c_str());
					}
				}
				ImGui::EndTable();
			}
		}
		std::vector<String> methods;
		for (const auto &method : info->methods) {
			const MethodBind *bind = method.second;
			String arguments;
			if (bind && bind->is_vararg) {
				arguments = "...";
			} else if (bind) {
				for (int i = 0; i < bind->argument_count; i++) {
					arguments += (i ? ", " : "") + String("arg") + std::to_string(i + 1);
				}
			}
			methods.push_back(method.first + "(" + arguments + ")");
		}
		std::sort(methods.begin(), methods.end());
		if (!methods.empty()) {
			ImGui::SeparatorText(p_inherited ? "Méthodes héritées" : "Méthodes");
			if (font_mono) {
				ImGui::PushFont(font_mono, 0.0f);
			}
			if (ImGui::BeginTable("##methodes", ImGui::GetContentRegionAvail().x > ES(520) ? 2 : 1)) {
				for (const String &method : methods) {
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(method.c_str());
				}
				ImGui::EndTable();
			}
			if (font_mono) {
				ImGui::PopFont();
			}
		}
		if (!info->signals.empty()) {
			ImGui::SeparatorText(p_inherited ? "Signaux hérités" : "Signaux");
			for (const MethodInfo &signal : info->signals) {
				String text = signal.name + "(";
				for (size_t i = 0; i < signal.arguments.size(); i++) {
					text += (i ? ", " : "") + signal.arguments[i].name;
				}
				text += ")";
				ImGui::TextColored(col(palette.success), ICON_SIGNAL);
				ImGui::SameLine();
				ImGui::TextUnformatted(text.c_str());
			}
		}
		if (!info->constants.empty()) {
			std::vector<std::pair<long long, String>> constants;
			for (const auto &constant : info->constants) {
				constants.push_back({ (long long)constant.second, constant.first });
			}
			std::sort(constants.begin(), constants.end());
			ImGui::SeparatorText(p_inherited ? "Constantes héritées" : "Constantes");
			if (ImGui::BeginTable("##constantes", ImGui::GetContentRegionAvail().x > ES(520) ? 2 : 1)) {
				for (const auto &constant : constants) {
					ImGui::TableNextColumn();
					ImGui::Text("%s = %lld", constant.second.c_str(), constant.first);
				}
				ImGui::EndTable();
			}
		}
		if (properties.empty() && methods.empty() && info->signals.empty() && info->constants.empty() && !p_inherited) {
			ImGui::TextDisabled("Cette classe n'ajoute aucun membre à ceux dont elle hérite.");
		}
		ImGui::PopID();
	};
	members(shown_class, false);
	// Membres hérités, classe par classe, repliés.
	for (String parent = ClassDB::get_parent_class(shown_class); !parent.empty(); parent = ClassDB::get_parent_class(parent)) {
		ImGui::Spacing();
		if (ImGui::CollapsingHeader((String("Hérité de ") + parent + "###herite_" + parent).c_str())) {
			ImGui::Indent(ES(8));
			members(parent, true);
			ImGui::Unindent(ES(8));
		}
	}
	ImGui::Spacing();
	EditorUI::hint("Ces listes sont lues dans le moteur en cours d'exécution. Les signatures détaillées et les exemples sont dans le fichier README.md.");
	ImGui::EndChild();
	ImGui::End();
}

// GameRunner : fait tourner une scène de jeu (arbre, pas de physique fixe, rendu).
// Utilisé par l'exécutable du jeu et par le mode « Jouer » de l'éditeur.
#pragma once

#include "core/input/input_event.h"
#include "core/math/vector2.h"

#include <vector>

class SceneTree;

class GameRunner {
	SceneTree *tree = nullptr;
	double accumulator = 0;
	String scene_path;
	std::vector<String> autoloads; // noms globaux des chargements automatiques de cette partie

public:
	// Charge la scène et démarre l'arbre. p_viewport_size : résolution logique du jeu.
	Error start(const String &p_scene_path, const Size2 &p_viewport_size);
	void stop();
	bool is_running() const { return tree != nullptr; }
	SceneTree *get_tree() const { return tree; }
	String get_scene_path() const { return scene_path; }

	void push_input(const Ref<InputEvent> &p_event);
	// Avance la simulation d'une image. Renvoie faux quand le jeu demande à quitter.
	bool iterate(double p_delta);
	// Efface la cible de rendu courante puis dessine la scène.
	void draw();

	~GameRunner();
};

#pragma once

#include "core/string/ustring.h"

#include <atomic>
#include <vector>

// Lancement d'un programme externe, sans fenêtre de console, en attendant sa fin.
// Bloquant : à appeler depuis un fil d'exécution secondaire quand le programme peut durer.
namespace Process {

// p_program : nom (cherché dans le PATH) ou chemin. p_stdin est envoyé sur l'entrée standard
// (moins de 60 Ko), r_output reçoit la sortie standard, r_errors la sortie d'erreur.
// p_working_dir : dossier de travail du programme (vide : celui de MEX).
// p_cancel, s'il passe à true, arrête le programme. Renvoie false s'il n'a pas pu démarrer.
bool run(const String &p_program, const std::vector<String> &p_arguments, const String &p_stdin, String *r_output, String *r_errors, int *r_exit_code, const std::atomic<bool> *p_cancel = nullptr, const String &p_working_dir = String());

} // namespace Process

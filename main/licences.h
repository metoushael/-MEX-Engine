#ifndef MEX_LICENCES_H
#define MEX_LICENCES_H

// Licences des composants tiers, incorporées à l'exécutable par CMake (voir CMakeLists.txt).
struct MexLicence {
	const char *name; // composant
	const char *kind; // nom court de la licence
	const char *text; // texte complet
};

extern const int MEX_LICENCE_COUNT;
extern const MexLicence MEX_LICENCES[];

#endif

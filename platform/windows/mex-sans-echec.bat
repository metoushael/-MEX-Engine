@echo off
rem MEX en mode de compatibilite : barre de titre de Windows et rendu Direct3D (sans 3D).
rem A utiliser si l image est decalee, coupee, lente ou scintille. Les preferences ne sont pas modifiees.
start "" "%~dp0mex.exe" --sans-echec %*

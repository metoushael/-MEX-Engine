// Page d'accueil d'un jeu exporté pour le web. L'éditeur y remplace les repères « @...@ » :
// titre, taille de l'écran du jeu, icône, et la manière de charger le moteur et l'archive.
#include "editor_export.h"

const char *EditorExport::get_web_shell() {
	return R"MEXWEB(<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>@TITRE@</title>
@ICONE@<style>
html, body { margin: 0; height: 100%; background: #0d0f1a; color: #e8e8f0; overflow: hidden;
	font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
#scene { position: fixed; inset: 0; display: flex; align-items: center; justify-content: center; }
#canvas { display: block; background: #000; outline: none; touch-action: none; user-select: none;
	-webkit-user-select: none; -webkit-tap-highlight-color: transparent;
	width: min(100vw, calc(100vh * @LARGEUR@ / @HAUTEUR@));
	height: min(100vh, calc(100vw * @HAUTEUR@ / @LARGEUR@)); }
#etat { position: fixed; inset: 0; display: flex; flex-direction: column; align-items: center;
	justify-content: center; gap: 14px; background: #0d0f1a; text-align: center; padding: 24px; }
#etat[hidden] { display: none; }
#etat h1 { margin: 0; font-size: 22px; font-weight: 600; }
#etat p { margin: 0; max-width: 560px; line-height: 1.5; color: #a9adc4; white-space: pre-line; }
#etat.erreur p { color: #ff9d8a; }
#barre { width: min(320px, 70vw); height: 6px; border-radius: 3px; background: #23263a; overflow: hidden; }
#barre div { width: 0; height: 100%; background: #ff7a1a; transition: width .1s linear; }
#etat.erreur #barre, #etat.fin #barre { display: none; }
</style>
</head>
<body>
<div id="scene"><canvas id="canvas" width="@LARGEUR@" height="@HAUTEUR@" tabindex="0" oncontextmenu="event.preventDefault()"></canvas></div>
<div id="etat"><h1>@TITRE@</h1><div id="barre"><div></div></div><p id="message">Chargement…</p></div>
<noscript><p style="position:fixed;top:12px;left:12px">Ce jeu a besoin de JavaScript.</p></noscript>
<script>
"use strict";
var MEX = (function () {
	var etat = document.getElementById("etat");
	var message = document.getElementById("message");
	var barre = document.querySelector("#barre div");
	var canvas = document.getElementById("canvas");
	var lance = false;

	function afficher(texte, classe) {
		message.textContent = texte;
		etat.className = classe || "";
		etat.hidden = false;
	}
	function erreur(texte) {
		afficher(texte, "erreur");
	}
	function progression(part) {
		barre.style.width = Math.round(Math.max(0, Math.min(1, part)) * 100) + "%";
	}
	function decoder(texte) {
		var binaire = atob(texte);
		var octets = new Uint8Array(binaire.length);
		for (var i = 0; i < binaire.length; i++) {
			octets[i] = binaire.charCodeAt(i);
		}
		return octets;
	}
	function archive(octets) {
		// L'archive du jeu est déposée dans le système de fichiers du moteur avant son démarrage.
		Module.preRun.push(function () {
			Module.FS.writeFile("/jeu.pck", octets);
		});
	}
	function telecharger(adresse) {
		return fetch(adresse).then(function (reponse) {
			if (!reponse.ok) {
				throw new Error(adresse + " : erreur " + reponse.status);
			}
			var total = parseInt(reponse.headers.get("Content-Length") || "0", 10);
			if (!reponse.body || !total) {
				return reponse.arrayBuffer().then(function (tampon) {
					return new Uint8Array(tampon);
				});
			}
			var lecteur = reponse.body.getReader();
			var morceaux = [];
			var recu = 0;
			function suite() {
				return lecteur.read().then(function (partie) {
					if (partie.done) {
						var octets = new Uint8Array(recu);
						var position = 0;
						morceaux.forEach(function (morceau) {
							octets.set(morceau, position);
							position += morceau.length;
						});
						return octets;
					}
					morceaux.push(partie.value);
					recu += partie.value.length;
					progression(recu / total);
					return suite();
				});
			}
			return suite();
		});
	}

	window.Module = {
		canvas: canvas,
		arguments: ["--main-pack", "/jeu.pck"],
		preRun: [],
		print: function (texte) { console.log(texte); },
		printErr: function (texte) { console.error(texte); },
		onRuntimeInitialized: function () {
			lance = true;
			etat.hidden = true;
			canvas.focus();
		},
		onAbort: function (raison) {
			erreur("Le jeu s'est arrêté sur une erreur.\n" + raison);
		},
		// Appelé par le moteur quand le jeu se termine (get_tree().quit()).
		mexFin: function () {
			afficher("Le jeu est terminé. Rechargez la page pour rejouer.", "fin");
		}
	};
	window.addEventListener("error", function (evenement) {
		if (!lance) {
			erreur("Le jeu n'a pas pu démarrer.\n" + (evenement.message || ""));
		}
	});
	// Un clic rend le clavier au jeu (page intégrée dans une autre, par exemple).
	canvas.addEventListener("pointerdown", function () { canvas.focus(); });

	if (typeof WebAssembly !== "object") {
		erreur("Ce navigateur ne sait pas exécuter WebAssembly. Essayez une version récente de Firefox, Chrome, Edge ou Safari.");
	}

	return {
		// Export en un seul fichier : l'archive est dans la page, en base64.
		incorporee: function (identifiant) {
			try {
				archive(decoder(document.getElementById(identifiant).textContent));
				progression(1);
			} catch (e) {
				erreur("L'archive du jeu est illisible.\n" + e);
			}
		},
		// Export en trois fichiers : l'archive puis le moteur sont téléchargés.
		demarrer: function (fichiers) {
			if (location.protocol === "file:") {
				erreur("Cette page doit être servie par un serveur web (http:// ou https://).\nPour un jeu qui s'ouvre d'un double-clic, cochez « Tout réunir dans la page » dans le préréglage d'export.");
				return;
			}
			telecharger(fichiers.archive).then(function (octets) {
				archive(octets);
				var moteur = document.createElement("script");
				moteur.src = fichiers.moteur;
				moteur.onerror = function () {
					erreur("Fichier introuvable : " + fichiers.moteur);
				};
				document.body.appendChild(moteur);
			}).catch(function (e) {
				erreur("Le jeu n'a pas pu être téléchargé.\n" + e.message);
			});
		}
	};
})();
</script>
@CHARGEMENT@
</body>
</html>
)MEXWEB";
}

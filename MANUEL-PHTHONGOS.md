# Phthongos — Manuel d’utilisation

Version de travail : 22 octobre 2026 · Application Windows native C++ / JUCE · Interface française.

Ce manuel décrit les commandes de cette version. Les autres branches expérimentales peuvent proposer d’autres moteurs. La notice `MANUEL.txt` et le bouton **?** restent les références rapides.

## Sommaire

1. [Découvrir Phthongos](#1-découvrir-phthongos)
2. [Créer, ouvrir et enregistrer un projet](#2-créer-ouvrir-et-enregistrer-un-projet)
3. [Importer et transcrire un audio](#3-importer-et-transcrire-un-audio)
4. [Lire le spectrogramme](#4-lire-le-spectrogramme)
5. [Travailler avec les voix et les calques](#5-travailler-avec-les-voix-et-les-calques)
6. [Éditer les notes](#6-éditer-les-notes)
7. [Comparer et compléter avec les calques](#7-comparer-et-compléter-avec-les-calques)
8. [Grille musicale et BPM](#8-grille-musicale-et-bpm)
9. [Lecture et contrôle](#9-lecture-et-contrôle)
10. [Exporter](#10-exporter)
11. [Méthodes de travail](#11-méthodes-de-travail)
12. [Référence rapide](#12-référence-rapide)
13. [Saisie MIDI et estimation du tempo](#13-saisie-midi-et-estimation-du-tempo)
14. [Métronome et précompte MIDI](#14-métronome-et-précompte-midi)
15. [Voix superposées et empilées](#15-voix-superposées-et-empilées)

## 1. Découvrir Phthongos

Phthongos permet de transcrire, vérifier et éditer du MIDI en regard d’un audio. Une transcription donne un premier jet ; la superposition, l’écoute et les corrections permettent ensuite de le compléter ou de l’arranger.

| Élément | Rôle |
| --- | --- |
| **Audio** | Source sonore de référence. |
| **Spectrogramme** | Représentation du signal selon le temps, la fréquence et l’énergie. |
| **MIDI** | Notes éditables : hauteur, début, fin, vélocité et instrument. |
| **Voix** | Calques musicaux indépendants pour organiser les notes. |
| **Grille** | Repère temporel, musical ou en secondes. |

Le piano roll place le temps horizontalement et les hauteurs verticalement. Les trois vues — **MIDI**, **Audio + MIDI** et **Superposition** — partagent la navigation temporelle.

Les corrections MIDI ne réécrivent pas le WAV source. Un calque permet de préserver une référence pendant que l’on travaille dans une autre voix. **Ctrl+Z** annule les opérations éditoriales ; les versions enregistrées permettent de conserver des étapes au-delà de la session.

## 2. Créer, ouvrir et enregistrer un projet

### Commencer une session

Le menu **☰** regroupe les opérations de fichiers.

- **Nouveau projet** revient à une session vide dans la même fenêtre. Les changements non enregistrés déclenchent une proposition d’enregistrement.
- **Ouvrir audio** charge une source pour transcription ou travail avec le spectrogramme.
- **Importer un MIDI…** ouvre un projet d’édition, avec ou sans audio associé.
- **Ajouter un MIDI au projet…** complète le projet courant.
- **Ouvrir un projet…** restaure un fichier `.pht`.

Déposer un fichier depuis l’Explorateur Windows déclenche son import. Si des notes MIDI sont présentes, le dépôt MIDI ouvre **Ajouter un MIDI au projet…**. Un audio déposé sur un MIDI devient sa référence audio, en conservant notes, voix et corrections. Un seul fichier à la fois ; formats et limites habituels s’appliquent.


Un projet PHT conserve l’état d’édition, ses voix, leurs couleurs et verrous, les 25 derniers états d’annulation et les réglages de travail. Il conserve également les données audio et d’analyse nécessaires à sa restauration. Un export MIDI ou WAV ne remplace pas cette sauvegarde de projet.

Le nom initial vient du MIDI ou de l’audio. Clic droit sur le titre, ou **☰ → Renommer le projet…**, pour le modifier. Conservé dans le `.pht`, il nomme les exports MIDI sans renommer les fichiers existants.

### Enregistrer et changer de dossier

**Ctrl+S** et **Enregistrer** sauvegardent le projet. Au premier enregistrement, choisir un nom et un emplacement. **Ctrl+Maj+S** ouvre **Enregistrer sous…** : le nom et le dossier choisis deviennent les références du projet après une sauvegarde réussie.

Dans **Paramètres**, **Dossier de projet par défaut (facultatif)** propose un emplacement pour les nouveaux projets. Laisser ce champ vide reste possible. Chaque projet utilise ensuite son dernier emplacement enregistré ; changer le dossier par défaut ne déplace pas les projets existants.

### Incrémenter les sauvegardes

L’option **Incrémenter les sauvegardes**, désactivée par défaut, fait créer un nouveau fichier à chaque **Ctrl+S / Enregistrer** :

```text
leaf_2026_10_20_115132.pht
leaf_2026_10_20_115132_02.pht
leaf_2026_10_20_115205.pht
```

Le nom de base reste `leaf`. Les dates ne s’accumulent pas. Les suffixes `_02`, `_03` distinguent plusieurs fichiers dans la même seconde. Le projet courant devient la nouvelle version et les précédentes restent intactes.

**Enregistrer sous…** respecte le nom explicitement choisi. Les prochains enregistrements incrémentés utilisent ce nouveau nom de base et ce dossier. Sans l’option, Ctrl+S écrase classiquement le fichier courant, y compris si son nom contient déjà une date.

### Projets récents

**☰ → Projets récents** conserve dix fichiers .pht distincts, classés par date de modification décroissante. Chaque version sauvegardée a sa propre entrée, avec son nom de fichier réel, sa date/heure de modification et son dossier. Un même chemin n’apparaît qu’une fois. Un fichier devenu introuvable est désactivé dans la liste.

**Effacer la liste des projets récents** efface seulement cette liste. Les fichiers restent sur le disque. L’ouverture d’un projet récent propose aussi de sauvegarder les changements en cours.

## 3. Importer et transcrire un audio

1. Cliquer sur **Ouvrir audio** et choisir le fichier, ou déposer un audio depuis Windows dans une session sans notes MIDI.
2. **Basic Pitch** est inclus, sélectionné par défaut et fonctionne sur CPU, hors ligne.
3. Choisir l’entrée voulue dans l’unique liste **Moteur** : Basic Pitch, ou une variante MuScriptor déclarée.
4. Lancer **Analyser**.
5. Écouter et examiner le résultat dans le piano roll.


Les huit réglages de Basic Pitch occupent un bandeau sur toute la largeur, entre le moteur et le transport, sur deux lignes compactes. Le bouton « Réglages Basic Pitch » replie le bandeau sans désactiver ses réglages, même pendant un calcul ; son état est mémorisé. « Réinitialiser » rétablit les huit valeurs initiales. Avant la première analyse, ils définissent le calcul à venir. Après celle-ci, ajuster un réglage actualise les notes après une courte pause, sans relancer le modèle neuronal.

| Réglage | Effet |
| --- | --- |
| Sensibilité des notes | Plus élevée : conserver davantage de notes ; plus basse : éliminer davantage de détections faibles. Valeur initiale : 70 %. |
| Séparation des attaques | Plus élevée : distinguer davantage de réattaques ; plus basse : favoriser des notes plus longues. Valeur initiale : 50 %. |
| Durée minimale | Écarter les fragments plus courts. Valeur initiale : 125 ms. |
| Tolérance aux coupures | 1 à 25 trames ; valeur initiale : 11 (environ 128 ms). Tolérer de brèves baisses d’activation pendant la reconstruction. Le survol affiche la durée approximative. Modifiable après calcul, sans nouvelle inférence, y compris après réouverture du projet. |
| Note minimale / maximale | Limiter le registre de détection, bornes incluses. Initialement : A0 à C8 (MIDI 21 à 108). Chaque borne est un réglage distinct. |
| Attaques supplémentaires | Déduire des attaques à partir des variations d’activation ; activé initialement. |
| Récupération complémentaire | Reconstruire des notes depuis les activations restantes ; activée initialement. |

Le registre et les deux interrupteurs reconstruisent également les notes en direct depuis les sorties complètes ; élargir de nouveau le registre fait réapparaître les détections précédemment exclues. Les huit réglages sont propres à chaque analyse et retrouvés en sélectionnant sa voix.

Les sorties neuronales sont conservées avec les réglages et les notes dans le `.pht`. Les réglages réutilisent le cache de la voix choisie, y compris après réouverture et lorsqu’un autre stem est chargé en référence. Avec plusieurs voix Basic Pitch, choisir une voix avant d’ajuster ses notes. Le temps et le pourcentage de progression apparaissent en haut uniquement pendant un calcul ; sa durée finale reste indiquée en bas. Une confirmation protège les corrections de la voix choisie ; les autres voix restent intactes. Changer de moteur ne lance aucun calcul.

Pour ajouter MuScriptor, cocher **Utiliser des moteurs tiers** dans **Paramètres**, puis déclarer ou télécharger explicitement les fichiers Small, Medium ou Large. Le réglage **Automatique / CPU / GPU** est commun à toute la famille MuScriptor, quel que soit le nombre de modèles installés. La liste **Moteur** affiche seulement les variantes présentes. Décocher les moteurs tiers revient à Basic Pitch sans effacer les fichiers ni leurs déclarations. Chaque fichier est vérifié par empreinte SHA-256 ; aucun téléchargement ne démarre automatiquement. Les poids MuScriptor de Kyutai et Mirelo sont sous CC BY-NC 4.0 (usage non commercial) ; ils sont téléchargés séparément et ne sont pas inclus dans l’installateur.

Basic Pitch est particulièrement adapté à une source instrumentale isolée. Sa vélocité initiale vient des activations du modèle ; **Dynamique depuis l’audio** permet ensuite d’exploiter l’énergie harmonique du stem pour proposer des vélocités à écouter et appliquer explicitement.

Les modèles fournissent des événements proposés. Des notes peuvent manquer, se fragmenter, être répétées ou correspondre à des harmoniques. Le résultat demande une vérification musicale et acoustique.

Importer un MIDI conserve ses notes. Avec un audio associé, **Analyser** ajoute la transcription dans une nouvelle voix ; il reste accessible après une analyse. Si des notes sont déjà présentes, **Ouvrir audio** ou déposer un autre WAV remplace seulement la référence audio, puis **Analyser** ajoute une nouvelle voix sans écraser les précédentes. Un **Ctrl+Z** annule cet ajout entier. **Nouveau projet** commence une session indépendante.

Lors d’un ajout MIDI, l’aperçu présente l’existant et les notes à importer. Choisir, pour chaque partie source, une voix existante ou une nouvelle voix. Une destination existante impose son instrument. Le décalage est exprimé en secondes ; zéro garde les positions temporelles importées. L’ajout entier s’annule par un Ctrl+Z.

Vous pouvez aussi ajouter vos propres fichiers **SF2** dans **Paramètres → Banques SF2 personnelles**. Dans **Instrument…** pour une voix, choisir la banque puis l’un de ses presets réels : la banque n’a pas besoin de suivre General MIDI. Ce choix est utilisé pour la lecture, la préécoute, le clavier MIDI et l’export WAV ; le nom de la voix et ses notes sont conservés. Le projet mémorise le chemin du fichier, son empreinte et le preset. Les banques personnelles sont lues sur place et ne sont pas intégrées au projet : garder le fichier disponible. Retirer une banque du catalogue n’efface pas le fichier et ne change pas les voix déjà configurées. L’export MIDI ne contient pas les sons SF2.

## 4. Lire le spectrogramme

Choisir **Superposition**, puis **Spectrogramme**. Le signal apparaît derrière les rectangles MIDI, aux mêmes coordonnées temporelles et de hauteur.

**Opacité** règle la présence du fond. **Gain visuel**, la palette et le contraste changent sa lisibilité. Ils ne changent ni l’audio ni les notes. La palette **Inferno** fortement contrastée peut faciliter l’examen des noyaux lumineux.

### Ce que montrent les formes

Une attaque apparaît souvent comme un renforcement bref ou une structure verticale. Une hauteur stable peut former une ligne horizontale. Plusieurs lignes espacées régulièrement en fréquence peuvent appartenir aux harmoniques d’un même son. Après l’attaque, une énergie décroissante peut continuer à cause de la résonance, de la pédale ou de la réverbération.

Les zones jaunes d’Inferno indiquent de l’énergie élevée dans les réglages affichés. Elles ne constituent pas à elles seules une mesure de vélocité MIDI ou de durée jouée : le timbre, le registre, les harmoniques et le contraste interviennent aussi.

Une ligne visible peut néanmoins être un excellent point de départ pour ajouter une note volontairement, doubler une mélodie ou enrichir un accord. Phthongos permet cette décision d’arrangement, indépendamment de la probabilité calculée par une analyse.

### Comparer localement

Zoomer sur un court passage, écouter l’audio, puis le MIDI. Examiner les attaques, les réattaques à hauteur identique et les fins de notes. Un MIDI déjà corrigé devient une référence utile pour voir ce qui reste inexpliqué dans le signal.

**Spectre / registre…** limite les hauteurs examinées par certaines analyses. Cela ne filtre pas le son. **Masque / MIDI guidé** permet de peindre les zones à examiner et de proposer des notes à partir du signal, sans lancer une nouvelle transcription neuronale.

Le pinceau et la gomme du masque dessinent une empreinte circulaire. Les propositions se répartissent en catégories, dont **Probables** et **À vérifier**. Les écouter avant de les ajouter : les catégories restent des indices. Le suivi d’une ligne dominante peut manquer des notes d’accords.

## 5. Travailler avec les voix et les calques

Le sélecteur de voix définit la partie active. **Toutes les parties** permet une vue globale. Pour une correction précise, choisir une voix, puis utiliser les autres comme contexte.

### Source audio de chaque voix

Les voix issues d’une transcription affichent **Source : nom-du-stem.wav**, avec un petit bouton **↺**. Il recharge ce fichier dans l’unique zone audio de référence, sans modifier les notes ni lancer d’analyse. Dans la vue groupée, cette ligne concerne la voix sélectionnée ; dans la vue empilée, chaque couloir possède sa ligne. Le chemin est mémorisé dans le `.pht`. Si le fichier a été déplacé, le bouton permet de le retrouver. Les voix dont la source est inconnue n’affichent pas de provenance supposée.

### Créer une voix vide

Cliquer sur **+**, près du sélecteur. Choisir :

- le nom de la voix, par exemple `Apports`, `Variante` ou `Référence` ;
- son instrument ;
- une des **12 couleurs** du nuancier.

La voix créée devient active. Elle peut recevoir des notes dessinées, collées ou importées par **Ajouter un MIDI au projet…**. Sa création ne transfère et ne supprime aucune note existante. Au démarrage ou après **Nouveau projet**, une voix Piano vide permet de dessiner des notes et de préécouter au clavier sans importer de fichier. La banque sonore se prépare au démarrage ; le message indique lorsque le projet est prêt.

Les pastilles du nuancier n’affichent pas de texte ; leurs noms restent accessibles au survol. Le contour indique la couleur choisie. Le bouton avec l’échantillon coloré, à côté du sélecteur, permet de changer la couleur d’une voix existante. La couleur est un réglage de projet ; elle ne modifie pas les événements MIDI ni le son.

Le bouton **Instrument…** change le son de la voix active, même vide et sans sélectionner de notes. Son nom personnalisé est conservé. Le changement d’instrument au clic droit suit la même règle. La couleur, les positions et les durées restent conservées ; Ctrl+Z permet d’annuler.

### Visibilité, son et protection

| Contrôle | Effet |
| --- | --- |
| **Œil** | Afficher ou masquer la voix active. Avec Toutes les parties, afficher ou masquer l’ensemble. Chaque voix conserve son état dans le projet, sans changer le son. |
| **Haut-parleur** | Couper ou réactiver la voix choisie. |
| **Volume de la voix** | Régler son niveau d’écoute de 0 à 100 %, sans modifier ses vélocités. Double-clic : retour à 100 %. |
| **Cadenas** | Protéger ses notes contre l’édition et les griser. |
| **Couleur** | Recolorer une voix non verrouillée. |
| **Instrument…** | Changer son instrument en conservant son nom. |
| **Poubelle** | Supprimer la voix et toutes ses notes, après confirmation. |

Changer de voix active ne change pas les voix audibles : toutes les voix non muettes restent entendues. Le volume par voix est mémorisé dans le projet ; il sert à l’écoute, sans modifier les exports MIDI ou WAV.

Le clavier horizontal en bas garde une hauteur compacte fixe et sert à la préécoute : maintenir une touche puis relâcher pour arrêter. Glisser entre touches pour chercher une hauteur. Il utilise l’instrument et le volume de la voix active, respecte le volume MIDI global et fonctionne sur une voix verrouillée. Aucune note n’est créée.

Pendant une prise, les changements de projet, de voix et les éditions sont désactivés ; la fermeture de la fenêtre termine d’abord la prise puis propose l’enregistrement habituel. Une déconnexion termine également la prise et conserve les notes reçues. La saisie pas à pas est désarmée à la déconnexion et au chargement d’un projet. La reconnexion restaure la préécoute, sans démarrer une prise. La quantification du jeu n’est jamais automatique.


Le verrouillage ne coupe pas le son. Le mute n’efface pas les notes. Réactiver une voix ne réactive pas les notes qui avaient été rendues muettes individuellement.

La suppression inclut les notes muettes ou cachées par le seuil de détection ; la confirmation indique le nom et le nombre total. Ctrl+Z restaure la voix en une fois. Une voix verrouillée reste protégée.

### Hachures et couleurs fonctionnelles

Les hachures signalent des **chevauchements de même hauteur** sur la portion commune, y compris entre voix. Elles ne représentent pas un verrouillage. Clic droit dans la zone hachurée donne accès aux notes superposées ; le diagnostic du même nom aide à les localiser.

Le magenta vif appartient au repérage **hors gamme**. D’autres couleurs signalent la lecture, des diagnostics ou le verrouillage. Ces indications passent avant la couleur personnelle de la voix. Une note hors gamme peut être parfaitement intentionnelle.

## 6. Éditer les notes

Cliquer sur une note la sélectionne et permet sa préécoute. **Ctrl+clic** ajoute ou retire une note de la sélection. Glisser dans le vide sélectionne les rectangles traversés ; **Maj+glisser dans le vide** sélectionne une plage temporelle.

| Geste | Résultat |
| --- | --- |
| Crayon, ou double-clic dans le vide | Ajouter une note de 500 ms, à ajuster ensuite. |
| Glisser le corps d’une note | Déplacer la note, ou le groupe sélectionné. |
| Maj+glisser une note | Déplacer sur un axe, choisi au début du geste. |
| Bord gauche / droit | Ajuster le début / la fin. |
| Bord haut / bas, avec assez de zoom | Régler la vélocité. |
| Ctrl+glisser une note | Dupliquer librement. |
| Ctrl+Maj+glisser une note | Dupliquer sur un seul axe. |
| Alt+clic | Supprimer la note touchée. |
| Suppr | Supprimer la sélection. |

Pour **Maj+glisser** et **Ctrl+Maj+glisser**, une direction suffisamment nette choisit l’axe ; les petits mouvements diagonaux initiaux sont ignorés. L’axe reste verrouillé jusqu’au lâcher de la souris, même si Maj est relâchée avant. En vertical, débuts et fins restent exactement identiques ; en horizontal, les hauteurs sont conservées. Un geste resté ambigu ne crée aucune copie et ne change pas la sélection.

Une duplication conserve les originales et sélectionne les copies. Glisser une note sélectionnée duplique le groupe éditable ; une note non sélectionnée est copiée seule. Un simple Ctrl+clic ne crée aucune copie. **Échap** annule le geste avant validation.

### Durée et vélocité : deux réglages distincts

La largeur totale du rectangle représente la durée MIDI. La proportion remplie représente la vélocité, de 1 à 127. **La portion transparente n’est pas une traîne de résonance.** Déplacer sa limite règle l’intensité de la note, pas son instant de relâchement.

Le survol indique le début, la fin, la durée en millisecondes et la vélocité. Le son peut continuer après la fin MIDI selon l’instrument, sa réverbération ou la pédale. Les estimations acoustiques de fin utile ne garantissent pas l’instant réel où une touche a été relâchée.

### Scinder, fusionner et lier

- Bouton **ciseaux**, ou **C maintenu + clic** : scinder au point choisi. Chaque fragment doit durer au moins 10 ms.
- **F** : fusionner les notes compatibles sélectionnées, de même hauteur et de même voix.
- **L** : lier les notes sélectionnées avec un chevauchement réglable, en conservant les attaques suivantes. Le clic droit propose aussi de retirer une liaison.
- Le clic droit donne accès aux modifications détaillées et aux actions de prolongement proposées.

La **gomme de notes** efface les rectangles qu’elle touche dans la partie active. Sa molette règle le diamètre ; un passage entier s’annule par un Ctrl+Z. La gomme de masque, elle, efface seulement le masque d’analyse.

## 7. Comparer et compléter avec les calques

### Appliquer les propositions spectrales

La recherche couvre MIDI 0–127 (C−1 à G9), bornée par le registre choisi. Des fenêtres longues distinguent les graves ; des fenêtres courtes et les transitoires aident à repérer les attaques. Les propositions restent expérimentales et à vérifier à l’écoute.

Dans **Vérifier le MIDI avec l’audio**, ouvrir une catégorie puis utiliser sa commande pour ajouter toutes les notes manquantes proposées, fusionner les tenues, appliquer les durées / réattaques ou supprimer les notes probablement en trop. **Appliquer toutes les propositions…** réunit les catégories de la dernière vérification, dans la zone analysée.

Les listes affichent 50 lignes par page, sans écrêter les résultats à 256. Les commandes par lot incluent toutes les pages. Le récapitulatif permet de choisir une voix ou **Nouvelle voix…**, même depuis un WAV sans MIDI. Les voix verrouillées et les conflits sont exclus. Écouter avant de confirmer ; **Ctrl+Z** annule ensemble la création de voix et les notes. Relancer la vérification après application ; le spectre compatible est réutilisé.

### Copier vers une autre voix

1. Choisir la voix source et sélectionner les notes.
2. Faire **Ctrl+C**.
3. Choisir la voix destination.
4. Faire **Ctrl+V**.

Si la destination est une autre voix, les copies gardent les temps et hauteurs d’origine, même si la tête de lecture est ailleurs. Elles adoptent l’instrument de destination. Les durées, espacements, vélocités et mutes individuels restent conservés.

Dans la **même voix**, le collage place la première attaque à la tête de lecture. Avec **Toutes les parties**, il garde les voix d’origine et place le groupe à la tête de lecture. Le presse-papiers reste interne à Phthongos. Ctrl+Z annule tout le collage.

### Tester une variante

Créer une voix `Variante`, y copier un passage, puis verrouiller la référence. Modifier seulement la variante. Alterner les mutes pour comparer ; pour éviter un doublage involontaire, ne pas laisser jouer simultanément la référence et sa copie lorsqu’on veut les écouter séparément.

Pour ajouter un enrichissement, créer une voix `Apports`, choisir sa couleur, puis y dessiner, coller ou importer les notes supplémentaires. Le contenu de la transcription initiale reste intact tant qu’on ne l’édite pas.

## 8. Grille musicale et BPM

Le bouton **BPM**, à droite des sélecteurs de grille, ouvre **Tempo / calculette rythmique**. Le BPM est une référence musicale ; changer sa valeur conserve les attaques et les durées réelles en secondes.

La durée d’une noire vaut `60 000 / BPM` millisecondes.

| Valeur simple | 65 BPM | 120 BPM |
| --- | ---: | ---: |
| Ronde | 3 692,308 ms | 2 000 ms |
| Blanche | 1 846,154 ms | 1 000 ms |
| Noire | 923,077 ms | 500 ms |
| Croche | 461,538 ms | 250 ms |
| Double-croche | 230,769 ms | 125 ms |

La calculette compare aussi une durée saisie à des valeurs simples, pointées et en triolet. Le résultat reste indicatif : il ne réécrit pas la note.

Trois modes sont disponibles : **Musicale**, **Secondes** et **Aucune**. La grille musicale propose notamment `1/4`, `1/8`, `1/16`, `1/8T`, `1/16T`. Le `T` indique un triolet. La grille en secondes sert aux mesures temporelles et au rapprochement avec l’audio ; sa densité s’adapte au zoom.

**La grille actuelle est visuelle : elle ne quantifie pas les notes.** Afficher 1/8 ne déplace aucune attaque et ne change aucune durée. Les microvariations de la performance restent présentes.

Un MIDI importé peut contenir plusieurs tempos : le réglage permet de conserver cette carte ou de fixer un BPM de référence pour l’export. Un WAV ne fournit pas automatiquement un BPM fiable : renseigner celui du morceau lorsqu’il est connu.

Un BPM cohérent aide la conversion en partition. Il ne garantit pas une écriture simple : les durées, les attaques, les chevauchements et les règles d’import du logiciel de notation comptent aussi.

Dans le panneau **Tempo / calculette rythmique**, **Estimer depuis les notes** calcule une proposition à partir des notes actuelles, après transcription ou import MIDI et après les corrections. Choisir une voix dans la liste pour analyser cette voix, ou **Toutes les parties** pour analyser l’ensemble. Les notes rendues muettes individuellement sont exclues ; la visibilité et le mute de lecture des voix ne changent pas le périmètre. Le résultat rappelle la voix analysée et propose le BPM, sa moitié, son double et d’autres hypothèses. Choisir une proposition, l’écouter au métronome puis cliquer **Appliquer** pour retenir le tempo. L’estimation seule et l’annulation ne modifient ni le BPM du projet ni les notes. Relancer l’estimation après modification des notes si nécessaire.

## 9. Lecture et contrôle

**Audio**, **MIDI** et **Audio + MIDI** choisissent les sources écoutées. Les volumes audio et MIDI se règlent séparément. **Espace** alterne lecture et pause. **Suivre la lecture** déplace la vue pendant la lecture.

Le piano fourni sert au programme piano. La banque General MIDI optionnelle fournit d’autres instruments. Le statut sous les commandes de lecture permet d’accéder aux réglages ; **Parcourir** peut désigner une banque déjà installée. La réverbération influence le rendu sonore.

Dans le piano roll actif, **← / →** déplace la tête de 0,5 s ; **Maj+← / →** utilise un pas de 50 ms. Les flèches conservent leur fonction habituelle dans une liste ou une saisie.

**Ctrl+molette** zoome le temps et les hauteurs ; **Alt+molette** zoome les hauteurs ; **Maj+molette** déplace horizontalement. La main ou le bouton central permet de déplacer la vue. **Tout voir** recadre le contenu.

Clic droit sur la règle temporelle permet d’ajouter un repère nommé. Cliquer sur un repère y amène la tête de lecture ; le glisser le déplace. Utiliser les repères pour retrouver les passages à vérifier plus tard. Ils sont conservés dans le projet.

## 10. Exporter

### MIDI

**Exporter MIDI** produit un fichier par voix, ou uniquement la voix sélectionnée. Une seule voix donne un seul fichier. Les voix muettes sont exclues ; les voix verrouillées restent exportées. Les fichiers partagent le même tempo et la même origine temporelle. Au clic comme en glisser-déposer : `Mon projet.mid` pour une voix ; `Mon projet - Piano.mid`, etc., pour plusieurs voix.

Glisser depuis **Exporter MIDI** vers le Bureau, un dossier ou une application compatible exporte un fichier par voix non muette. Un aperçu affiche le nom et le nombre de fichiers. Le clic ouvre le choix de destination. Les fichiers temporaires restent disponibles après le dépôt.


Le MIDI conserve les événements édités et leurs positions temporelles ; l’export ne constitue pas une quantification. La couleur des calques et les diagnostics sont des éléments du projet Phthongos, sans rôle dans le son MIDI.

Pour préparer une partition, importer le MIDI dans le logiciel de notation ou de conversion choisi, puis régler son interprétation rythmique. Des triolets, liaisons ou petits silences peuvent provenir de la façon dont ce logiciel interprète les timings. Phthongos n’exporte pas directement de partition MusicXML dans cette version.

### WAV

**☰ → Exporter en WAV…** rend les notes MIDI audibles avec les instruments utilisés pour la lecture. Choisir la portée proposée dans le formulaire. Cet export est une restitution du MIDI, pas un export du WAV source accompagné de toutes les surimpressions visuelles.

Si une banque nécessaire manque, suivre la proposition d’installation ou de choix d’un fichier existant. Attendre le message confirmant la création du fichier. En cas de saturation, réduire le volume MIDI avant de refaire le rendu.

## 11. Méthodes de travail

### Nettoyer une transcription

1. Sauvegarder une étape et déclarer le BPM connu.
2. Choisir une voix et un passage court, puis afficher la superposition.
3. Comparer les attaques et les notes avec l’audio.
4. Examiner les chevauchements ; fusionner seulement les fragments qui appartiennent à une même note.
5. Utiliser la gomme ou le mute pour les parasites identifiés.
6. Régler la gamme puis **Sélectionner les notes hors gamme** : cette commande affiche aussi leur repérage. Le détail par voix aide en vue globale.
7. Réécouter et enregistrer une nouvelle étape.

Le hors-gamme, la brièveté et le chevauchement sont des indices, pas des motifs de suppression automatique.

### Estimer la dynamique depuis l’audio

**Dynamique depuis l’audio** propose des vélocités relatives : **Niveau moyen**, **Contraste (%)** (0–250 %) et bornes **Min/Max** (1–127, encadrant le niveau moyen). Les réglages réutilisent le cache ; **Recalculer depuis l’audio** renouvelle les mesures sans modifier le MIDI ni l’historique.

**Appliquer les vélocités** remplace les valeurs du périmètre choisi, même manuelles ou spectrales, sauf voix verrouillées et percussions. Hauteurs et durées sont conservées. Écouter avant application ; Ctrl+Z annule. Une saisie manuelle ultérieure reprend la main. **Retirer toute la dynamique audio** restaure les valeurs antérieures. Réglages et corrections sont sauvegardés.

### Reconstruire une ligne manquante

Créer une voix `Complément`, choisir son instrument, peindre un masque limité dans le spectrogramme et examiner les propositions guidées. Ajouter les propositions utiles et compléter au crayon. Vérifier les répétitions à même hauteur et distinguer les nouvelles attaques de la résonance.

### Comparer une variante ou enrichir l’arrangement

Copier le passage vers une nouvelle voix, verrouiller la référence, puis éditer les copies. Pour comparer, alterner les mutes. Pour enrichir, laisser les deux voix audibles après avoir contrôlé les doublages et les équilibres de vélocité.

### Réunir plusieurs MIDI

Créer les voix destinations ou les laisser être créées pendant l’import. Ajouter chaque fichier avec un décalage approprié, vérifier le départ à l’écoute et dans le spectrogramme, puis choisir une référence de tempo cohérente. Importer dans une voix existante garde son instrument. Le fichier ajouté n’est pas modifié sur le disque.

## 12. Référence rapide

Les gestes ci-dessous supposent le piano roll actif et l’absence d’une boîte de saisie.

| Raccourci / geste | Fonction |
| --- | --- |
| Ctrl+S | Enregistrer ; nouvelle version si l’incrémentation est activée. |
| Ctrl+Maj+S | Enregistrer sous… |
| Ctrl+Z | Annuler (25 derniers états conservés). |
| Ctrl+Y ou Ctrl+Maj+Z | Rétablir. |
| Ctrl+A | Sélectionner les notes éditables. |
| Ctrl+clic | Ajouter / retirer une note de la sélection. |
| Ctrl+C | Copier les notes sélectionnées. |
| Ctrl+V | Autre voix : coordonnées d’origine ; même voix / global : tête de lecture. |
| Ctrl+glisser une note | Dupliquer librement. |
| Ctrl+Maj+glisser une note | Dupliquer sur un axe. |
| Maj+glisser une note | Déplacer sur un axe. |
| Maj+glisser dans le vide | Sélection temporelle. |
| C maintenu + clic | Scinder. |
| F | Fusionner les notes compatibles. |
| L | Lier la sélection. |
| M | Muet / audible pour la sélection. |
| Alt+clic | Supprimer une note. |
| Suppr | Supprimer la sélection. |
| Échap | Annuler le geste en cours ; quitter l’outil / effacer la sélection selon le contexte. |
| R | Démarrer / arrêter et valider une prise MIDI en temps réel. |
| Espace | Lecture / pause ; pendant une prise MIDI, arrêter et valider. |
| ← / → | Reculer / avancer de 0,5 s. Avec le pas à pas actif, → inscrit les touches tenues et avance d’un pas. |
| Maj+← / → | Reculer / avancer de 50 ms. |
| Ctrl+molette | Zoom proportionnel temps et hauteurs. |
| Alt+molette | Zoom des hauteurs. |
| Maj+molette | Déplacement horizontal. |
| Main ou bouton central + glisser | Déplacer la vue. |
| Molette avec la gomme de notes | Régler son diamètre. |
| Double-clic dans la marge gauche | Revenir au début en conservant le zoom. |
| Clic droit sur la timeline | Ajouter / modifier un repère. |
| Clic droit sur une zone hachurée | Accéder aux notes superposées. |

Les boutons **+**, **Couleur**, **Instrument…**, **Œil**, **Haut-parleur**, **Verrouiller** et **Poubelle** concernent les voix. Le bouton **?** ouvre l’aide des raccourcis à l’intérieur de l’application.


## 13. Saisie MIDI et estimation du tempo

**Clavier USB / MIDI.** Dans les paramètres, choisir **Entrée USB / MIDI**, puis enregistrer. Les notes et accords jouent l’instrument de la voix active, avec leur vélocité et le volume de cette voix. Le clavier du bas indique les touches actives. La préécoute fonctionne sur une voix verrouillée et ne crée aucune note. La pédale de sustain MIDI est prise en compte.

**Saisie pas à pas (outil secondaire).** Ouvrir **Pas à pas…** pour activer ce mode et choisir **Durée des notes et avancement**. Fermer le panneau, maintenir une ou plusieurs touches MIDI, puis appuyer sur **→** dans le piano roll pour inscrire les notes tenues et avancer de cette durée. Une touche relâchée avant l’avance ne crée aucune note ; les autres restent disponibles pour le pas suivant. Aucun choix Mélodie / Accords ni détection d’accord n’est imposé. La durée suit le tempo local et reste indépendante de la grille visuelle. Ctrl+Z annule un pas. Le maintien réel des touches ne fixe pas la longueur dans ce mode. **R** démarre directement l’enregistrement en temps réel et désactive le pas à pas.

**Enregistrement MIDI en temps réel.** Sélectionner une voix déverrouillée et placer la tête de lecture au départ souhaité, puis appuyer sur **R** ou cliquer le bouton **● rouge** près de Lecture et Stop. La lecture démarre à cette position après le précompte, s’il est activé. Jouer librement : chaque Note On affiche une note qui s’allonge jusqu’au Note Off. Les attaques horodatées, les durées, les chevauchements, les accords et les vélocités sont conservés sans quantification, quelle que soit la grille affichée. Les nouvelles notes sont préécoutées avec l’instrument et le volume de la voix active. La pédale agit sur la préécoute ; les durées écrites correspondent aux touches physiques, sans transformer le sustain en allongement des rectangles. Cliquer à nouveau sur le bouton d’enregistrement actif (carré blanc sur fond rouge), **Stop**, **Pause** ou appuyer sur **R** ou **Espace** pour terminer. Les touches encore tenues sont fermées à la fin de la prise. Ctrl+Z annule la prise entière ; Rétablir la restaure. La prise peut dépasser la fin de l’audio source, jusqu’à la limite du projet de 20 minutes.

Extension de la zone de travail : déplacer, glisser-copier ou allonger des notes peut dépasser l’ancienne fin du projet. Maintenir le pointeur près d’un bord horizontal fait défiler la vue automatiquement, sans changer l’échelle temporelle. Le collage prolonge également le projet si nécessaire. Échap annule le glissement et son extension provisoire. La limite de 20 minutes reste applicable.


Pendant l’enregistrement temps réel, l’indicateur supérieur affiche la durée écoulée de la prise (minutes:secondes), indépendamment de la position de lecture dans le projet. Après Stop, Phthongos peut proposer un BPM stable à partir des attaques de la dernière prise. Le bouton BPM conserve le tempo du projet et ajoute ≈ suivi de l’estimation en attente. L’ouvrir pour distinguer BPM du projet, BPM estimé et BPM retenu. Choisir l’estimation, sa moitié ou son double, puis appliquer explicitement, ou conserver le BPM du projet. Aucun tempo n’est imposé et aucune quantification n’est effectuée : les débuts, fins, vélocités et chevauchements restent identiques en secondes. Une prise courte, peu fournie ou irrégulière peut ne pas donner d’estimation. La proposition reste indicative et ambiguë à la moitié ou au double ; ses métadonnées sont conservées dans le PHT. Cette analyse ne modifie ni ne regroupe les événements enregistrés.

L’estimation après prise utilise un portage C++ natif de MIDI BPM Detection (MIT). Plusieurs tempos candidats et leurs scores relatifs sont conservés : ces poids comparent les pics du calcul, sans mesurer une probabilité de justesse. La liste propose moitié/double et d’autres hypothèses ; confirmer un choix ne déplace ni ne quantifie les notes. Le calcul exploite les attaques, hauteurs et vélocités, sans analyser les durées ni le phrasé complet. Les prises longues ou denses utilisent des fenêtres d’analyse bornées. Aucun moteur ni dépendance de compilation Rust n’est livré.

## 14. Métronome et précompte MIDI

Le bouton Métronome, près du transport, active le clic pendant la lecture et la prise. Le bouton ⋯ adjacent règle son volume et le précompte : Aucun, 1 mesure (valeur initiale) ou 2 mesures. Le précompte suit le tempo et la mesure au point de départ et reste audible même si le clic est désactivé pour la suite. La tête reste en place jusqu’au départ de la prise. R, Espace ou Stop annulent le précompte sans insérer de notes. Les touches jouées puis relâchées avant le départ ne sont pas inscrites ; celles maintenues au départ commencent à cet instant avec leur vélocité d’appui. Le précompte ne concerne pas la saisie pas à pas.

Dans le panneau BPM, « Écouter la prise avec ce tempo » lit le MIDI avec un clic temporaire au BPM proposé. Le premier clic est aligné sur la première note disponible, ce qui fournit un repère d’écoute sans prouver qu’elle est un premier temps. « Arrêter l’écoute du tempo » ou fermer le panneau arrête cet essai et restaure le clic du projet. Les notes et le BPM retenu ne changent pas tant que tu n’appliques pas un choix. Le clic n’est jamais ajouté aux notes ni à l’export MIDI.

Les volumes d’écoute sont affichés en pourcentage : 0 % est le silence et 100 % la fin de course du curseur. Les niveaux des anciens projets sont conservés. Un double-clic rétablit le niveau initial de chaque volume. Le métronome propose un curseur de volume et trois boutons de précompte. La pulsation visuelle suit le transport audio et la mesure courante ; elle est masquée lorsque le métronome est désactivé, sauf pendant le précompte ou l’écoute d’un tempo candidat. Pendant le précompte, un grand chiffre apparaît sur le piano roll, avec un décompte répété à chaque mesure.

## 15. Voix superposées et empilées

En affichage MIDI, le sélecteur Superposées / Empilées change uniquement la présentation des mêmes notes MIDI. Audio + MIDI conserve les voix groupées avec une zone audio séparée ; Superposition conserve le MIDI groupé sur le signal audio ou le spectrogramme. Le retour en MIDI retrouve la présentation des voix choisie. La vue empilée propose un couloir par voix, avec les commandes de couleur, visibilité, mute, volume, instrument et verrouillage. Cliquer dans un couloir choisit la voix active pour l’édition et la préécoute ; cela ne change pas les voix audibles. Le clavier horizontal reste en bas, à hauteur compacte fixe ; seuls les couloirs de voix et la zone audio sont redimensionnables. La zone audio séparée appartient à l’affichage Audio + MIDI ; aucun espace audio n’est réservé en affichage MIDI.

Cadrage par voix adapte initialement le registre à chaque voix. Échelle commune utilise la même hauteur de demi-ton et le même cadrage relatif dans tous les couloirs. La règle, la tête de lecture, le zoom et le défilement temporel restent communs. La molette parcourt les hauteurs du couloir survolé ; Maj+molette déplace le temps ; Alt+molette zoome les hauteurs ; Ctrl+molette zoome le temps et les hauteurs. L’outil Main ou le clic molette maintenu déplace le registre local et la chronologie commune. La molette sur les en-têtes ou l’unique ascenseur extérieur permet de parcourir les voix.

Tirer la séparation visible entre deux couloirs répartit leur hauteur sans changer la taille des notes ; un double-clic rétablit la répartition automatique. La flèche du couloir le replie sans masquer ses notes ni couper le son. Ajuster à la voix adapte uniquement son registre et son zoom, sans modifier sa hauteur. Les hauteurs, cadrages et replis sont conservés dans le projet.

Pour réaffecter des notes, activer Transférer puis les glisser vers le couloir cible, qui se surligne avant le dépôt. Alt+Maj+glisser permet aussi ce geste directement. Ce transfert conserve hauteur, attaque, durée et vélocité. Échap annule le geste. Une seule annulation rétablit le transfert. Les voix verrouillées ne peuvent être ni source ni destination. Le glisser ordinaire dans un couloir conserve les gestes d’édition habituels.

À l’ouverture en vue Empilées, les couloirs se partagent la hauteur disponible, avec un minimum compact et un registre cadré sur les notes. Un seul ascenseur extérieur apparaît si nécessaire ; le clavier ne recouvre aucun couloir.

## Aide et licences

Le menu **☰ → Paramètres…** rassemble les paramètres globaux ; le moteur et les réglages de travail restent accessibles dans l’interface. **☰ → Manuel utilisateur (PDF)** ouvre hors ligne le manuel correspondant à la langue de l’interface, dans votre lecteur PDF habituel.

**☰ → Crédits et licences** remplace l’ancien panneau Informations. La liste permet de lire les textes de licence livrés sans connexion. Elle distingue les composants intégrés, Upright Piano KW livré sous CC0, les poids MuScriptor optionnels et la banque MuseScore General HQ optionnelle. Les banques SF2 et modèles déclarés manuellement restent soumis à leurs propres licences et ne deviennent pas des composants distribués par Phthongos.

# OXFORD — Reference Console DAW

*Une station de travail audio complète construite autour de la console Sony Oxford OXF-R3 (1995) — la dernière grande console numérique britannique.*

> Projet personnel BourrinAudio, non commercial. DSP **100 % original** (modélisation comportementale d'après les spécifications publiques et le manuel OXF-R3), aucun code ni asset Sony/Sonnox. Basé sur [Ardour](https://ardour.org) (GPL).

---

## C'est quoi, Oxford ?

Oxford est une DAW complète (fork d'Ardour 9.7) dont **chaque piste passe par le chemin de signal de la console OXF-R3** : EQ 5 bandes, dynamique complète, coloration des convertisseurs Sony. Ce n'est pas une collection de plugins — c'est une console. Tu n'as rien à insérer : le traitement est déjà là, sur chaque piste, chaque bus et le master. Et tout Ardour reste dessous : multipiste, édition, automation, plugins VST3/LV2, MIDI…

**La philosophie de la R3, respectée à la lettre :**
- La **sommation est 100 % propre** (32 bits float, aucune coloration cachée de bus).
- La couleur est toujours **un choix conscient** : tape, warmth, saturation — tout se dose, tout se bypasse.
- Une seule exception, fidèle à la réalité : la **micro-distorsion des convertisseurs de la console** sur le master (THD < −96 dBFS — inaudible, mais mesurable, comme sur la vraie).

---

## Installation & premier lancement

1. Lance `Oxford-9.7.0-Setup.exe` et suis l'assistant.
2. Au premier démarrage, choisis ton **backend audio** : pilote ASIO si tu en as un (recommandé), sinon PortAudio/WASAPI.
3. Crée une session : tu obtiens des pistes + **12 bus Mix** + le master, tous câblés console.

> Ta configuration personnelle vit dans `%LOCALAPPDATA%\Oxford9`.

---

## Le panneau console (à droite du Mixer)

Le cœur d'Oxford : un **panneau central unique** qui suit la piste sélectionnée — comme la section centrale assignable de la vraie R3, où un seul jeu de commandes servait 96 canaux. Clique sur une tranche, le panneau affiche ses réglages ; **chaque piste garde sa propre mémoire**.

En tête : **Pan** et **Width** (largeur stéréo M/S, masqué en mono). Trois onglets :

### EQ
- **Écran de courbe interactif** — il trace la **réponse réelle des filtres** (coefficients exacts, échelle verticale automatique) ; les pastilles colorées sont des **poignées** :
  - **glisser** = fréquence + gain ;
  - **molette** = Q (ou résonance de coude en mode Shelf) ;
  - les deux pastilles **crème** = filtres HP/LP : glisser les engage et règle la fréquence, la molette change la pente (6 → 36 dB/oct ; en dessous de 6 = off).
- **5 bandes** LF / LMF / MF / HMF / HF, ±20 dB, **Q 0,5–16** (potard log, défaut 2,83), plages OXF-R3 : LF 20–400, LMF 30–600, MF 100–6 k, HMF 600–18 k, HF 2 k–20 k. Bouton **In** par bande.
- **LF/HF commutables Shelf** — le Q pilote alors l'**overshoot** du coude.
- **Curve** — 5 lois de Q. Les Types 1–4 sont **calibrés par mesure sur l'Oxford EQ de référence** (banc automatisé, écart médian 0,015 dB) :
  - *Type 1* : Q constant « console » — se resserre en poussant le gain (chirurgical, résonances)
  - *Type 2* : boost = Type 1, mais les **cuts deviennent des notches** avec la profondeur (le seul asymétrique — dompteur de fûts)
  - *Type 3* : **le défaut d'usine** — large et constant à tous les gains, le plus musical
  - *Type 4* : très large aux petits gains (« aire constante »), rejoint le Type 3 à fond (mastering doux)
  - *GML* : émulation **GML 8200** — vraie **topologie parallèle réciproque** (un cut annule exactement le boost identique), gains ±15 dB, Q 0,4–4, shelves doux. Essaie-le en touches larges sur un master.
- L'EQ est **« decramped »** : les cloches gardent leur symétrie analogique jusqu'à 20 kHz (design Orfanidis — la signature du son Oxford dans l'aigu, vérifiée à la mesure : largeur de bande constante sur tout le spectre).

### DYN
La dynamique complète de la R3, **feed-forward, sidechain logarithmique**, avec l'écran de transfert IN/OUT (genou, seuil, GR en direct) et **4 vumètres de réduction** LED avec peak-hold :
- **GATE** — seuil −80..0, plage, hystérésis +4 dB (pas de claquement à la réouverture).
- **EXPANDER** — downward, ratio 1–16.
- **COMPRESSOR** — loi de ratio non linéaire du hardware (1:1 → ∞, affichée en X:1), soft knee 0–20 dB, makeup ≤ +24 dB, **Hold** (gel de la réduction avant le release, 10 ms–30 s).
- **LIMITER** — seuil référencé à la **sortie** (post-makeup), Hold 50 ms–30 s.
- Sur une piste **stéréo**, la dynamique est **stéréo-linkée** : sidechain commun aux deux canaux, l'image ne bouge pas quand ça compresse (comportement console).

### COLOR
- **TAPE 3348** — *actif par défaut sur chaque piste.* Sur une vraie session R3, le signal passait d'abord par le magnétophone multipiste Sony PCM-3348 (DASH). Ce module reproduit le caractère de ses convertisseurs : **Drive** (saturation douce), **Emph** (emphasis HF ±), **Grain** (le grain numérique DASH : la distorsion *monte* quand le niveau descend — l'inverse d'une bande analogique ; écoute une queue de réverbe avec Grain à fond pour comprendre). Désactive-le pour une piste transparente.
- **WARMTH** — saturation de densité : augmente la loudness perçue **sans monter les crêtes**. Essaie 30 % sur un bus batterie.
- **PCM-1630 · MASTER** — les trims du module master, visibles quelle que soit la piste (voir plus bas).

**Ergonomie console :**
- **Alt + glisser** un potard (ou Alt + clic sur un bouton) = applique le geste à **toutes les pistes sélectionnées**.
- **Alt + Shift + clic** sur l'entrée/sortie d'une tranche = route toute la sélection vers la même destination.

---

## Pistes, bus, master : qui fait quoi

### Pistes
Chaîne : **Tape 3348 → Filtres HP/LP + EQ → Dynamique → Warmth**, verrouillée **avant le fader**. Un plugin pré-fader passe donc *avant* le tape (utile pour re-amper).

### Les 12 bus Mix = les « Returns » de la R3
Sur la vraie console, les bus n'avaient **aucun traitement** — on traitait un groupe en le retournant dans un **canal Return** (la R3 en avait 12 stéréo). Oxford reproduit ça : sélectionne un bus et le panneau passe en **mode Return** authentique :
- EQ **3 bandes** (LF / MF / HF, peak ou shelf) — pas de LMF/HMF ni de filtres à pente ;
- **Gate + Compresseur** seulement — pas d'expander ni de limiteur ;
- Warmth disponible ; pas de tape (un return ne passait pas par le magnéto).

### Master
La chaîne se scinde pour laisser la place à tes plugins :
1. **Bus-compressor** Oxford (stéréo-linké, avant le fader) ;
2. tes plugins post-fader ;
3. **Oxford Tail** : Warmth (opt.) → Limiteur brickwall (opt.) → **convertisseur OXF-R3** (structurel, toujours actif, −96 dBFS) ;
4. **Oxford PCM-1630** — tout dernier de la chaîne, bypassable.

### Le PCM-1630 (le « son de master » d'Oxford)
Une **capture neuronale réelle** (NAM) du convertisseur du Sony PCM-1630 — l'enregistreur U-matic 16 bits sur lequel on gravait les masters de CD ; le profil vient de l'unité de **Bob Ludwig**.

**Important — modèle non linéaire dépendant du niveau** : si ton mix arrive fort, il sature, et un limiteur en amont n'y change rien (c'est le niveau *moyen* qui compte). Le geste juste, dans COLOR → PCM-1630 · MASTER :
- baisse **In** de −6 à −10 dB jusqu'à ce que la saturation devienne de la colle ;
- remonte **Out** d'autant pour compenser.

---

## L'apparence : trois ambiances, une identité

**Préférences → Apparence → Color Theme** — le choix s'applique en direct, panneau console compris :
- **Oxford** — le bleu clair authentique du hardware OXF-R3 (le « musée »).
- **Oxford Warm** — les mêmes principes en greige chaud, reposant pour les longues sessions.
- **Oxford Night** — anthracite, texte crème, ambre : le studio de nuit.

Quel que soit le fond, la signature reste : **écrans navy** (graphes du panneau ET compteurs de la barre de transport, chiffres verts fluorescents façon compteur de magnéto), pastilles de bandes EQ colorées, ambre pour l'édition, rouge pour le record. Chaque tranche du mixer porte une **bande de couleur en pied** — la couleur de la piste (clic droit sur son nom → Couleur).

## Surfaces de contrôle

Le support **Mackie Control** est intégré (Préférences → Control Surfaces). Testé avec le **Behringer X-Touch One** : choisis le device « X-Touch One » (le fader suit alors la piste sélectionnée dans le GUI, boutons CH ‹/› = piste précédente/suivante) et mets le contrôleur en mode **MC**.

## Raccourcis clavier : mode Pro Tools

Oxford utilise par défaut un jeu de raccourcis **style Pro Tools** :

| Raccourci | Action |
|---|---|
| `Ctrl` + `=` | basculer Édition ↔ Mixer |
| `Espace` / `Ctrl` + `Espace` | lecture / **enregistrement** |
| `Ctrl` + `E` | séparer la région au curseur |
| `Tab` / `Shift` + `Tab` | transitoire suivant / précédent |
| `Entrée` | poser un marqueur |
| `Ctrl` + `D` / `Alt` + `D` | dupliquer / multi-dupliquer |
| `J` / `K` | trim début / fin de région au curseur |
| `Ctrl` + `[` / `]` | zoom arrière / avant |
| `Ctrl` + `/` et `Ctrl` + `\` | fade in / out jusqu'au curseur |
| `,` / `.` | début / fin de sélection de range |
| `Q` / `W` | marqueur précédent / suivant |
| `Z` | zoom sur la sélection |
| `Shift` + `"` | nouvelles playlists (takes) pour les pistes sélectionnées |

Tout est modifiable dans **Window → Keyboard Shortcuts**.

## Outils d'édition « Pro Tools » (menu Edit → Lua Scripts)

- **PT : Batch Fades** — fade in/out (durée + forme) sur toutes les régions sélectionnées d'un coup.
- **PT : Heal Separation** — ressoude des régions issues d'un même split (si elles n'ont pas bougé l'une par rapport à l'autre).
- **PT : Batch Rename** — renommage en série des pistes sélectionnées : chercher/remplacer, préfixe/suffixe, numérotation.

Via **Edit → Lua Scripts → Script Manager**, assigne-les à des slots d'action pour leur donner un raccourci clavier.

## Plugins Sony inclus

L'installeur pose aussi deux VST3 maison (utilisables dans n'importe quelle DAW) :
- **Sony Reverb** — convolution sur 130 impulsions des Sony **DRE-2000** et **DPS-V77** (menu en cascade par appareil/type, low cut / high cut du wet, predelay, output).
- **Sony DPS-D7** — le délai numérique de 1991, 7 presets complets + Mix.

---

## Conseils de performance

- La jauge DSP dépend surtout de la **taille de buffer** : 64/128 pour enregistrer, **512/1024 pour mixer**.
- Les modules Oxford **éteints ne coûtent rien**, et **le silence non plus** : à l'arrêt, le Tape 3348 et le réseau neuronal du PCM-1630 se mettent en veille automatiquement (bit-exact — ils se réveillent au premier échantillon de signal). Une session posée tourne à quelques % de jauge.
- En lecture, le poste principal est le **PCM-1630** du master (réseau neuronal) : bypasse-le pendant le mix, réactive-le pour l'écoute finale et l'export.
- Le moteur parallélise les pistes sur tous les cœurs.
- **Fenêtre → Performance Meters** donne le détail réel (pire cas + moyenne au survol) si tu veux comprendre où va chaque milliseconde.

---

*Sony, Oxford OXF-R3, PCM-3348, PCM-1630, DRE-2000, DPS-V77, DPS-D7 sont des marques de leurs propriétaires respectifs, citées à titre de référence historique.*

*Bon mix — la console est à toi.* 🎛️

# Anleitung: Probe-Test in Premiere Pro und DaVinci Resolve

> **Achtung, Stand 2026-11-13: den Premiere-Probe unter Windows (`TemporalProbeCPU.aex`) NICHT
> installieren.** Schon installiert, ohne Anwendung, stört er Premiere Pro 2027: Das Bild
> aktualisiert sich nicht mehr, und das Programmfenster streckt das Video. Entfernen der `.aex`
> behebt das. Die Ursache wird untersucht (siehe README.md, "Host results so far").

Ziel: herausfinden, ob ein Effekt in Premiere und Resolve **die Nachbarbilder** (Frame davor und
danach) bekommt, und zwar **in derselben Verarbeitungsstufe** wie das aktuelle Bild. Davon hängt
ab, ob der Denoiser über mehrere Frames arbeiten kann.

Die Probes entrauschen nichts. Sie zeigen nur an, welches Bild sie bekommen. Sie sind nur für
diesen Test gedacht: in einem **Wegwerf-Projekt** verwenden, danach wieder entfernen.

Dauer: etwa 30–45 Minuten pro Programm.

---

## 1. Herunterladen

1. Öffne die Seite des Probe-Builds (GitHub-Login nötig):
   https://github.com/d1anarene/venosta-visuals-plugins/actions/runs/43506576453
2. Unten unter **Artifacts** herunterladen:

| Datei | Für | Inhalt |
| --- | --- | --- |
| `research-premiere-temporal-probes-macOS` | Premiere, Mac | `TemporalProbeCPU.plugin`, `TemporalProbeMetal.plugin` |
| `research-premiere-temporal-probes-Windows` | Premiere, Windows | `TemporalProbeCPU.aex` |
| `research-openfx-temporal-probe-macOS` | Resolve, Mac | Ordner `Contents` (Inhalt von `VVTemporalProbe.ofx.bundle`) |
| `research-openfx-temporal-probe-Windows` | Resolve, Windows | Ordner `Contents` (Inhalt von `VVTemporalProbe.ofx.bundle`) |

3. Die Dateien in deinem **Downloads**-Ordner lassen, egal ob als ZIP oder schon entpackt. Die
   Befehle in Schritt 2 holen sie von dort.

## 2. Installieren

Premiere und Resolve vorher **beenden**. Unsere normalen Plugins bleiben, wo sie sind. Die Befehle
kopieren die Probes aus deinem Downloads-Ordner in die Plugin-Ordner der Programme. Achtung: Im
Resolve-Download fehlt der äußere Ordner `VVTemporalProbe.ofx.bundle`, die Befehle legen ihn selbst an.

**Mac:** Programm **Terminal** öffnen (Spotlight: "Terminal"), den ganzen Block einfügen, Enter.
Danach fragt es nach deinem Mac-Passwort. Beim Tippen siehst du nichts, das ist normal.

```bash
cd ~/Downloads
for n in research-premiere-temporal-probes-macOS research-openfx-temporal-probe-macOS; do
  [ -d "$n" ] || unzip -q -o "$n.zip" -d "$n"
done
P="/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
B=/Library/OFX/Plugins/VVTemporalProbe.ofx.bundle
sudo mkdir -p "$P" "$B"
sudo cp -R research-premiere-temporal-probes-macOS/*.plugin "$P/"
sudo cp -R research-openfx-temporal-probe-macOS/Contents "$B/"
sudo chmod -R a+rX "$P"/TemporalProbe*.plugin "$B"
sudo chmod +x "$P"/TemporalProbe*.plugin/Contents/MacOS/* "$B"/Contents/MacOS/*
sudo xattr -dr com.apple.quarantine "$P"/TemporalProbe*.plugin "$B"
ls "$P" | grep TemporalProbe; ls "$B/Contents"
```

Am Ende sollten `TemporalProbeCPU.plugin`, `TemporalProbeMetal.plugin` sowie `Info.plist` und
`MacOS` aufgelistet werden.

**Windows:** Startmenü → "PowerShell" → Rechtsklick → **Als Administrator ausführen**, Block
einfügen, Enter:

```powershell
cd "$env:USERPROFILE\Downloads"
foreach ($n in "research-premiere-temporal-probes-Windows", "research-openfx-temporal-probe-Windows") {
  if (-not (Test-Path $n)) { Expand-Archive "$n.zip" -DestinationPath $n -Force }
}
$P = "C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore"
$B = "C:\Program Files\Common Files\OFX\Plugins\VVTemporalProbe.ofx.bundle"
New-Item -ItemType Directory -Force $P, $B | Out-Null
Get-ChildItem "research-premiere-temporal-probes-Windows" -Recurse -Filter *.aex | Copy-Item -Destination $P -Force
Copy-Item "research-openfx-temporal-probe-Windows\Contents" $B -Recurse -Force
Get-ChildItem $P\TemporalProbe*, $B -Recurse | Unblock-File
Get-ChildItem $P\TemporalProbe*; Get-ChildItem $B\Contents -Recurse | Select-Object FullName
```

Am Ende sollten `TemporalProbeCPU.aex` und eine Datei `...\Contents\Win64\VVTemporalProbe.ofx`
aufgelistet werden.

Wenn eine Fehlermeldung kommt, schick mir den Text, dann passe ich die Befehle an.

Danach das Programm starten. Die Probes stehen in einer eigenen Gruppe **Venosta Visuals Research**:
- Premiere: **Temporal Probe CPU (research)** und, nur auf dem Mac, **Temporal Probe Metal (research)**.
- Resolve: **Temporal Probe (research)** unter OpenFX.

## 3. Testclip mit Bildnummern vorbereiten (einmalig)

Für den Test brauchen wir einen Clip, auf dem **jedes Bild seine Nummer zeigt**. Die Nummer muss
fest im Video stehen und darf kein Effekt sein, der vor dem Probe sitzt.

1. Premiere: neue Sequenz mit einem beliebigen Clip **mit Bewegung**, etwa 10 Sekunden.
2. Effekt **Zeitcode** (Timecode) auf den Clip, Anzeige auf **Frame-Nummer** stellen, schön groß.
3. Als ProRes 422 (Mac) oder mit hoher Qualität (Windows) **exportieren**.
4. Diese exportierte Datei ist dein **Testclip**. Für alle Tests den Testclip importieren, ohne den
   Zeitcode-Effekt. Denselben Clip auch in Resolve verwenden.

Im Folgenden heißt die Nummer, die oben im Bild steht, **n**.

## 4. Die Regler des Probes

| Regler | Bedeutung |
| --- | --- |
| **Neighbour offset (frames)** | Welcher Nachbar: −1 = Bild davor, +1 = Bild danach, 0 = dasselbe Bild nochmal holen |
| **View** | 0 = aktuelles Bild · 1 = das geholte Nachbarbild · 2 = Unterschied zwischen beiden |
| **Difference gain** | Verstärkung für View 2. Für die Tests auf **10** stellen |
| **Measure current input** (Knopf) | Misst das aktuelle Bild und speichert einen Wert im Regler darunter |
| **Saved measured RGB mean** | Der gespeicherte Messwert |
| Log diagnostics | Aus lassen |

Wenn das Programm kein Nachbarbild liefert, rendern View 1 und 2 absichtlich **nicht**. Dann
erscheint ein Fehler, ein rotes oder ein schwarzes Bild. Bitte genau notieren, was du siehst.

## 5. Die Tests

Jeden Test in **Premiere** durchführen (Mac: mit dem CPU- **und** dem Metal-Probe; Windows: CPU-Probe;
für den Metal-Probe muss in Datei → Projekteinstellungen → Allgemein der Renderer
"Mercury Playback Engine GPU-Beschleunigung (Metal)" eingestellt sein)
und in **Resolve** (Edit- oder Color-Seite). Wiedergabe gestoppt, Playhead auf einem Bild in der
Mitte des Clips.

### Test A — Holt der Probe das richtige Bild?
Probe direkt auf den Testclip legen.

| Einstellung | Erwartet | Notieren |
| --- | --- | --- |
| View 0 | normales Bild mit Nummer n | ok? |
| View 1, Offset 0 | dieselbe Nummer n | welche Nummer? |
| View 1, Offset −1 | Nummer n−1 | welche Nummer? |
| View 1, Offset +1 | Nummer n+1 | welche Nummer? |
| View 1, Offset −3 und +3 | n−3 / n+3 | welche Nummern? |
| View 2, Offset 0 | **komplett schwarz** | schwarz? |
| View 2, Offset −1 | nur bewegte Kanten leuchten | so? |

### Test B — Gleiche Verarbeitungsstufe (der wichtigste Test)
Auf denselben Clip **vor** den Probe einen auffälligen Farbeffekt legen: Premiere Lumetri mit
Sättigung 0 (Schwarzweiß), Resolve auf der Color-Seite einen Node davor mit Sättigung 0. Der
Probe kommt danach (Premiere: in der Effektliste darunter; Resolve: im Node danach).

| Einstellung | Erwartet | Notieren |
| --- | --- | --- |
| View 1, Offset −1 | Nachbarbild ist **auch schwarzweiß** | schwarzweiß oder farbig? |
| View 2, Offset 0 | komplett schwarz | schwarz? |

Danach dasselbe mit einer **Bewegung/Skalierung** davor statt der Farbe (Premiere: Bewegung →
Skalierung 150 %; Resolve: Zoom 1,5 im Inspector). Ist das Nachbarbild auch vergrößert?

**Wenn das Nachbarbild farbig oder unvergrößert ist**, bekommt der Effekt die Nachbarn *roh* und
das aktuelle Bild *bearbeitet*. Das ist das wichtigste mögliche Ergebnis, bitte unbedingt melden.

### Test C — Schnitte und Clipanfang
1. Zwei **verschiedene** Clips direkt hintereinander, darüber eine **Einstellungsebene**
   (Resolve: Adjustment Clip) mit dem Probe.
   Playhead auf das **erste Bild nach dem Schnitt**, View 1, Offset −1: Zeigt es das letzte Bild
   des ersten Clips oder etwas anderes?
2. Probe direkt auf einen Clip, Playhead auf dessen **erstes Bild**, View 1, Offset −1:
   Was erscheint? Ein Fehler, schwarz, dasselbe Bild oder etwas vor dem Clipanfang?

### Test D — Messknopf und Speichern
1. View 0, Knopf **Measure** drücken. Steht danach eine Zahl in *Saved measured RGB mean*?
2. Projekt speichern, schließen, wieder öffnen. Ist die Zahl noch da?
3. Rückgängig (Strg/Cmd+Z) direkt nach dem Messen: Geht die Zahl zurück?

### Test E — Export
Probe auf den Testclip, **View 1, Offset −1**. Einen kurzen Bereich exportieren (Premiere: über
die Exportfunktion; Resolve: Deliver-Seite). Im exportierten Video bei gleicher Position die
Nummer mit der Timeline vergleichen. Ist sie gleich (n−1)?

## 6. Ergebnis schicken

Für jedes Programm und jeden Probe bitte kurz:

- Programm, Version, Mac/Windows, und in Premiere den Renderer: Datei → Projekteinstellungen →
  Allgemein, zum Beispiel "Metal" oder "CUDA".
- Test A–E: was du gesehen hast, Zahlen notieren. Screenshots helfen sehr.
- Alles, was abstürzt oder komisch aussieht.

"Weiß nicht" oder "nicht getestet" ist eine völlig gute Antwort. Bitte nichts schätzen.

## 7. Aufräumen

Programm beenden und die Probe-Dateien wieder löschen: `TemporalProbe…` im Premiere-Ordner und
den Ordner `VVTemporalProbe.ofx.bundle` im OFX-Ordner (Pfade siehe Punkt 2). Projekte, in
denen ein Probe steckt, zeigen danach eine Meldung über einen fehlenden Effekt. Deshalb nur das
Wegwerf-Projekt verwenden.

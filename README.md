# Glizzarine Harmonizer

Live-Harmonizer: Du singst ins Mikro und spielst auf dem Keyboard Akkorde. Jede gedrückte Taste wird zu einer eigenen Stimme aus deiner Stimme, genau auf der Tonhöhe der Taste (wie bei Jacob Collier).

## Bauen (GitHub Actions)

1. Neues GitHub-Repository anlegen, z. B. `GlizzarineHarmonizer`.
2. Alle Dateien hochladen. `build.yml` muss in `.github/workflows/build.yml` liegen.
3. Unter **Actions** läuft der Build automatisch. Am Ende bei **Artifacts** `GlizzarineHarmonizer-Windows` herunterladen.
4. Darin sind zwei Sachen:
   - `VST3/Glizzarine Harmonizer.vst3`: nach `C:\Program Files\Common Files\VST3\` kopieren
   - `Standalone/Glizzarine Harmonizer.exe`: läuft ohne DAW

## Schnellster Test: Standalone

1. `Glizzarine Harmonizer.exe` starten, oben auf **Options → Audio/MIDI Settings**.
2. Audio-Gerät: dein Interface (am besten **ASIO**), Eingang = Mikro-Kanal, Ausgang = Kopfhörer.
3. Unter **Active MIDI inputs** die KeyLab anhaken.
4. Singen und Akkorde spielen.

## In Cakewalk Sonar

1. **Treiber:** Edit → Preferences → Audio → Playback and Recording → Driver Mode **ASIO**. Notfalls WASAPI Exclusive, nicht Shared (zu viel Verzögerung). Puffer klein (64–128 Samples).
2. **Gesangsspur:** Audiospur anlegen, Eingang = Mikro, **Input Echo** (Monitoring) an.
3. In die FX-Box dieser Spur **Glizzarine Harmonizer** einfügen.
4. **Keyboard-Spur:** MIDI-Spur anlegen, Eingang = KeyLab.
5. Den **Ausgang** der MIDI-Spur auf **Glizzarine Harmonizer** stellen. MIDI-fähige Effekte erscheinen dort in der Ausgangs-Liste.
6. Auch bei der MIDI-Spur **Input Echo** an, dann singen und spielen.

## Regler

**VOICE**
- **Input:** Eingangsverstärkung. Die Anzeige links soll beim Singen gut ausschlagen, aber nicht rot werden.
- **Voice:** Lautstärke deiner Originalstimme.
- **Harmonies:** Lautstärke der erzeugten Stimmen.
- **Formant:** Stimmcharakter der Harmonien, links dunkler/männlicher, rechts heller.
- **Consonants:** wie viel S, T und Atem in die Harmonien geht.
- **Velocity:** wie stark der Anschlag die Lautstärke der Stimmen beeinflusst.

**PLAY**
- **Attack:** wie schnell eine Stimme einsetzt. Hoch heißt weicher Swell.
- **Release:** wie lange sie nach Loslassen von Taste oder Pedal ausklingt.
- **Spread:** Stereobreite der Stimmen.
- **Lowest note:** tiefster Ton, den du singst. Höher eingestellt heißt weniger Verzögerung.

**SPACE**
- **Chorus**, **Delay** (Ping-Pong, **Time**, **Feedback**), **Reverb** (**Size**), **Output**

Sustain-Pedal (CC64) funktioniert wie beim Klavier.

**INPUT**-Anzeige: Eingangspegel, ob gerade Gesang erkannt wird, erkannter Ton, gehaltene Tasten und die Verzögerung.

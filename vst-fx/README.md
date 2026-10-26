# Oneiroi FX

**Insert-Effekt** · Befaco Oneiroi mit Audioeingang: dieselbe Engine wie das Instrument (`../engine`), gebaut mit `-DONEIROI_INPUT` · Hersteller in der Force: Befaco · Lizenz: GPL-3.0-or-later

Das Audio des Tracks ist Oneirois Eingang. IN VOL ist der Eingangsfader des Moduls, LOOP SOURCE der Schalter IN / RESAMPLE (was RECORD aufnimmt). Der Eingang wird über einen kurzen Puffer von 44,1 auf 48 kHz gebracht (etwa 1,5 ms zusätzlich).

## Noten

Die Force schickt einem Insert-Effekt keine Noten (auf einer Force ausprobiert). Oneiroi FX öffnet deshalb einen eigenen MIDI-Eingang, den die Force als MIDI-Ausgang **Oneiroi FX MIDI In** anzeigt (eine zweite Instanz: „Oneiroi FX 2 MIDI In"). Ein MIDI-Track mit diesem Ausgang spielt den Effekt: Noten, Pitch Bend und Controller. PLAY = GATE öffnet die Oszillatoren bei gehaltener Note, PLAY = DRONE lässt sie dauernd laufen. Die Presets außer „Init" stehen auf DRONE.

Alles Weitere wie beim Instrument: `../vst/README.md`. Die Engine liegt in `../engine`, dieser Ordner enthält nur `vst.json`, Parameter, Bildschirm und Presets.

## Stand

Auf der Force getestet (Version aus dem ZIP vom 08.10.2026): läuft als Insert, Eingang mit Echo und Hall, Looper nimmt vom Eingang auf, und eine MIDI-Spur mit Ausgang „Oneiroi FX MIDI In“ spielt den Effekt.

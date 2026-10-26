# mpc-vst-oneiroi

Befaco **Oneiroi** (Ambient-/Drone-Modul: Sinus, Supersaw/Wavetable, Looper, Filter, Resonator, Echo, Hall, Modulator) für die **Akai Force** und als **Oneiroi Pocket** im Handy-Browser. Der Klang ist der Original-Code des Hardware-Moduls (OWL-Patch 1.2.2 von Roberto Noris) mit den nötigen Teilen der OWL-Bibliothek. Lizenz: GPL-3.0-or-later.

| Ordner | Was | In der Force |
|---|---|---|
| `vst/` | **[SYN] Oneiroi**: Instrument, gespielt von Pads, Tasten und MIDI-Clips | `/sdcard/vst/oneiroi.so` |
| `vst-fx/` | **[FX] Oneiroi FX**: Insert-Effekt mit Audioeingang und eigenem MIDI-Eingang „Oneiroi FX MIDI In“ | `/sdcard/vst/oneiroi_fx.so` |
| `engine/` | die gemeinsame Engine: `src/` (Befaco + OWL, Herkunft in `UPSTREAM`, Änderungen in `upstream-changes.diff`), `mpc/oneiroi_engine.cc` (ersetzt die Bedienlogik des Moduls) | – |
| `pocket/` | **Oneiroi Pocket**: dieselbe Engine als WebAssembly, eine Seite fürs Handy | – |

Beschreibung der Seiten, Parameter und Unterschiede zum Modul: `vst/README.md` und `vst-fx/README.md`.

## Bauen: GitHub Actions

Das Repo baut mit dem Framework [sd88me/mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) und dessen wiederverwendbarem Workflow `vst-release.yml`, auf einen festen Commit gepinnt (`28da1ce`, in beiden Workflows an je zwei Stellen).

- **Build** (`.github/workflows/build.yml`): bei jedem Push. Baut beide Plugins für ARMv7 (Force), fährt den Offline-Test und packt sie. Die ZIPs und Bildschirm-Vorschauen liegen danach unter *Actions → Lauf → Artifacts*. Kein Release.
- **Release** (`.github/workflows/release.yml`): von Hand unter *Actions → Release → Run workflow*, Version eintragen. Legt je Plugin einen **Entwurf** unter *Releases* an. ZIP auf der Force installieren und testen, dann den Entwurf veröffentlichen.

Die ZIPs installiert der [MPC-Plugin-Installer](https://github.com/sd88me/mpc-vst-plugins/releases/latest) oder das `install.sh` darin (Anleitung in `INSTALL.md` im ZIP). Oneiroi FX hat denselben Namen und dieselbe UID wie die Testfassung aus dem ZIP vom 08.10.2026 und ersetzt sie.

## Bauen: lokal

```
git clone https://github.com/sd88me/mpc-vst-plugins
export MPC_VST=$PWD/mpc-vst-plugins
"$MPC_VST/tools/test_port.sh" vst/vst.json        # Offline-Test (x86, ASan/UBSan)
"$MPC_VST/tools/build_port.sh" vst/vst.json        # ARM-Build, braucht Docker
```

Für `vst-fx` dasselbe mit `vst-fx/vst.json`. Ergebnis in `vst/build/` bzw. `vst-fx/build/`.

## Oneiroi Pocket

`pocket/oneiroi-pocket.html` ist die fertige Seite (Engine als Base64 eingebettet). Neu bauen nach Änderungen an Engine, Seite oder Presets:

```
WASI_SDK=/pfad/zu/wasi-sdk-25.0-x86_64-linux MPC_VST=/pfad/zu/mpc-vst-plugins pocket/build.sh
```

[wasi-sdk 25](https://github.com/WebAssembly/wasi-sdk/releases) liefert den Compiler. `pocket/pocket.cc` bindet `engine/mpc/oneiroi_engine.cc` direkt ein (mit `ONEIROI_INPUT`, ohne MIDI-Port); die Seite rechnet wie die Force mit 44,1 kHz.

Ein Sound aus Pocket kommt so auf die Force: in Pocket unter **SOUNDS → Für die Force** den Eintrag kopieren, in `vst/presets.src.json` einfügen, `python3 vst/make_presets.py`, pushen. Nach dem Actions-Build steht er im PRESET-Menü.

## Herkunft

Der Force-Port entstand am 08.10.2026 im Checkout von saustin2010/vst_instruments (`vcv-rack/oneiroi`, `vcv-rack/oneiroifx`). Seit 26.10.2026 ist er hier eigenständig: Die Engine ist an den aktuellen Wrapper-Vertrag von mpc-vst-plugins angepasst (`process()` für den Effekt, `song_pos()` für SYNC), die Variante „Oneiroi IN“ ist entfallen.

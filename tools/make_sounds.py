#!/usr/bin/env python3
"""Cut the sound effects and the opening music from recordings on Wikimedia Commons
(see assets/SOUND-SOURCES.md for each one's author and licence):

  - the opener's music: "Mr. Rooster March and Two Step", Arthur Pryor's Band, 1910
    (public domain), given the same old-record sound as the rest of the music;
  - ducks: a pond of Pekin ducks and mallards quacking, and a mallard's
    "QUACK quack quack quack", cut into single quacks (CC BY-SA);
  - the horn: a bread seller's rubber bulb horn from Orizaba, Veracruz, cut into
    single honks (CC BY-SA).

    tools/make_sounds.py SOUNDS_DIR

Writes assets/art/m_opener.ogg and s_*.ogg. Needs ffmpeg.
"""
import os, subprocess, sys

OUT = "assets/art"
OLD_RECORD = ("highpass=f=140,lowpass=f=4200,equalizer=f=1800:t=q:w=1:g=3,"
              "acompressor=threshold=-22dB:ratio=3:attack=10:release=200,loudnorm=I=-18:TP=-2:LRA=9")
# name: recording, start, end (seconds), and how it is evened out
CUTS = {
    "s_pond": ("ducks-pekin-and-mallard.ogg", 64.0, 80.5, "highpass=f=250"),
    "s_quacks": ("ducks-mallard-female.mp3", 6.9, 8.85, "highpass=f=200"),
    "s_quack1": ("ducks-mallard-female.mp3", 6.96, 7.30, "highpass=f=200"),
    "s_quack2": ("ducks-mallard-female.mp3", 7.32, 7.64, "highpass=f=200"),
    "s_quack3": ("ducks-mallard-female.mp3", 7.63, 7.92, "highpass=f=200"),
    "s_quack4": ("ducks-pekin-and-mallard.ogg", 65.0, 65.42, "highpass=f=300"),
    "s_quack5": ("ducks-pekin-and-mallard.ogg", 79.15, 79.80, "highpass=f=300"),
    "s_honk1": ("horn-bocina-del-panadero.ogg", 0.42, 0.68, "highpass=f=150"),
    "s_honk2": ("horn-bocina-del-panadero.ogg", 2.04, 2.32, "highpass=f=150"),
    "s_honk3": ("horn-bocina-del-panadero.ogg", 3.56, 4.30, "highpass=f=150"),   # a double honk
}

def main(src):
    os.makedirs(OUT, exist_ok=True)
    for name, (f, a, b, flt) in CUTS.items():
        d = b - a
        fade = min(0.015, d / 4)
        chain = (f"atrim={a}:{b},asetpts=PTS-STARTPTS,aformat=channel_layouts=mono,aresample=22050,{flt},"
                 f"afade=t=in:d={fade},afade=t=out:st={d - fade * 2}:d={fade * 2},loudnorm=I=-16:TP=-1.5")
        if name == "s_pond":
            chain += f",afade=t=in:d=1.0,afade=t=out:st={d - 1.5}:d=1.5"
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", os.path.join(src, f), "-af", chain,
                        "-ac", "1", "-ar", "22050", "-c:a", "libvorbis", "-q:a", "4", f"{OUT}/{name}.ogg"], check=True)
        print(f"{name}.ogg: {d:.2f} s")
    # the opener's music, with the old-record sound
    out = f"{OUT}/m_opener.ogg"
    flt = (f"[0:a]atrim=0.5:25,asetpts=PTS-STARTPTS,aformat=channel_layouts=mono,aresample=44100,{OLD_RECORD},"
           "afade=t=in:d=0.2,afade=t=out:st=23:d=1.5[m];"
           "[1:a]highpass=f=1500,lowpass=f=6000[n];[m][n]amix=inputs=2:duration=first:weights=1 1:normalize=0,aresample=22050[o]")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", os.path.join(src, "opener-mr-rooster-1910.mp3"),
                    "-f", "lavfi", "-i", "anoisesrc=color=pink:amplitude=0.004:r=44100",
                    "-filter_complex", flt, "-map", "[o]", "-ac", "1", "-c:a", "libvorbis", "-q:a", "2", out], check=True)
    print(out)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])

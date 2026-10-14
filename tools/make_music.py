#!/usr/bin/env python3
"""Cut the music for each scene from the public-domain recordings (Wikimedia
Commons, all made in the United States before 1926), and give them all the same
sound: the treble narrowed and a faint hiss, like a 78, at the same loudness.

    tools/make_music.py MUSIC_DIR

MUSIC_DIR holds the recordings under the names below (see its SOURCES.md). Writes
assets/art/m_*.ogg. The lemonade stand and the mirror have no music, as in the film.
Needs ffmpeg.
"""
import os, subprocess, sys

OUT = "assets/art"
# scene: the recording, and where to cut it (seconds), past any spoken announcement
CUTS = {
    "arrival": ("arrival-the-thunderer-1896.ogg", 15.0, 52.0),        # The Thunderer, US Marine Band, 1896
    "sidecar": ("sidecar-that-flying-rag-1911.ogg", 1.0, 25.0),       # That Flying Rag, Arthur Pryor's Band, 1911
    "radio": ("war-stars-and-stripes-1909.ogg", 1.0, 16.0),           # The Stars and Stripes Forever, Sousa's Band, 1909
    "ride": ("ride-william-tell-edison.ogg", 129.0, 160.0),           # William Tell overture, the galop, Edison
    "war": ("war-johnny-marching-home-1898.oga", 6.0, 40.0),          # When Johnny Comes Marching Home, 1898
    "finale": ("finale-hearts-and-flowers-victor.ogg", 2.0, 24.0),   # Hearts and Flowers, Victor Orchestra
}
OLD_RECORD = ("highpass=f=140,lowpass=f=4200,equalizer=f=1800:t=q:w=1:g=3,"
              "acompressor=threshold=-22dB:ratio=3:attack=10:release=200,loudnorm=I=-18:TP=-2:LRA=9")

def main(src):
    os.makedirs(OUT, exist_ok=True)
    for scene, (name, a, b) in CUTS.items():
        out = f"{OUT}/m_{scene}.ogg"
        d = b - a
        flt = (f"[0:a]atrim={a}:{b},asetpts=PTS-STARTPTS,aformat=channel_layouts=mono,aresample=44100,{OLD_RECORD},"
               f"afade=t=in:d=0.3,afade=t=out:st={d - 1.5}:d=1.5[m];"
               "[1:a]highpass=f=1500,lowpass=f=6000[n];[m][n]amix=inputs=2:duration=first:weights=1 1:normalize=0,aresample=22050[o]")
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", os.path.join(src, name),
                        "-f", "lavfi", "-i", "anoisesrc=color=pink:amplitude=0.004:r=44100",
                        "-filter_complex", flt, "-map", "[o]", "-ac", "1", "-c:a", "libvorbis", "-q:a", "2", out], check=True)
        print(f"{out}: {d:.0f} s, {os.path.getsize(out) // 1024} KB")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])

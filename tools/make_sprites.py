#!/usr/bin/env python3
"""Turn the Gemini drawings in assets/src into the pictures the screensaver builds
in, under assets/art.

    tools/make_sprites.py [MIRROR_SCENE_DIR]

The sheets (02, 03, 05-08, 10, 12) are 4 x 4 grids on flat magenta. Each figure is
cut from its own grid cell: the cell's main drawing and the bits near it (smoke,
flames, motion marks) are kept, and anything that strays in from a neighbouring
cell, or floats off on its own, is left out. Every figure is written to one
layout: its feet 10 above the bottom edge and the middle of its body in the middle
across, at a scale set per sheet so that the characters stand at their right
heights beside each other. The backgrounds (01, 04, 09, 11, 13) are scaled to 2560
wide.

The Mirror Scene's pictures are taken as they are from MIRROR_SCENE_DIR/assets/art
(default ../mirror-scene), with mr_ in front of their names.
Needs ImageMagick and numpy.
"""
import os, shutil, subprocess, sys
import numpy as np

SRC, OUT = "assets/src", "assets/art"

# each sheet: its frames' names in reading order, which frame sets the scale, and
# how high that frame stands once written
SHEETS = {
    "02-firefly": (["f_w1", "f_w2", "f_w3", "f_w4", "f_pole", "f_land", "f_stand", "f_bow",
                    "f_ash", "f_lean", "f_brows", "f_point", "f_sit", "f_situp", "f_fist", "f_huff"], 6, 480),
    "03-guards": (["gd_stand", "gd_raise", "gd_blow", "gd_lower", "bl_sword", "bl_curtsy", "bl_toes", "bl_bow",
                   "gd_arch", "gd_salute", "gd_present", "gd_ease", "dr_stand", "dr_hit", "pg_stand", "pg_wave"], 0, 470),
    "05-vendor": (["v_wipe", "v_glare", "v_arms", "v_tap", "v_grab", "v_twohats", "v_slam", "v_rage",
                   "v_leg", "v_fist", "v_chase", "v_trip", "v_fire1", "v_fire2", "v_wet", "v_burn"], 1, 440),
    "06-chico": (["c_w1", "c_w2", "c_w3", "c_w4", "c_hands", "c_reach", "c_twohats", "c_hat_on",
                  "c_shrug", "c_thumb", "c_laugh", "c_whisper", "c_ride", "c_lean", "c_bye", "c_tip"], 4, 420),
    "07-harpo-street": (["h_w1", "h_w2", "h_w3", "h_w4", "h_honk", "h_leg", "h_foot", "h_wade",
                         "h_snip", "h_strike", "h_match", "h_tip", "h_ride", "h_bye", "h_smitten", "h_horn"], 4, 450),
    "08-props": (["p_boater", "p_tophat", "p_tyrol", "p_bulb", "p_scissors", "p_match", "p_hatfire", "p_tub",
                  "p_moto", "p_motoside", "p_sidecar", "p_puff", "p_radio", "p_radio2", "p_tomato", "p_splat"], 9, 300),
    "10-revere": (["r_g1", "r_g2", "r_g3", "r_g4", "r_g5", "r_g6", "r_g7", "r_g8",
                   "r_stop", "r_look", "r_kiss", "r_off", "hs_stand", "hs_eat", "hs_bored", "hs_trot"], 12, 470),
    "12-uniforms": (["u_union", "u_confed", "u_redcoat", "u_scout", "u_crockett", "u_napoleon", "u_ww1", "u_general",
                     "u_r1", "u_r2", "u_r3", "u_r4", "u_binoc", "u_duck", "u_salute", "u_grin"], 0, 480),
    "17-bed": (["b_sleep", "b_in", "b_out", "b_snore", "b_sit", "b_pat", "b_yawn", "b_pull",
                "b_pullback", "b_tug", "b_honk", "b_offended", "b_kiss", "b_cuddle", "b_alone", "b_empty"], 0, 480),
    "15-ducks": (["d_b1", "d_b2", "d_b3", "d_b4", "d_quack", "d_back", "d_up", "d_sweat",
                  "d_dive", "d_tail", "d_pop", "d_shake", "d_big", "d_little", "d_boater", "d_tophat"], 0, 300),
}
# frames with a loose motion mark (a waving hand) in the top left corner, to take off
LOOSE_TOP_LEFT = {"gd_arch", "h_snip", "r_stop"}
# and where one touches the drawing, the corner to clear (fractions of width, height)
CLEAR = {"gd_arch": (0.27, 0.2)}
# bits Gemini drew twice, to take out of a frame (left, top, right, bottom, as parts
# of the written frame): Groucho flicking his ash has a cigar in his mouth as well
ERASE = {"f_ash": [(0.64, 0.215, 0.75, 0.30)]}
# and where the rest of it shows on his face, painted over in the colour of his chin
SKIN = {"f_ash": ((0.56, 0.235, 0.64, 0.295), (0.55, 0.315))}
BACKGROUNDS = ["14-pot", "01-reception-hall", "04-street", "09-garden", "11-street-night", "13-battlefield"]

def load(path):
    w, h = map(int, subprocess.check_output(["magick", "identify", "-format", "%w %h", path]).split())
    raw = subprocess.check_output(["magick", path, "-depth", "8", "RGB:-"])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3).astype(np.float32)

def key(img):
    """Magenta background to alpha, and take its colour cast off the edges."""
    r, g, b = img[..., 0].copy(), img[..., 1].copy(), img[..., 2].copy()
    spill = np.minimum(r, b) - g
    cut = np.clip(spill, 0, None) * 0.92
    r, b = r - cut, b - cut
    alpha = np.clip(1.0 - (spill - 24) / 70.0, 0, 1)
    return np.dstack([r, g, b, alpha * 255]).clip(0, 255).astype(np.uint8)

def label(m):
    """Connected shapes in a small boolean grid (4-neighbour)."""
    lab = np.zeros(m.shape, np.int32)
    n = 0
    for y0, x0 in zip(*np.nonzero(m)):
        if lab[y0, x0]: continue
        n += 1
        stack = [(y0, x0)]
        lab[y0, x0] = n
        while stack:
            y, x = stack.pop()
            for yy, xx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if 0 <= yy < m.shape[0] and 0 <= xx < m.shape[1] and m[yy, xx] and not lab[yy, xx]:
                    lab[yy, xx] = n; stack.append((yy, xx))
    return lab, n

def cells(a):
    """The sixteen drawings of a 4 x 4 sheet, in reading order, one per grid cell."""
    C = 4                                              # work on a grid of 4 x 4 pixel blocks
    H, W = a.shape[:2]
    solid = a[..., 3] > 128
    h, w = H // C, W // C
    grid = solid[:h * C, :w * C].reshape(h, C, w, C).any(axis=(1, 3))
    grown = grid.copy()                                # join a drawing's own loose bits
    for dy in (-2, -1, 1, 2): grown |= np.roll(grid, dy, axis=0)
    for dx in (-2, -1, 1, 2): grown |= np.roll(grown, dx, axis=1)
    lab, n = label(grown)
    cell = (np.arange(h)[:, None] * 4 // h) * 4 + (np.arange(w)[None, :] * 4 // w)
    # a shape lying mostly in one cell belongs to it (a hand poking over the line comes
    # too); two drawings that touch across a line are split along it
    owner = np.full((h, w), -1)
    for i in range(1, n + 1):
        m = lab == i
        counts = np.bincount(cell[m], minlength=16)
        first, second = np.sort(counts)[::-1][:2]
        owner[m] = cell[m] if second > 0.25 * first else np.argmax(counts)
    out = []
    for c in range(16):
        pl, pn = label((owner == c) & (lab > 0))
        area = np.bincount(pl.ravel(), minlength=pn + 1)
        main = int(np.argmax(area[1:])) + 1
        yy, xx = np.nonzero(pl == main)
        y0, y1, x0, x1 = yy.min(), yy.max(), xx.min(), xx.max()
        near = 0.012 * w                                # loose bits this close to the drawing are its own
        keep = []
        for i in range(1, pn + 1):
            yy, xx = np.nonzero(pl == i)
            if i == main or area[i] > 0.25 * area[main] or (
                    max(yy.min() - y1, y0 - yy.max(), 0) < near and max(xx.min() - x1, x0 - xx.max(), 0) < near):
                keep.append(i)
        full = np.repeat(np.repeat(np.isin(pl, keep), C, axis=0), C, axis=1)
        full = np.pad(full, ((0, H - full.shape[0]), (0, W - full.shape[1])))
        piece = a.copy()
        piece[..., 3] = np.where(full, piece[..., 3], 0)
        yy, xx = np.nonzero(piece[..., 3] > 16)
        out.append(piece[yy.min():yy.max() + 1, xx.min():xx.max() + 1])
    return out

def save(rgba, w, h, path):
    subprocess.run(["magick", "-size", f"{rgba.shape[1]}x{rgba.shape[0]}", "-depth", "8", "RGBA:-",
                    "-filter", "Lanczos", "-resize", f"{w}x{h}!", "-define", "png:compression-level=9",
                    "-define", "png:exclude-chunk=date,time", path],
                   input=np.ascontiguousarray(rgba).tobytes(), check=True)

def drop_top_left(p):
    """Take off small shapes standing on their own in the top left quarter."""
    lab, n = label(p[..., 3] > 128)
    area = np.bincount(lab.ravel(), minlength=n + 1)
    H, W = lab.shape
    p = p.copy()
    for i in range(1, n + 1):
        yy, xx = np.nonzero(lab == i)
        if area[i] < 0.05 * area[1:].sum() and yy.max() < H * 0.35 and xx.max() < W * 0.4:
            y0, y1, x0, x1 = max(yy.min() - 6, 0), yy.max() + 7, max(xx.min() - 6, 0), xx.max() + 7
            p[y0:y1, x0:x1, 3] = np.where(lab[y0:y1, x0:x1] == i, 0, p[y0:y1, x0:x1, 3]) * (lab[y0:y1, x0:x1] != 0) + \
                                 p[y0:y1, x0:x1, 3] * (lab[y0:y1, x0:x1] == 0) * (np.abs(np.arange(y0, y1)[:, None] - yy.mean()) > (yy.max() - yy.min()) / 2 + 6)
    yy, xx = np.nonzero(p[..., 3] > 16)
    return p[yy.min():yy.max() + 1, xx.min():xx.max() + 1]

CENTRE_BY_BOX = {"17-bed"}                    # the same bed in every frame: keep it still
def frame(p, k, path, by_box=False):
    """One figure at scale k, on a canvas with its feet at the bottom and its body in
    the middle across."""
    solid = p[..., 3] > 128
    hgt = p.shape[0]
    band = solid[int(hgt * 0.35):int(hgt * 0.6)]
    mid = p.shape[1] / 2 if by_box or not band.any() else np.nonzero(band)[1].mean()
    half = int(max(mid, p.shape[1] - mid)) + 2
    canvas = np.zeros((hgt, 2 * half, 4), np.uint8)
    x0 = int(half - mid)
    canvas[:, x0:x0 + p.shape[1]] = p
    canvas = np.pad(canvas, ((0, int(round(10 / k))), (0, 0), (0, 0)))
    w, h = max(1, round(canvas.shape[1] * k)), max(1, round(canvas.shape[0] * k))
    save(canvas, w, h, path)
    return w, h

def main(mirror):
    os.makedirs(OUT, exist_ok=True)
    for sheet, (names, ref, tall) in SHEETS.items():
        d = cells(key(load(f"{SRC}/{sheet}.jpeg")))
        k = tall / d[ref].shape[0]
        for i, n in enumerate(names):
            if n in CLEAR:
                d[i] = d[i].copy()
                d[i][:int(d[i].shape[0] * CLEAR[n][1]), :int(d[i].shape[1] * CLEAR[n][0]), 3] = 0
        d = [drop_top_left(p) if n in LOOSE_TOP_LEFT else p for n, p in zip(names, d)]
        sizes = [frame(p, k, f"{OUT}/{n}.png", sheet in CENTRE_BY_BOX) for n, p in zip(names, d)]
        for n in names:
            if n in ERASE:
                from PIL import Image
                path = f"{OUT}/{n}.png"
                im = np.array(Image.open(path).convert("RGBA"))
                h, w = im.shape[:2]
                for x0, y0, x1, y1 in ERASE[n]:
                    im[round(y0 * h):round(y1 * h), round(x0 * w):round(x1 * w), 3] = 0
                if n in SKIN:
                    (x0, y0, x1, y1), (sx, sy) = SKIN[n]
                    part = im[round(y0 * h):round(y1 * h), round(x0 * w):round(x1 * w)]
                    r, g, b = (part[..., i].astype(int) for i in range(3))
                    tobacco = (r - b > 45) & (r < 200) & (g < 150)       # the cigar's brown, not his skin or moustache
                    part[tobacco, :3] = im[round(sy * h), round(sx * w), :3]
                save(im, w, h, path)
        print(f"{sheet}: " + " ".join(f"{n} {w}x{h}" for n, (w, h) in zip(names, sizes)))
    for bg in BACKGROUNDS:
        subprocess.run(["magick", f"{SRC}/{bg}.jpeg", "-resize", "2560x", "-quality", "88", f"{OUT}/{bg[3:]}.jpg"], check=True)
        print(f"{bg[3:]}.jpg")
    # the bedroom, cut to its wall and moonlit window: the bed in it stands at an angle,
    # so Harpo's bed, seen straight on from the sheet, is put in front of the wall instead
    subprocess.run(["magick", f"{SRC}/16-bedroom.jpeg", "-gravity", "northeast", "-crop", "45%x62%+0+0", "+repage",
                    "-resize", "2560x", "-quality", "88", f"{OUT}/bedroom.jpg"], check=True)
    print("bedroom.jpg")
    # the Mirror Scene, as it is
    src = os.path.join(mirror, "assets/art")
    for f in sorted(os.listdir(src)):
        shutil.copyfile(os.path.join(src, f), f"{OUT}/mr_{f}")
    print(f"mr_*: {len(os.listdir(src))} pictures from {src}")
    write_list()

def write_list():
    """src/art.h: every picture in assets/art, for the program to build in."""
    files = sorted(f for f in os.listdir(OUT) if f.endswith((".png", ".jpg")))
    items = [f'X({os.path.splitext(f)[0].replace("-", "_")}, "{f}")' for f in files]
    lines = ["/* Made by tools/make_sprites.py: every picture in assets/art. */", "#define SPRITES \\"]
    for i in range(0, len(items), 4):
        lines.append("    " + " ".join(items[i:i + 4]) + (" \\" if i + 4 < len(items) else ""))
    open("src/art.h", "w").write("\n".join(lines) + "\n")
    print(f"src/art.h: {len(files)} pictures")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "../mirror-scene")

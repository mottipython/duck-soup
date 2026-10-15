#!/usr/bin/env python3
"""Margaret Dumont, as a cut-out from a frame of Animal Crackers (1930, US public
domain): the two pieces made for the Captain Spaulding screensaver (assets/src/
dumont-top.png and dumont-body.png, cut at the mouth with the head on a hinge),
put together and held up on a wooden stick that reaches the floor, so that she can
stand in the cartoon hall and wait. Three pictures, to the suite's scale (a 480-high
figure), feet (the foot of the stick) at the middle of the bottom edge:

  dm_wait   waiting, mouth shut
  dm_huff   the top of her head flapped open on its hinge
  dm_swoon  leaning back on her stick

    tools/make_dumont.py      (then tools/make_sprites.py's list, or make)
"""
from PIL import Image, ImageDraw

OUT = "assets/art"
TOP_HINGE, BODY_HINGE = (10, 137), (71, 23)       # from captain-spaulding's src/cast.h
STICK = 700                                       # the stick, below her waist, in the cut-out's pixels
HEIGHT = 490                                      # the whole of her, stick and all, at the suite's scale
INK, WOOD, WOOD2 = (30, 24, 20, 255), (150, 108, 62, 255), (110, 76, 42, 255)

def figure(open_deg):
    top, body = Image.open("assets/src/dumont-top.png"), Image.open("assets/src/dumont-body.png")
    ox, oy = BODY_HINGE[0] - TOP_HINGE[0], BODY_HINGE[1] - TOP_HINGE[1]
    pad = 80                                      # room for the head to open
    W, H = body.width + 2 * pad, body.height - oy + pad + STICK
    im = Image.new("RGBA", (W, H))
    by = -oy + pad
    # the stick, from inside her waist down to the floor
    d = ImageDraw.Draw(im)
    sx = pad + body.width // 2
    d.line([(sx, by + body.height - 60), (sx, H - 1)], fill=INK, width=22)
    d.line([(sx, by + body.height - 60), (sx, H - 1)], fill=WOOD, width=14)
    d.line([(sx + 3, by + body.height - 60), (sx + 3, H - 1)], fill=WOOD2, width=4)
    im.alpha_composite(body, (pad, by))
    t = top
    if open_deg:                                  # turn the top on its hinge
        big = Image.new("RGBA", (top.width * 3, top.height * 3))
        cx, cy = top.width, top.height
        big.alpha_composite(top, (cx, cy))
        big = big.rotate(open_deg, center=(cx + TOP_HINGE[0], cy + TOP_HINGE[1]), resample=Image.BICUBIC)
        im.alpha_composite(big, (pad + ox - cx, by + oy - cy))
    else:
        im.alpha_composite(t, (pad + ox, by + oy))
    return im, sx

def finish(im, foot_x, name, lean=0):
    if lean:                                      # lean on the stick, turning about its foot
        W, H = im.size
        big = Image.new("RGBA", (W * 3, H * 2))
        big.alpha_composite(im, (W + (W // 2 - foot_x), H))
        big = big.rotate(lean, center=(W + W // 2, 2 * H - 1), resample=Image.BICUBIC)
        im, foot_x = big, W + W // 2
    box = im.getbbox()
    half = max(foot_x - box[0], box[2] - foot_x)
    im = im.crop((foot_x - half, box[1], foot_x + half, im.height))
    s = HEIGHT / im.height
    im = im.resize((max(1, round(im.width * s)), HEIGHT), Image.LANCZOS)
    im.save(f"{OUT}/{name}.png", optimize=True)
    print(f"{name}.png {im.width}x{im.height}")

def main():
    im, fx = figure(0); finish(im, fx, "dm_wait")
    im, fx = figure(24); finish(im, fx, "dm_huff")
    im, fx = figure(0); finish(im, fx, "dm_swoon", lean=12)

if __name__ == "__main__":
    main()

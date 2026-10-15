"""A brass bulb horn, made: a reed's buzz (a sharp pulse train with a rough
half-pitch under it) through the resonances of a horn's bell, with the squeeze's
quick rise in pitch at the start."""
import sys, wave, numpy as np
R = 22050

def biquad_bp(x, f, q):
    w = 2 * np.pi * f / R; al = np.sin(w) / (2 * q)
    b0, b1, b2, a0, a1, a2 = al, 0, -al, 1 + al, -2 * np.cos(w), 1 - al
    y = np.zeros_like(x); x1 = x2 = y1 = y2 = 0.0
    for i, xi in enumerate(x):
        yi = (b0 * xi + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0
        x2, x1, y2, y1 = x1, xi, y1, yi; y[i] = yi
    return y

def honk(f0=200, dur=0.36, seed=1):
    rng = np.random.default_rng(seed)
    n = int(R * dur); t = np.arange(n) / R
    f = f0 * (0.78 + 0.22 * (1 - np.exp(-t / 0.025))) * (1 - 0.05 * t / dur)   # squeezed: up, then sagging
    f *= 1 + 0.006 * rng.standard_normal(n).cumsum() / np.sqrt(np.arange(1, n + 1))
    ph = 2 * np.pi * np.cumsum(f) / R
    reed = np.tanh(4 * (np.sin(ph) + 0.55 * np.sin(2 * ph + 0.3) + 0.35 * np.sin(3 * ph + 0.9)))   # a buzzing, clipped reed
    reed += 0.35 * np.sin(ph / 2) * np.tanh(3 * np.sin(ph))                                       # the rasp: a half-pitch growl
    reed += 0.08 * rng.standard_normal(n)                                                         # air
    bell = biquad_bp(reed, 480, 2.2) * 1.0 + biquad_bp(reed, 1150, 3.0) * 0.8 + biquad_bp(reed, 2400, 4.0) * 0.35
    env = np.minimum(1, t / 0.012) * np.minimum(1, (dur - t) / 0.07) ** 1.5
    y = bell * env
    return y / np.abs(y).max() * 0.85

def write(path, y):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(R)
        w.writeframes((np.clip(y, -1, 1) * 32767).astype("<i2").tobytes())

if __name__ == "__main__":
    write(sys.argv[1], honk(float(sys.argv[2]) if len(sys.argv) > 2 else 200))

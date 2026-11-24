"""Rebuild the factory CV samples with pyopenjtalk-plus 0.4.1.post10 (Mei normal)."""
from io import BytesIO
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED
import wave

import numpy as np
import pyopenjtalk


def syllables():
    for row in ("あいうえお", "かきくけこ", "がぎぐげご", "さしすせそ", "ざじずぜぞ",
                "たちつてと", "だでど", "なにぬねの", "はひふへほ", "ばびぶべぼ",
                "ぱぴぷぺぽ", "まみむめも", "やゆよ", "らりるれろ", "わをん"):
        yield from row
    for head in "きぎしじちにひびぴみり":
        for tail in "ゃゅょ":
            yield head + tail
    yield from ("ふぁ", "ふぃ", "ふぇ", "ふぉ", "てぃ", "でぃ", "うぃ", "うぇ",
                "うぉ", "ゔ", "ゔぁ", "ゔぃ", "ゔぇ", "ゔぉ", "つぁ", "つぃ", "つぇ", "つぉ")


def sample(kana):
    # A lengthened nucleus gives the sampler a stable, voiced sustain region.
    audio, rate = pyopenjtalk.tts(kana + "ー", speed=.85, half_tone=5, use_vanilla=True)
    kernel = .47 * np.sinc(.47 * np.arange(-40, 41)) * np.kaiser(81, 8)
    audio = np.convolve(audio, kernel / kernel.sum(), mode="same")[::2]
    sr = rate // 2
    peak = np.max(np.abs(audio))
    if peak < 1:
        raise ValueError(kana)
    audible = np.flatnonzero(np.abs(audio) > peak * .004)
    start = max(0, audible[0] - sr // 200)
    audio = audio[start:min(audible[-1]+1, start+int(sr*.48))]
    out = BytesIO()
    with wave.open(out, "wb") as wav:
        wav.setparams((1, 2, sr, 0, "NONE", ""))
        wav.writeframes(np.round(audio * (21000 / peak)).astype("<i2").tobytes())
    return out.getvalue()


if __name__ == "__main__":
    with ZipFile(Path(__file__).with_name("mei-cv.zip"), "w") as bank:
        for kana in syllables():
            entry = ZipInfo(kana + ".wav")
            bank.writestr(entry, sample(kana), compress_type=ZIP_DEFLATED)

"""The codec screens' space backdrops (0.57.0) - the owner-approved mock-up look ("more space themed"): nebula,
dust lanes, a dense starfield with flares, a planet horizon with a pink neon rim. Normal and the red betrayal one.
Usage: python3 bake_codec_bg.py OUTDIR   (writes codec_space-pc.jpg, codec_alert-pc.jpg and, 0.58.0, the Miyoo's
854x480 -miyoo.jpg copies)"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image  # noqa: E402

import codec  # noqa: E402


def main(out):
    for name, kw in (('codec_space', {}), ('codec_alert', dict(tint=(22, 4, 10), neb=((200, 30, 50), (120, 20, 90)),
                                                               planet=(110, 40, 40), seed=11))):
        img = Image.new('RGBA', (1920, 1080))
        codec.background(img, **kw)
        img.convert('RGB').save(os.path.join(out, name + '-pc.jpg'), quality=88)
        Image.open(os.path.join(out, name + '-pc.jpg')).resize((854, 480), Image.LANCZOS).save(
            os.path.join(out, name + '-miyoo.jpg'), quality=88)
        print(name, os.path.getsize(os.path.join(out, name + '-pc.jpg')))


if __name__ == '__main__':
    main(sys.argv[1])

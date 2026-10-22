"""Create original, editable RGB565 emotion assets. Requires Pillow."""
from pathlib import Path
import json
import struct
import zipfile
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'main/assets/emotions'
OUTPUT = ROOT / 'output/emotions'
SCALE = 3
EMOTIONS = [
    ('joy', 'FELIZ'), ('sad', 'TRISTE'), ('angry', 'ENFADADO'),
    ('upset', 'MOLESTO'), ('serious', 'SERIO'), ('sorry', 'AVERGONZADO'),
    ('excited', 'EMOCIONADO'), ('inlove', 'ENAMORADO'), ('nervous', 'NERVIOSO'),
    ('thought', 'PENSATIVO'), ('bored', 'ABURRIDO'), ('surprise', 'SORPRENDIDO'),
    ('worried', 'PREOCUPADO'), ('scared', 'ASUSTADO'), ('crying', 'DEPRIMIDO'),
    ('proud', 'ORGULLOSO'),
]
BG = '#101b2b'
INK = '#242738'


def face(kind, label):
    im = Image.new('RGB', (240*SCALE, 240*SCALE), BG)
    d = ImageDraw.Draw(im)

    def box(b):
        return tuple(round(x*SCALE) for x in b)

    def ellipse(b, fill, outline=None, width=3):
        d.ellipse(box(b), fill=fill, outline=outline, width=width*SCALE)

    def line(points, fill=INK, width=4):
        points = [(round(x*SCALE), round(y*SCALE)) for x, y in points]
        d.line(points, fill=fill, width=width*SCALE, joint='curve')
        r = width*SCALE/2
        for x, y in (points[0], points[-1]):
            d.ellipse((int(x-r), int(y-r), int(x+r), int(y+r)), fill=fill)

    def curve(points, fill=INK, width=4):
        # Quadratic Bezier gives smooth, reproducible editable strokes.
        a, b, c = points
        line([((1-t)**2*a[0]+2*(1-t)*t*b[0]+t*t*c[0],
               (1-t)**2*a[1]+2*(1-t)*t*b[1]+t*t*c[1])
              for t in [i/40 for i in range(41)]], fill, width)

    def polygon(points, fill, outline=None, width=3):
        p = [(int(x*SCALE), int(y*SCALE)) for x, y in points]
        d.polygon(p, fill=fill)
        if outline:
            line(points+[points[0]], outline, width)

    def eye(x, y=88, size=6):
        ellipse((x-size, y-size, x+size, y+size), INK)
        ellipse((x-2, y-3, x, y-1), '#ffffff', width=0)

    def smile(y=126):
        curve([(91, y), (120, y+29), (149, y)], width=5)

    def frown(y=146):
        curve([(94, y), (120, y-24), (146, y)], width=5)

    def tear(x, y):
        polygon([(x, y-12), (x-7, y+1), (x-7, y+6), (x, y+10), (x+7, y+6), (x+7, y+1)], '#42c5ff')
        curve([(x-3, y), (x-6, y+6), (x, y+7)], '#b4f0ff', 2)

    def heart(x, y):
        polygon([(x, y+14), (x-13, y), (x-12, y-7), (x-6, y-10),
                 (x, y-5), (x+6, y-10), (x+12, y-7), (x+13, y)], '#ff4568', INK, 2)

    def upper_teeth(bounds, separators):
        # Una banda redondeada e insetada deja un borde oscuro de labio.
        d.rounded_rectangle(box(bounds), radius=2*SCALE, fill='#fffdf5')
        for x in separators:
            d.line(box((x, bounds[1]+1, x, bounds[3]-1)),
                   fill='#d9dce4', width=2)

    ellipse((38, 27, 202, 191), '#080e18')
    base = '#ffca45' if kind != 'angry' else '#ff7659'
    ellipse((38, 22, 202, 186), base, INK, 3)
    curve([(64, 57), (78, 37), (103, 35)], '#ffe58e', 4)
    if kind == 'joy':
        curve([(75, 90), (88, 70), (101, 90)])
        curve([(139, 90), (152, 70), (165, 90)])
        smile()
    elif kind == 'sad':
        eye(88); eye(152)
        curve([(73, 70), (86, 72), (99, 64)])
        curve([(141, 64), (154, 72), (167, 70)])
        frown(); tear(164, 116)
    elif kind == 'angry':
        eye(88); eye(152)
        line([(72, 65), (101, 78)], width=6)
        line([(139, 78), (168, 65)], width=6)
        frown()
    elif kind == 'upset':
        curve([(73, 82), (88, 102), (103, 82)])
        curve([(137, 82), (152, 102), (167, 82)])
        line([(76, 67), (101, 73)])
        line([(139, 73), (164, 67)])
        frown()
    elif kind == 'serious':
        eye(88); eye(152)
        line([(94, 136), (146, 136)])
    elif kind == 'sorry':
        eye(90, 95, 5); eye(150, 95, 5)
        ellipse((65, 108, 91, 121), '#f28b70')
        ellipse((149, 108, 175, 121), '#f28b70')
        curve([(103, 140), (120, 127), (137, 140)], width=3)
        tear(61, 65)
    elif kind == 'excited':
        for x in (86, 154):
            ellipse((x-13, 73, x+13, 102), INK)
            ellipse((x-8, 76, x+1, 86), '#ffffff')
            ellipse((x+3, 90, x+7, 94), '#ffffff')
        ellipse((96, 119, 144, 155), INK)
        ellipse((105, 140, 135, 152), '#ff667c')
    elif kind == 'inlove':
        heart(85, 87); heart(155, 87)
        # Sonrisa abierta: comisuras altas y parte inferior curva.
        mouth = []
        for a, b, c in [((91, 120), (120, 115), (149, 120)),
                        ((149, 120), (145, 158), (120, 158)),
                        ((120, 158), (95, 158), (91, 120))]:
            for i in range(31):
                t = i/30
                mouth.append(((1-t)**2*a[0]+2*(1-t)*t*b[0]+t*t*c[0],
                              (1-t)**2*a[1]+2*(1-t)*t*b[1]+t*t*c[1]))
        polygon(mouth, INK)
        upper_teeth((98, 122, 142, 130), (112, 128))
        ellipse((107, 141, 134, 153), '#ff667c')
    elif kind == 'nervous':
        eye(88); eye(152)
        line([(72, 68), (101, 68)], width=3)
        line([(139, 68), (168, 68)], width=3)
        line([(90, 138), (100, 130), (110, 138), (120, 130),
              (130, 138), (140, 130), (150, 138)], width=4)
        tear(174, 111)
    elif kind == 'thought':
        line([(73, 88), (99, 88)])
        line([(141, 88), (167, 88)])
        line([(104, 135), (136, 135)])
        for b in [(181, 51, 189, 59), (192, 36, 205, 49), (207, 17, 229, 39)]:
            ellipse(b, '#eff6ff', INK, 2)
    elif kind == 'bored':
        line([(73, 89), (99, 89)])
        line([(141, 89), (167, 89)])
        ellipse((106, 120, 134, 157), INK)
        font = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 19*SCALE)
        d.text((170*SCALE, 34*SCALE), 'Z', fill=INK, font=font)
        d.text((188*SCALE, 16*SCALE), 'Z', fill='#ffffff', font=font)
    elif kind == 'surprise':
        eye(87, 89, 8); eye(153, 89, 8)
        curve([(72, 62), (87, 50), (102, 62)])
        curve([(138, 62), (153, 50), (168, 62)])
        ellipse((108, 120, 132, 154), INK)
    elif kind == 'worried':
        eye(88, 94); eye(152, 94)
        curve([(72, 76), (91, 77), (102, 61)])
        curve([(138, 61), (149, 77), (168, 76)])
        frown()
    elif kind == 'scared':
        for x in (84, 156):
            ellipse((x-14, 73, x+14, 103), '#ffffff', INK, 3)
            ellipse((x-4, 85, x+4, 96), INK)
        curve([(69, 59), (84, 49), (99, 59)])
        curve([(141, 59), (156, 49), (171, 59)])
        ellipse((104, 118, 136, 157), INK)
        upper_teeth((112, 123, 128, 129), (120,))
        ellipse((111, 145, 129, 154), '#ff667c')
    elif kind == 'crying':
        curve([(72, 87), (87, 73), (102, 87)], width=5)
        curve([(138, 87), (153, 73), (168, 87)], width=5)
        line([(85, 94), (80, 143)], '#42c5ff', 12)
        line([(155, 94), (160, 143)], '#42c5ff', 12)
        ellipse((104, 124, 136, 158), INK)
        ellipse((111, 146, 129, 155), '#ff667c')
    elif kind == 'proud':
        for x in (65, 133):
            d.rounded_rectangle(box((x, 72, x+42, 101)), radius=8*SCALE, fill=INK)
            line([(x+9, 78), (x+22, 78)], '#64788d', 3)
        line([(107, 79), (133, 79)], width=5)
        smile()

    font_path = 'C:/Windows/Fonts/arialbd.ttf'
    prefix_font = ImageFont.truetype(font_path, 10*SCALE)
    prefix = 'ESTOY'
    bounds = d.textbbox((0, 0), prefix, font=prefix_font)
    d.text(((240*SCALE-(bounds[2]-bounds[0]))/2, 195*SCALE), prefix,
           font=prefix_font, fill='#a7bad0')
    size = 17 if len(label) > 11 else 20
    font = ImageFont.truetype(font_path, size*SCALE)
    bounds = d.textbbox((0, 0), label, font=font)
    d.text(((240*SCALE-(bounds[2]-bounds[0]))/2, 209*SCALE), label, font=font, fill='#f3f7ff')
    return im.resize((240, 240), Image.Resampling.LANCZOS)


def encode(im):
    out = bytearray(struct.pack('>4sHH', b'TFT1', *im.size))
    for r, g, b in zip(*[iter(im.tobytes())]*3):
        out.extend(struct.pack('>H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
    return out


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (4*260, 4*270), '#e3eaf3')
    manifest = []
    for i, (name, label) in enumerate(EMOTIONS):
        image = face(name, label)
        binary = encode(image)
        assert len(binary) == 115208
        (ASSETS / (name+'.tft')).write_bytes(binary)
        image.save(OUTPUT / (name+'.png'))
        sheet.paste(image, ((i % 4)*260+10, (i // 4)*270+10))
        manifest.append({'file': name+'.tft', 'emotion': 'ESTOY '+label, 'bytes': len(binary)})
    sheet.save(OUTPUT / 'emociones.png')
    (OUTPUT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    with zipfile.ZipFile(OUTPUT / 'emociones_sd.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        for name, label in EMOTIONS:
            z.write(ASSETS/(name+'.tft'), 'emotions/'+name+'.tft')
    print(f'{len(EMOTIONS)} dibujos TFT1 listos: {ASSETS}')
    print(f'Vista y ZIP: {OUTPUT}')


if __name__ == '__main__':
    main()

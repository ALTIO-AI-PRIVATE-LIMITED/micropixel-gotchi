#!/usr/bin/env python3
"""Render the app thumbnails in tools/app_site/covers (640x360 PNG).

Each cover is a hand-drawn SVG scene: a Gotchi (296x240 screen) showing the
app, on a colored background. Run: python3 design/covers.py
"""
from pathlib import Path
import os, subprocess, tempfile

CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")

OUT = Path(__file__).resolve().parents[1] / "covers"

def gotchi(x, y, w, screen, body="#f3efe6", shade="#d9d2c3", tilt=0):
    """A Gotchi: rounded body, 296x240-ratio screen, two side buttons."""
    h = w * 0.86
    sw, sh = w * 0.80, w * 0.80 * 240 / 296
    sx, sy = x + (w - sw) / 2, y + w * 0.09
    return f'''<g transform="rotate({tilt} {x + w/2} {y + h/2})">
  <rect x="{x+4}" y="{y+10}" width="{w}" height="{h}" rx="{w*0.16}" fill="#000" opacity=".25"/>
  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{w*0.16}" fill="{body}"/>
  <rect x="{x}" y="{y+h*0.82}" width="{w}" height="{h*0.18}" rx="{w*0.12}" fill="{shade}" opacity=".6"/>
  <rect x="{x+w}" y="{y+h*0.28}" width="{w*0.03}" height="{h*0.12}" rx="2" fill="{shade}"/>
  <rect x="{x+w}" y="{y+h*0.46}" width="{w*0.03}" height="{h*0.12}" rx="2" fill="{shade}"/>
  <rect x="{sx-3}" y="{sy-3}" width="{sw+6}" height="{sh+6}" rx="{w*0.05}" fill="#1b1d24"/>
  <svg x="{sx}" y="{sy}" width="{sw}" height="{sh}" viewBox="0 0 296 240" preserveAspectRatio="none">
    <rect width="296" height="240" rx="14" fill="#0d0f15"/>{screen}
  </svg>
</g>'''

def frame(bg1, bg2, art, title, tag, ink="#fff"):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="640" height="360" viewBox="0 0 640 360">
<defs><linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{bg1}"/><stop offset="1" stop-color="{bg2}"/></linearGradient>
<radialGradient id="glow" cx=".72" cy=".45" r=".6"><stop offset="0" stop-color="#fff" stop-opacity=".22"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient></defs>
<rect width="640" height="360" fill="url(#bg)"/><rect width="640" height="360" fill="url(#glow)"/>
{art}
<text x="36" y="282" font-family="Chakra Petch, Arial Narrow, sans-serif" font-weight="700" font-size="44" fill="{ink}">{title}</text>
<text x="38" y="318" font-family="IBM Plex Sans, Arial, sans-serif" font-weight="500" font-size="19" fill="{ink}" opacity=".85">{tag}</text>
</svg>'''

def dots(n, cx, cy, r, color, gap):
    return "".join(f'<circle cx="{cx + i*gap}" cy="{cy}" r="{r}" fill="{color}"/>' for i in range(n))

COVERS = {}

# ---------------- DevCon Wall: 3x3 wall of Gotchis playing one logo ----------------
tiles = ""
colors = ["#ff6b2c", "#ffb020", "#ff3d6e", "#7c5cff", "#2ad1c4", "#ff6b2c", "#3aa0ff", "#ffb020", "#ff3d6e"]
for i in range(9):
    r, c = divmod(i, 3)
    x, y = 360 + c * 84, 26 + r * 74
    # each tile shows a slice of one big "8" logo
    tiles += f'<rect x="{x}" y="{y}" width="78" height="68" rx="10" fill="#f3efe6"/><rect x="{x+6}" y="{y+6}" width="66" height="54" rx="5" fill="#0d0f15"/>'
logo = '''<clipPath id="wallclip">''' + "".join(
    f'<rect x="{366 + (i%3)*84}" y="{32 + (i//3)*74}" width="66" height="54" rx="5"/>' for i in range(9)) + '''</clipPath>
<g clip-path="url(#wallclip)">
  <rect x="360" y="26" width="246" height="216" fill="#120a2a"/>
  <circle cx="483" cy="96" r="52" fill="none" stroke="#ff6b2c" stroke-width="22"/>
  <circle cx="483" cy="178" r="60" fill="none" stroke="#ffb020" stroke-width="22"/>
  <circle cx="400" cy="60" r="16" fill="#2ad1c4"/><circle cx="572" cy="214" r="12" fill="#ff3d6e"/>
</g>'''
COVERS["cheeko.devcon-wall"] = frame("#2a0f4a", "#0b0620", tiles + logo, "DevCon Wall", "9 Gotchis · one giant screen")

# ---------------- Walkie-Talkie: two Gotchis with sound waves ----------------
talk = '<circle cx="148" cy="120" r="70" fill="#ff5a36"/><text x="148" y="132" text-anchor="middle" font-family="Chakra Petch" font-weight="700" font-size="34" fill="#fff">TALK</text>'
listen = '<circle cx="148" cy="120" r="70" fill="none" stroke="#2ad1c4" stroke-width="10"/><path d="M118 120 h12 l8 -30 l12 60 l10 -40 l8 20 h12" stroke="#2ad1c4" stroke-width="8" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'
waves = "".join(f'<path d="M{292+i*26} 92 q16 40 0 80" stroke="#fff" stroke-width="6" fill="none" opacity="{1-i*0.25}" stroke-linecap="round"/>' for i in range(3))
COVERS["cheeko.walkie-talkie"] = frame("#ff7a3d", "#c2185b", gotchi(120, 40, 160, talk, tilt=-6) + waves + gotchi(400, 52, 160, listen, tilt=6), "Walkie-Talkie", "Push to talk between two Gotchis")

# ---------------- Radio Test: signal bars + ping ----------------
bars = "".join(f'<rect x="{30+i*34}" y="{200-i*30}" width="24" height="{20+i*30}" rx="5" fill="#4fd48a"/>' for i in range(4))
radio = bars + '<text x="276" y="80" text-anchor="end" font-family="IBM Plex Mono" font-size="34" fill="#fff">12 ms</text><text x="276" y="118" text-anchor="end" font-family="IBM Plex Mono" font-size="22" fill="#98a1b3">loss 0%</text>'
ping = '<path d="M300 110 C 340 60, 420 60, 460 110" stroke="#fff" stroke-width="5" fill="none" stroke-dasharray="10 10"/><circle cx="380" cy="72" r="10" fill="#ffc34d"/>'
COVERS["cheeko.peer-test"] = frame("#0f3d3a", "#06171a", ping + gotchi(80, 46, 190, radio) + gotchi(470, 70, 130, '<circle cx="148" cy="120" r="50" fill="#4fd48a"/>'), "Radio Test", "Check the link between two Gotchis")

# ---------------- Mic Test: waveform + mic ----------------
wave = "".join(f'<rect x="{20+i*13}" y="{120-h}" width="8" height="{2*h}" rx="4" fill="#ff5a36"/>' for i, h in enumerate([8,18,34,52,70,44,26,60,84,58,30,16,40,66,48,22,10,6,14,28,12]))
mic = '<rect x="470" y="70" width="64" height="110" rx="32" fill="#fff"/><path d="M450 150 q52 70 104 0" stroke="#fff" stroke-width="10" fill="none" stroke-linecap="round"/><rect x="497" y="200" width="10" height="34" fill="#fff"/><rect x="470" y="232" width="64" height="10" rx="5" fill="#fff"/>'
COVERS["cheeko.mic-test"] = frame("#3a1c71", "#d76d77", gotchi(90, 40, 220, wave) + mic, "Mic Test", "Record 3 seconds, hear it back")

# ---------------- WisprKey Pro: big circle button + Mac keyboard ----------------
wk = '<circle cx="148" cy="118" r="78" fill="#7c5cff"/><circle cx="148" cy="118" r="78" fill="none" stroke="#fff" stroke-width="6" opacity=".4"/><path d="M128 92 h40 v40 a20 20 0 0 1 -40 0z" fill="#fff"/><rect x="144" y="150" width="8" height="18" fill="#fff"/>' + dots(5, 116, 218, 5, "#fff", 16)
kb = "".join(f'<rect x="{420+c*34}" y="{150+r*30}" width="28" height="24" rx="5" fill="#e9ecf2" opacity=".9"/>' for r in range(3) for c in range(5))
COVERS["cheeko.wispr-key"] = frame("#1e1b4b", "#4c1d95", gotchi(90, 36, 220, wk) + '<path d="M330 150 q40 -40 80 0" stroke="#fff" stroke-width="5" fill="none" stroke-dasharray="8 8"/>' + kb, "WisprKey Pro", "One-tap Wispr Flow keys for your Mac")

# ---------------- Chat Remote: chat bubbles + volume ----------------
cr = '<rect x="22" y="30" width="118" height="110" rx="20" fill="#25d366"/><path d="M60 70 a26 26 0 1 1 10 40 l-18 6 l6 -16 a26 26 0 0 1 2 -30z" fill="#fff"/><rect x="156" y="30" width="118" height="110" rx="20" fill="#07c160"/><circle cx="200" cy="82" r="18" fill="#fff"/><circle cx="232" cy="96" r="14" fill="#fff"/><rect x="22" y="156" width="118" height="60" rx="16" fill="#2a2f3d"/><text x="81" y="198" text-anchor="middle" font-family="Chakra Petch" font-weight="700" font-size="36" fill="#fff">−</text><rect x="156" y="156" width="118" height="60" rx="16" fill="#2a2f3d"/><text x="215" y="198" text-anchor="middle" font-family="Chakra Petch" font-weight="700" font-size="36" fill="#fff">+</text>'
COVERS["cheeko.chat-remote"] = frame("#065f46", "#022c22", gotchi(330, 30, 250, cr, tilt=4), "Chat Remote", "Jump to WhatsApp or WeChat on your Mac")

# ---------------- iPhone Agent: phone mirrored onto Gotchi ----------------
phone = '<rect x="420" y="26" width="120" height="232" rx="22" fill="#111"/><rect x="428" y="38" width="104" height="208" rx="14" fill="#3aa0ff"/>' + "".join(f'<rect x="{438+c*32}" y="{60+r*36}" width="22" height="22" rx="6" fill="#fff" opacity=".85"/>' for r in range(4) for c in range(3))
mirror = '<rect width="296" height="240" fill="#3aa0ff"/>' + "".join(f'<rect x="{40+c*80}" y="{24+r*70}" width="56" height="56" rx="14" fill="#fff" opacity=".85"/>' for r in range(3) for c in range(3)) + '<path d="M150 130 l0 50 l12 -12 l10 22 l8 -4 l-10 -22 l18 0z" fill="#111" stroke="#fff" stroke-width="3"/>'
COVERS["cheeko.iphone-agent"] = frame("#0b3b8c", "#061a40", gotchi(70, 40, 230, mirror) + '<path d="M322 140 h80" stroke="#fff" stroke-width="6" stroke-dasharray="10 8"/><path d="M396 128 l14 12 l-14 12" stroke="#fff" stroke-width="6" fill="none"/>' + phone, "iPhone Agent", "Mirror and drive your iPhone from a Gotchi")

# ---------------- Polygon Benchmark: wireframe triangles + fps ----------------
tri = "".join(f'<polygon points="{a}" fill="{c}" opacity=".85"/>' for a, c in [
    ("30,200 120,40 180,190", "#ff5a36"), ("110,210 200,60 270,200", "#3aa0ff"), ("60,120 160,20 230,140", "#ffc34d")])
tri += '<text x="260" y="36" text-anchor="end" font-family="IBM Plex Mono" font-size="24" fill="#4fd48a">58 fps</text>'
COVERS["micropixel.polygon-benchmark"] = frame("#111827", "#374151", gotchi(330, 30, 250, tri), "Polygon Benchmark", "How fast can a Gotchi draw?")

# ---------------- Ball Fall: falling ball through platforms ----------------
bf = '<rect width="296" height="240" fill="#14121f"/><rect x="0" y="0" width="296" height="6" fill="#ff3b3b"/>' + "".join(
    f'<rect x="{x}" y="{y}" width="{w}" height="12" rx="6" fill="#4b5bd6"/>' for x, y, w in [(20,70,120),(180,70,96),(0,140,90),(140,140,156),(40,210,200)]) + '<circle cx="160" cy="112" r="16" fill="#ffc34d"/><path d="M160 60 v30" stroke="#ffc34d" stroke-width="5" opacity=".4"/>'
COVERS["lovelyhim.ball-fall"] = frame("#312e81", "#0f0a2a", gotchi(330, 30, 250, bf, tilt=-3), "Ball Fall", "Steer a golden ball down an endless shaft")

def main():
    OUT.mkdir(exist_ok=True)
    tmp = Path(tempfile.mkdtemp())
    for app_id, svg in COVERS.items():
        html = ('<html><head><link href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@700&family=IBM+Plex+Sans:wght@500&family=IBM+Plex+Mono&display=swap" rel="stylesheet">'
                '<style>html,body{margin:0;overflow:hidden}</style></head><body>' + svg + '</body></html>')
        page = tmp / f"{app_id}.html"
        page.write_text(html)
        subprocess.run([CHROME, "--headless=new", "--hide-scrollbars", "--force-device-scale-factor=1",
                        "--window-size=640,460", "--virtual-time-budget=3000",
                        f"--screenshot={OUT / (app_id + '.png')}", page.as_uri()],
                       check=True, capture_output=True)
        subprocess.run(["magick", str(OUT / (app_id + '.png')), "-crop", "640x360+0+0", "+repage", str(OUT / (app_id + '.png'))], check=True)
        print("wrote", app_id)

if __name__ == "__main__":
    main()

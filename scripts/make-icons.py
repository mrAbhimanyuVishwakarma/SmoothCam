"""Builds the app icon and installer artwork from assets/icons/SmoothCam.png.

    python scripts/make-icons.py

Needs Pillow (pip install pillow). Outputs are committed, so this only has to
be re-run when the logo changes.
"""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ICONS = ROOT / "assets" / "icons"
INSTALLER = ROOT / "installer" / "images"


def square(logo: Image.Image, padding: float = 0.04) -> Image.Image:
    """Centers the logo on a transparent square canvas with a little margin."""
    side = int(max(logo.size) * (1 + 2 * padding))
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(logo, ((side - logo.width) // 2, (side - logo.height) // 2), logo)
    return canvas


def on_background(img: Image.Image, size: tuple[int, int], logo_px: int, color=(255, 255, 255)) -> Image.Image:
    bg = Image.new("RGB", size, color)
    logo = img.resize((logo_px, logo_px), Image.LANCZOS)
    bg.paste(logo, ((size[0] - logo_px) // 2, (size[1] - logo_px) // 2), logo)
    return bg


def main() -> None:
    logo = Image.open(ICONS / "SmoothCam.png").convert("RGBA")
    sq = square(logo)

    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    sq.save(ICONS / "SmoothCam.ico", sizes=[(s, s) for s in sizes])
    sq.resize((256, 256), Image.LANCZOS).save(ICONS / "SmoothCam-256.png")

    # Inno Setup wizard artwork, at 100/125/150/200/250% scale.
    INSTALLER.mkdir(parents=True, exist_ok=True)
    for scale, suffix in [(1.0, "100"), (1.25, "125"), (1.5, "150"), (2.0, "200"), (2.5, "250")]:
        small = (round(55 * scale), round(58 * scale))
        on_background(sq, small, round(52 * scale)).save(INSTALLER / f"small-{suffix}.bmp")
        large = (round(164 * scale), round(314 * scale))
        wizard = Image.new("RGB", large, (24, 22, 48))
        px = round(132 * scale)
        lg = sq.resize((px, px), Image.LANCZOS)
        wizard.paste(lg, ((large[0] - px) // 2, round(large[1] * 0.32) - px // 2), lg)
        wizard.save(INSTALLER / f"wizard-{suffix}.bmp")

    print("icons written to", ICONS, "and", INSTALLER)


if __name__ == "__main__":
    main()

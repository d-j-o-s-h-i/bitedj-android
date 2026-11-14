#!/usr/bin/env python3
"""Render the repository's Mixxx SVG for both Android packaging paths.

Requires ksvgtopng (or ksvgtopng5) and Pillow. Generated PNGs are checked in;
normal APK builds do not need the renderer.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from PIL import Image

ANDROID = Path(__file__).resolve().parents[1]
SOURCE = ANDROID.parent / "res/images/icons/scalable/apps/mixxx.svg"


def main():
    renderer = shutil.which("ksvgtopng") or shutil.which("ksvgtopng5")
    if not renderer:
        raise SystemExit("Install ksvgtopng to regenerate the checked-in launcher icons")
    with tempfile.TemporaryDirectory(prefix="mixxx-icons-") as temporary:
        png = Path(temporary) / "mixxx.png"
        subprocess.run([renderer, "1024", "1024", str(SOURCE), str(png)],
                       env={**os.environ, "QT_QPA_PLATFORM": "offscreen"}, check=True)
        icon = Image.open(png).convert("RGBA")
        for resources in (ANDROID / "package/res", ANDROID / "app/src/main/res"):
            for density, size in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96),
                                  ("xxhdpi", 144), ("xxxhdpi", 192)):
                destination = resources / ("mipmap-" + density)
                destination.mkdir(parents=True, exist_ok=True)
                for name in ("ic_launcher", "ic_launcher_round"):
                    (destination / (name + ".webp")).unlink(missing_ok=True)
                    icon.resize((size, size), Image.Resampling.LANCZOS).save(
                        destination / (name + ".png"))
            # Keep the complete source icon inside Android's adaptive safe area.
            foreground = Image.new("RGBA", (432, 432))
            foreground.alpha_composite(icon.resize((264, 264), Image.Resampling.LANCZOS), (84, 84))
            drawable = resources / "drawable-nodpi"
            drawable.mkdir(parents=True, exist_ok=True)
            foreground.save(drawable / "mixxx_launcher_foreground.png")
            adaptive = resources / "mipmap-anydpi-v26"
            adaptive.mkdir(parents=True, exist_ok=True)
            xml = '''<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/mixxx_launcher_background" />
    <foreground android:drawable="@drawable/mixxx_launcher_foreground" />
</adaptive-icon>
'''
            for name in ("ic_launcher", "ic_launcher_round"):
                (adaptive / (name + ".xml")).write_text(xml)
                (resources / "mipmap-anydpi" / (name + ".xml")).unlink(missing_ok=True)
            values = resources / "values"
            values.mkdir(parents=True, exist_ok=True)
            (values / "launcher_colors.xml").write_text('''<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="mixxx_launcher_background">#F1F1F1</color>
</resources>
''')


if __name__ == "__main__":
    main()

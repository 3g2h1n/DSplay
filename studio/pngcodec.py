"""Minimal PNG decoder / encoder (no dependencies).

Decodes 8-bit and palette PNGs (all colour types, bit depths 1-8 and 16, non-interlaced)
so downloaded box art can be re-encoded as small RGB PNGs the console's decoder
(source/upng.c: RGB / RGBA / grey, 8 bit, non-interlaced) understands.
"""

import struct
import zlib


class PngError(Exception):
    pass


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode(data):
    """Returns (width, height, rgb_bytes)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise PngError("not a png")
    pos = 8
    idat = []
    plte = None
    trns = None
    w = h = depth = ctype = interlace = None
    while pos + 8 <= len(data):
        ln, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            plte = body
        elif typ == b"tRNS":
            trns = body
        elif typ == b"IDAT":
            idat.append(body)
        elif typ == b"IEND":
            break
    if not w or not h or w > 4096 or h > 4096:
        raise PngError("bad size")
    if interlace:
        raise PngError("interlaced")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None:
        raise PngError("colour type")
    raw = zlib.decompress(b"".join(idat))
    bits_pp = channels * depth
    bpp = max(1, bits_pp // 8)
    stride = (w * bits_pp + 7) // 8
    out = bytearray()
    prev = bytearray(stride)
    p = 0
    rows = []
    for _ in range(h):
        ft = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                ul = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(left, prev[i], ul)) & 0xFF
        rows.append(line)
        prev = line

    def samples(line):
        if depth == 8:
            return line
        if depth == 16:
            return line[0::2]
        per = 8 // depth
        mask = (1 << depth) - 1
        res = bytearray()
        for byte in line:
            for k in range(per):
                res.append((byte >> (8 - depth * (k + 1))) & mask)
        return res

    scale = 255 // ((1 << depth) - 1) if depth < 8 else 1
    bg = 40  # composite transparency on dark grey, like the console does
    for line in rows:
        s = samples(line)
        if ctype == 3:
            if plte is None:
                raise PngError("no palette")
            for x in range(w):
                i = s[x] * 3
                r, g, b = plte[i:i + 3] if i + 3 <= len(plte) else (0, 0, 0)
                a = trns[s[x]] if trns and s[x] < len(trns) else 255
                if a < 255:
                    r = (r * a + bg * (255 - a)) // 255
                    g = (g * a + bg * (255 - a)) // 255
                    b = (b * a + bg * (255 - a)) // 255
                out += bytes((r, g, b))
        elif ctype == 0:
            for x in range(w):
                v = s[x] * scale
                out += bytes((v, v, v))
        elif ctype == 4:
            for x in range(w):
                v, a = s[2 * x] * scale, s[2 * x + 1] * scale
                v = (v * a + bg * (255 - a)) // 255
                out += bytes((v, v, v))
        elif ctype == 2:
            out += s[:w * 3]
        else:  # 6
            for x in range(w):
                r, g, b, a = s[4 * x:4 * x + 4]
                if a < 255:
                    r = (r * a + bg * (255 - a)) // 255
                    g = (g * a + bg * (255 - a)) // 255
                    b = (b * a + bg * (255 - a)) // 255
                out += bytes((r, g, b))
    return w, h, bytes(out)


def resize_fit(w, h, rgb, max_side):
    """Box-filter downscale so the longest side is <= max_side."""
    if max(w, h) <= max_side:
        return w, h, rgb
    if w >= h:
        nw, nh = max_side, max(1, round(h * max_side / w))
    else:
        nw, nh = max(1, round(w * max_side / h)), max_side
    out = bytearray(nw * nh * 3)
    for y in range(nh):
        y0, y1 = y * h // nh, max(y * h // nh + 1, (y + 1) * h // nh)
        for x in range(nw):
            x0, x1 = x * w // nw, max(x * w // nw + 1, (x + 1) * w // nw)
            r = g = b = n = 0
            for yy in range(y0, y1):
                base = (yy * w + x0) * 3
                for xx in range(x1 - x0):
                    i = base + xx * 3
                    r += rgb[i]
                    g += rgb[i + 1]
                    b += rgb[i + 2]
                    n += 1
            o = (y * nw + x) * 3
            out[o], out[o + 1], out[o + 2] = r // n, g // n, b // n
    return nw, nh, bytes(out)


def encode_rgb(w, h, rgb):
    def chunk(t, body):
        c = struct.pack(">I", len(body)) + t + body
        return c + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)

    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def to_console_png(data, max_side=128):
    """Any PNG/JPEG -> small RGB PNG for sd:/_hyper/covers. Uses Pillow when available."""
    try:
        from PIL import Image  # type: ignore
        import io
        im = Image.open(io.BytesIO(data))
        im = im.convert("RGBA")
        bg = Image.new("RGBA", im.size, (40, 40, 40, 255))
        im = Image.alpha_composite(bg, im).convert("RGB")
        im.thumbnail((max_side, max_side), Image.LANCZOS)
        buf = io.BytesIO()
        im.save(buf, "PNG", optimize=True)
        return buf.getvalue()
    except ImportError:
        pass
    w, h, rgb = decode(data)
    w, h, rgb = resize_fit(w, h, rgb, max_side)
    return encode_rgb(w, h, rgb)

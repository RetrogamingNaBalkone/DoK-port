"""Independent N19 target raster from original RES, never from the UI blob.

The target layout is the N06 DS adaptation with the user's N19 adjustment:
health/charge text starts immediately after its icon, without right alignment.
This validates resources and the chosen DS raster, not DOS framebuffer equality.
"""
from __future__ import annotations

import hashlib

import audit_assets


class OriginalHudArt:
    def __init__(self):
        self.sources = audit_assets.originals()
        self.resources = {language: {e["name"]: e["data"] for e in source["entries"]}
                          for language, source in self.sources.items()}

    def palette(self, language, level):
        raw = self.resources[language][f"GAMEP{level}"]
        return [sum((raw[i*3+k] >> 1) << (k*5) for k in range(3)) for i in range(256)]

    def panel(self, language):
        width, height, _, pixels, _ = audit_assets.decode_image(self.resources[language]["ПАНЕЛЬ"])
        assert (width, height) == (320, 39)
        out = bytearray(pixels[((2*y+1)*39//76)*320+(2*x+1)*320//512]
                        for y in range(38) for x in range(256))
        for y in range(1,36):
            out[y*256+3:y*256+253] = bytes([86])*250
        return out

    def icon(self, canvas, language, name, x, y, width, height):
        sw, sh, _, pixels, _ = audit_assets.decode_image(self.resources[language][name])
        occupied = []
        for dy in range(height):
            sy = (2*dy+1)*sh//(2*height)
            for dx in range(width):
                sx = (2*dx+1)*sw//(2*width)
                value = pixels[sy*sw+sx]
                if value:
                    canvas[(y+dy)*256+x+dx] = value
                    occupied.append((x+dx,y+dy))
        return occupied

    def text(self, canvas, language, value, x, y, height=8):
        raw = value.encode("cp866") if isinstance(value,str) else value
        font = self.resources[language][f"FONT{height}"]
        if x < 0 or y < 0 or x+8*len(raw)>256:
            raise AssertionError("Expected original font text would be clipped")
        occupied = []
        for i, code in enumerate(raw):
            for row in range(height):
                glyph = font[code*height+row]
                if y+row>=38:
                    assert not glyph, 'Visible original glyph would be clipped'
                    continue
                for col in range(8):
                    if glyph & (128>>col):
                        canvas[(y+row)*256+x+i*8+col] = 2
                        occupied.append((x+i*8+col,y+row))
        return occupied


def message_pages(raw: bytes, columns=28):
    """Keep every printable source byte; native CR/LF/NUL/$ semantics."""
    stop = next((i for i,b in enumerate(raw) if b in (0,36)),len(raw))
    pages=[]
    cursor=0
    while cursor<stop or not pages:
        start=cursor;line=bytearray()
        while cursor<stop:
            code=raw[cursor]
            if code==13:
                cursor+=1;continue
            if code==10:
                cursor+=1;break
            if len(line)==columns:
                break
            if code!=32 and line and (cursor==0 or raw[cursor-1] in (10,13,32)):
                end=cursor
                while end<stop and raw[end] not in (10,13,32):
                    end+=1
                if end-cursor<=columns and len(line)+end-cursor>columns:
                    break
            line.append(code);cursor+=1
        pages.append(dict(start=start,end=cursor,bytes=bytes(line)))
        if cursor==stop:
            break
        if cursor==start:
            raise AssertionError("Message pagination did not advance")
    expected=bytes(b for b in raw[:stop] if b not in (10,13))
    assert b"".join(p["bytes"] for p in pages)==expected
    return pages


def render_hud(art: OriginalHudArt, state: dict, message=b"", page=0):
    language, level = state["language"],state["level"]
    canvas=bytearray([86]*9728)
    border=art.panel(language)
    for y in (0,):
        canvas[y*256:(y+1)*256]=border[y*256:(y+1)*256]
    canvas[20*256:21*256]=border[:256]
    icon_hp=art.icon(canvas,language,"ПЖИЗНЬ",4,23,17,14)
    icon_charge=art.icon(canvas,language,"ПМАГИЯ",82,22,13,16)
    text_bounds={}
    for name,x in (("health",23),("charge",97)):
        value=state[name]
        if not -32768<=value<=32767:
            raise AssertionError("HUD numeric source domain is signed WORD")
        text_bounds[name]=art.text(canvas,language,str(value),x,23,16)
    for bit,name,x in ((1,"ПКЛЮЧМ",212),(2,"ПКЛЮЧЖ",226),(4,"ПКЛЮЧЗ",240)):
        if state["keys"]&bit:
            art.icon(canvas,language,name,x,23,10,14)
    for bit,name,x in ((2,"ПОРУЖИЕ1",150),(4,"ПОРУЖИЕ2",178)):
        if state["weapon"]&bit:
            sw,sh,_,_,_=audit_assets.decode_image(art.resources[language][name])
            factor=min(20/sw,10/sh);w,h=max(1,round(sw*factor)),max(1,round(sh*factor))
            art.icon(canvas,language,name,x+(20-w)//2,25+(10-h)//2,w,h)
    pages=message_pages(message,30)
    assert len(pages)<=2, "Original gameplay message must fit the message area"
    font=art.resources[language]["FONT8"]
    top=7 if len(pages)==1 else 3
    for row,line in enumerate(pages):
        for col,code in enumerate(line["bytes"]):
            for y in range(8):
                for x in range(8):
                    if font[code*8+y] & (128 >> x):
                        canvas[(top+row*8+y)*256+8+col*8+x]=2
    adjacency={}
    for key,icons in (("health",icon_hp),("charge",icon_charge)):
        ink=text_bounds[key]
        adjacency[key]=dict(icon_right=max(x for x,y in icons),number_left=min(x for x,y in ink),
                            optical_gap=min(x for x,y in ink)-max(x for x,y in icons)-1)
    return bytes(canvas),dict(sha256=hashlib.sha256(canvas).hexdigest(),adjacency=adjacency,
                              message_page=page,message_pages=1)

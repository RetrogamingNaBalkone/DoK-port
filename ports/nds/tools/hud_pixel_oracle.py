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
        if x < 0 or y < 0 or x+8*len(raw)>256 or y+height>38:
            raise AssertionError("Expected original font text would be clipped")
        occupied = []
        for i, code in enumerate(raw):
            for row in range(height):
                glyph = font[code*height+row]
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
    canvas=art.panel(language)
    icon_hp=art.icon(canvas,language,"ПЖИЗНЬ",4,3,17,14)
    icon_charge=art.icon(canvas,language,"ПМАГИЯ",82,2,13,16)
    text_bounds={}
    for name,x in (("health",23),("charge",97)):
        value=state[name]
        if not -32768<=value<=32767:
            raise AssertionError("HUD numeric source domain is signed WORD")
        text_bounds[name]=art.text(canvas,language,str(value),x,2,16)
        maximum=state["max_health" if name=="health" else "max_charge"]
        extent=0 if maximum<=0 else min(48,max(0,value)*48//maximum)
        canvas[18*256+x:18*256+x+48]=bytes([0])*48
        canvas[18*256+x:18*256+x+extent]=bytes([2])*extent
    for bit,name,x in ((1,"ПКЛЮЧМ",156),(2,"ПКЛЮЧЖ",172),(4,"ПКЛЮЧЗ",188)):
        if state["keys"]&bit:
            art.icon(canvas,language,name,x,3,10,14)
    weapon=("О:" if language==1 else "W:")+str(state["weapon"])
    art.text(canvas,language,weapon,220,2,16)
    secondary=("П:" if language==1 else "L:")+str(state["lives"])
    secondary+="  "+("С:" if language==1 else "S:")+str(state["score"])
    art.text(canvas,language,secondary,4,20)
    pages=message_pages(message)
    if not 0<=page<len(pages):
        raise AssertionError("Message page outside full source text")
    art.text(canvas,language,pages[page]["bytes"],4,28)
    if len(pages)>1:
        art.text(canvas,language,f"{page+1}/{len(pages)}",228,28)
    adjacency={}
    for key,icons in (("health",icon_hp),("charge",icon_charge)):
        ink=text_bounds[key]
        adjacency[key]=dict(icon_right=max(x for x,y in icons),number_left=min(x for x,y in ink),
                            optical_gap=min(x for x,y in ink)-max(x for x,y in icons)-1)
    return bytes(canvas),dict(sha256=hashlib.sha256(canvas).hexdigest(),adjacency=adjacency,
                              message_page=page,message_pages=len(pages))

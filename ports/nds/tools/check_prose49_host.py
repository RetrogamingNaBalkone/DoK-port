"""Native prose pagination: exact-width separators, page edges and real RES text."""
import ctypes as C
import json
from pathlib import Path
import check_hud_host as hud
from frontend_pixel_oracle import OriginalFrontendArt, prepared_text, text_pages
from layout_ui import paginate as original_plan


def main():
    hud.OUT=hud.ROOT/'build/nds/n23/prose49-host';hud.build()
    dll=C.CDLL(str(hud.OUT/'ui-host.dll'));fn=dll.n49_prose
    fn.argtypes=[hud.P8,C.c_uint32,C.c_uint32,C.c_uint32,C.c_uint32,hud.CALLBACK,C.c_void_p]
    fn.restype=C.c_uint32
    cases=[]
    def verify(raw,cols,rows,expected=None):
        data=(hud.U8*max(1,len(raw))).from_buffer_copy(raw or b'\0')
        plan=text_pages(raw,cols,rows,prose=True);emitted=bytearray();actual=[]
        for page,lines in enumerate(plan):
            glyphs=[]
            callback=hud.CALLBACK(lambda _,offset,col,row:glyphs.append((offset,col,row)))
            assert fn(data,len(raw),cols,rows,page,callback,None)==len(plan)
            canvas=[bytearray(b' '*cols) for _ in range(rows)]
            for offset,col,row in glyphs:
                assert col<cols and row<rows
                canvas[row][col]=raw[offset];emitted.append(raw[offset])
            drawn=[bytes(row).rstrip() for row in canvas]
            oracle=[line.rstrip() for line in lines]+[b'']*(rows-len(lines))
            assert drawn==oracle,(raw,page,drawn,oracle)
            actual.extend(drawn)
        assert bytes(emitted).replace(b' ',b'')==raw.replace(b' ',b'').replace(b'\r',b'').replace(b'\n',b'')
        if expected is not None:assert actual==expected,(actual,expected)
        cases.append(dict(bytes=len(raw),columns=cols,rows=rows,pages=len(plan)))
    verify(b'ABCD efgh ijkl',4,1,[b'ABCD',b'efgh',b'ijkl'])
    verify(b'ABCD   efgh',4,1,[b'ABCD',b'efgh'])
    verify(b'ABCD ',4,1,[b'ABCD'])
    verify(b'ABCDEFGHI',4,1,[b'ABCD',b'EFGH',b'I'])
    verify(b'  Hi\n  Bye',8,2,[b'  Hi',b'  Bye'])
    art=OriginalFrontendArt()
    for language in (1,2):
        for index,name in enumerate(('README','BINTRO1','EINTRO1','BINTRO2','EINTRO2','BINTRO3','EINTRO3')):
            raw=prepared_text(art.resources[language][name],index)
            verify(raw,30,11)
            assert all(not line.startswith(b' ') for p in text_pages(raw,30,11,prose=True) for line in p)
    result=dict(passed=True,cases=cases,scope=__doc__)
    legacy=dll.n19_paginate;legacy.argtypes=fn.argtypes;legacy.restype=fn.restype
    legacy_pages=0
    for raw in (b'',b'A\r\nB$ignored',b'A\rB\0ignored',b'A'*61,b' '*30+b'B\n'*40,
                bytes(range(128,256))*10,b'A'*28+b'\r\nB',b'\n\nA\n',b'A B C   ',bytes(range(1,256))):
        data=(hud.U8*max(1,len(raw))).from_buffer_copy(raw or b'\0')
        for cols,rows in ((28,1),(30,9),(1,1),(7,3)):
            plan=original_plan(raw,cols,rows)
            for page,expected in enumerate(plan['pages']):
                glyphs=[]
                callback=hud.CALLBACK(lambda _,o,c,r:glyphs.append([o,c,r]))
                assert legacy(data,len(raw),cols,rows,page,callback,None)==len(plan['pages'])
                assert glyphs==expected['glyphs']
                legacy_pages+=1
    result['unchanged_hud_pagination_pages']=legacy_pages
    (hud.OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,cases=len(cases),unchanged_hud_pagination_pages=legacy_pages)))


if __name__=='__main__':main()
